#pragma once

#include "common_enums.h"
#include "completion_fence.h"
#include "submission_frontier.h"
#include "command_pool.h"
#include "utility/task_scheduler.h"

class CommandBuffer;
class DeviceContext;

class FrameContext final
{
    friend class DeviceContext;

public:
    class RecordingTicket final
    {
        friend class FrameContext;

    private:
        size_t m_frameIndex = SIZE_MAX;
        uint64_t m_serial = 0;

    public:
        auto IsValid() const -> bool { return m_frameIndex != SIZE_MAX && m_serial != 0; }
    };

    struct RecordedPayload
    {
        QueueFamilyType queue = QueueFamilyType::UNSET;
        std::vector<VkCommandBuffer> vkCommandBuffers;
    };

private:
    struct RecordContext;

    using ThreadCommandPools =
        std::array<std::unique_ptr<CommandPool>, MyTaskScheduler::THREAD_COUNT>;

    static auto _GetQueueIndex(QueueFamilyType inQueueFamilyType) -> size_t;

    size_t m_frameIndex = 0;
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
    explicit FrameContext(size_t inFrameIndex);
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
