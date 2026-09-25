#include "command_queue.h"

#include "allocator/semaphore_allocator.h"
#include "device.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace
{
	constexpr size_t COMMAND_COUNT_PER_VK_COMMAND_BUFFER = 256;

	struct _CommandBufferRecordBatch final
	{
		std::vector<const Command*> commands;
		VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
	};

	struct _CommandPoolRecordBatch final
	{
		std::vector<size_t> commandBufferBatchIndices;
	};

	auto _RecordCommandBufferBatch(const _CommandBufferRecordBatch& inBatch)->void
	{
		CHECK_TRUE(inBatch.vkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		VK_CHECK(vkBeginCommandBuffer(inBatch.vkCommandBuffer, &beginInfo), "Failed to begin command buffer!");

		for (const Command* command : inBatch.commands)
		{
			CHECK_TRUE(command != nullptr, "Invalid command!");
			command->Record(inBatch.vkCommandBuffer);
		}

		VK_CHECK(vkEndCommandBuffer(inBatch.vkCommandBuffer), "Failed to end command buffer!");
	}
}

auto CommandQueue::SubmitInfo::AddWaitQueueDependency(
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

auto CommandQueue::SubmitInfo::AddSignalQueueDependency(QueueDependency& inDependency)->SubmitInfo&
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

auto CommandQueue::SubmitInfo::AddWaitSemaphore(
	VkSemaphore inSemaphore,
	VkPipelineStageFlags2 inWaitStage)->SubmitInfo&
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Invalid wait semaphore!");
	CHECK_TRUE(inWaitStage != 0, "Invalid wait stage!");
	m_waitSemaphoreEntries.push_back({ inSemaphore, inWaitStage });
	return *this;
}

auto CommandQueue::SubmitInfo::AddSemaphoreToSignal(VkSemaphore inSemaphore)->SubmitInfo&
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Invalid signal semaphore!");
	m_signalSemaphores.push_back(inSemaphore);
	return *this;
}

auto CommandQueue::SubmitInfo::SetFence(HostFence& inFence)->SubmitInfo&
{
	m_completionFence = &inFence;
	return *this;
}

CommandQueue::CommandQueue() = default;

CommandQueue::~CommandQueue()
{
	_Deinit();
}

void CommandQueue::_Init(CommandQueueManager& inCommandQueueManager, QueueFamilyType inQueueFamilyType)
{
	CHECK_TRUE(inQueueFamilyType != QueueFamilyType::UNSET, "Invalid command queue family type!");
	CHECK_TRUE(m_commandQueueManager == nullptr, "Command queue is already initialized!");

	m_commandQueueManager = &inCommandQueueManager;
	m_queueStateIndex = inCommandQueueManager._GetQueueStateIndex(inQueueFamilyType);

	CommandPoolCreateInfo commandPoolCreateInfo;
	commandPoolCreateInfo.CustomizeQueueFamilyType(inQueueFamilyType);
	for (auto& commandPool : m_commandPools)
	{
		CHECK_TRUE(commandPool == nullptr, "Command pool is already initialized!");
		commandPool = std::make_unique<CommandPool>();
		commandPool->Create(&commandPoolCreateInfo);
	}
}

void CommandQueue::_Deinit()
{
	m_recordedCommandBuffers.clear();
	for (auto& commandPool : m_commandPools)
	{
		if (commandPool != nullptr)
		{
			commandPool->Destroy();
			commandPool.reset();
		}
	}

	m_commandQueueManager = nullptr;
	m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
}

void CommandQueue::_ResetCommandPools()
{
}

auto CommandQueue::_GetCommandPool(uint8_t inThreadIndex) const->CommandPool*
{
	CHECK_TRUE(inThreadIndex < THREAD_COUNT, "Command queue thread index out of range!");
	const auto& commandPool = m_commandPools[inThreadIndex];
	CHECK_TRUE(commandPool != nullptr, "Command pool is not created!");
	return commandPool.get();
}

auto CommandQueue::Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&
{
	_RecordCommandBuffer(inCommandBuffers, inCount);
	return *this;
}

