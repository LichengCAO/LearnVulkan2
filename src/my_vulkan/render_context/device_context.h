#pragma once

#include "common.h"
#include "common_enums.h"
#include "command_buffer.h"
#include "render_context/synchronization.h"
#include "render_context/private/submission_manager.h"
#include "render_context/private/frame_slot.h"
#include <span>

class SwapchainImage;

class SubmissionSyncInfo final
{
	friend class DeviceContext;

private:
	SubmissionManager::SubmitInfo m_submitInfo;
	std::vector<const SwapchainImage*> m_acquireWaitImages;

public:
	auto AddWaitQueueDependency(
		QueueDependency& inDependency,
		VkPipelineStageFlags2 inWaitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)->SubmissionSyncInfo&;
	auto AddSignalQueueDependency(QueueDependency& inDependency)->SubmissionSyncInfo&;
	auto WaitSwapchainImage(const SwapchainImage& inImage)->SubmissionSyncInfo&;
	auto SetFence(HostFence& inFence)->SubmissionSyncInfo&;
};

// Thread-affine. Public methods must be called from the owning thread.
class DeviceContext
{
    friend struct ResourceSubmissionTestProbe;
private:
	struct QueueRecordingState final
	{
		std::vector<std::variant<FrameSlot::RecordingTicket, FrameSlot::RecordingResult>> pendingRecordings;
	};

	static auto _GetQueueIndex(QueueFamilyType inQueue) -> size_t;
	auto _GetCurrentFrameContext() -> FrameSlot&;
	std::unique_ptr<SubmissionManager> m_uptrCommandQueueManager;
	std::vector<std::unique_ptr<FrameSlot>> m_frameContexts;
	std::vector<std::vector<VkSemaphore>> m_acquireSemaphores;
	std::vector<size_t> m_acquireSemaphoreUsedCounts;
	std::vector<const SwapchainImage*> m_acquiredImages;
	size_t m_currentFrameIndex = SIZE_MAX;
	bool m_frameActive = false;
	std::array<QueueRecordingState, SubmissionFrontier::MAX_QUEUE_COUNT> m_queueRecordingStates;

public:
	explicit DeviceContext(size_t inFrameCount);
	DeviceContext(const DeviceContext&) = delete;
	DeviceContext& operator=(const DeviceContext&) = delete;
	~DeviceContext() noexcept;

	void StartFrame();
	auto GetNextAvailableSwapchainImage() -> SwapchainImage*;
	void PresentSwapchainImage(const SwapchainImage& inImage, std::span<QueueDependency* const> inWaits);

	void EndFrame();

	// Dispatches recording and returns immediately. Requests stay ordered per queue.
	void CommitCommandsToQueue(QueueFamilyType inQueue, std::vector<CommandBuffer> inBuffers);

	// Joins recordings and submits them in commit order. A failed recording is discarded.
	// Other recordings stay queued for retry if recording or submission fails.
	void SubmitQueue(QueueFamilyType inQueue, const SubmissionSyncInfo& inSubmitInfo);

	// Records, submits, and waits until the specified queue completes the commands.
	// This operation is independent of the active frame lifecycle.
	void ExecuteCommandsAndWait(QueueFamilyType inQueue, std::vector<CommandBuffer> inBuffers);

	// Consumes the fence's current logical submission. A fence whose Vulkan
	// resource was already reclaimed by a later wait completes immediately.
	void Wait(HostFence& inFence);

	// Returns false only while the fence's current submission is incomplete.
	auto Poll(HostFence& inFence)->bool;

	// Runs once after all queues used by the current frame have completed.
	void AddCurrentFrameCompletionCallback(HostFence::Callback inCallback);
};

