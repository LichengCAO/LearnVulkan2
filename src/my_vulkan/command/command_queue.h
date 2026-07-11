#pragma once
#include "command_buffer.h"
#include "command_pool.h"
#include "common_enums.h"
class FenceAllocator;
class MyDevice;

class CommandQueue
{
public:
	class SyncInfo
	{
	private:
		std::vector<VkSemaphore> m_waitSemaphores;
		std::vector<VkPipelineStageFlags> m_waitStages;
		std::vector<VkSemaphore> m_signalSemaphores;

	public:
		auto AddWaitSemaphore(VkSemaphore inSemaphore, VkPipelineStageFlags inStage)->SyncInfo&;
		auto AddSemaphoreToSignal(VkSemaphore inSemaphore)->SyncInfo&;

	private:
		friend class CommandQueue;
	};

protected:
	static constexpr uint8_t THREAD_COUNT = 4;
	static constexpr uint8_t FRAME_IN_FLIGHT_COUNT = 3;

protected:
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	uint8_t m_currentFrameIndex = FRAME_IN_FLIGHT_COUNT - 1;
	std::array<std::array<std::unique_ptr<CommandPool>, THREAD_COUNT>, FRAME_IN_FLIGHT_COUNT> m_commandPools;
	std::array<std::vector<VkFence>, FRAME_IN_FLIGHT_COUNT> m_frameFences;
	std::vector<VkCommandBuffer> m_recordedCommandBuffers;
	std::unique_ptr<FenceAllocator> m_uptrFenceAllocator;

protected:
	auto _GetCommandPool(uint8_t inFrameIndex, uint8_t inThreadIndex) const->CommandPool*;
	auto _WaitFrameFences(uint8_t inFrameIndex)->void;
	auto _ResetFrameCommandPools(uint8_t inFrameIndex)->void;
	auto _RecordCommandBuffer(CommandBuffer* inCommandBuffers, size_t inCount)->void;

public:
	CommandQueue();
	CommandQueue(const CommandQueue&) = delete;
	CommandQueue& operator=(const CommandQueue&) = delete;
	virtual ~CommandQueue();

	virtual auto Destroy()->void;
	virtual auto StartFrame()->void;
	virtual auto Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&;
	virtual auto Submit(SyncInfo inSyncInfo)->void;
	virtual auto WaitTillDone()->void;
};

class GraphicsQueue final : public CommandQueue
{
public:
	GraphicsQueue();
	auto Create()->void;
};

class ComputeQueue final : public CommandQueue
{
public:
	ComputeQueue();
	auto Create()->void;
};

class TransferQueue final : public CommandQueue
{
public:
	TransferQueue();
	auto Create()->void;
};
