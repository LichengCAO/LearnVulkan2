#pragma once
#include "command_buffer.h"
#include "command_pool.h"
#include "common_enums.h"
#include "queue_signal_chain.h"
#include "completion_fence.h"

class MyDevice;

class CommandQueue
{
public:
	// Describes the synchronization objects used by one queue submission.
	class SubmitInfo final
	{
		friend class CommandQueue;

	private:
		struct ChainEntry final
		{
			QueueSemaphore* chain = nullptr;
			VkPipelineStageFlags2 waitStage = 0;
			bool useWait = false;
			bool useSignal = false;
		};
		struct WaitSemaphoreEntry final
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			VkPipelineStageFlags2 stage = 0;
		};

		std::vector<ChainEntry> m_chainEntries;
		std::vector<WaitSemaphoreEntry> m_waitSemaphoreEntries;
		std::vector<VkSemaphore> m_signalSemaphores;
		HostFence* m_completionFence = nullptr;

	public:
		SubmitInfo() = default;
		SubmitInfo(const SubmitInfo&) = default;
		SubmitInfo& operator=(const SubmitInfo&) = default;
		SubmitInfo(SubmitInfo&&) noexcept = default;
		SubmitInfo& operator=(SubmitInfo&&) noexcept = default;

		// Duplicate wait requests for a chain are merged by ORing their stages.
		auto AddWaitQueueSignalChain(
			QueueSemaphore& inChain,
			VkPipelineStageFlags2 inWaitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)->SubmitInfo&;

		auto AddSignalQueueSignalChain(QueueSemaphore& inChain)->SubmitInfo&;

		auto AddWaitSemaphore(
			VkSemaphore inSemaphore,
			VkPipelineStageFlags2 inWaitStage)->SubmitInfo&;

		auto AddSemaphoreToSignal(VkSemaphore inSemaphore)->SubmitInfo&;

		auto SetFence(HostFence& inFence) -> SubmitInfo&;
	};

protected:
	static constexpr uint8_t THREAD_COUNT = 4;

protected:
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	QueueFamilyType m_queueFamilyType = QueueFamilyType::UNSET;
	std::array<std::unique_ptr<CommandPool>, THREAD_COUNT> m_commandPools;
	std::vector<VkCommandBuffer> m_recordedCommandBuffers;
	std::vector<std::function<void()>> m_pendingRecycleActions;

protected:
	auto _GetCommandPool(uint8_t inThreadIndex) const->CommandPool*;
	auto _Init(QueueFamilyType inQueueFamilyType)->void;
	auto _Deinit()->void;
	auto _ResetCommandPools()->void;
	auto _RecordCommandBuffer(CommandBuffer* inCommandBuffers, size_t inCount)->void;

protected:
	CommandQueue();

public:
	CommandQueue(const CommandQueue&) = delete;
	CommandQueue& operator=(const CommandQueue&) = delete;
	virtual ~CommandQueue();

	virtual auto Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&;
	// Submit the currently recorded command buffers. A missing completion fence
	// is valid; reclamation is deferred until a later fenced submission.
	virtual auto Submit(SubmitInfo inSubmitInfo)->void;
	virtual auto Submit()->void;
	// Waits for all work on this queue and runs deferred recycle actions.
	virtual auto WaitTillDone()->void;

	auto GetVkQueue() const->VkQueue { return m_vkQueue; };
	auto GetQueueFamilyIndex() const->uint32_t { return m_queueFamilyIndex; };
	auto GetQueueFamilyType() const->QueueFamilyType { return m_queueFamilyType; };
};

class GraphicsQueue final : public CommandQueue
{
public:
	explicit GraphicsQueue();
	auto Init()->void;
};

class ComputeQueue final : public CommandQueue
{
public:
	explicit ComputeQueue();
	auto Init()->void;
};

class TransferQueue final : public CommandQueue
{
public:
	explicit TransferQueue();
	auto Init()->void;
};