void CommandQueue::Submit(SubmitInfo inSubmitInfo)
{
	CHECK_TRUE(m_commandQueueManager != nullptr, "Command queue is not initialized!");
	CHECK_TRUE(!m_recordedCommandBuffers.empty(), "No command buffers to submit!");
	std::vector<VkCommandBuffer> commandBuffers = std::move(m_recordedCommandBuffers);
	m_commandQueueManager->_Submit(
		m_queueStateIndex,
		commandBuffers.data(),
		commandBuffers.size(),
		std::move(inSubmitInfo));
}

void CommandQueue::_SubmitVkCommandBuffers(
	const VkCommandBuffer* inCommandBuffers,
	size_t inCount,
	SubmitInfo inSubmitInfo)
{
	CHECK_TRUE(m_commandQueueManager != nullptr, "Command queue is not initialized!");
	m_commandQueueManager->_Submit(
		m_queueStateIndex,
		inCommandBuffers,
		inCount,
		std::move(inSubmitInfo));
}

GraphicsQueue::GraphicsQueue() = default;

void GraphicsQueue::Init(CommandQueueManager& inCommandQueueManager)
{
	_Init(inCommandQueueManager, QueueFamilyType::GRAPHICS);
}

ComputeQueue::ComputeQueue() = default;

void ComputeQueue::Init(CommandQueueManager& inCommandQueueManager)
{
	_Init(inCommandQueueManager, QueueFamilyType::COMPUTE);
}

TransferQueue::TransferQueue() = default;

void TransferQueue::Init(CommandQueueManager& inCommandQueueManager)
{
	_Init(inCommandQueueManager, QueueFamilyType::TRANSFER);
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
		QueueState& state = *m_queueStates[queueIndex];
		if (state.familyIndex == inFamilyIndex && state.queueIndex == inQueueIndex)
		{
			CHECK_TRUE(state.vkQueue == inVkQueue, "Queue identity maps to different Vulkan handles!");
			m_roleToQueueState[roleIndex] = queueIndex;
			return queueIndex;
		}
	}

	CHECK_TRUE(m_queueStateCount < SubmissionFrontier::MAX_QUEUE_COUNT, "Too many physical command queues!");
	const size_t queueStateIndex = m_queueStateCount++;
	m_queueStates[queueStateIndex] = std::make_unique<QueueState>();
	QueueState& state = *m_queueStates[queueStateIndex];
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
	CHECK_TRUE(m_queueStates[inQueueStateIndex] != nullptr, "Command queue state is not created!");
	return *m_queueStates[inQueueStateIndex];
}

auto CommandQueueManager::_GetQueueState(size_t inQueueStateIndex) const->const QueueState&
{
	CHECK_TRUE(inQueueStateIndex < m_queueStateCount, "Command queue state index is out of range!");
	CHECK_TRUE(m_queueStates[inQueueStateIndex] != nullptr, "Command queue state is not created!");
	return *m_queueStates[inQueueStateIndex];
}

