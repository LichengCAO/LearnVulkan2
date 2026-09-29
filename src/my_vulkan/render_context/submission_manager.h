#pragma once

#include "allocator/fence_allocator.h"
#include "allocator/semaphore_allocator.h"
#include "common_enums.h"

#include <algorithm>

class MyDevice;
class RenderGraphInstance;
struct CommandQueueManagerTestProbe;

class SubmissionFrontier final
{
public:
	static constexpr size_t MAX_QUEUE_COUNT = 3;

private:
	std::array<uint64_t, MAX_QUEUE_COUNT> m_versions{};

public:
	void Merge(const SubmissionFrontier& inOther)
	{
		for (size_t queueIndex = 0; queueIndex < MAX_QUEUE_COUNT; ++queueIndex)
		{
			m_versions[queueIndex] = std::max(m_versions[queueIndex], inOther.m_versions[queueIndex]);
		}
	}

	void Advance(size_t inQueueIndex)
	{
		CHECK_TRUE(inQueueIndex < MAX_QUEUE_COUNT, "Submission frontier queue index is out of range!");
		CHECK_TRUE(m_versions[inQueueIndex] != UINT64_MAX, "Submission version overflow!");
		++m_versions[inQueueIndex];
	}

	auto GetVersion(size_t inQueueIndex) const->uint64_t
	{
		CHECK_TRUE(inQueueIndex < MAX_QUEUE_COUNT, "Submission frontier queue index is out of range!");
		return m_versions[inQueueIndex];
	}

	auto Covers(size_t inQueueIndex, uint64_t inVersion) const->bool
	{
		return GetVersion(inQueueIndex) >= inVersion;
	}

	auto Covers(const SubmissionFrontier& inOther) const->bool
	{
		for (size_t queueIndex = 0; queueIndex < MAX_QUEUE_COUNT; ++queueIndex)
		{
			if (!Covers(queueIndex, inOther.m_versions[queueIndex]))
			{
				return false;
			}
		}
		return true;
	}
};

// A linear GPU dependency transferred between queue submissions.
//
// A chain starts with a signal-only submit, may be advanced by wait-and-signal
// submits, and must be terminated by a wait-only submit:
//   first submit  : signal S0
//   middle submit : wait S0, signal S1
//   final submit  : wait S1
//
// SubmissionManager owns the underlying synchronization resources. This
// object only carries a pending dependency and its producer submission frontier.
class QueueDependency final
{
	friend class SubmissionManager;
	friend class RenderGraphInstance;

private:
	bool m_hasPendingSignal = false;
	SubmissionFrontier m_frontier;

public:
	QueueDependency() = default;
	QueueDependency(const QueueDependency&) = delete;
	QueueDependency& operator=(const QueueDependency&) = delete;
	QueueDependency(QueueDependency&&) = delete;
	QueueDependency& operator=(QueueDependency&&) = delete;
	~QueueDependency() noexcept;

private:
	auto _HasPendingSignal() const->bool { return m_hasPendingSignal; }
	auto _GetPendingFrontier() const->const SubmissionFrontier&;
	void _CommitWait() noexcept;
	void _CommitSignal(const SubmissionFrontier& inFrontier) noexcept;
};

// A reusable GPU-to-host completion object.
//
// DeviceContext::Wait() or Poll() must consume the previous submission before
// this object is reused. Destroying an in-flight object is allowed because the
// manager owns its submitted callbacks and synchronization resources. Callbacks
// are one-shot and belong to a submission, not permanently to the fence.
class HostFence final
{
	friend class SubmissionManager;

public:
	using Callback = std::function<void()>;

private:
	bool m_isInFlight = false;
	size_t m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	SubmissionFrontier m_submissionFrontier;
	std::vector<Callback> m_callbacks;

public:
	HostFence() = default;
	HostFence(const HostFence&) = delete;
	HostFence& operator=(const HostFence&) = delete;
	HostFence(HostFence&&) = delete;
	HostFence& operator=(HostFence&&) = delete;
	~HostFence() = default;

	// Registers a one-shot callback for the next submission using this fence.
	// The previous submission must already have been consumed through DeviceContext.
	auto AddCallback(Callback inCallback)->HostFence&;

private:
	void _PrepareForSubmit();
	auto _CommitSubmit(
		size_t inQueueStateIndex,
		const SubmissionFrontier& inSubmissionFrontier) noexcept->std::vector<Callback>;
	void _Complete();
};

// Not thread-safe. Its owner must serialize submission and completion calls.
class SubmissionManager final
{
	friend struct CommandQueueManagerTestProbe;

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
