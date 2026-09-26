#include "render_context/device_context.h"

#include "render_context/submission_manager.h"

auto SubmissionSyncInfo::AddWaitQueueDependency(
	QueueDependency& inDependency,
	VkPipelineStageFlags2 inWaitStage)->SubmissionSyncInfo&
{
	m_submitInfo.AddWaitQueueDependency(inDependency, inWaitStage);
	return *this;
}

auto SubmissionSyncInfo::AddSignalQueueDependency(
	QueueDependency& inDependency)->SubmissionSyncInfo&
{
	m_submitInfo.AddSignalQueueDependency(inDependency);
	return *this;
}

auto SubmissionSyncInfo::SetFence(HostFence& inFence)->SubmissionSyncInfo&
{
	m_submitInfo.SetFence(inFence);
	return *this;
}

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
	m_uptrCommandQueueManager = std::make_unique<SubmissionManager>();

	m_frameContexts.reserve(inFrameCount);
	while (m_frameContexts.size() < inFrameCount)
	{
		m_frameContexts.push_back(
			std::unique_ptr<FrameSlot>(new FrameSlot()));
	}
}

DeviceContext::~DeviceContext() noexcept(false)
{
	for (const QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Cannot destroy a device context with pending recording tickets!");
	}
	for (const std::unique_ptr<FrameSlot>& frameContext : m_frameContexts)
	{
		if (frameContext != nullptr)
		{
			frameContext->_Wait(*this);
		}
	}
	if (m_uptrCommandQueueManager != nullptr)
	{
		m_uptrCommandQueueManager.reset();
	}
}

auto DeviceContext::_GetCurrentFrameContext() -> FrameSlot&
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
	FrameSlot& frameContext = *m_frameContexts[nextFrameIndex];
	frameContext.ResetForReuse(*this);

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
	FrameSlot& frameContext = _GetCurrentFrameContext();
	FrameSlot::RecordingTicket ticket =
		frameContext.DispatchRecording(inQueue, std::move(inBuffers));
	m_queueRecordingStates[queueIndex].pendingTickets.push_back(ticket);
}

void DeviceContext::EndFrame()
{
	FrameSlot& frameContext = _GetCurrentFrameContext();
	for (const QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Cannot end frame while recording tickets remain pending!");
	}
	CHECK_TRUE(m_uptrCommandQueueManager != nullptr, "Command queue manager is not available!");
	const auto submitCompletionMarker =
		[this, &frameContext](QueueFamilyType inQueue)
		{
			SubmissionManager::SubmitInfo submitInfo;
			submitInfo.SetFence(frameContext.GetCompletionFence(inQueue));
			m_uptrCommandQueueManager->Submit(inQueue, std::move(submitInfo));
		};

	submitCompletionMarker(QueueFamilyType::GRAPHICS);
	submitCompletionMarker(QueueFamilyType::COMPUTE);
	submitCompletionMarker(QueueFamilyType::TRANSFER);

	m_frameActive = false;
}

void DeviceContext::SubmitQueue(
	QueueFamilyType inQueue,
	const SubmissionSyncInfo& inSubmitInfo)
{
	const size_t queueIndex = _GetQueueIndex(inQueue);
	FrameSlot& frameContext = _GetCurrentFrameContext();
	QueueRecordingState& recordingState = m_queueRecordingStates[queueIndex];
	std::vector<VkCommandBuffer> commandBuffers;

	while (!recordingState.pendingTickets.empty())
	{
		const FrameSlot::RecordingTicket ticket = recordingState.pendingTickets.front();
		FrameSlot::RecordingResult payload = frameContext.TakeRecordingResult(ticket);
		CHECK_TRUE(payload.queue == inQueue, "Recorded payload belongs to another queue!");
		commandBuffers.insert(
			commandBuffers.end(),
			payload.vkCommandBuffers.begin(),
			payload.vkCommandBuffers.end());
		recordingState.pendingTickets.erase(recordingState.pendingTickets.begin());
	}

	CHECK_TRUE(m_uptrCommandQueueManager != nullptr, "Command queue manager is not available!");
	SubmissionManager::SubmitInfo submitInfo = inSubmitInfo.m_submitInfo;
	submitInfo.SetCommandBuffers(std::move(commandBuffers));
	m_uptrCommandQueueManager->Submit(inQueue, std::move(submitInfo));
}

void DeviceContext::ExecuteCommandsAndWait(
	QueueFamilyType inQueue,
	std::vector<CommandBuffer> inBuffers)
{
	_GetQueueIndex(inQueue);

	FrameSlot immediateFrameContext;
	const FrameSlot::RecordingTicket ticket =
		immediateFrameContext.DispatchRecording(inQueue, std::move(inBuffers));
	FrameSlot::RecordingResult payload =
		immediateFrameContext.TakeRecordingResult(ticket);
	CHECK_TRUE(payload.queue == inQueue, "Immediate recorded payload belongs to another queue!");

	CHECK_TRUE(m_uptrCommandQueueManager != nullptr, "Command queue manager is not available!");
	HostFence completionFence;
	SubmissionManager::SubmitInfo submitInfo;
	submitInfo
		.SetCommandBuffers(std::move(payload.vkCommandBuffers))
		.SetFence(completionFence);
	m_uptrCommandQueueManager->Submit(inQueue, std::move(submitInfo));
	Wait(completionFence);
}

void DeviceContext::Wait(HostFence& inFence)
{
	CHECK_TRUE(m_uptrCommandQueueManager != nullptr, "Command queue manager is not available!");
	m_uptrCommandQueueManager->Wait(inFence);
}

auto DeviceContext::Poll(HostFence& inFence)->bool
{
	CHECK_TRUE(m_uptrCommandQueueManager != nullptr, "Command queue manager is not available!");
	return m_uptrCommandQueueManager->Poll(inFence);
}

void DeviceContext::AddCurrentFrameCompletionCallback(
	HostFence::Callback inCallback)
{
	_GetCurrentFrameContext().AddCompletionCallback(std::move(inCallback));
}
