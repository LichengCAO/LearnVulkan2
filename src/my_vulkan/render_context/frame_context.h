#pragma once

#include "common_enums.h"
#include "completion_fence.h"
#include "submission_frontier.h"
#include "command_pool.h"
#include "utility/task_scheduler.h"

class CommandBuffer;
class DeviceContext;

// Internal frame-slot implementation owned and coordinated by DeviceContext.
// Other modules must not create, retain, or access FrameContext directly;
// frame recording, submission, and completion operations must go through DeviceContext.
class FrameContext final
{
    friend class DeviceContext;

public:
    class RecordingTicket final
    {
        friend class FrameContext;

    private:
        uint64_t m_serial = 0;

    public:
        auto IsValid() const -> bool { return m_serial != 0; }
    };

    struct RecordedPayload
    {
        QueueFamilyType queue = QueueFamilyType::UNSET;
        std::vector<VkCommandBuffer> vkCommandBuffers;
    };

private:
    FrameContext();

    struct RecordContext;

    using ThreadCommandPools =
        std::array<std::unique_ptr<CommandPool>, MyTaskScheduler::THREAD_COUNT>;

    static auto _GetQueueIndex(QueueFamilyType inQueueFamilyType) -> size_t;

    std::array<std::unique_ptr<HostFence>, SubmissionFrontier::MAX_QUEUE_COUNT> m_completionFences;
    std::vector<HostFence::Callback> m_completionCallbacks;
    std::array<ThreadCommandPools, SubmissionFrontier::MAX_QUEUE_COUNT> m_commandPools;
    std::unordered_map<uint64_t, std::unique_ptr<RecordContext>> m_recordTasks;
    uint64_t m_nextRecordingSerial = 1;

private:
    void _RunCompletionCallbacks();
    auto _GetCommandPool(QueueFamilyType inQueue, uint32_t inThreadIndex) -> CommandPool&;
    void _WaitForRecordingTasks() noexcept;
    // Waits for all queue completion fences and runs frame callbacks.
    void _Wait();

public:
    FrameContext(const FrameContext&) = delete;
    FrameContext& operator=(const FrameContext&) = delete;
    FrameContext(FrameContext&&) = delete;
    FrameContext& operator=(FrameContext&&) = delete;
    ~FrameContext();

    // Borrowed Command pointers inside inBuffers must remain alive until the
    // returned ticket is consumed by TakeRecordedPayload().
    auto DispatchRecording(
        QueueFamilyType inQueue,
        std::vector<CommandBuffer> inBuffers) -> RecordingTicket;
    auto TakeRecordedPayload(RecordingTicket inTicket) -> RecordedPayload;

    // The fence must be attached to the final submission for this queue in the frame.
    auto GetCompletionFence(QueueFamilyType inQueueFamilyType) -> HostFence&;

    auto AddCompletionCallback(HostFence::Callback inCallback) -> FrameContext&;

    void ResetForReuse();
};
