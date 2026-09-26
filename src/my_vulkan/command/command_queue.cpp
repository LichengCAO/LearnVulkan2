#include "command_queue.h"

#include "device.h"

#include <algorithm>

auto CommandQueueManager::SubmitInfo::SetCommandBuffers(
	std::vector<VkCommandBuffer> inCommandBuffers)->SubmitInfo&
{
	m_commandBuffers = std::move(inCommandBuffers);
	return *this;
}

auto CommandQueueManager::SubmitInfo::AddWaitQueueDependency(
	QueueDependency& inDependency,
	VkPipelineStageFlags2 inWaitStage)->SubmitInfo&
{
	CHECK_TRUE(inWaitStage != 0, "Queue dependency wait stage cannot be zero!");
	for (DependencyEntry& entry : m_dependencyEntries)
	{
		if (entry.dependency == &inDependency)
		{
			entry.waitStage |= inWaitStage;
			entry.useWait = true;
			return *this;
		}
	}

	m_dependencyEntries.push_back({ &inDependency, inWaitStage, true, false });
	return *this;
}

auto CommandQueueManager::SubmitInfo::AddSignalQueueDependency(QueueDependency& inDependency)->SubmitInfo&
{
	for (DependencyEntry& entry : m_dependencyEntries)
	{
		if (entry.dependency == &inDependency)
		{
			entry.useSignal = true;
			return *this;
		}
	}

	m_dependencyEntries.push_back({ &inDependency, 0, false, true });
	return *this;
}

auto CommandQueueManager::SubmitInfo::SetFence(HostFence& inFence)->SubmitInfo&
{
	m_completionFence = &inFence;
	return *this;
}

auto CommandQueueManager::_GetRoleIndex(QueueFamilyType inQueueFamilyType)->size_t
{
	switch (inQueueFamilyType)
	{
	case QueueFamilyType::GRAPHICS:
		return 0;
	case QueueFamilyType::COMPUTE:
		return 1;
	case QueueFamilyType::TRANSFER:
		return 2;
	default:
		CHECK_TRUE(false, "Invalid command queue role!");
		return SubmissionFrontier::MAX_QUEUE_COUNT;
	}
}

CommandQueueManager::CommandQueueManager()
{
	m_fenceAllocator.Create();
	m_semaphoreAllocator.Create();
	auto& device = MyDevice::GetInstance();
	_RegisterQueue(
		QueueFamilyType::GRAPHICS,
		device.GetQueueOfType(QueueFamilyType::GRAPHICS),
		device.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS),
		0);
	_RegisterQueue(
		QueueFamilyType::COMPUTE,
		device.GetQueueOfType(QueueFamilyType::COMPUTE),
		device.GetQueueFamilyIndexOfType(QueueFamilyType::COMPUTE),
		0);
	_RegisterQueue(
		QueueFamilyType::TRANSFER,
		device.GetQueueOfType(QueueFamilyType::TRANSFER),
		device.GetQueueFamilyIndexOfType(QueueFamilyType::TRANSFER),
		0);
	m_created = true;
}

CommandQueueManager::CommandQueueManager(UninitializedTag)
{
}

auto CommandQueueManager::_RegisterQueue(
	QueueFamilyType inQueueFamilyType,
	VkQueue inVkQueue,
	uint32_t inFamilyIndex,
	uint32_t inQueueIndex)->size_t
{
	CHECK_TRUE(inVkQueue != VK_NULL_HANDLE, "Invalid Vulkan queue!");
	const size_t roleIndex = _GetRoleIndex(inQueueFamilyType);
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = m_queueStates[queueIndex];
		if (state.familyIndex == inFamilyIndex && state.queueIndex == inQueueIndex)
		{
			CHECK_TRUE(state.vkQueue == inVkQueue, "Queue identity maps to different Vulkan handles!");
			m_roleToQueueState[roleIndex] = queueIndex;
			return queueIndex;
		}
	}

	CHECK_TRUE(m_queueStateCount < SubmissionFrontier::MAX_QUEUE_COUNT, "Too many physical command queues!");
	const size_t queueStateIndex = m_queueStateCount++;
	QueueState& state = m_queueStates[queueStateIndex];
	state.vkQueue = inVkQueue;
	state.familyIndex = inFamilyIndex;
	state.queueIndex = inQueueIndex;
	m_roleToQueueState[roleIndex] = queueStateIndex;
	return queueStateIndex;
}