void CommandQueueManager::_Submit(
	size_t inQueueStateIndex,
	const VkCommandBuffer* inCommandBuffers,
	size_t inCommandBufferCount,
	CommandQueue::SubmitInfo inSubmitInfo)
{
	CHECK_TRUE(
		inCommandBufferCount == 0 || inCommandBuffers != nullptr,
		"Command buffer pointer is null for a non-empty submission!");

	QueueState& state = _GetQueueState(inQueueStateIndex);
	if (inSubmitInfo.m_completionFence != nullptr)
	{
		inSubmitInfo.m_completionFence->_PrepareForSubmit();
	}

	std::lock_guard<std::mutex> submitLock(state.submitMutex);
	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	std::vector<QueueDependency::PendingSignal> dependencyWaitSignals(
		inSubmitInfo.m_dependencyEntries.size());
	std::vector<VkSemaphore> dependencySignalSemaphores(
		inSubmitInfo.m_dependencyEntries.size(), VK_NULL_HANDLE);
	std::unordered_set<VkSemaphore> waitHandles;
	std::unordered_set<VkSemaphore> signalHandles;
	SubmissionFrontier submissionFrontier = state.tailFrontier;

	waitInfos.reserve(inSubmitInfo.m_dependencyEntries.size() + inSubmitInfo.m_waitSemaphoreEntries.size());
	signalInfos.reserve(inSubmitInfo.m_dependencyEntries.size() + inSubmitInfo.m_signalSemaphores.size());
	waitHandles.reserve(inSubmitInfo.m_dependencyEntries.size() + inSubmitInfo.m_waitSemaphoreEntries.size());
	signalHandles.reserve(inSubmitInfo.m_dependencyEntries.size() + inSubmitInfo.m_signalSemaphores.size());
	{
		std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
		m_abandonedSemaphores.reserve(
			m_abandonedSemaphores.size() + inSubmitInfo.m_dependencyEntries.size());
	}

	try
	{
		for (const CommandQueue::SubmitInfo::WaitSemaphoreEntry& entry : inSubmitInfo.m_waitSemaphoreEntries)
		{
			CHECK_TRUE(waitHandles.insert(entry.semaphore).second,
				"A semaphore cannot appear more than once in queue wait list!");
			VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitInfo.semaphore = entry.semaphore;
			waitInfo.stageMask = entry.stage;
			waitInfos.push_back(waitInfo);
		}

		for (VkSemaphore semaphore : inSubmitInfo.m_signalSemaphores)
		{
			CHECK_TRUE(signalHandles.insert(semaphore).second,
				"A semaphore cannot appear more than once in queue signal list!");
			VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			signalInfo.semaphore = semaphore;
			signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			signalInfos.push_back(signalInfo);
		}

		for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
		{
			const CommandQueue::SubmitInfo::DependencyEntry& entry = inSubmitInfo.m_dependencyEntries[dependencyIndex];
			CHECK_TRUE(entry.dependency != nullptr, "Queue dependency entry is null!");
			CHECK_TRUE(entry.useWait || entry.useSignal,
				"Queue dependency submit has no wait or signal operation!");

			if (entry.useWait)
			{
				dependencyWaitSignals[dependencyIndex] = entry.dependency->_Take();
				submissionFrontier.Merge(dependencyWaitSignals[dependencyIndex].frontier);
			}
			else
			{
				CHECK_TRUE(!entry.dependency->_HasSemaphore(),
					"A pending queue dependency must be waited before it can be signaled again!");
			}

			if (entry.useSignal)
			{
				SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
				CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
				{
					std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
					dependencySignalSemaphores[dependencyIndex] = allocator->Allocate();
				}
			}

			const VkSemaphore waitSemaphore = dependencyWaitSignals[dependencyIndex].semaphore;
			const VkSemaphore signalSemaphore = dependencySignalSemaphores[dependencyIndex];
			if (waitSemaphore != VK_NULL_HANDLE)
			{
				CHECK_TRUE(waitHandles.insert(waitSemaphore).second,
					"A semaphore cannot appear in duplicate queue wait entries!");
				VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				waitInfo.semaphore = waitSemaphore;
				waitInfo.stageMask = entry.waitStage;
				waitInfos.push_back(waitInfo);
			}
			if (signalSemaphore != VK_NULL_HANDLE)
			{
				CHECK_TRUE(signalHandles.insert(signalSemaphore).second,
					"A semaphore cannot appear in duplicate queue signal entries!");
				VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				signalInfo.semaphore = signalSemaphore;
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				signalInfos.push_back(signalInfo);
			}
		}

		for (VkSemaphore semaphore : waitHandles)
		{
			CHECK_TRUE(signalHandles.find(semaphore) == signalHandles.end(),
				"A semaphore cannot be both waited and signaled in one queue submit!");
		}

		submissionFrontier.Advance(inQueueStateIndex);
		state.retiredSemaphores.reserve(
			state.retiredSemaphores.size() + inSubmitInfo.m_dependencyEntries.size());

		std::vector<VkCommandBufferSubmitInfo> commandInfos;
		commandInfos.reserve(inCommandBufferCount);
		for (size_t commandBufferIndex = 0; commandBufferIndex < inCommandBufferCount; ++commandBufferIndex)
		{
			CHECK_TRUE(inCommandBuffers[commandBufferIndex] != VK_NULL_HANDLE, "Invalid command buffer!");
			VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			commandInfo.commandBuffer = inCommandBuffers[commandBufferIndex];
			commandInfos.push_back(commandInfo);
		}

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
		submitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
		submitInfo.pCommandBufferInfos = commandInfos.empty() ? nullptr : commandInfos.data();
		submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
		submitInfo.pSignalSemaphoreInfos = signalInfos.empty() ? nullptr : signalInfos.data();

		const VkFence vkFence = inSubmitInfo.m_completionFence == nullptr
			? VK_NULL_HANDLE
			: inSubmitInfo.m_completionFence->m_vkFence;
		VK_CHECK(vkQueueSubmit2(state.vkQueue, 1, &submitInfo, vkFence),
			"Failed to submit command queue!");
	}
	catch (...)
	{
		SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
		if (allocator != nullptr)
		{
			std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
			for (const QueueDependency::PendingSignal& pendingSignal : dependencyWaitSignals)
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
					allocator->Free(semaphore);
				}
			}
		}
		throw;
	}

	// vkQueueSubmit2 succeeded. From this point the frontier and synchronization
	// ownership must be committed and cannot be rolled back as an unsubmitted batch.
	state.tailFrontier = submissionFrontier;
	for (size_t dependencyIndex = 0; dependencyIndex < inSubmitInfo.m_dependencyEntries.size(); ++dependencyIndex)
	{
		const CommandQueue::SubmitInfo::DependencyEntry& entry = inSubmitInfo.m_dependencyEntries[dependencyIndex];
		if (dependencySignalSemaphores[dependencyIndex] != VK_NULL_HANDLE)
		{
			entry.dependency->_Store(dependencySignalSemaphores[dependencyIndex], submissionFrontier);
		}
		if (dependencyWaitSignals[dependencyIndex].semaphore != VK_NULL_HANDLE)
		{
			state.retiredSemaphores.push_back({
				dependencyWaitSignals[dependencyIndex].semaphore,
				submissionFrontier.GetVersion(inQueueStateIndex) });
		}
	}

	if (inSubmitInfo.m_completionFence != nullptr)
	{
		inSubmitInfo.m_completionFence->AddCallback(
			[this, submissionFrontier]
			{
				_NotifyCompletion(submissionFrontier);
			});
		inSubmitInfo.m_completionFence->_CommitSubmit();
	}
}

