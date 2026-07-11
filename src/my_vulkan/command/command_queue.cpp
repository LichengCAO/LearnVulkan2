#include "command_queue.h"
#include "allocator/fence_allocator.h"
#include "device.h"

namespace
{
	constexpr size_t COMMAND_COUNT_PER_VK_COMMAND_BUFFER = 256;

	struct _ScopeRecordItem final
	{
		CommandBuffer::Scope scope;
		size_t commandCount = 0;
	};

	struct _CommandBufferRecordBatch final
	{
		std::vector<_ScopeRecordItem> scopes;
		size_t commandCount = 0;
		VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
	};

	struct _CommandPoolRecordBatch final
	{
		std::vector<size_t> commandBufferBatchIndices;
	};

	auto _CountRenderPassScopeCommands(const CommandBuffer::RenderPassScope& inScope)->size_t
	{
		size_t result = 0;
		for (const CommandBuffer::SubpassScope& subpassScope : inScope.subpassScopes)
		{
			result += subpassScope.commands.size();
		}

		return result;
	}

	auto _RecordPrimaryScope(VkCommandBuffer inVkCommandBuffer, const CommandBuffer::PrimaryScope& inScope)->void
	{
		for (const Command* command : inScope.commands)
		{
			CHECK_TRUE(command != nullptr, "Invalid command!");
			command->Record(inVkCommandBuffer);
		}
	}

	auto _RecordRenderPassScope(VkCommandBuffer inVkCommandBuffer, const CommandBuffer::RenderPassScope& inScope)->void
	{
		CHECK_TRUE(inScope.renderPass != VK_NULL_HANDLE, "Invalid render pass!");
		CHECK_TRUE(inScope.framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");
		CHECK_TRUE(inScope.contents == VK_SUBPASS_CONTENTS_INLINE, "Only inline render pass scopes are supported!");

		BeginRenderPassCommand::Parameters beginParameters;
		beginParameters.renderPass = inScope.renderPass;
		beginParameters.framebuffer = inScope.framebuffer;
		beginParameters.renderArea = inScope.renderArea;
		beginParameters.clearValues = inScope.clearValues;
		beginParameters.contents = inScope.contents;
		beginParameters.next = inScope.next;

		BeginRenderPassCommand beginRenderPassCommand;
		beginRenderPassCommand.SetParameters(beginParameters).Record(inVkCommandBuffer);

		for (size_t subpassIndex = 0; subpassIndex < inScope.subpassScopes.size(); ++subpassIndex)
		{
			const CommandBuffer::SubpassScope& subpassScope = inScope.subpassScopes[subpassIndex];
			for (const Command* command : subpassScope.commands)
			{
				CHECK_TRUE(command != nullptr, "Invalid command!");
				command->Record(inVkCommandBuffer);
			}

			if (subpassIndex + 1 < inScope.subpassScopes.size())
			{
				vkCmdNextSubpass(inVkCommandBuffer, inScope.contents);
			}
		}

		EndRenderPassCommand{}.Record(inVkCommandBuffer);
	}

	auto _RecordScope(VkCommandBuffer inVkCommandBuffer, const _ScopeRecordItem& inScope)->void
	{
		if (std::holds_alternative<CommandBuffer::PrimaryScope>(inScope.scope))
		{
			_RecordPrimaryScope(inVkCommandBuffer, std::get<CommandBuffer::PrimaryScope>(inScope.scope));
		}
		else if (std::holds_alternative<CommandBuffer::RenderPassScope>(inScope.scope))
		{
			_RecordRenderPassScope(inVkCommandBuffer, std::get<CommandBuffer::RenderPassScope>(inScope.scope));
		}
		else
		{
			CHECK_TRUE(false, "Unsupported command buffer scope!");
		}
	}

	auto _RecordCommandBufferBatch(const _CommandBufferRecordBatch& inBatch)->void
	{
		CHECK_TRUE(inBatch.vkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		VK_CHECK(vkBeginCommandBuffer(inBatch.vkCommandBuffer, &beginInfo), "Failed to begin command buffer!");

		for (const _ScopeRecordItem& scope : inBatch.scopes)
		{
			_RecordScope(inBatch.vkCommandBuffer, scope);
		}

		VK_CHECK(vkEndCommandBuffer(inBatch.vkCommandBuffer), "Failed to end command buffer!");
	}
}

auto CommandQueue::SyncInfo::AddWaitSemaphore(VkSemaphore inSemaphore, VkPipelineStageFlags inStage)->SyncInfo&
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Invalid wait semaphore!");
	CHECK_TRUE(inStage != 0, "Invalid wait stage!");

	m_waitSemaphores.push_back(inSemaphore);
	m_waitStages.push_back(inStage);
	return *this;
}

auto CommandQueue::SyncInfo::AddSemaphoreToSignal(VkSemaphore inSemaphore)->SyncInfo&
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Invalid signal semaphore!");
	m_signalSemaphores.push_back(inSemaphore);
	return *this;
}