auto CommandQueueManager::_GetQueueStateIndex(QueueFamilyType inQueueFamilyType) const->size_t
{
	const size_t roleIndex = _GetRoleIndex(inQueueFamilyType);
	CHECK_TRUE(m_roleToQueueState[roleIndex] < m_queueStateCount, "Command queue role is not registered!");
	return m_roleToQueueState[roleIndex];
}

auto CommandQueueManager::_GetQueueState(size_t inQueueStateIndex)->QueueState&
{
	CHECK_TRUE(inQueueStateIndex < m_queueStateCount, "Command queue state index is out of range!");
	return m_queueStates[inQueueStateIndex];
}

auto CommandQueueManager::_GetQueueState(size_t inQueueStateIndex) const->const QueueState&
{
	CHECK_TRUE(inQueueStateIndex < m_queueStateCount, "Command queue state index is out of range!");
	return m_queueStates[inQueueStateIndex];
}

void CommandQueueManager::Submit(
	QueueFamilyType inQueueFamilyType,
	SubmitInfo inSubmitInfo)
{
	CHECK_TRUE(m_created, "Command queue manager is not created!");
	const size_t inQueueStateIndex = _GetQueueStateIndex(inQueueFamilyType);
	QueueState& state = _GetQueueState(inQueueStateIndex);
	if (inSubmitInfo.m_completionFence != nullptr)
	{
		inSubmitInfo.m_completionFence->_PrepareForSubmit();
	}

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	std::vector<DependencySignal> dependencyWaitSignals(
		inSubmitInfo.m_dependencyEntries.size());
	std::vector<VkSemaphore> dependencySignalSemaphores(
		inSubmitInfo.m_dependencyEntries.size(), VK_NULL_HANDLE);
	std::vector<bool> dependencySignalsStored(
		inSubmitInfo.m_dependencyEntries.size(), false);
	SubmissionFrontier submissionFrontier = state.tailFrontier;
	VkFence completionVkFence = VK_NULL_HANDLE;
	std::vector<HostFence::Callback> completionCallbacks;
	bool completionCallbacksTaken = false;
	bool completionRecordStored = false;
	bool completionFenceCommitted = false;

	waitInfos.reserve(inSubmitInfo.m_dependencyEntries.size());
	signalInfos.reserve(inSubmitInfo.m_dependencyEntries.size());
	m_abandonedSemaphores.reserve(
		m_abandonedSemaphores.size() + inSubmitInfo.m_dependencyEntries.size());

	try
	{
		for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
		{
			const SubmitInfo::DependencyEntry& entry = inSubmitInfo.m_dependencyEntries[dependencyIndex];
			CHECK_TRUE(entry.dependency != nullptr, "Queue dependency entry is null!");
			CHECK_TRUE(entry.useWait || entry.useSignal,
				"Queue dependency submit has no wait or signal operation!");

			if (entry.useWait)
			{
				dependencyWaitSignals[dependencyIndex] = _TakeDependencySignal(*entry.dependency);
				submissionFrontier.Merge(dependencyWaitSignals[dependencyIndex].frontier);
			}
			else
			{
				CHECK_TRUE(!entry.dependency->_HasPendingSignal(),
					"A pending queue dependency must be waited before it can be signaled again!");
			}

			if (entry.useSignal)
			{
				dependencySignalSemaphores[dependencyIndex] = m_semaphoreAllocator.Allocate();
			}

			const VkSemaphore waitSemaphore = dependencyWaitSignals[dependencyIndex].semaphore;
			const VkSemaphore signalSemaphore = dependencySignalSemaphores[dependencyIndex];
			if (waitSemaphore != VK_NULL_HANDLE)
			{
				VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				waitInfo.semaphore = waitSemaphore;
				waitInfo.stageMask = entry.waitStage;
				waitInfos.push_back(waitInfo);
			}
			if (signalSemaphore != VK_NULL_HANDLE)
			{
				VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signalInfo.semaphore = signalSemaphore;
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(signalInfo);
			}
		}

		submissionFrontier.Advance(inQueueStateIndex);
		state.retiredSemaphores.reserve(
			state.retiredSemaphores.size() + inSubmitInfo.m_dependencyEntries.size());
		if (inSubmitInfo.m_completionFence != nullptr)
		{
			state.pendingHostFences.reserve(state.pendingHostFences.size() + 1);
		}

		std::vector<VkCommandBufferSubmitInfo> commandInfos;
		commandInfos.reserve(inSubmitInfo.m_commandBuffers.size());
		for (VkCommandBuffer commandBuffer : inSubmitInfo.m_commandBuffers)
		{
			CHECK_TRUE(commandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
			VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			commandInfo.commandBuffer = commandBuffer;
			commandInfos.push_back(commandInfo);
		}

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
		submitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
		submitInfo.pCommandBufferInfos = commandInfos.empty() ? nullptr : commandInfos.data();
		submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
		submitInfo.pSignalSemaphoreInfos = signalInfos.empty() ? nullptr : signalInfos.data();

		for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
		{
			if (dependencySignalSemaphores[dependencyIndex] == VK_NULL_HANDLE)
			{
				continue;
			}

			QueueDependency* dependency = inSubmitInfo.m_dependencyEntries[dependencyIndex].dependency;
			CHECK_TRUE(dependency != nullptr, "Queue dependency entry is null!");
			_StoreDependencySignal(
				*dependency,
				dependencySignalSemaphores[dependencyIndex],
				submissionFrontier);
			dependencySignalsStored[dependencyIndex] = true;
		}

		if (inSubmitInfo.m_completionFence != nullptr)
		{
			completionVkFence = m_fenceAllocator.CreateOrGetVkFence();
			completionCallbacks = inSubmitInfo.m_completionFence->_TakeCallbacks();
			completionCallbacksTaken = true;
			state.pendingHostFences.push_back({
				completionVkFence,
				submissionFrontier,
				std::move(completionCallbacks) });
			completionRecordStored = true;
			inSubmitInfo.m_completionFence->_CommitSubmit(
				inQueueStateIndex,
				submissionFrontier);
			completionFenceCommitted = true;
		}

		VK_CHECK(vkQueueSubmit2(state.vkQueue, 1, &submitInfo, completionVkFence),
			"Failed to submit command queue!");
	}
	catch (...)
	{
		if (completionRecordStored)
		{
			CHECK_TRUE(
				!state.pendingHostFences.empty(),
				"Submitted host fence record is missing!");
			completionCallbacks = std::move(state.pendingHostFences.back().callbacks);
			state.pendingHostFences.pop_back();
		}
		if (inSubmitInfo.m_completionFence != nullptr)
		{
			if (completionFenceCommitted)
			{
				inSubmitInfo.m_completionFence->_AbortSubmit(std::move(completionCallbacks));
			}
			else if (completionCallbacksTaken)
			{
				inSubmitInfo.m_completionFence->_RestoreCallbacks(std::move(completionCallbacks));
			}
		}
		if (completionVkFence != VK_NULL_HANDLE)
		{
			m_fenceAllocator.FreeVkFence(&completionVkFence, 1);
		}

		for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
		{
			if (!dependencySignalsStored[dependencyIndex])
			{
				continue;
			}

			QueueDependency* dependency = inSubmitInfo.m_dependencyEntries[dependencyIndex].dependency;
			CHECK_TRUE(dependency != nullptr, "Queue dependency entry is null!");
			const DependencySignal storedSignal = _TakeDependencySignal(*dependency);
			CHECK_TRUE(
				storedSignal.semaphore == dependencySignalSemaphores[dependencyIndex],
				"Stored queue dependency semaphore does not match the allocated semaphore!");
			dependencySignalsStored[dependencyIndex] = false;
		}

		for (const DependencySignal& pendingSignal : dependencyWaitSignals)
		{
			if (pendingSignal.semaphore != VK_NULL_HANDLE)
			{
				m_abandonedSemaphores.push_back({
					pendingSignal.semaphore,
					pendingSignal.frontier });
			}
		}
		for (VkSemaphore semaphore : dependencySignalSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
			{
				m_semaphoreAllocator.Free(semaphore);
			}
		}
		throw;
	}

	// vkQueueSubmit2 succeeded. From this point the frontier and synchronization
	// ownership must be committed and cannot be rolled back as an unsubmitted batch.
	state.tailFrontier = submissionFrontier;
	for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
	{
		if (dependencyWaitSignals[dependencyIndex].semaphore != VK_NULL_HANDLE)
		{
			state.retiredSemaphores.push_back({
				dependencyWaitSignals[dependencyIndex].semaphore,
				submissionFrontier.GetVersion(inQueueStateIndex) });
		}
	}
}

auto CommandQueueManager::_TakeDependencySignal(
	QueueDependency& inDependency)->DependencySignal
{
	const auto iter = m_dependencySemaphores.find(&inDependency);
	CHECK_TRUE(
		iter != m_dependencySemaphores.end(),
		"Queue dependency has no pending semaphore in this manager!");
	const QueueDependency::PendingSignal pendingSignal = inDependency._Take();
	DependencySignal result;
	result.semaphore = iter->second;
	result.frontier = pendingSignal.frontier;
	m_dependencySemaphores.erase(iter);
	return result;
}

void CommandQueueManager::_StoreDependencySignal(
	QueueDependency& inDependency,
	VkSemaphore inSemaphore,
	const SubmissionFrontier& inFrontier)
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Cannot store a null queue dependency semaphore!");
	CHECK_TRUE(
		m_dependencySemaphores.find(&inDependency) == m_dependencySemaphores.end(),
		"Queue dependency already has a semaphore in this manager!");
	const auto [iter, inserted] = m_dependencySemaphores.emplace(&inDependency, inSemaphore);
	(void)iter;
	CHECK_TRUE(inserted, "Failed to store queue dependency semaphore!");
	try
	{
		inDependency._Store(inFrontier);
	}
	catch (...)
	{
		m_dependencySemaphores.erase(&inDependency);
		throw;
	}
}

