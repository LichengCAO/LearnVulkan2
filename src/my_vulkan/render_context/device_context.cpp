#include "render_context/device_context.h"

#include "render_context/submission_manager.h"
#include "device.h"

#include <algorithm>

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

auto SubmissionSyncInfo::WaitSwapchainImage(const SwapchainImage* inImage)->SubmissionSyncInfo&
{
	CHECK_TRUE(inImage != nullptr, "Swapchain wait image is null!");
	CHECK_TRUE(std::find(m_acquireWaitImages.begin(), m_acquireWaitImages.end(), inImage) ==
		m_acquireWaitImages.end(), "Swapchain image has a duplicate acquire wait!");
	m_submitInfo.AddExternalBinaryWait(
		inImage->GetAcquireSemaphore(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	m_acquireWaitImages.push_back(inImage);
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
	m_acquireSemaphores.resize(inFrameCount);
	m_acquireSemaphoreUsedCounts.resize(inFrameCount);
	while (m_frameContexts.size() < inFrameCount)
	{
		m_frameContexts.push_back(
			std::unique_ptr<FrameSlot>(new FrameSlot()));
	}
}

DeviceContext::~DeviceContext() noexcept
{
	for (const QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Cannot destroy a device context with pending recording tickets!");
	}
	MyDevice::GetInstance().WaitIdle();
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
	for (const std::vector<VkSemaphore>& semaphores : m_acquireSemaphores)
	{
		for (VkSemaphore semaphore : semaphores)
		{
			vkDestroySemaphore(MyDevice::GetInstance().vkDevice, semaphore, nullptr);
		}
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
	m_acquireSemaphoreUsedCounts[nextFrameIndex] = 0;

	for (QueueRecordingState& recordingState : m_queueRecordingStates)
	{
		CHECK_TRUE(
			recordingState.pendingTickets.empty(),
			"Previous frame still has pending recording tickets!");
	}

	m_currentFrameIndex = nextFrameIndex;
	m_frameActive = true;
}

auto DeviceContext::GetNextAvailableSwapchainImage() -> SwapchainImage*
{
	_GetCurrentFrameContext();
	std::vector<VkSemaphore>& pool = m_acquireSemaphores[m_currentFrameIndex];
	size_t& usedCount = m_acquireSemaphoreUsedCounts[m_currentFrameIndex];
	if (usedCount == pool.size())
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VK_CHECK(vkCreateSemaphore(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &semaphore),
			"Failed to create swapchain acquire semaphore!");
		try
		{
			pool.push_back(semaphore);
		}
		catch (...)
		{
			vkDestroySemaphore(MyDevice::GetInstance().vkDevice, semaphore, nullptr);
			throw;
		}
	}
	SwapchainImage* image = MyDevice::GetInstance().GetNextAvailableSwapchainImage(pool[usedCount]);
	if (image != nullptr)
	{
		try
		{
			m_acquiredImages.push_back(image);
		}
		catch (...)
		{
			// A successful Vulkan acquire cannot be rolled back.
			std::terminate();
		}
		++usedCount;
	}
	return image;
}

void DeviceContext::PresentSwapchainImage(
	const SwapchainImage* inImage, std::span<QueueDependency* const> inWaits)
{
	_GetCurrentFrameContext();
	const auto imageIter = std::find(m_acquiredImages.begin(), m_acquiredImages.end(), inImage);
	CHECK_TRUE(inImage != nullptr && imageIter != m_acquiredImages.end(),
		"Presented image was not acquired in the current frame!");
	CHECK_TRUE(inImage->m_acquireWaitSubmitted, "Swapchain acquire wait was not submitted!");
	CHECK_TRUE(m_queueRecordingStates[_GetQueueIndex(QueueFamilyType::GRAPHICS)].pendingTickets.empty(),
		"Cannot present while graphics recording tickets remain pending!");
	SubmissionManager::SubmitInfo bridge;
	for (QueueDependency* dependency : inWaits)
	{
		CHECK_TRUE(dependency != nullptr, "Present queue dependency is null!");
		bridge.AddWaitQueueDependency(*dependency);
	}
	bridge.AddExternalBinarySignal(inImage->GetRenderFinishedSemaphore());
	m_uptrCommandQueueManager->Submit(QueueFamilyType::GRAPHICS, std::move(bridge));
	MyDevice::GetInstance().PresentSwapchainImage(inImage);
	m_acquiredImages.erase(imageIter);
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
	CHECK_TRUE(m_acquiredImages.empty(), "Cannot end frame with unpresented swapchain images!");
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
	CHECK_TRUE(inQueue == QueueFamilyType::GRAPHICS || inSubmitInfo.m_acquireWaitImages.empty(),
		"Swapchain acquire waits must be submitted to the graphics queue!");
	for (const SwapchainImage* image : inSubmitInfo.m_acquireWaitImages)
	{
		CHECK_TRUE(std::find(m_acquiredImages.begin(), m_acquiredImages.end(), image) !=
			m_acquiredImages.end(), "Swapchain wait image was not acquired in this frame!");
		CHECK_TRUE(!image->m_acquireWaitSubmitted, "Swapchain acquire wait was already submitted!");
	}
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
	for (const SwapchainImage* image : inSubmitInfo.m_acquireWaitImages)
	{
		const_cast<SwapchainImage*>(image)->_CommitAcquireWait();
	}
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
