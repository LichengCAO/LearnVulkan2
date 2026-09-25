#include "render_context/device_context.h"

#include "command/command_queue.h"
#include "device.h"

auto DeviceContext::_GetQueueIndex(QueueFamilyType inQueue) -> size_t
{
	switch (inQueue)
	{
	case QueueFamilyType::GRAPHICS:
		return 0;
	case QueueFamilyType::COMPUTE:
		return 1;
	case QueueFamilyType::TRANSFER:
		return 2;
	default:
		CHECK_TRUE(false, "Invalid queue family type for device context!");
		return SubmissionFrontier::MAX_QUEUE_COUNT;
	}
}

DeviceContext::DeviceContext(size_t inFrameCount)
{
	CHECK_TRUE(inFrameCount > 0, "Device context must have at least one frame slot!");

	m_frameContexts.reserve(inFrameCount);
	while (m_frameContexts.size() < inFrameCount)
	{
		m_frameContexts.push_back(
			std::unique_ptr<FrameContext>(new FrameContext()));
	}
}

auto DeviceContext::_GetCurrentFrameContext() -> FrameContext&
{
	CHECK_TRUE(m_frameActive, "Device context has no active frame!");
	CHECK_TRUE(
		m_currentFrameIndex < m_frameContexts.size(),
		"Device context has no active frame!");
	CHECK_TRUE(
		m_frameContexts[m_currentFrameIndex] != nullptr,
		"Current frame context is not initialized!");
	return *m_frameContexts[m_currentFrameIndex];
}

void DeviceContext::StartFrame()
{
	CHECK_TRUE(!m_frameActive, "A device context frame is already active!");
	CHECK_TRUE(!m_frameContexts.empty(), "Device context has no frame slots!");

	const size_t nextFrameIndex = m_currentFrameIndex == SIZE_MAX
		? 0
		: (m_currentFrameIndex + 1) % m_frameContexts.size();
	FrameContext& frameContext = *m_frameContexts[nextFrameIndex];
	frameContext.ResetForReuse();

	for (QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Previous frame still has pending recording tickets!");
	}

	m_currentFrameIndex = nextFrameIndex;
	m_frameActive = true;
}

void DeviceContext::CommitCommandsToQueue(
	QueueFamilyType inQueue,
	std::vector<CommandBuffer> inBuffers)
{
	const size_t queueIndex = _GetQueueIndex(inQueue);
	FrameContext& frameContext = _GetCurrentFrameContext();
	FrameContext::RecordingTicket ticket =
		frameContext.DispatchRecording(inQueue, std::move(inBuffers));
	m_queueRecordingStates[queueIndex].pendingTickets.push_back(ticket);
}

void DeviceContext::EndFrame()
{
	FrameContext& frameContext = _GetCurrentFrameContext();
	for (const QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Cannot end frame while recording tickets remain pending!");
	}

	const auto submitCompletionMarker =
		[&frameContext](QueueFamilyType inQueue, CommandQueue* inCommandQueue)
		{
			CHECK_TRUE(inCommandQueue != nullptr, "Command queue is not available!");
			CommandQueue::SubmitInfo submitInfo;
			submitInfo.SetFence(frameContext.GetCompletionFence(inQueue));
			inCommandQueue->_SubmitVkCommandBuffers(nullptr, 0, std::move(submitInfo));
		};

	submitCompletionMarker(
		QueueFamilyType::GRAPHICS,
		MyDevice::GetInstance().GetGraphicsCommandQueue());
	submitCompletionMarker(
		QueueFamilyType::COMPUTE,
		MyDevice::GetInstance().GetComputeCommandQueue());
	submitCompletionMarker(
		QueueFamilyType::TRANSFER,
		MyDevice::GetInstance().GetTransferCommandQueue());

	m_frameActive = false;
}