void CommandQueueManager::_NotifyCompletion(const SubmissionFrontier& inSubmissionFrontier)
{
	SubmissionFrontier completedFrontier;
	{
		std::lock_guard<std::mutex> completionLock(m_completionMutex);
		m_completedFrontier.Merge(inSubmissionFrontier);
		completedFrontier = m_completedFrontier;
	}
	_CollectRetiredSemaphores(completedFrontier);
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
	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	if (allocator == nullptr)
	{
		return;
	}

	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		std::lock_guard<std::mutex> submitLock(state.submitMutex);
		std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
		const size_t reclaimCount = _GetRetiredSemaphoreReclaimCount(state, queueIndex, inCompletedFrontier);
		for (size_t retiredIndex = 0; retiredIndex < reclaimCount; ++retiredIndex)
		{
			allocator->Free(state.retiredSemaphores[retiredIndex].semaphore);
		}
		state.retiredSemaphores.erase(
			state.retiredSemaphores.begin(),
			state.retiredSemaphores.begin() + static_cast<std::ptrdiff_t>(reclaimCount));

		const auto abandonedPartition = std::stable_partition(
			m_abandonedSemaphores.begin(),
			m_abandonedSemaphores.end(),
			[&inCompletedFrontier](const AbandonedSemaphore& abandoned)
			{
				return !inCompletedFrontier.Covers(abandoned.producerFrontier);
			});
		for (auto iter = abandonedPartition; iter != m_abandonedSemaphores.end(); ++iter)
		{
			allocator->Discard(iter->semaphore);
		}
		m_abandonedSemaphores.erase(abandonedPartition, m_abandonedSemaphores.end());
	}
}

