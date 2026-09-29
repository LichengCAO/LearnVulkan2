#include "frame_slot.h"

#include "command/command_buffer.h"
#include "command/command.h"
#include "render_context/device_context.h"

#include <limits>

namespace
{
    constexpr size_t COMMAND_COUNT_PER_VK_COMMAND_BUFFER = 256;
}

struct FrameSlot::RecordingTask final
{
    struct RecordBatch final
    {
        std::vector<const Command*> commands;
        VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
    };

    FrameSlot* owner = nullptr;
    QueueFamilyType queue = QueueFamilyType::UNSET;
    std::vector<CommandBuffer> sourceBuffers;
    std::vector<RecordBatch> batches;
    std::unique_ptr<MyMultiThreadTask> task;
    bool dispatched = false;
    bool waited = false;

    RecordingTask(
        FrameSlot& inOwner,
        QueueFamilyType inQueue,
        std::vector<CommandBuffer> inSourceBuffers,
        std::vector<RecordBatch> inBatches)
        : owner(&inOwner),
          queue(inQueue),
          sourceBuffers(std::move(inSourceBuffers)),
          batches(std::move(inBatches))
    {
    }

    RecordingTask(const RecordingTask&) = delete;
    RecordingTask& operator=(const RecordingTask&) = delete;
    ~RecordingTask()
    {
        _Wait();
    }

    void Dispatch()
    {
        CHECK_TRUE(!dispatched, "Recording task was already dispatched!");
        if (batches.empty())
        {
            return;
        }

        CHECK_TRUE(
            batches.size() <= std::numeric_limits<uint32_t>::max(),
            "Too many command buffer recording batches!");
        task = std::make_unique<MyMultiThreadTask>(
            [this](uint32_t inStart, uint32_t inEnd, uint32_t inThreadIndex)
            {
                _RecordRange(inStart, inEnd, inThreadIndex);
            },
            static_cast<uint32_t>(batches.size()));
        MyTaskScheduler::GetInstance().AddMutiThreadTask(task.get());
        dispatched = true;
    }

    auto WaitAndTakePayload() -> RecordingResult
    {
        _Wait();

        RecordingResult payload;
        payload.queue = queue;
        payload.vkCommandBuffers.reserve(batches.size());
        for (const RecordBatch& batch : batches)
        {
            CHECK_TRUE(batch.vkCommandBuffer != VK_NULL_HANDLE, "Recording task produced an invalid command buffer!");
            payload.vkCommandBuffers.push_back(batch.vkCommandBuffer);
        }

        sourceBuffers.clear();
        return payload;
    }

    void Wait()
    {
        _Wait();
    }

private:
    void _RecordRange(uint32_t inStart, uint32_t inEnd, uint32_t inThreadIndex)
    {
        CHECK_TRUE(owner != nullptr, "Recording task has no owning frame context!");
        CHECK_TRUE(inStart <= inEnd && inEnd <= batches.size(), "Invalid recording task range!");
        CommandPool& commandPool = owner->_GetCommandPool(queue, inThreadIndex);

        for (uint32_t batchIndex = inStart; batchIndex < inEnd; ++batchIndex)
        {
            RecordBatch& batch = batches[batchIndex];
            batch.vkCommandBuffer =
                commandPool.AllocateOrGetCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK_CHECK(
                vkBeginCommandBuffer(batch.vkCommandBuffer, &beginInfo),
                "Failed to begin command buffer recording!");
            for (const Command* command : batch.commands)
            {
                CHECK_TRUE(command != nullptr, "Recording batch contains an invalid command!");
                command->Record(batch.vkCommandBuffer);
            }
            VK_CHECK(
                vkEndCommandBuffer(batch.vkCommandBuffer),
                "Failed to end command buffer recording!");
        }
    }

    void _Wait()
    {
        if (!dispatched || waited)
        {
            return;
        }

        MyTaskScheduler::GetInstance().WaitForTask(task.get());
        waited = true;
    }
};

FrameSlot::FrameSlot()
{
}

FrameSlot::~FrameSlot()
{
    _WaitForRecordingTasks();
}

auto FrameSlot::_GetQueueIndex(QueueFamilyType inQueueFamilyType) -> size_t
{
    switch (inQueueFamilyType)
    {
    case QueueFamilyType::GRAPHICS:
        return 0;
    case QueueFamilyType::COMPUTE:
        return 1;
    case QueueFamilyType::TRANSFER:
        return 2;
    default:
        CHECK_TRUE(false, "Invalid queue family type for frame completion fence!");
        return SubmissionFrontier::MAX_QUEUE_COUNT;
    }
}

auto FrameSlot::_GetCommandPool(
    QueueFamilyType inQueue,
    uint32_t inThreadIndex) -> CommandPool&
{
    CHECK_TRUE(inThreadIndex < MyTaskScheduler::THREAD_COUNT, "Recording thread index is out of range!");
    const size_t queueIndex = _GetQueueIndex(inQueue);
    std::unique_ptr<CommandPool>& commandPool = m_commandPools[queueIndex][inThreadIndex];
    if (commandPool == nullptr)
    {
        CommandPoolCreateInfo createInfo;
        createInfo.CustomizeQueueFamilyType(inQueue);
        commandPool = std::make_unique<CommandPool>();
        commandPool->Create(&createInfo);
    }

    return *commandPool;
}

