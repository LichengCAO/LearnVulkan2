#pragma once
#include "command_buffer.h"
#include "command_pool.h"

class MyDevice;

class CommandQueue
{
public:
	enum class QueueType
	{
		GRAPHICS,
		COMPUTE,
		TRANSFER,
	};

	class SyncObject
	{
	public:
		virtual ~SyncObject() = default;

	private:
		virtual auto _GetWaitSemaphores() const->const std::vector<VkSemaphore>& = 0;
		virtual auto _GetWaitStages() const->const std::vector<VkPipelineStageFlags>& = 0;
		virtual auto _GetSignalSemaphores() const->const std::vector<VkSemaphore>& = 0;
		virtual auto _GetFence() const->VkFence = 0;

		friend class CommandQueue;
	};

private:
	QueueType m_queueType = QueueType::GRAPHICS;
	MyDevice* m_device = nullptr;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	uint32_t m_threadCount = 1;
	uint32_t m_frameInFlightCount = 1;
	uint32_t m_currentFrameIndex = 0;
	std::vector<std::unique_ptr<CommandPool>> m_commandPools;
	std::vector<std::vector<VkFence>> m_frameFences;
	std::vector<const CommandBuffer*> m_pendingCommandBuffers;
	std::vector<VkCommandBuffer> m_recordedCommandBuffers;

private:
	auto _GetCommandPoolIndex(uint32_t inFrameIndex, uint32_t inThreadIndex) const->uint32_t;
	auto _GetCommandPool(uint32_t inFrameIndex, uint32_t inThreadIndex) const->CommandPool*;
	auto _WaitFrameFences(uint32_t inFrameIndex)->void;
	auto _ResetFrameCommandPools(uint32_t inFrameIndex)->void;
	auto _RecordPendingCommandBuffers()->void;
	auto _RecordCommandBuffer(const CommandBuffer* inCommandBuffer, VkCommandBuffer inVkCommandBuffer)->void;
	auto _TrackFence(VkFence inFence)->void;

public:
	CommandQueue() = default;
	explicit CommandQueue(QueueType inQueueType);
	CommandQueue(const CommandQueue&) = delete;
	CommandQueue& operator=(const CommandQueue&) = delete;
	virtual ~CommandQueue();

	auto Create(
		MyDevice* inDevice,
		VkQueue inVkQueue,
		uint32_t inQueueFamilyIndex,
		uint32_t inThreadCount,
		uint32_t inFrameInFlightCount)->void;
	auto Destroy()->void;

	auto StartFrame(uint32_t inFrameIndex)->void;
	auto Enqueue(const CommandBuffer* const* inCommandBuffers, size_t inCount)->CommandQueue&;
	auto Submit(const SyncObject* inSyncObject)->void;
};

class GraphicsCommandQueue final : public CommandQueue
{
public:
	GraphicsCommandQueue();
};

class ComputeCommandQueue final : public CommandQueue
{
public:
	ComputeCommandQueue();
};

class TransferCommandQueue final : public CommandQueue
{
public:
	TransferCommandQueue();
};
