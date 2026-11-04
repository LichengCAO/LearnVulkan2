#pragma once

#include "../synchronization.h"
#include "allocator/private/fence_allocator.h"
#include "allocator/private/semaphore_allocator.h"
#include "common_enums.h"

class MyDevice;
struct CommandQueueManagerTestProbe;

// Not thread-safe. Its owner must serialize submission and completion calls.
class SubmissionManager final
{
	friend struct CommandQueueManagerTestProbe;
	friend struct ResourceSubmissionTestProbe;

public:
	class SubmitInfo final
	{
		friend class SubmissionManager;

	private:
		struct DependencyEntry final
		{
			QueueDependency* dependency = nullptr;
			VkPipelineStageFlags2 waitStage = 0;
			bool useWait = false;
			bool useSignal = false;
		};
		struct ExternalSemaphoreEntry final
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			VkPipelineStageFlags2 stage = 0;
		};

		std::vector<VkCommandBuffer> m_commandBuffers;
		std::vector<DependencyEntry> m_dependencyEntries;
		std::vector<ExternalSemaphoreEntry> m_externalWaits;
		std::vector<ExternalSemaphoreEntry> m_externalSignals;
		HostFence* m_completionFence = nullptr;

	public:
		auto SetCommandBuffers(std::vector<VkCommandBuffer> inCommandBuffers)->SubmitInfo&;
		auto AddWaitQueueDependency(
			QueueDependency& inDependency,
			VkPipelineStageFlags2 inWaitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)->SubmitInfo&;
		auto AddSignalQueueDependency(QueueDependency& inDependency)->SubmitInfo&;
		auto AddExternalBinaryWait(VkSemaphore inSemaphore, VkPipelineStageFlags2 inWaitStage)->SubmitInfo&;
		auto AddExternalBinarySignal(VkSemaphore inSemaphore)->SubmitInfo&;
		auto SetFence(HostFence& inFence)->SubmitInfo&;
	};

private:
	struct UninitializedTag final
	{
	};
	using DependencySemaphoreMap = std::unordered_map<QueueDependency*, VkSemaphore>;

	struct DependencySignal final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		SubmissionFrontier frontier;
	};

	struct PreparedDependency final
	{
		QueueDependency* dependency = nullptr;
		VkSemaphore waitSemaphore = VK_NULL_HANDLE;
		VkSemaphore signalSemaphore = VK_NULL_HANDLE;
		bool useWait = false;
		bool useSignal = false;
	};

	struct RetiredSemaphore final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		uint64_t consumerVersion = 0;
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
	DependencySemaphoreMap m_dependencySemaphores;
	bool m_created = false;

private:
	explicit SubmissionManager(UninitializedTag);
	static auto _GetRoleIndex(QueueFamilyType inQueueFamilyType)->size_t;
	auto _RegisterQueue(
		QueueFamilyType inQueueFamilyType,
		VkQueue inVkQueue)->size_t;
	auto _GetQueueState(
		QueueFamilyType inQueueFamilyType)->std::pair<size_t, QueueState&>;
	auto _PeekDependencySignal(QueueDependency& inDependency)->DependencySignal;
	void _CommitPreparedSubmission(
		size_t inQueueStateIndex,
		QueueState& inoutState,
		const SubmissionFrontier& inSubmissionFrontier,
		uint64_t inConsumerVersion,
		const std::vector<PreparedDependency>& inDependencies,
		DependencySemaphoreMap& inoutStagedDependencySemaphores,
		HostFence* inCompletionFence,
		VkFence inCompletionVkFence) noexcept;
	auto _AdvanceCompletion(
		const SubmissionFrontier& inSubmissionFrontier)->std::vector<HostFence::Callback>;
	auto _FindHostFenceVkFence(const HostFence& inFence)->VkFence;
	static void _RunCallbacks(std::vector<HostFence::Callback> inCallbacks);
	auto _ConsumeHostFence(HostFence& inFence, uint64_t inTimeout)->bool;
	void _CollectRetiredSemaphores(const SubmissionFrontier& inCompletedFrontier);

public:
	SubmissionManager();
	SubmissionManager(const SubmissionManager&) = delete;
	SubmissionManager& operator=(const SubmissionManager&) = delete;
	~SubmissionManager();

	void Submit(
		QueueFamilyType inQueueFamilyType,
		SubmitInfo inSubmitInfo);
	void Wait(HostFence& inFence);
	auto Poll(HostFence& inFence)->bool;
};
