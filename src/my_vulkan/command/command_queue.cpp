#include "command_queue.h"

#include "device.h"

#include <algorithm>
#include <cassert>
#include <utility>

QueueDependency::~QueueDependency() noexcept(false)
{
	CHECK_TRUE(!m_hasPendingSignal,
		"Queue dependency must be consumed before destruction!");
}

auto QueueDependency::_GetPendingFrontier() const->const SubmissionFrontier&
{
	CHECK_TRUE(m_hasPendingSignal, "Queue dependency has no pending signal!");
	return m_frontier;
}

void QueueDependency::_CommitWait() noexcept
{
	m_hasPendingSignal = false;
	m_frontier = {};
}

void QueueDependency::_CommitSignal(const SubmissionFrontier& inFrontier) noexcept
{
	m_hasPendingSignal = true;
	m_frontier = inFrontier;
}

auto HostFence::AddCallback(Callback inCallback)->HostFence&
{
	CHECK_TRUE(static_cast<bool>(inCallback), "Completion callback is empty!");
	CHECK_TRUE(
		!m_isInFlight,
		"Cannot add a callback while the previous submission is unconsumed!");
	m_callbacks.push_back(std::move(inCallback));
	return *this;
}

void HostFence::_PrepareForSubmit()
{
	CHECK_TRUE(
		!m_isInFlight,
		"Host fence is still bound to an unconsumed submission!");
}

auto HostFence::_CommitSubmit(
	size_t inQueueStateIndex,
	const SubmissionFrontier& inSubmissionFrontier) noexcept->std::vector<Callback>
{
	m_queueStateIndex = inQueueStateIndex;
	m_submissionFrontier = inSubmissionFrontier;
	m_isInFlight = true;
	return std::exchange(m_callbacks, {});
}

void HostFence::_Complete()
{
	CHECK_TRUE(m_isInFlight, "Host fence has no in-flight submission!");
	m_isInFlight = false;
	m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	m_submissionFrontier = {};
}

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
		device.GetQueueOfType(QueueFamilyType::GRAPHICS));
	_RegisterQueue(
		QueueFamilyType::COMPUTE,
		device.GetQueueOfType(QueueFamilyType::COMPUTE));
	_RegisterQueue(
		QueueFamilyType::TRANSFER,
		device.GetQueueOfType(QueueFamilyType::TRANSFER));
	m_created = true;
}

CommandQueueManager::CommandQueueManager(UninitializedTag)
{
}

auto CommandQueueManager::_RegisterQueue(
	QueueFamilyType inQueueFamilyType,
	VkQueue inVkQueue)->size_t
{
	CHECK_TRUE(inVkQueue != VK_NULL_HANDLE, "Invalid Vulkan queue!");
	const size_t roleIndex = _GetRoleIndex(inQueueFamilyType);
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = m_queueStates[queueIndex];
		if (state.vkQueue == inVkQueue)
		{
			m_roleToQueueState[roleIndex] = queueIndex;
			return queueIndex;
		}
	}

	CHECK_TRUE(m_queueStateCount < SubmissionFrontier::MAX_QUEUE_COUNT, "Too many physical command queues!");
	const size_t queueStateIndex = m_queueStateCount++;
	QueueState& state = m_queueStates[queueStateIndex];
	state.vkQueue = inVkQueue;
	m_roleToQueueState[roleIndex] = queueStateIndex;
	return queueStateIndex;
}

auto CommandQueueManager::_GetQueueState(
	QueueFamilyType inQueueFamilyType)->std::pair<size_t, QueueState&>
{
	const size_t roleIndex = _GetRoleIndex(inQueueFamilyType);
	const size_t queueStateIndex = m_roleToQueueState[roleIndex];
	CHECK_TRUE(queueStateIndex < m_queueStateCount, "Command queue role is not registered!");
	return { queueStateIndex, m_queueStates[queueStateIndex] };
}