void DeviceContext::SubmitQueue(
	QueueFamilyType inQueue,
	const QueueSubmitInfo& inSubmitInfo)
{
	const size_t queueIndex = _GetQueueIndex(inQueue);
	FrameContext& frameContext = _GetCurrentFrameContext();
	QueueRecordingState& recordingState = m_queueRecordingStates[queueIndex];
	std::vector<VkCommandBuffer> commandBuffers;

	while (!recordingState.pendingTickets.empty())
	{
		const FrameContext::RecordingTicket ticket = recordingState.pendingTickets.front();
		FrameContext::RecordedPayload payload = frameContext.TakeRecordedPayload(ticket);
		CHECK_TRUE(payload.queue == inQueue, "Recorded payload belongs to another queue!");
		commandBuffers.insert(
			commandBuffers.end(),
			payload.vkCommandBuffers.begin(),
			payload.vkCommandBuffers.end());
		recordingState.pendingTickets.erase(recordingState.pendingTickets.begin());
	}

	CommandQueue* commandQueue = nullptr;
	switch (inQueue)
	{
	case QueueFamilyType::GRAPHICS:
		commandQueue = MyDevice::GetInstance().GetGraphicsCommandQueue();
		break;
	case QueueFamilyType::COMPUTE:
		commandQueue = MyDevice::GetInstance().GetComputeCommandQueue();
		break;
	case QueueFamilyType::TRANSFER:
		commandQueue = MyDevice::GetInstance().GetTransferCommandQueue();
		break;
	default:
		CHECK_TRUE(false, "Invalid queue family type for device context submit!");
		break;
	}
	CHECK_TRUE(commandQueue != nullptr, "Command queue is not available!");

	CommandQueue::SubmitInfo submitInfo;
	for (QueueDependency* dependency : inSubmitInfo.queueSignals)
	{
		CHECK_TRUE(dependency != nullptr, "Queue signal dependency is null!");
		submitInfo.AddSignalQueueDependency(*dependency);
	}
	if (inSubmitInfo.hostFence != nullptr)
	{
		submitInfo.SetFence(*inSubmitInfo.hostFence);
	}

	commandQueue->_SubmitVkCommandBuffers(
		commandBuffers.empty() ? nullptr : commandBuffers.data(),
		commandBuffers.size(),
		std::move(submitInfo));
}

void DeviceContext::ExecuteCommandsAndWait(
	QueueFamilyType inQueue,
	std::vector<CommandBuffer> inBuffers)
{
	_GetQueueIndex(inQueue);

	FrameContext immediateFrameContext;
	const FrameContext::RecordingTicket ticket =
		immediateFrameContext.DispatchRecording(inQueue, std::move(inBuffers));
	FrameContext::RecordedPayload payload =
		immediateFrameContext.TakeRecordedPayload(ticket);
	CHECK_TRUE(payload.queue == inQueue, "Immediate recorded payload belongs to another queue!");

	CommandQueue* commandQueue = nullptr;
	switch (inQueue)
	{
	case QueueFamilyType::GRAPHICS:
		commandQueue = MyDevice::GetInstance().GetGraphicsCommandQueue();
		break;
	case QueueFamilyType::COMPUTE:
		commandQueue = MyDevice::GetInstance().GetComputeCommandQueue();
		break;
	case QueueFamilyType::TRANSFER:
		commandQueue = MyDevice::GetInstance().GetTransferCommandQueue();
		break;
	default:
		CHECK_TRUE(false, "Invalid queue family type for immediate execution!");
		break;
	}
	CHECK_TRUE(commandQueue != nullptr, "Command queue is not available!");

	HostFence completionFence;
	CommandQueue::SubmitInfo submitInfo;
	submitInfo.SetFence(completionFence);
	commandQueue->_SubmitVkCommandBuffers(
		payload.vkCommandBuffers.empty() ? nullptr : payload.vkCommandBuffers.data(),
		payload.vkCommandBuffers.size(),
		std::move(submitInfo));
	completionFence.Wait();
}

void DeviceContext::AddCurrentFrameCompletionCallback(
	HostFence::Callback inCallback)
{
	_GetCurrentFrameContext().AddCompletionCallback(std::move(inCallback));
}
