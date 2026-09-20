#pragma once

#include "command_buffer.h"
#include "command_pool.h"
#include "common_enums.h"
#include "completion_fence.h"
#include "queue_dependency.h"

#include <mutex>

class MyDevice;
class Buffer;
class CommandQueueManager;
class RenderGraphInstance;
struct CommandQueueManagerTestProbe;

class CommandQueue
{
	friend class Buffer;

public:
	class SubmitInfo final
	{
		friend class CommandQueueManager;
		friend class RenderGraphInstance;

	private:
		struct DependencyEntry final
		{
			QueueDependency* dependency = nullptr;
			VkPipelineStageFlags2 waitStage = 0;
			bool useWait = false;
			bool useSignal = false;
		};

		struct WaitSemaphoreEntry final
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			VkPipelineStageFlags2 stage = 0;
		};

		std::vector<DependencyEntry> m_dependencyEntries;
		std::vector<WaitSemaphoreEntry> m_waitSemaphoreEntries;
		std::vector<VkSemaphore> m_signalSemaphores;
		HostFence* m_completionFence = nullptr;

	public:
		SubmitInfo() = default;
		SubmitInfo(const SubmitInfo&) = default;
		SubmitInfo& operator=(const SubmitInfo&) = default;
		SubmitInfo(SubmitInfo&&) noexcept = default;
		SubmitInfo& operator=(SubmitInfo&&) noexcept = default;

		auto AddWaitQueueDependency(
			QueueDependency& inDependency,
			VkPipelineStageFlags2 inWaitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)->SubmitInfo&;
		auto AddSignalQueueDependency(QueueDependency& inDependency)->SubmitInfo&;
		auto AddWaitSemaphore(
			VkSemaphore inSemaphore,
			VkPipelineStageFlags2 inWaitStage)->SubmitInfo&;
		auto AddSemaphoreToSignal(VkSemaphore inSemaphore)->SubmitInfo&;
		auto SetFence(HostFence& inFence)->SubmitInfo&;
	};

private:
	void _SubmitVkCommandBuffers(
		const VkCommandBuffer* inCommandBuffers,
		size_t inCount,
		SubmitInfo inSubmitInfo);

protected:
	static constexpr uint8_t THREAD_COUNT = 4;

	CommandQueueManager* m_commandQueueManager = nullptr;
	size_t m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	std::array<std::unique_ptr<CommandPool>, THREAD_COUNT> m_commandPools;
	std::vector<VkCommandBuffer> m_recordedCommandBuffers;

protected:
	auto _GetCommandPool(uint8_t inThreadIndex) const->CommandPool*;
	void _Init(CommandQueueManager& inCommandQueueManager, QueueFamilyType inQueueFamilyType);
	void _Deinit();
	void _ResetCommandPools();
	void _RecordCommandBuffer(CommandBuffer* inCommandBuffers, size_t inCount);

	CommandQueue();

public:
	CommandQueue(const CommandQueue&) = delete;
	CommandQueue& operator=(const CommandQueue&) = delete;
	virtual ~CommandQueue();

	auto Enqueue(CommandBuffer* inCommandBuffers, size_t inCount)->CommandQueue&;
	void Submit(SubmitInfo inSubmitInfo);
};

class GraphicsQueue final : public CommandQueue
{
	friend class CommandQueueManager;

public:
	explicit GraphicsQueue();
	void Init(CommandQueueManager& inCommandQueueManager);
};

class ComputeQueue final : public CommandQueue
{
	friend class CommandQueueManager;

public:
	explicit ComputeQueue();
	void Init(CommandQueueManager& inCommandQueueManager);
};

class TransferQueue final : public CommandQueue
{
	friend class CommandQueueManager;

public:
	explicit TransferQueue();
	void Init(CommandQueueManager& inCommandQueueManager);
};

class CommandQueueManager final
{
	friend class CommandQueue;
	friend class HostFence;
	friend class RenderGraphInstance;
	friend struct CommandQueueManagerTestProbe;

private:
	struct RetiredSemaphore final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		uint64_t consumerVersion = 0;
	};

	struct AbandonedSemaphore final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		SubmissionFrontier producerFrontier;
	};

	struct QueueState final
	{
		VkQueue vkQueue = VK_NULL_HANDLE;
		uint32_t familyIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32_t queueIndex = 0;
		SubmissionFrontier tailFrontier;
		std::vector<RetiredSemaphore> retiredSemaphores;
		std::mutex submitMutex;
	};

	std::array<std::unique_ptr<QueueState>, SubmissionFrontier::MAX_QUEUE_COUNT> m_queueStates;
	std::array<size_t, SubmissionFrontier::MAX_QUEUE_COUNT> m_roleToQueueState
	{
		SubmissionFrontier::MAX_QUEUE_COUNT,
		SubmissionFrontier::MAX_QUEUE_COUNT,
		SubmissionFrontier::MAX_QUEUE_COUNT
	};
	size_t m_queueStateCount = 0;
	SubmissionFrontier m_completedFrontier;
	std::vector<AbandonedSemaphore> m_abandonedSemaphores;
	std::mutex m_completionMutex;
	std::mutex m_semaphoreMutex;
	std::unique_ptr<GraphicsQueue> m_graphicsQueue;
	std::unique_ptr<ComputeQueue> m_computeQueue;
	std::unique_ptr<TransferQueue> m_transferQueue;
	bool m_created = false;

private:
	static auto _GetRoleIndex(QueueFamilyType inQueueFamilyType)->size_t;
	auto _RegisterQueue(
		QueueFamilyType inQueueFamilyType,
		VkQueue inVkQueue,
		uint32_t inFamilyIndex,
		uint32_t inQueueIndex)->size_t;
	auto _GetQueueStateIndex(QueueFamilyType inQueueFamilyType) const->size_t;
	auto _GetQueueState(size_t inQueueStateIndex)->QueueState&;
	auto _GetQueueState(size_t inQueueStateIndex) const->const QueueState&;
	void _Submit(
		size_t inQueueStateIndex,
		const VkCommandBuffer* inCommandBuffers,
		size_t inCommandBufferCount,
		CommandQueue::SubmitInfo inSubmitInfo);
	void _NotifyCompletion(const SubmissionFrontier& inSubmissionFrontier);
	static auto _GetRetiredSemaphoreReclaimCount(
		const QueueState& inState,
		size_t inQueueStateIndex,
		const SubmissionFrontier& inCompletedFrontier)->size_t;
	void _CollectRetiredSemaphores(const SubmissionFrontier& inCompletedFrontier);
	void _DrainRetiredSemaphores();
	void _WaitIdleAndDiscardDependencies(QueueDependency* const* inDependencies, size_t inCount);

public:
	CommandQueueManager() = default;
	CommandQueueManager(const CommandQueueManager&) = delete;
	CommandQueueManager& operator=(const CommandQueueManager&) = delete;
	~CommandQueueManager();

	void Create();
	void Destroy();

	auto GetGraphicsQueue()->GraphicsQueue* { return m_graphicsQueue.get(); }
	auto GetComputeQueue()->ComputeQueue* { return m_computeQueue.get(); }
	auto GetTransferQueue()->TransferQueue* { return m_transferQueue.get(); }
};
