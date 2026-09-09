#include "command_queue.h"
#include "allocator/semaphore_allocator.h"
#include "device.h"

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

namespace
{
	auto _RunRecycleActions(std::vector<std::function<void()>> actions)->void
	{
		for (auto& action : actions)
		{
			if (action)
			{
				action();
			}
		}
	}
}

auto CommandQueue::SubmitInfo::AddWaitQueueSignalChain(
	QueueSignalChain& inChain,
	VkPipelineStageFlags2 inWaitStage)->SubmitInfo&
{
	CHECK_TRUE(inWaitStage != 0, "Queue signal chain wait stage cannot be zero!");
	for (ChainEntry& entry : m_chainEntries)
	{
		if (entry.chain == &inChain)
		{
			entry.waitStage |= inWaitStage;
			entry.useWait = true;
			return *this;
		}
	}

	m_chainEntries.push_back({ &inChain, inWaitStage, true, false });
	return *this;
}

auto CommandQueue::SubmitInfo::AddSignalQueueSignalChain(QueueSignalChain& inChain)->SubmitInfo&
{
	for (ChainEntry& entry : m_chainEntries)
	{
		if (entry.chain == &inChain)
		{
			entry.useSignal = true;
			return *this;
		}
	}

	m_chainEntries.push_back({ &inChain, 0, false, true });
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

auto CommandQueue::SubmitInfo::SetCompletionFence(CompletionFence& inFence)->SubmitInfo&
{
	m_completionFence = &inFence;
	return *this;
}

CommandQueue::CommandQueue()
{
}

CommandQueue::~CommandQueue()
{
	_Deinit();
}

auto CommandQueue::_Init(QueueFamilyType inQueueFamilyType)->void
{
	CHECK_TRUE(inQueueFamilyType != QueueFamilyType::UNSET, "Invalid command queue family type!");
	CHECK_TRUE(m_vkQueue == VK_NULL_HANDLE, "Command queue is already initialized!");
	CHECK_TRUE(m_queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED, "Command queue family index is already initialized!");

	auto& device = MyDevice::GetInstance();
	m_vkQueue = device.GetQueueOfType(inQueueFamilyType);
	m_queueFamilyIndex = device.GetQueueFamilyIndexOfType(inQueueFamilyType);
	m_queueFamilyType = inQueueFamilyType;
	CHECK_TRUE(m_vkQueue != VK_NULL_HANDLE, "Invalid command queue!");
	CHECK_TRUE(m_queueFamilyIndex != VK_QUEUE_FAMILY_IGNORED, "Invalid command queue family index!");

	CommandPoolCreateInfo commandPoolCreateInfo;
	commandPoolCreateInfo.CustomizeQueueFamilyType(inQueueFamilyType);

	for (auto& frameCommandPools : m_commandPools)
	{
		for (auto& commandPool : frameCommandPools)
		{
			CHECK_TRUE(commandPool == nullptr, "Command pool is already initialized!");
			commandPool = std::make_unique<CommandPool>();
			commandPool->Create(&commandPoolCreateInfo);
		}
	}

	m_currentFrameIndex = FRAME_IN_FLIGHT_COUNT - 1;
}

auto CommandQueue::_Deinit()->void
{
	for (CompletionFence*& fence : m_frameCompletionFences)
	{
		if (fence != nullptr)
		{
			fence->Wait();
			fence = nullptr;
		}
	}
	if (!m_pendingRecycleActions.empty())
	{
		MyDevice::GetInstance().WaitIdle();
		_RunRecycleActions(std::move(m_pendingRecycleActions));
		m_pendingRecycleActions.clear();
	}

	m_recordedCommandBuffers.clear();

	for (auto& frameCommandPools : m_commandPools)
	{
		for (auto& commandPool : frameCommandPools)
		{
			if (commandPool != nullptr)
			{
				commandPool->Destroy();
				commandPool.reset();
			}
		}
	}

	m_vkQueue = VK_NULL_HANDLE;
	m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	m_queueFamilyType = QueueFamilyType::UNSET;
	m_currentFrameIndex = FRAME_IN_FLIGHT_COUNT - 1;
}

GraphicsQueue::GraphicsQueue() = default;

auto GraphicsQueue::Init()->void
{
	_Init(QueueFamilyType::GRAPHICS);
}

ComputeQueue::ComputeQueue() = default;

auto ComputeQueue::Init()->void
{
	_Init(QueueFamilyType::COMPUTE);
}

TransferQueue::TransferQueue() = default;

auto TransferQueue::Init()->void
{
	_Init(QueueFamilyType::TRANSFER);
}

auto CommandQueue::_GetCommandPool(uint8_t inFrameIndex, uint8_t inThreadIndex) const->CommandPool*
{
	CHECK_TRUE(inFrameIndex < FRAME_IN_FLIGHT_COUNT, "Command queue frame index out of range!");
	CHECK_TRUE(inThreadIndex < THREAD_COUNT, "Command queue thread index out of range!");

	const auto& commandPool = m_commandPools[inFrameIndex][inThreadIndex];
	CHECK_TRUE(commandPool != nullptr, "Command pool is not created!");

	return commandPool.get();
}

auto CommandQueue::_ResetFrameCommandPools(uint8_t inFrameIndex)->void
{
	CHECK_TRUE(inFrameIndex < FRAME_IN_FLIGHT_COUNT, "Command queue frame index out of range!");

	for (uint8_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
	{
		_GetCommandPool(inFrameIndex, threadIndex)->Reset();
	}
}

auto CommandQueue::Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&
{
	CompletionFence* frameFence = m_frameCompletionFences[m_currentFrameIndex];
	if (frameFence != nullptr && frameFence->IsInFlight())
	{
		// The slot was used by an earlier fenced submission. Waiting here is the
		// point at which the slot becomes legal to record into again.
		frameFence->Wait();
	}
	_RecordCommandBuffer(inCommandBuffers, inCount);
	return *this;
}

auto CommandQueue::Submit(SubmitInfo inSubmitInfo)->void
{
	CHECK_TRUE(m_vkQueue != VK_NULL_HANDLE, "Command queue is not created!");
	CHECK_TRUE(!m_recordedCommandBuffers.empty(), "No command buffers to submit!");

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	std::vector<VkSemaphore> consumedSemaphores;
	waitInfos.reserve(inSubmitInfo.m_chainEntries.size() + inSubmitInfo.m_waitSemaphoreEntries.size());
	signalInfos.reserve(inSubmitInfo.m_chainEntries.size() + inSubmitInfo.m_signalSemaphores.size());
	consumedSemaphores.reserve(inSubmitInfo.m_chainEntries.size());

	std::vector<VkSemaphore> preparedWaitSemaphores;
	std::vector<VkSemaphore> preparedSignalSemaphores;
	preparedWaitSemaphores.reserve(inSubmitInfo.m_chainEntries.size());
	preparedSignalSemaphores.reserve(inSubmitInfo.m_chainEntries.size());

	std::unordered_set<VkSemaphore> waitHandles;
	std::unordered_set<VkSemaphore> signalHandles;
	waitHandles.reserve(inSubmitInfo.m_chainEntries.size() + inSubmitInfo.m_waitSemaphoreEntries.size());
	signalHandles.reserve(inSubmitInfo.m_chainEntries.size() + inSubmitInfo.m_signalSemaphores.size());

	try
	{
		for (const SubmitInfo::WaitSemaphoreEntry& entry : inSubmitInfo.m_waitSemaphoreEntries)
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

		for (const SubmitInfo::ChainEntry& entry : inSubmitInfo.m_chainEntries)
		{
			CHECK_TRUE(entry.chain != nullptr, "Queue signal chain entry is null!");
			VkSemaphore waitSemaphore = VK_NULL_HANDLE;
			VkSemaphore signalSemaphore = VK_NULL_HANDLE;
			entry.chain->PrepareForSubmit(entry.useWait, entry.useSignal, waitSemaphore, signalSemaphore);
			preparedWaitSemaphores.push_back(waitSemaphore);
			preparedSignalSemaphores.push_back(signalSemaphore);

			if (waitSemaphore != VK_NULL_HANDLE)
			{
				CHECK_TRUE(waitHandles.insert(waitSemaphore).second,
					"A semaphore cannot appear in both duplicate queue wait entries!");
				VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
				waitInfo.semaphore = waitSemaphore;
				waitInfo.stageMask = entry.waitStage;
				waitInfos.push_back(waitInfo);
			}

			if (signalSemaphore != VK_NULL_HANDLE)
			{
				CHECK_TRUE(signalHandles.insert(signalSemaphore).second,
					"A semaphore cannot appear in both duplicate queue signal entries!");
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

		if (inSubmitInfo.m_completionFence != nullptr)
		{
			inSubmitInfo.m_completionFence->PrepareForSubmit();
		}

		std::vector<VkCommandBufferSubmitInfo> commandInfos;
		commandInfos.reserve(m_recordedCommandBuffers.size());
		for (VkCommandBuffer commandBuffer : m_recordedCommandBuffers)
		{
			VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			commandInfo.commandBuffer = commandBuffer;
			commandInfos.push_back(commandInfo);
		}

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
		submitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
		submitInfo.pCommandBufferInfos = commandInfos.data();
		submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
		submitInfo.pSignalSemaphoreInfos = signalInfos.empty() ? nullptr : signalInfos.data();

		const VkFence vkFence = inSubmitInfo.m_completionFence == nullptr
			? VK_NULL_HANDLE
			: inSubmitInfo.m_completionFence->m_vkFence;
		VK_CHECK(vkQueueSubmit2(m_vkQueue, 1, &submitInfo, vkFence),
			"Failed to submit command queue!");

		for (size_t chainIndex = 0; chainIndex < inSubmitInfo.m_chainEntries.size(); ++chainIndex)
		{
			const SubmitInfo::ChainEntry& entry = inSubmitInfo.m_chainEntries[chainIndex];
			const VkSemaphore consumedSemaphore = entry.chain->CommitSubmit(
				entry.useWait,
				preparedWaitSemaphores[chainIndex],
				preparedSignalSemaphores[chainIndex]);
			if (consumedSemaphore != VK_NULL_HANDLE)
			{
				consumedSemaphores.push_back(consumedSemaphore);
			}
		}

		std::vector<std::function<void()>> recycleActions;
		recycleActions.reserve(consumedSemaphores.size() + 1);
		for (VkSemaphore semaphore : consumedSemaphores)
		{
			recycleActions.push_back([semaphore]
			{
				SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
				if (allocator != nullptr)
				{
					allocator->Free(semaphore);
				}
			});
		}

		if (inSubmitInfo.m_completionFence != nullptr)
		{
			const uint8_t submittedFrameIndex = m_currentFrameIndex;
			CompletionFence* completionFence = inSubmitInfo.m_completionFence;
			recycleActions.push_back([this, submittedFrameIndex, completionFence]
			{
				_ResetFrameCommandPools(submittedFrameIndex);
				if (m_frameCompletionFences[submittedFrameIndex] == completionFence)
				{
					m_frameCompletionFences[submittedFrameIndex] = nullptr;
				}
			});

			std::vector<std::function<void()>> allActions = std::move(m_pendingRecycleActions);
			m_pendingRecycleActions.clear();
			allActions.insert(
				allActions.end(),
				std::make_move_iterator(recycleActions.begin()),
				std::make_move_iterator(recycleActions.end()));
			inSubmitInfo.m_completionFence->AddCallback(
				[actions = std::move(allActions)]() mutable
				{
					_RunRecycleActions(std::move(actions));
				});
			inSubmitInfo.m_completionFence->CommitSubmit();
			m_frameCompletionFences[m_currentFrameIndex] = inSubmitInfo.m_completionFence;
			m_currentFrameIndex = static_cast<uint8_t>((m_currentFrameIndex + 1) % FRAME_IN_FLIGHT_COUNT);
		}
		else
		{
			m_pendingRecycleActions.insert(
				m_pendingRecycleActions.end(),
				std::make_move_iterator(recycleActions.begin()),
				std::make_move_iterator(recycleActions.end()));
		}

		m_recordedCommandBuffers.clear();
	}
	catch (...)
	{
		for (auto iter = inSubmitInfo.m_chainEntries.rbegin(); iter != inSubmitInfo.m_chainEntries.rend(); ++iter)
		{
			if (iter->chain != nullptr)
			{
				iter->chain->AbortSubmit();
			}
		}
		throw;
	}
}

auto CommandQueue::WaitTillDone()->void
{
	CHECK_TRUE(m_vkQueue != VK_NULL_HANDLE, "Command queue is not created!");

	MyDevice::GetInstance().WaitIdle();
	for (CompletionFence*& fence : m_frameCompletionFences)
	{
		if (fence != nullptr)
		{
			fence->Wait();
			fence = nullptr;
		}
	}

	if (!m_pendingRecycleActions.empty())
	{
		_RunRecycleActions(std::move(m_pendingRecycleActions));
		m_pendingRecycleActions.clear();
	}
}

auto CommandQueue::Submit()->void
{
	Submit(SubmitInfo{});
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

		// Rendering state cannot be inherited by another VkCommandBuffer. Keep a stream
		// containing rendering commands in one physical command buffer for now.
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

	// Assign contiguous command-buffer batches to per-thread command pools. The batches
	// are independent after this point and can be recorded on worker threads later.
	for (uint8_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
	{
		const size_t threadBatchCount = baseBatchCountPerThread + (threadIndex < extraBatchCount ? 1 : 0);
		if (threadBatchCount == 0)
		{
			continue;
		}

		CommandPool* commandPool = _GetCommandPool(m_currentFrameIndex, threadIndex);
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

	// Record per-pool batches as isolated units. This is single-threaded for now, but the
	// outer loop is the intended future parallelization boundary.
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