void CommandQueueManager::Submit(
	QueueFamilyType inQueueFamilyType,
	SubmitInfo inSubmitInfo)
{
	auto [queueStateIndex, state] = _GetQueueState(inQueueFamilyType);
	if (inSubmitInfo.m_completionFence != nullptr)
	{
		inSubmitInfo.m_completionFence->_PrepareForSubmit();
	}

	std::vector<PreparedDependency> preparedDependencies;
	SubmissionFrontier submissionFrontier = state.tailFrontier;
	size_t waitCount = 0;
	size_t signalCount = 0;
	size_t signalOnlyCount = 0;
	preparedDependencies.reserve(inSubmitInfo.m_dependencyEntries.size());
	for (const SubmitInfo::DependencyEntry& entry : inSubmitInfo.m_dependencyEntries)
	{
		CHECK_TRUE(entry.dependency != nullptr, "Queue dependency entry is null!");
		CHECK_TRUE(entry.useWait || entry.useSignal,
			"Queue dependency submit has no wait or signal operation!");

		PreparedDependency prepared;
		prepared.dependency = entry.dependency;
		prepared.useWait = entry.useWait;
		prepared.useSignal = entry.useSignal;
		if (entry.useWait)
		{
			const DependencySignal waitSignal = _PeekDependencySignal(*entry.dependency);
			prepared.waitSemaphore = waitSignal.semaphore;
			submissionFrontier.Merge(waitSignal.frontier);
			++waitCount;
		}
		else
		{
			CHECK_TRUE(!entry.dependency->_HasPendingSignal(),
				"A pending queue dependency must be waited before it can be signaled again!");
			CHECK_TRUE(
				m_dependencySemaphores.find(entry.dependency) == m_dependencySemaphores.end(),
				"Queue dependency semaphore mapping is inconsistent!");
		}
		if (entry.useSignal)
		{
			++signalCount;
			if (!entry.useWait)
			{
				++signalOnlyCount;
			}
		}
		preparedDependencies.push_back(prepared);
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

	submissionFrontier.Advance(queueStateIndex);
	const uint64_t consumerVersion = submissionFrontier.GetVersion(queueStateIndex);
	state.retiredSemaphores.reserve(state.retiredSemaphores.size() + waitCount);
	if (inSubmitInfo.m_completionFence != nullptr)
	{
		state.pendingHostFences.reserve(state.pendingHostFences.size() + 1);
	}
	m_dependencySemaphores.reserve(m_dependencySemaphores.size() + signalOnlyCount);

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	waitInfos.reserve(waitCount);
	signalInfos.reserve(signalCount);
	DependencySemaphoreMap stagedDependencySemaphores;
	stagedDependencySemaphores.reserve(signalOnlyCount);
	VkFence completionVkFence = VK_NULL_HANDLE;

	try
	{
		for (size_t dependencyIndex = 0; dependencyIndex < preparedDependencies.size(); ++dependencyIndex)
		{
			PreparedDependency& prepared = preparedDependencies[dependencyIndex];
			const SubmitInfo::DependencyEntry& entry = inSubmitInfo.m_dependencyEntries[dependencyIndex];
			if (prepared.useSignal)
			{
				prepared.signalSemaphore = m_semaphoreAllocator.Allocate();
				if (!prepared.useWait)
				{
					const auto [iter, inserted] = stagedDependencySemaphores.emplace(
						prepared.dependency,
						prepared.signalSemaphore);
					(void)iter;
					CHECK_TRUE(inserted, "Failed to stage queue dependency semaphore!");
				}
			}

			if (prepared.useWait)
			{
				VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				waitInfo.semaphore = prepared.waitSemaphore;
				waitInfo.stageMask = entry.waitStage;
				waitInfos.push_back(waitInfo);
			}
			if (prepared.useSignal)
			{
				VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signalInfo.semaphore = prepared.signalSemaphore;
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(signalInfo);
			}
		}

		if (inSubmitInfo.m_completionFence != nullptr)
		{
			completionVkFence = m_fenceAllocator.CreateOrGetVkFence();
		}

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
		submitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
		submitInfo.pCommandBufferInfos = commandInfos.empty() ? nullptr : commandInfos.data();
		submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
		submitInfo.pSignalSemaphoreInfos = signalInfos.empty() ? nullptr : signalInfos.data();

		VK_CHECK(vkQueueSubmit2(state.vkQueue, 1, &submitInfo, completionVkFence),
			"Failed to submit command queue!");
	}
	catch (...)
	{
		if (completionVkFence != VK_NULL_HANDLE)
		{
			m_fenceAllocator.FreeVkFence(&completionVkFence, 1);
		}
		for (const PreparedDependency& prepared : preparedDependencies)
		{
			if (prepared.signalSemaphore != VK_NULL_HANDLE)
			{
				m_semaphoreAllocator.Free(prepared.signalSemaphore);
			}
		}
		throw;
	}

	_CommitPreparedSubmission(
		queueStateIndex,
		state,
		submissionFrontier,
		consumerVersion,
		preparedDependencies,
		stagedDependencySemaphores,
		inSubmitInfo.m_completionFence,
		completionVkFence);
}

auto CommandQueueManager::_PeekDependencySignal(
	QueueDependency& inDependency)->DependencySignal
{
	const auto iter = m_dependencySemaphores.find(&inDependency);
	CHECK_TRUE(
		iter != m_dependencySemaphores.end(),
		"Queue dependency has no pending semaphore in this manager!");
	DependencySignal result;
	result.semaphore = iter->second;
	result.frontier = inDependency._GetPendingFrontier();
	return result;
}

void CommandQueueManager::_CommitPreparedSubmission(
	size_t inQueueStateIndex,
	QueueState& inoutState,
	const SubmissionFrontier& inSubmissionFrontier,
	uint64_t inConsumerVersion,
	const std::vector<PreparedDependency>& inDependencies,
	DependencySemaphoreMap& inoutStagedDependencySemaphores,
	HostFence* inCompletionFence,
	VkFence inCompletionVkFence) noexcept
{
	inoutState.tailFrontier = inSubmissionFrontier;
	for (const PreparedDependency& prepared : inDependencies)
	{
		assert(prepared.dependency != nullptr);
		assert(prepared.useWait || prepared.useSignal);
		if (prepared.useWait)
		{
			auto mappingIter = m_dependencySemaphores.find(prepared.dependency);
			assert(mappingIter != m_dependencySemaphores.end());
			assert(mappingIter->second == prepared.waitSemaphore);
			if (prepared.useSignal)
			{
				mappingIter->second = prepared.signalSemaphore;
			}
			else
			{
				m_dependencySemaphores.erase(mappingIter);
			}

			prepared.dependency->_CommitWait();
			inoutState.retiredSemaphores.push_back({
				prepared.waitSemaphore,
				inConsumerVersion });
		}
		else
		{
			assert(prepared.useSignal);
			auto node = inoutStagedDependencySemaphores.extract(prepared.dependency);
			assert(!node.empty());
			const auto insertResult = m_dependencySemaphores.insert(std::move(node));
			assert(insertResult.inserted);
		}

		if (prepared.useSignal)
		{
			prepared.dependency->_CommitSignal(inSubmissionFrontier);
		}
	}

	if (inCompletionFence != nullptr)
	{
		assert(inCompletionVkFence != VK_NULL_HANDLE);
		inoutState.pendingHostFences.push_back({
			inCompletionVkFence,
			inSubmissionFrontier,
			{} });
		HostFenceRecord& record = inoutState.pendingHostFences.back();
		record.callbacks = inCompletionFence->_CommitSubmit(
			inQueueStateIndex,
			inSubmissionFrontier);
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
		QueueState& state = m_queueStates[queueIndex];
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
		QueueState& state = m_queueStates[queueIndex];
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
	QueueState& state = m_queueStates[inFence.m_queueStateIndex];
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

void CommandQueueManager::_CollectRetiredSemaphores(const SubmissionFrontier& inCompletedFrontier)
{
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = m_queueStates[queueIndex];
		size_t reclaimCount = 0;
		while (
			reclaimCount < state.retiredSemaphores.size() &&
			inCompletedFrontier.Covers(
				queueIndex,
				state.retiredSemaphores[reclaimCount].consumerVersion))
		{
			++reclaimCount;
		}
		for (size_t retiredIndex = 0; retiredIndex < reclaimCount; ++retiredIndex)
		{
			m_semaphoreAllocator.Free(state.retiredSemaphores[retiredIndex].semaphore);
		}
		state.retiredSemaphores.erase(
			state.retiredSemaphores.begin(),
			state.retiredSemaphores.begin() + static_cast<std::ptrdiff_t>(reclaimCount));
	}

}

CommandQueueManager::~CommandQueueManager()
{
	if (!m_created)
	{
		return;
	}

	MyDevice::GetInstance().WaitIdle();
	SubmissionFrontier idleFrontier;
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		idleFrontier.Merge(m_queueStates[queueIndex].tailFrontier);
	}
	std::vector<HostFence::Callback> completedCallbacks;
	completedCallbacks = _AdvanceCompletion(idleFrontier);
	_RunCallbacks(std::move(completedCallbacks));
	m_dependencySemaphores.clear();
	m_semaphoreAllocator.Destroy();
	m_fenceAllocator.Destroy();
}