CommandQueue::CommandQueue()
{
	m_uptrFenceAllocator = std::make_unique<FenceAllocator>();
	m_uptrFenceAllocator->Create();
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

	if (m_uptrFenceAllocator == nullptr)
	{
		m_uptrFenceAllocator = std::make_unique<FenceAllocator>();
	}
	m_uptrFenceAllocator->Create();

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
	for (uint8_t frameIndex = 0; frameIndex < FRAME_IN_FLIGHT_COUNT; ++frameIndex)
	{
		_WaitFrameFences(frameIndex);
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

	if (m_uptrFenceAllocator != nullptr)
	{
		m_uptrFenceAllocator->Destroy();
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

auto CommandQueue::_WaitFrameFences(uint8_t inFrameIndex)->void
{
	CHECK_TRUE(inFrameIndex < FRAME_IN_FLIGHT_COUNT, "Command queue frame index out of range!");

	std::vector<VkFence>& frameFences = m_frameFences[inFrameIndex];
	if (frameFences.empty())
	{
		return;
	}

	CHECK_TRUE(m_uptrFenceAllocator != nullptr, "Command queue fence allocator is not created!");

	VK_CHECK(
		MyDevice::GetInstance().WaitForFences(frameFences, true, UINT64_MAX),
		"Failed to wait for command queue frame fences!");

	m_uptrFenceAllocator->FreeVkFence(frameFences.data(), frameFences.size());
	frameFences.clear();
}

auto CommandQueue::_ResetFrameCommandPools(uint8_t inFrameIndex)->void
{
	CHECK_TRUE(inFrameIndex < FRAME_IN_FLIGHT_COUNT, "Command queue frame index out of range!");

	for (uint8_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
	{
		_GetCommandPool(inFrameIndex, threadIndex)->Reset();
	}
}

auto CommandQueue::StartFrame()->void
{
	m_currentFrameIndex = static_cast<uint8_t>((m_currentFrameIndex + 1) % FRAME_IN_FLIGHT_COUNT);

	_WaitFrameFences(m_currentFrameIndex);
	_ResetFrameCommandPools(m_currentFrameIndex);
}

auto CommandQueue::Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&
{
	_RecordCommandBuffer(inCommandBuffers, inCount);
	return *this;
}

auto CommandQueue::Submit(SyncInfo inSyncInfo)->void
{
	CHECK_TRUE(m_vkQueue != VK_NULL_HANDLE, "Command queue is not created!");
	CHECK_TRUE(!m_recordedCommandBuffers.empty(), "No command buffers to submit!");
	CHECK_TRUE(
		inSyncInfo.m_waitSemaphores.size() == inSyncInfo.m_waitStages.size(),
		"Wait semaphore count must match wait stage count!");
	CHECK_TRUE(m_uptrFenceAllocator != nullptr, "Command queue fence allocator is not created!");

	VkFence frameFence = m_uptrFenceAllocator->CreateOrGetVkFence();

	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(inSyncInfo.m_waitSemaphores.size());
	submitInfo.pWaitSemaphores = inSyncInfo.m_waitSemaphores.empty() ? nullptr : inSyncInfo.m_waitSemaphores.data();
	submitInfo.pWaitDstStageMask = inSyncInfo.m_waitStages.empty() ? nullptr : inSyncInfo.m_waitStages.data();
	submitInfo.commandBufferCount = static_cast<uint32_t>(m_recordedCommandBuffers.size());
	submitInfo.pCommandBuffers = m_recordedCommandBuffers.data();
	submitInfo.signalSemaphoreCount = static_cast<uint32_t>(inSyncInfo.m_signalSemaphores.size());
	submitInfo.pSignalSemaphores = inSyncInfo.m_signalSemaphores.empty() ? nullptr : inSyncInfo.m_signalSemaphores.data();

	VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, frameFence), "Failed to submit command queue!");

	m_frameFences[m_currentFrameIndex].push_back(frameFence);
	m_recordedCommandBuffers.clear();
}

auto CommandQueue::WaitTillDone()->void
{
	_WaitFrameFences(m_currentFrameIndex);
}

auto CommandQueue::_RecordCommandBuffer(CommandBuffer* inCommandBuffers, size_t inCount)->void
{
	if (inCount == 0)
	{
		return;
	}

	CHECK_TRUE(inCommandBuffers != nullptr, "No command buffers!");

	std::vector<_ScopeRecordItem> scopeItems;

	// Move all input scopes into a local linear stream. This consumes the input
	// CommandBuffers and keeps Vulkan scope boundaries intact for batching.
	for (size_t commandBufferIndex = 0; commandBufferIndex < inCount; ++commandBufferIndex)
	{
		CommandBuffer& commandBuffer = inCommandBuffers[commandBufferIndex];

		for (CommandBuffer::Scope& scopeVariant : commandBuffer.m_scopes)
		{
			if (std::holds_alternative<CommandBuffer::PrimaryScope>(scopeVariant))
			{
				const auto& primaryScope = std::get<CommandBuffer::PrimaryScope>(scopeVariant);
				if (primaryScope.commands.empty())
				{
					continue;
				}

				_ScopeRecordItem item;
				item.commandCount = primaryScope.commands.size();
				item.scope = std::move(scopeVariant);
				scopeItems.push_back(std::move(item));
			}
			else if (std::holds_alternative<CommandBuffer::RenderPassScope>(scopeVariant))
			{
				const auto& renderPassScope = std::get<CommandBuffer::RenderPassScope>(scopeVariant);
				const size_t commandCount = _CountRenderPassScopeCommands(renderPassScope);
				if (commandCount == 0)
				{
					continue;
				}

				_ScopeRecordItem item;
				item.commandCount = commandCount;
				item.scope = std::move(scopeVariant);
				scopeItems.push_back(std::move(item));
			}
			else
			{
				CHECK_TRUE(false, "Unsupported command buffer scope!");
			}
		}

		commandBuffer.m_scopes.clear();
	}

	std::vector<_CommandBufferRecordBatch> commandBufferBatches;
	_CommandBufferRecordBatch currentBatch;

	// Pack multiple scopes into one VkCommandBuffer until the command-count budget is
	// reached. A single oversized scope is never split, so render pass validity is preserved.
	for (_ScopeRecordItem& scopeItem : scopeItems)
	{
		if (!currentBatch.scopes.empty() &&
			currentBatch.commandCount + scopeItem.commandCount > COMMAND_COUNT_PER_VK_COMMAND_BUFFER)
		{
			commandBufferBatches.push_back(std::move(currentBatch));
			currentBatch = _CommandBufferRecordBatch{};
		}

		currentBatch.commandCount += scopeItem.commandCount;
		currentBatch.scopes.push_back(std::move(scopeItem));
	}

	if (!currentBatch.scopes.empty())
	{
		commandBufferBatches.push_back(std::move(currentBatch));
	}

	if (commandBufferBatches.empty())
	{
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
}