void CommandQueueManager::_DrainRetiredSemaphores()
{
	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	if (allocator == nullptr)
	{
		return;
	}

	std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		for (const RetiredSemaphore& retired : state.retiredSemaphores)
		{
			allocator->Free(retired.semaphore);
		}
		state.retiredSemaphores.clear();
	}
	for (const AbandonedSemaphore& abandoned : m_abandonedSemaphores)
	{
		allocator->Discard(abandoned.semaphore);
	}
	m_abandonedSemaphores.clear();
}

void CommandQueueManager::_WaitIdleAndDiscardDependencies(
	QueueDependency* const* inDependencies,
	size_t inCount)
{
	CHECK_TRUE(inDependencies != nullptr || inCount == 0, "Dependency list is null!");
	std::array<std::unique_lock<std::mutex>, SubmissionFrontier::MAX_QUEUE_COUNT> submitLocks;
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		submitLocks[queueIndex] = std::unique_lock<std::mutex>(_GetQueueState(queueIndex).submitMutex);
	}

	MyDevice::GetInstance().WaitIdle();

	SubmissionFrontier idleFrontier;
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		idleFrontier.Merge(_GetQueueState(queueIndex).tailFrontier);
	}
	{
		std::lock_guard<std::mutex> completionLock(m_completionMutex);
		m_completedFrontier.Merge(idleFrontier);
	}

	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
	std::lock_guard<std::mutex> semaphoreLock(m_semaphoreMutex);
	for (size_t queueIndex = 0; queueIndex < m_queueStateCount; ++queueIndex)
	{
		QueueState& state = _GetQueueState(queueIndex);
		for (const RetiredSemaphore& retired : state.retiredSemaphores)
		{
			allocator->Free(retired.semaphore);
		}
		state.retiredSemaphores.clear();
	}
	for (const AbandonedSemaphore& abandoned : m_abandonedSemaphores)
	{
		allocator->Discard(abandoned.semaphore);
	}
	m_abandonedSemaphores.clear();

	for (size_t dependencyIndex = 0; dependencyIndex < inCount; ++dependencyIndex)
	{
		QueueDependency* dependency = inDependencies[dependencyIndex];
		if (dependency != nullptr && dependency->_HasSemaphore())
		{
			const QueueDependency::PendingSignal pendingSignal = dependency->_Take();
			allocator->Discard(pendingSignal.semaphore);
		}
	}
}

void CommandQueueManager::Create()
{
	CHECK_TRUE(!m_created, "Command queue manager is already created!");
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

	m_graphicsQueue = std::make_unique<GraphicsQueue>();
	m_computeQueue = std::make_unique<ComputeQueue>();
	m_transferQueue = std::make_unique<TransferQueue>();
	m_graphicsQueue->Init(*this);
	m_computeQueue->Init(*this);
	m_transferQueue->Init(*this);
	m_created = true;
}

void CommandQueueManager::Destroy()
{
	if (!m_created)
	{
		return;
	}

	MyDevice::GetInstance().WaitIdle();
	_DrainRetiredSemaphores();
	m_transferQueue.reset();
	m_computeQueue.reset();
	m_graphicsQueue.reset();
	for (auto& state : m_queueStates)
	{
		state.reset();
	}
	m_roleToQueueState.fill(SubmissionFrontier::MAX_QUEUE_COUNT);
	m_queueStateCount = 0;
	m_completedFrontier = {};
	m_created = false;
}

CommandQueueManager::~CommandQueueManager()
{
	Destroy();
}