auto CommandQueueManager::_AdvanceCompletion(
	const SubmissionFrontier& inSubmissionFrontier)->std::vector<HostFence::Callback>
{
	SubmissionFrontier completedFrontier = m_completedFrontier;
	completedFrontier.Merge(inSubmissionFrontier);

	size_t completedFenceCount = 0;
	size_t completedCallbackCount = 0;
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		for (const HostFenceRecord& record : state.pendingHostFences)
		{
			if (!completedFrontier.Covers(record.submissionFrontier))
			{
				break;
			}
			++completedFenceCount;
			completedCallbackCount += record.callbacks.size();
		}
	}

	std::vector<VkFence> completedVkFences;
	std::vector<HostFence::Callback> completedCallbacks;
	completedVkFences.reserve(completedFenceCount);
	completedCallbacks.reserve(completedCallbackCount);
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		size_t completedRecordCount = 0;
		while (
			completedRecordCount < state.pendingHostFences.size() &&
			completedFrontier.Covers(
				state.pendingHostFences[completedRecordCount].submissionFrontier))
		{
			HostFenceRecord& record = state.pendingHostFences[completedRecordCount];
			completedVkFences.push_back(record.vkFence);
			for (HostFence::Callback& callback : record.callbacks)
			{
				completedCallbacks.push_back(std::move(callback));
			}
			++completedRecordCount;
		}
		state.pendingHostFences.erase(
			state.pendingHostFences.begin(),
			state.pendingHostFences.begin() + static_cast<std::ptrdiff_t>(completedRecordCount));
	}

	if (!completedVkFences.empty())
	{
		m_fenceAllocator.FreeVkFence(completedVkFences.data(), completedVkFences.size());
	}

	m_completedFrontier = completedFrontier;
	_CollectRetiredSemaphores(completedFrontier);
	return completedCallbacks;
}