void FrameSlot::_WaitForRecordingTasks() noexcept
{
    for (auto& [serial, recordContext] : m_recordTasks)
    {
        (void)serial;
        recordContext->Wait();
    }
}

void FrameSlot::ResetForReuse(DeviceContext& inDeviceContext)
{
    CHECK_TRUE(
        m_recordTasks.empty(),
        "Cannot reuse a frame context while recording tickets remain unconsumed!");
    _Wait(inDeviceContext);

    for (ThreadCommandPools& queuePools : m_commandPools)
    {
        for (std::unique_ptr<CommandPool>& commandPool : queuePools)
        {
            if (commandPool != nullptr)
            {
                commandPool->Reset();
            }
        }
    }
}

void FrameSlot::_RunCompletionCallbacks()
{
    std::vector<HostFence::Callback> callbacks = std::move(m_completionCallbacks);
    m_completionCallbacks.clear();

    for (HostFence::Callback& callback : callbacks)
    {
        callback();
    }
}

auto FrameSlot::GetCompletionFence(QueueFamilyType inQueueFamilyType) -> HostFence&
{
    const size_t queueIndex = _GetQueueIndex(inQueueFamilyType);
    std::unique_ptr<HostFence>& completionFence = m_completionFences[queueIndex];
    if (completionFence == nullptr)
    {
        completionFence = std::make_unique<HostFence>();
    }

    return *completionFence;
}

auto FrameSlot::AddCompletionCallback(HostFence::Callback inCallback) -> FrameSlot&
{
    CHECK_TRUE(static_cast<bool>(inCallback), "Frame completion callback is empty!");
    m_completionCallbacks.push_back(std::move(inCallback));
    return *this;
}

void FrameSlot::_Wait(DeviceContext& inDeviceContext)
{
    for (const std::unique_ptr<HostFence>& completionFence : m_completionFences)
    {
        if (completionFence != nullptr)
        {
            inDeviceContext.Wait(*completionFence);
        }
    }

    _RunCompletionCallbacks();
}

auto FrameSlot::DispatchRecording(
    QueueFamilyType inQueue,
    std::vector<CommandBuffer> inBuffers) -> RecordingTicket
{
    _GetQueueIndex(inQueue);

    std::vector<RecordingTask::RecordBatch> batches;
    RecordingTask::RecordBatch currentBatch;
    for (CommandBuffer& commandBuffer : inBuffers)
    {
        CHECK_TRUE(
            std::holds_alternative<std::monostate>(commandBuffer.m_renderingScopeState),
            "Command buffer has an active rendering scope!");

        if (commandBuffer.m_hasRenderingCommands)
        {
            if (!currentBatch.commands.empty())
            {
                batches.push_back(std::move(currentBatch));
                currentBatch = RecordingTask::RecordBatch{};
            }

            if (!commandBuffer.m_commands.empty())
            {
                RecordingTask::RecordBatch renderingBatch;
                renderingBatch.commands = commandBuffer.m_commands;
                batches.push_back(std::move(renderingBatch));
            }
            continue;
        }

        for (const Command* command : commandBuffer.m_commands)
        {
            if (currentBatch.commands.size() >= COMMAND_COUNT_PER_VK_COMMAND_BUFFER)
            {
                batches.push_back(std::move(currentBatch));
                currentBatch = RecordingTask::RecordBatch{};
            }
            currentBatch.commands.push_back(command);
        }
    }

    if (!currentBatch.commands.empty())
    {
        batches.push_back(std::move(currentBatch));
    }

    CHECK_TRUE(m_nextRecordingSerial != 0, "Recording ticket serial overflow!");
    const uint64_t serial = m_nextRecordingSerial++;
    auto recordContext = std::make_unique<RecordingTask>(
        *this,
        inQueue,
        std::move(inBuffers),
        std::move(batches));
    RecordingTask* recordContextPtr = recordContext.get();
    const bool inserted = m_recordTasks.emplace(serial, std::move(recordContext)).second;
    CHECK_TRUE(inserted, "Recording ticket already exists!");
    recordContextPtr->Dispatch();

    RecordingTicket ticket;
    ticket.m_serial = serial;
    return ticket;
}

auto FrameSlot::TakeRecordingResult(RecordingTicket inTicket) -> RecordingResult
{
    CHECK_TRUE(inTicket.IsValid(), "Recording ticket is invalid!");

    const auto iter = m_recordTasks.find(inTicket.m_serial);
    CHECK_TRUE(iter != m_recordTasks.end(), "Recording ticket does not exist or was already consumed!");
    std::unique_ptr<RecordingTask> recordContext = std::move(iter->second);
    m_recordTasks.erase(iter);
    return recordContext->WaitAndTakePayload();
}
