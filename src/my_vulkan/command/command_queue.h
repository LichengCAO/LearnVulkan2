#pragma once

#include "allocator/fence_allocator.h"
#include "allocator/semaphore_allocator.h"
#include "common_enums.h"
#include "completion_fence.h"
#include "queue_dependency.h"

class MyDevice;
struct CommandQueueManagerTestProbe;

// Not thread-safe. Its owner must serialize submission and completion calls.
class CommandQueueManager final
{
	friend struct CommandQueueManagerTestProbe;

public:
	class SubmitInfo final
	{
		friend class CommandQueueManager;

	private:
		struct DependencyEntry final
		{
			QueueDependency* dependency = nullptr;
			VkPipelineStageFlags2 waitStage = 0;
			bool useWait = false;
			bool useSignal = false;
		};

		std::vector<VkCommandBuffer> m_commandBuffers;
		std::vector<DependencyEntry> m_dependencyEntries;
		HostFence* m_completionFence = nullptr;

	public:
		auto SetCommandBuffers(std::vector<VkCommandBuffer> inCommandBuffers)->SubmitInfo&;
		auto AddWaitQueueDependency(
			QueueDependency& inDependency,
			VkPipelineStageFlags2 inWaitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)->SubmitInfo&;
		auto AddSignalQueueDependency(QueueDependency& inDependency)->SubmitInfo&;
		auto SetFence(HostFence& inFence)->SubmitInfo&;
	};

private:
	struct UninitializedTag final
	{
	};

	struct DependencySignal final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		SubmissionFrontier frontier;
	};

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

	struct HostFenceRecord final
	{
		VkFence vkFence = VK_NULL_HANDLE;
		SubmissionFrontier submissionFrontier;
		std::vector<HostFence::Callback> callbacks;
	};

	struct QueueState final
	{
		VkQueue vkQueue = VK_NULL_HANDLE;
		uint32_t familyIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32_t queueIndex = 0;
		SubmissionFrontier tailFrontier;
		std::vector<RetiredSemaphore> retiredSemaphores;
		std::vector<HostFenceRecord> pendingHostFences;
	};

	std::array<QueueState, SubmissionFrontier::MAX_QUEUE_COUNT> m_queueStates;
	std::array<size_t, SubmissionFrontier::MAX_QUEUE_COUNT> m_roleToQueueState
	{
		SubmissionFrontier::MAX_QUEUE_COUNT,
		SubmissionFrontier::MAX_QUEUE_COUNT,
		SubmissionFrontier::MAX_QUEUE_COUNT
	};
	FenceAllocator m_fenceAllocator;
	SemaphoreAllocator m_semaphoreAllocator;
	size_t m_queueStateCount = 0;
	SubmissionFrontier m_completedFrontier;
	std::unordered_map<QueueDependency*, VkSemaphore> m_dependencySemaphores;
	std::vector<AbandonedSemaphore> m_abandonedSemaphores;
	bool m_created = false;

private:
	explicit CommandQueueManager(UninitializedTag);
	static auto _GetRoleIndex(QueueFamilyType inQueueFamilyType)->size_t;
	auto _RegisterQueue(
		QueueFamilyType inQueueFamilyType,
		VkQueue inVkQueue,
		uint32_t inFamilyIndex,
		uint32_t inQueueIndex)->size_t;
	auto _GetQueueStateIndex(QueueFamilyType inQueueFamilyType) const->size_t;
	auto _GetQueueState(size_t inQueueStateIndex)->QueueState&;
	auto _GetQueueState(size_t inQueueStateIndex) const->const QueueState&;
	auto _TakeDependencySignal(QueueDependency& inDependency)->DependencySignal;
	void _StoreDependencySignal(
		QueueDependency& inDependency,
		VkSemaphore inSemaphore,
		const SubmissionFrontier& inFrontier);
	auto _AdvanceCompletion(
		const SubmissionFrontier& inSubmissionFrontier)->std::vector<HostFence::Callback>;
	auto _FindHostFenceVkFence(const HostFence& inFence)->VkFence;
	static void _RunCallbacks(std::vector<HostFence::Callback> inCallbacks);
	auto _ConsumeHostFence(HostFence& inFence, uint64_t inTimeout)->bool;
	static auto _GetRetiredSemaphoreReclaimCount(
		const QueueState& inState,
		size_t inQueueStateIndex,
		const SubmissionFrontier& inCompletedFrontier)->size_t;
	void _CollectRetiredSemaphores(const SubmissionFrontier& inCompletedFrontier);
	void _Destroy();

public:
	CommandQueueManager();
	CommandQueueManager(const CommandQueueManager&) = delete;
	CommandQueueManager& operator=(const CommandQueueManager&) = delete;
	~CommandQueueManager();

	void Submit(
		QueueFamilyType inQueueFamilyType,
		SubmitInfo inSubmitInfo);
	void Wait(HostFence& inFence);
	auto Poll(HostFence& inFence)->bool;
};