auto CommandQueueManager::_FindHostFenceVkFence(const HostFence& inFence)->VkFence
{
	CHECK_TRUE(inFence.m_isInFlight, "Host fence has no in-flight submission!");
	CHECK_TRUE(
		inFence.m_queueStateIndex < m_queueStateCount,
		"Host fence queue state is not available in this manager!");
	QueueState& state = _GetQueueState(inFence.m_queueStateIndex);
	const uint64_t targetVersion =
		inFence.m_submissionFrontier.GetVersion(inFence.m_queueStateIndex);
	for (const HostFenceRecord& record : state.pendingHostFences)
	{
		if (record.submissionFrontier.GetVersion(inFence.m_queueStateIndex) == targetVersion)
		{
			return record.vkFence;
		}
	}

	CHECK_TRUE(false, "Host fence completion record is not available in this manager!");
	return VK_NULL_HANDLE;
}

void CommandQueueManager::_RunCallbacks(std::vector<HostFence::Callback> inCallbacks)
{
	for (HostFence::Callback& callback : inCallbacks)
	{
		callback();
	}
}

auto CommandQueueManager::_ConsumeHostFence(
	HostFence& inFence,
	uint64_t inTimeout)->bool
{
	if (!inFence.m_isInFlight)
	{
		return true;
	}

	std::vector<HostFence::Callback> completedCallbacks;
	if (!m_completedFrontier.Covers(inFence.m_submissionFrontier))
	{
		const VkFence vkFence = _FindHostFenceVkFence(inFence);
		const VkResult result = vkWaitForFences(
			MyDevice::GetInstance().GetVkDevice(),
			1,
			&vkFence,
			VK_TRUE,
			inTimeout);
		if (result == VK_TIMEOUT)
		{
			return false;
		}
		VK_CHECK(result, "Failed to consume host fence!");
		completedCallbacks = _AdvanceCompletion(inFence.m_submissionFrontier);
	}
	inFence._Complete();

	_RunCallbacks(std::move(completedCallbacks));
	return true;
}

