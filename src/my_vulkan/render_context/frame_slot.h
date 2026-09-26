#pragma once

#include "submission_manager.h"
#include "common_enums.h"
#include "command_pool.h"
#include "utility/task_scheduler.h"

class CommandBuffer;
class DeviceContext;

// Internal frame-slot implementation owned and coordinated by DeviceContext.
// Other modules must not create, retain, or access FrameSlot directly;
// frame recording, submission, and completion operations must go through DeviceContext.
class FrameSlot final
{
    friend class DeviceContext;

public:
    class RecordingTicket final
    {
        friend class FrameSlot;

    private:
        uint64_t m_serial = 0;

    public:
        auto IsValid() const -> bool { return m_serial != 0; }
    };

    struct RecordingResult
    {
        QueueFamilyType queue = QueueFamilyType::UNSET;
        std::vector<VkCommandBuffer> vkCommandBuffers;
    };

private:
    FrameSlot();

    struct RecordingTask;

    using ThreadCommandPools =
        std::array<std::unique_ptr<CommandPool>, MyTaskScheduler::THREAD_COUNT>;

    static auto _GetQueueIndex(QueueFamilyType inQueueFamilyType) -> size_t;

    std::array<std::unique_ptr<HostFence>, SubmissionFrontier::MAX_QUEUE_COUNT> m_completionFences;
    std::vector<HostFence::Callback> m_completionCallbacks;
    std::array<ThreadCommandPools, SubmissionFrontier::MAX_QUEUE_COUNT> m_commandPools;
    std::unordered_map<uint64_t, std::unique_ptr<RecordingTask>> m_recordTasks;
    uint64_t m_nextRecordingSerial = 1;

private:
    void _RunCompletionCallbacks();
    auto _GetCommandPool(QueueFamilyType inQueue, uint32_t inThreadIndex) -> CommandPool&;
    void _WaitForRecordingTasks() noexcept;
    // Waits for all queue completion fences and runs frame callbacks.
    void _Wait(DeviceContext& inDeviceContext);

public:
    FrameSlot(const FrameSlot&) = delete;
    FrameSlot& operator=(const FrameSlot&) = delete;
    FrameSlot(FrameSlot&&) = delete;
    FrameSlot& operator=(FrameSlot&&) = delete;
    ~FrameSlot();

    // Borrowed Command pointers inside inBuffers must remain alive until the
    // returned ticket is consumed by TakeRecordingResult().
    auto DispatchRecording(
        QueueFamilyType inQueue,
        std::vector<CommandBuffer> inBuffers) -> RecordingTicket;
    auto TakeRecordingResult(RecordingTicket inTicket) -> RecordingResult;

    // The fence must be attached to the final submission for this queue in the frame.
    auto GetCompletionFence(QueueFamilyType inQueueFamilyType) -> HostFence&;

    auto AddCompletionCallback(HostFence::Callback inCallback) -> FrameSlot&;

    void ResetForReuse(DeviceContext& inDeviceContext);
};