auto CommandQueue::_RecordCommandBuffer(CommandBuffer* inCommandBuffers, size_t inCount)->void
{
	if (inCount == 0)
	{
		return;
	}

	CHECK_TRUE(inCommandBuffers != nullptr, "No command buffers!");

	std::vector<CommandBuffer*> consumedCommandBuffers;
	consumedCommandBuffers.reserve(inCount);
	std::vector<_CommandBufferRecordBatch> commandBufferBatches;
	_CommandBufferRecordBatch currentBatch;

	for (size_t commandBufferIndex = 0; commandBufferIndex < inCount; ++commandBufferIndex)
	{
		CommandBuffer& commandBuffer = inCommandBuffers[commandBufferIndex];
		CHECK_TRUE(
			std::holds_alternative<std::monostate>(commandBuffer.m_renderingScopeState),
			"Command buffer has an active rendering scope!");
		consumedCommandBuffers.push_back(&commandBuffer);

		if (commandBuffer.m_hasRenderingCommands)
		{
			if (!currentBatch.commands.empty())
			{
				commandBufferBatches.push_back(std::move(currentBatch));
				currentBatch = _CommandBufferRecordBatch{};
			}

			if (!commandBuffer.m_commands.empty())
			{
				_CommandBufferRecordBatch renderingBatch;
				renderingBatch.commands = commandBuffer.m_commands;
				commandBufferBatches.push_back(std::move(renderingBatch));
			}
			continue;
		}

		for (const Command* command : commandBuffer.m_commands)
		{
			if (currentBatch.commands.size() >= COMMAND_COUNT_PER_VK_COMMAND_BUFFER)
			{
				commandBufferBatches.push_back(std::move(currentBatch));
				currentBatch = _CommandBufferRecordBatch{};
			}
			currentBatch.commands.push_back(command);
		}
	}

	if (!currentBatch.commands.empty())
	{
		commandBufferBatches.push_back(std::move(currentBatch));
	}

	if (commandBufferBatches.empty())
	{
		for (CommandBuffer* commandBuffer : consumedCommandBuffers)
		{
			commandBuffer->m_commands.clear();
			commandBuffer->m_ownedCommands.clear();
			commandBuffer->m_renderingScopeState = std::monostate{};
			commandBuffer->m_hasRenderingCommands = false;
		}
		return;
	}

	std::array<_CommandPoolRecordBatch, THREAD_COUNT> commandPoolBatches;
	const size_t batchCount = commandBufferBatches.size();
	const size_t baseBatchCountPerThread = batchCount / THREAD_COUNT;
	const size_t extraBatchCount = batchCount % THREAD_COUNT;
	size_t nextBatchIndex = 0;

	for (uint8_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
	{
		const size_t threadBatchCount = baseBatchCountPerThread + (threadIndex < extraBatchCount ? 1 : 0);
		if (threadBatchCount == 0)
		{
			continue;
		}

		CommandPool* commandPool = _GetCommandPool(threadIndex);
		_CommandPoolRecordBatch& commandPoolBatch = commandPoolBatches[threadIndex];
		commandPoolBatch.commandBufferBatchIndices.reserve(threadBatchCount);

		for (size_t localBatchIndex = 0; localBatchIndex < threadBatchCount; ++localBatchIndex)
		{
			_CommandBufferRecordBatch& commandBufferBatch = commandBufferBatches[nextBatchIndex];
			commandBufferBatch.vkCommandBuffer = commandPool->AllocateOrGetCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			commandPoolBatch.commandBufferBatchIndices.push_back(nextBatchIndex);
			++nextBatchIndex;
		}
	}

	for (const _CommandBufferRecordBatch& commandBufferBatch : commandBufferBatches)
	{
		CHECK_TRUE(commandBufferBatch.vkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
		m_recordedCommandBuffers.push_back(commandBufferBatch.vkCommandBuffer);
	}

	for (const _CommandPoolRecordBatch& commandPoolBatch : commandPoolBatches)
	{
		for (size_t commandBufferBatchIndex : commandPoolBatch.commandBufferBatchIndices)
		{
			_RecordCommandBufferBatch(commandBufferBatches[commandBufferBatchIndex]);
		}
	}

	for (CommandBuffer* commandBuffer : consumedCommandBuffers)
	{
		commandBuffer->m_commands.clear();
		commandBuffer->m_ownedCommands.clear();
		commandBuffer->m_renderingScopeState = std::monostate{};
		commandBuffer->m_hasRenderingCommands = false;
	}
}