void CommandQueueManager::Wait(HostFence& inFence)
{
	CHECK_TRUE(
		_ConsumeHostFence(inFence, UINT64_MAX),
		"Infinite host fence wait timed out!");
}

auto CommandQueueManager::Poll(HostFence& inFence)->bool
{
	return _ConsumeHostFence(inFence, 0);
}

auto CommandQueueManager::_GetRetiredSemaphoreReclaimCount(
	const QueueState& inState,
	size_t inQueueStateIndex,
	const SubmissionFrontier& inCompletedFrontier)->size_t
{
	size_t reclaimCount = 0;
	while (
		reclaimCount < inState.retiredSemaphores.size() &&
		inCompletedFrontier.Covers(
			inQueueStateIndex,
			inState.retiredSemaphores[reclaimCount].consumerVersion))
	{
		++reclaimCount;
	}
	return reclaimCount;
}

void CommandQueueManager::_CollectRetiredSemaphores(const SubmissionFrontier& inCompletedFrontier)
{
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		const size_t reclaimCount = _GetRetiredSemaphoreReclaimCount(state, queueIndex, inCompletedFrontier);
		for (size_t retiredIndex = 0; retiredIndex < reclaimCount; ++retiredIndex)
		{
			m_semaphoreAllocator.Free(state.retiredSemaphores[retiredIndex].semaphore);
		}
		state.retiredSemaphores.erase(
			state.retiredSemaphores.begin(),
			state.retiredSemaphores.begin() + static_cast<std::ptrdiff_t>(reclaimCount));
	}

	const auto abandonedPartition = std::stable_partition(
		m_abandonedSemaphores.begin(),
		m_abandonedSemaphores.end(),
		[&inCompletedFrontier](const AbandonedSemaphore& abandoned)
		{
			return !inCompletedFrontier.Covers(abandoned.producerFrontier);
		});
	for (auto iter = abandonedPartition; iter != m_abandonedSemaphores.end(); ++iter)
	{
		m_semaphoreAllocator.Discard(iter->semaphore);
	}
	m_abandonedSemaphores.erase(abandonedPartition, m_abandonedSemaphores.end());
}

void CommandQueueManager::_Destroy()
{
	if (!m_created)
	{
		return;
	}

	MyDevice::GetInstance().WaitIdle();
	SubmissionFrontier idleFrontier;
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		idleFrontier.Merge(_GetQueueState(queueIndex).tailFrontier);
	}
	std::vector<HostFence::Callback> completedCallbacks;
	completedCallbacks = _AdvanceCompletion(idleFrontier);
	_RunCallbacks(std::move(completedCallbacks));
	m_dependencySemaphores.clear();
	m_semaphoreAllocator.Destroy();
	m_fenceAllocator.Destroy();
	m_created = false;
}

CommandQueueManager::~CommandQueueManager()
{
	_Destroy();
}
