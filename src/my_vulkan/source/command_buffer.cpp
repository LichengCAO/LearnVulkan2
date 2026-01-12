#include "command_buffer.h"
#include "device.h"
#include "image.h"
#include "buffer.h"
void CommandSubmission::_CreateSynchronizeObjects()
{
	auto& device = MyDevice::GetInstance();

	m_vkSemaphore = device.CreateVkSemaphore();
	vkFence = device.CreateFence();
}

void CommandSubmission::_UpdateImageLayout(VkImage vkImage, VkImageSubresourceRange range, VkImageLayout layout) const
{
	MyDevice& device = MyDevice::GetInstance();
	if (device.imageLayouts.find(vkImage) != device.imageLayouts.end())
	{
		device.imageLayouts.at(vkImage).SetLayout(layout, range);
	}
}

void CommandSubmission::_BeginRenderPass(const VkRenderPassBeginInfo& info, VkSubpassContents content)
{
	CHECK_TRUE(!m_isInRenderpass, "Already in render pass!");
	m_isInRenderpass = true;
	vkCmdBeginRenderPass(vkCommandBuffer, &info, content);
}

void CommandSubmission::_DoCallbacks(CALLBACK_BINDING_POINT _bindingPoint)
{
	auto& queue = m_callbacks[_bindingPoint];
	while (!queue.empty())
	{
		auto& func = queue.front();
		func(this);
		queue.pop();
	}
}

void CommandSubmission::_AddPipelineBarrier2(const VkDependencyInfo& _dependency)
{
	vkCmdPipelineBarrier2KHR(vkCommandBuffer, &_dependency);
}

void CommandSubmission::PresetQueueFamilyIndex(uint32_t _queueFamilyIndex)
{
	m_optQueueFamilyIndex = _queueFamilyIndex;
}

void CommandSubmission::Init()
{
	if (!m_optQueueFamilyIndex.has_value())
	{
		auto fallbackIndex = MyDevice::GetInstance().queueFamilyIndices.graphicsAndComputeFamily;
		CHECK_TRUE(fallbackIndex.has_value(), "Queue family index is not set!");
		m_optQueueFamilyIndex = fallbackIndex.value();
	}
	m_vkQueue = MyDevice::GetInstance().GetQueue(m_optQueueFamilyIndex.value(), 0);
	VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocInfo.commandPool = MyDevice::GetInstance().vkCommandPools[m_optQueueFamilyIndex.value()];
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	VK_CHECK(vkAllocateCommandBuffers(MyDevice::GetInstance().vkDevice, &allocInfo, &vkCommandBuffer), "Failed to allocate command buffer!");
}

void CommandSubmission::Uninit()
{
	auto& device = MyDevice::GetInstance();
	WaitTillAvailable();
	if (vkCommandBuffer != VK_NULL_HANDLE)
	{
		// vkFreeCommandBuffers(MyDevice::GetInstance().vkDevice, MyDevice::GetInstance().vkCommandPools[m_optQueueFamilyIndex.value()], 1, &vkCommandBuffer);
		vkCommandBuffer = VK_NULL_HANDLE;
	}
	device.DestroyVkFence(vkFence);
	device.DestroyVkSemaphore(m_vkSemaphore);
	m_vkWaitSemaphores.clear();
	CHECK_TRUE(!m_isRecording, "Still recording commands!");
}

void CommandSubmission::StartCommands(const std::vector<WaitInformation>& _waitInfos)
{
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	size_t n = _waitInfos.size();
	CHECK_TRUE(vkCommandBuffer != VK_NULL_HANDLE, "Command buffer is not initialized!");
	CHECK_TRUE(!m_isRecording, "Already start commands!");
	
	WaitTillAvailable(); // To make sure that the first time we call start commands won't trigger COMMANDS_DONE
	if (vkFence == VK_NULL_HANDLE)
	{
		_CreateSynchronizeObjects();
	}
	vkResetFences(MyDevice::GetInstance().vkDevice, 1, &vkFence);
	vkResetCommandBuffer(vkCommandBuffer, 0);
	m_isRecording = true;
	VK_CHECK(vkBeginCommandBuffer(vkCommandBuffer, &beginInfo), "Failed to begin command!");
	m_vkWaitSemaphores.clear();
	m_vkWaitStages.clear();
	m_vkWaitSemaphores.reserve(n);
	m_vkWaitStages.reserve(n);
	for (const auto& waitInfo : _waitInfos)
	{
		m_vkWaitStages.push_back(waitInfo.waitPipelineStage);
		m_vkWaitSemaphores.push_back(waitInfo.waitSamaphore);
	}
}

std::optional<uint32_t> CommandSubmission::GetQueueFamilyIndex() const
{
	return m_optQueueFamilyIndex;
}

void CommandSubmission::StartOneTimeCommands(const std::vector<WaitInformation>& _waitInfos)
{
	CHECK_TRUE(vkCommandBuffer != VK_NULL_HANDLE, "Command buffer is not initialized!");
	CHECK_TRUE(!m_isRecording, "Already start commands!");
	
	m_isRecording = true;
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	
	VK_CHECK(vkBeginCommandBuffer(vkCommandBuffer, &beginInfo), "Failed to begin single time command!");
	m_vkWaitSemaphores.clear();
	m_vkWaitStages.clear();
	m_vkWaitSemaphores.reserve(_waitInfos.size());
	m_vkWaitStages.reserve(_waitInfos.size());
	for (const auto& waitInfo : _waitInfos)
	{
		m_vkWaitStages.push_back(waitInfo.waitPipelineStage);
		m_vkWaitSemaphores.push_back(waitInfo.waitSamaphore);
	}
}

void CommandSubmission::StartRenderPass(const RenderPass* pRenderPass, const Framebuffer* pFramebuffer)
{
	pRenderPass->StartRenderPass(this, pFramebuffer);
}

VkQueue CommandSubmission::GetVkQueue() const
{
	return m_vkQueue;
}

void CommandSubmission::EndRenderPass()
{
	CHECK_TRUE(m_isInRenderpass, "Not in render pass!");
	m_isInRenderpass = false;
	vkCmdEndRenderPass(vkCommandBuffer);
	_DoCallbacks(CALLBACK_BINDING_POINT::END_RENDER_PASS);
}

VkSemaphore CommandSubmission::SubmitCommands()
{
	CHECK_TRUE(m_isRecording, "Do not start commands yet!");
	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vkCommandBuffer;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(m_vkWaitSemaphores.size());
	submitInfo.pWaitSemaphores = m_vkWaitSemaphores.data();
	submitInfo.pWaitDstStageMask = m_vkWaitStages.data();
	if (vkFence == VK_NULL_HANDLE)
	{
		SubmitCommandsAndWait();
		Uninit(); // destroy the command buffer after one submission
	}
	else
	{
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &m_vkSemaphore;
		m_isRecording = false;
		VK_CHECK(vkEndCommandBuffer(vkCommandBuffer), "Failed to end command buffer!");
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, vkFence), "Failed to submit commands to queue!");
	}
	return m_vkSemaphore;
}

void CommandSubmission::SubmitCommands(const std::vector<VkSemaphore>& _semaphoresToSignal)
{
	CHECK_TRUE(m_isRecording, "Do not start commands yet!");
	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vkCommandBuffer;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(m_vkWaitSemaphores.size());
	submitInfo.pWaitSemaphores = m_vkWaitSemaphores.data();
	submitInfo.pWaitDstStageMask = m_vkWaitStages.data();
	if (vkFence == VK_NULL_HANDLE)
	{
		SubmitCommandsAndWait();
		Uninit(); // destroy the command buffer after one submission
	}
	else
	{
		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(_semaphoresToSignal.size());
		submitInfo.pSignalSemaphores = _semaphoresToSignal.data();
		m_isRecording = false;
		VK_CHECK(vkEndCommandBuffer(vkCommandBuffer), "Failed to end command buffer!");
		VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, vkFence), "Failed to submit commands to queue!");
	}
}

void CommandSubmission::SubmitCommandsAndWait()
{
	CHECK_TRUE(m_isRecording, "Do not start commands yet!");
	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vkCommandBuffer;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(m_vkWaitSemaphores.size());
	submitInfo.pWaitSemaphores = m_vkWaitSemaphores.data();
	submitInfo.pWaitDstStageMask = m_vkWaitStages.data();

	VK_CHECK(vkEndCommandBuffer(vkCommandBuffer), "Failed to end command buffer!");
	m_isRecording = false;
	VK_CHECK(vkQueueSubmit(m_vkQueue, 1, &submitInfo, vkFence), "Failed to submit single time commands to queue!");
	vkQueueWaitIdle(m_vkQueue);
	_DoCallbacks(CALLBACK_BINDING_POINT::COMMANDS_DONE);
}

void CommandSubmission::AddPipelineBarrier(
	VkPipelineStageFlags srcStageMask, 
	VkPipelineStageFlags dstStageMask, 
	const std::vector<VkImageMemoryBarrier>& imageBarriers)
{
	AddPipelineBarrier(srcStageMask, dstStageMask, {}, imageBarriers);
}
void CommandSubmission::AddPipelineBarrier(
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers)
{
	std::vector<VkImageMemoryBarrier> imageBarriers{};
	AddPipelineBarrier(srcStageMask, dstStageMask, memoryBarriers, imageBarriers);
}
void CommandSubmission::AddPipelineBarrier(
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers,
	const std::vector<VkImageMemoryBarrier>& imageBarriers)
{
	uint32_t memoryBarriersCount = static_cast<uint32_t>(memoryBarriers.size());
	uint32_t imageBarriersCount = static_cast<uint32_t>(imageBarriers.size());
	const VkMemoryBarrier* pMemoryBarriers = memoryBarriersCount == 0 ? nullptr : memoryBarriers.data();
	const VkImageMemoryBarrier* pImageBarriers = imageBarriersCount == 0 ? nullptr : imageBarriers.data();
	vkCmdPipelineBarrier(vkCommandBuffer,
		srcStageMask,
		dstStageMask,
		0,
		memoryBarriersCount, pMemoryBarriers,
		0, nullptr,
		imageBarriersCount, pImageBarriers
	);
	// update image layout
	for (const auto& imageBarrier : imageBarriers)
	{
		_UpdateImageLayout(imageBarrier.image, imageBarrier.subresourceRange, imageBarrier.newLayout);
	}
}

void CommandSubmission::ClearColorImage(VkImage vkImage, VkImageLayout vkImageLayout, const VkClearColorValue& clearColor, const std::vector<VkImageSubresourceRange>& ranges)
{
	vkCmdClearColorImage(
		vkCommandBuffer,
		vkImage,
		vkImageLayout,
		&clearColor,
		static_cast<uint32_t>(ranges.size()),
		ranges.data()
	);
}

void CommandSubmission::FillBuffer(VkBuffer vkBuffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
	vkCmdFillBuffer(vkCommandBuffer, vkBuffer, offset, size, data);
}

void CommandSubmission::BlitImage(VkImage srcImage, VkImageLayout srcLayout, VkImage dstImage, VkImageLayout dstLayout, const std::vector<VkImageBlit>& regions, VkFilter filter)
{
	vkCmdBlitImage(
		vkCommandBuffer,
		srcImage,
		srcLayout,
		dstImage,
		dstLayout,
		static_cast<uint32_t>(regions.size()),
		regions.data(),
		filter
	);

	for (const auto& region : regions)
	{
		VkImageSubresourceRange range{};
		range.aspectMask = region.dstSubresource.aspectMask;
		range.baseArrayLayer = region.dstSubresource.baseArrayLayer;
		range.layerCount = region.dstSubresource.layerCount;
		range.baseMipLevel = region.dstSubresource.mipLevel;
		range.levelCount = 1;
		_UpdateImageLayout(dstImage, range, dstLayout);
	}
}

void CommandSubmission::CopyBuffer(VkBuffer vkBufferFrom, VkBuffer vkBufferTo, std::vector<VkBufferCopy> const& copies) const
{
	vkCmdCopyBuffer(vkCommandBuffer, vkBufferFrom, vkBufferTo, static_cast<uint32_t>(copies.size()), copies.data());
}

void CommandSubmission::CopyBufferToImage(VkBuffer vkBuffer, VkImage vkImage, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions) const
{
	vkCmdCopyBufferToImage(
		vkCommandBuffer,
		vkBuffer,
		vkImage,
		layout,
		static_cast<uint32_t>(regions.size()),
		regions.data()
	);
}

void CommandSubmission::BuildAccelerationStructures(const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& buildGeomInfos, const std::vector<const VkAccelerationStructureBuildRangeInfoKHR*>& buildRangeInfoPtrs) const
{
	vkCmdBuildAccelerationStructuresKHR(vkCommandBuffer, static_cast<uint32_t>(buildGeomInfos.size()), buildGeomInfos.data(), buildRangeInfoPtrs.data());
}

void CommandSubmission::WriteAccelerationStructuresProperties(const std::vector<VkAccelerationStructureKHR>& vkAccelerationStructs, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery) const
{
	vkCmdWriteAccelerationStructuresPropertiesKHR(vkCommandBuffer, static_cast<uint32_t>(vkAccelerationStructs.size()), vkAccelerationStructs.data(), queryType, queryPool, firstQuery);
}

void CommandSubmission::CopyAccelerationStructure(const VkCopyAccelerationStructureInfoKHR& copyInfo) const
{
	vkCmdCopyAccelerationStructureKHR(vkCommandBuffer, &copyInfo);
}

void CommandSubmission::BindCallback(CALLBACK_BINDING_POINT bindPoint, std::function<void(CommandSubmission*)>&& callback)
{
	m_callbacks[bindPoint].push(std::move(callback));
}

void CommandSubmission::WaitTillAvailable()
{
	if (vkFence != VK_NULL_HANDLE && !m_isRecording)
	{
		vkWaitForFences(MyDevice::GetInstance().vkDevice, 1, &vkFence, VK_TRUE, UINT64_MAX);
		_DoCallbacks(CALLBACK_BINDING_POINT::COMMANDS_DONE);
	}
}

ImageBarrierBuilder::ImageBarrierBuilder()
{
	Reset();
}
ImageBarrierBuilder& ImageBarrierBuilder::Reset()
{
	m_subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
	m_subresourceRange.baseArrayLayer = 0;
	m_subresourceRange.baseMipLevel = 0;
	m_subresourceRange.layerCount = 1;
	m_subresourceRange.levelCount = 1;
	m_dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	m_srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeMipLevelRange(uint32_t baseMipLevel, uint32_t levelCount)
{
	m_subresourceRange.baseMipLevel = baseMipLevel;
	m_subresourceRange.levelCount = levelCount;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_subresourceRange.baseArrayLayer = baseArrayLayer;
	m_subresourceRange.layerCount = layerCount;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeImageAspect(VkImageAspectFlags aspectMask)
{
	m_subresourceRange.aspectMask = aspectMask;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeQueueFamilyTransfer(uint32_t inQueueFamilyTransferFrom, uint32_t inQueueFamilyTransferTo)
{
	m_dstQueueFamilyIndex = inQueueFamilyTransferTo;
	m_srcQueueFamilyIndex = inQueueFamilyTransferFrom;
	return *this;
}
VkImageMemoryBarrier ImageBarrierBuilder::NewBarrier(
	VkImage _image, 
	VkImageLayout _oldLayout, 
	VkImageLayout _newLayout, 
	VkAccessFlags _srcAccessMask, 
	VkAccessFlags _dstAccessMask) const
{
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = _oldLayout;
	barrier.newLayout = _newLayout;
	barrier.srcQueueFamilyIndex = m_srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = m_dstQueueFamilyIndex;
	barrier.image = _image;
	barrier.subresourceRange = m_subresourceRange;
	barrier.srcAccessMask = _srcAccessMask;
	barrier.dstAccessMask = _dstAccessMask;
	return barrier;
}

ImageBlitBuilder::ImageBlitBuilder()
{
	Reset();
}

ImageBlitBuilder& ImageBlitBuilder::Reset()
{
	m_srcSubresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	m_srcSubresourceLayers.baseArrayLayer = 0;
	m_srcSubresourceLayers.layerCount = 1;

	m_dstSubresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	m_dstSubresourceLayers.baseArrayLayer = 0;
	m_dstSubresourceLayers.layerCount = 1;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeSrcAspect(VkImageAspectFlags aspectMask)
{
	m_srcSubresourceLayers.aspectMask = aspectMask;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeDstAspect(VkImageAspectFlags aspectMask)
{
	m_dstSubresourceLayers.aspectMask = aspectMask;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeSrcArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_srcSubresourceLayers.baseArrayLayer = baseArrayLayer;
	m_srcSubresourceLayers.layerCount = layerCount;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeDstArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_dstSubresourceLayers.baseArrayLayer = baseArrayLayer;
	m_dstSubresourceLayers.layerCount = layerCount;

	return *this;
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkOffset3D& srcOffsetUL, 
	const VkOffset3D& srcOffsetLR, 
	uint32_t srcMipLevel, 
	const VkOffset3D& dstOffsetUL, 
	const VkOffset3D& dstOffsetLR, 
	uint32_t dstMipLevel) const
{
	VkImageBlit blit{};
	blit.srcOffsets[0] = srcOffsetUL;
	blit.srcOffsets[1] = srcOffsetLR;
	blit.dstOffsets[0] = dstOffsetUL;
	blit.dstOffsets[1] = dstOffsetLR;
	blit.srcSubresource = m_srcSubresourceLayers;
	blit.srcSubresource.mipLevel = srcMipLevel;
	blit.dstSubresource = m_dstSubresourceLayers;
	blit.dstSubresource.mipLevel = dstMipLevel;
	return blit;
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkExtent2D& srcImageSize, 
	uint32_t srcMipLevel, 
	const VkExtent2D& dstImageSize, 
	uint32_t dstMipLevel) const
{
	VkOffset3D srcOffset;
	VkOffset3D dstOffset;

	srcOffset.z = 1;
	dstOffset.z = 1;

	srcOffset.x = static_cast<int32_t>(srcImageSize.width);
	srcOffset.y = static_cast<int32_t>(srcImageSize.height);
	dstOffset.x = static_cast<int32_t>(dstImageSize.width);
	dstOffset.y = static_cast<int32_t>(dstImageSize.height);

	return NewBlit(
		{ 0, 0, 0 }, 
		srcOffset, 
		srcMipLevel, 
		{ 0, 0, 0 }, 
		dstOffset, 
		dstMipLevel);
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkOffset2D& inSrcOffsetUpperLeftXY, 
	const VkOffset2D& inSrcOffsetLowerRightXY, 
	uint32_t inSrcMipLevel, 
	const VkOffset2D& inDstOffsetUpperLeftXY,
	const VkOffset2D& inDstOffsetLowerRightXY,
	uint32_t inDstMipLevel) const
{
	return NewBlit(
		{ inSrcOffsetUpperLeftXY.x, inSrcOffsetUpperLeftXY.y, 0 },
		{ inSrcOffsetLowerRightXY.x, inSrcOffsetLowerRightXY.y, 1},
		inSrcMipLevel,
		{ inDstOffsetUpperLeftXY.x, inDstOffsetUpperLeftXY.y, 0},
		{ inDstOffsetLowerRightXY.x, inDstOffsetLowerRightXY.y, 1},
		inDstMipLevel);
}

void CommandBuffer::Initializer::InitCommandBuffer(CommandBuffer* outCommandBufferPtr) const
{
	auto& myDevice = MyDevice::GetInstance();

	outCommandBufferPtr->m_vkCommandPool = m_vkCommandPool;
	outCommandBufferPtr->m_vkCommandBufferLevel = m_bufferLevel;
	outCommandBufferPtr->m_vkCommandBuffer = myDevice.AllocateCommandBuffer(m_vkCommandPool, m_bufferLevel);
	outCommandBufferPtr->m_queueFamily = myDevice.GetQueueFamilyIndex(m_vkCommandPool);
}

CommandBuffer::Initializer& CommandBuffer::Initializer::Reset()
{
	*this = CommandBuffer::Initializer{};

	return *this;
}

CommandBuffer::Initializer& CommandBuffer::Initializer::SetCommandPool(VkCommandPool inCommandPool)
{
	m_vkCommandPool = inCommandPool;

	return *this;
}

CommandBuffer::Initializer& CommandBuffer::Initializer::CustomizeCommandBufferLevel(VkCommandBufferLevel inLevel)
{
	m_bufferLevel = inLevel;

	return *this;
}

void CommandBuffer::_ProcessInCmdScope(const std::function<void()>& inFunction)
{
	// common things to do before processing
	CHECK_TRUE(m_isRecording, "Command buffer is not in recording state!");

	inFunction();
	// common things to do after processing
}

uint32_t CommandBuffer::GetQueueFamliyIndex() const
{
	return m_queueFamily;
}

VkCommandBuffer CommandBuffer::GetVkCommandBuffer() const
{
	return m_vkCommandBuffer;
}

VkCommandBufferLevel CommandBuffer::GetCommandBufferLevel() const
{
	return m_vkCommandBufferLevel;
}

bool CommandBuffer::IsRecording() const
{
	return m_isRecording;
}

CommandBuffer::~CommandBuffer()
{
	// Do nothing, it's ok that command buffer is not freed, it will be freed by comannd pool when it is destroyed anyway
}

void CommandBuffer::_Init(const ICommandBufferInitializer* inInitializerPtr)
{
	inInitializerPtr->InitCommandBuffer(this);
}

void CommandBuffer::_Uninit()
{
	auto& myDevice = MyDevice::GetInstance();

	myDevice.FreeCommandBuffer(m_vkCommandPool, m_vkCommandBuffer);
	m_vkCommandBuffer = VK_NULL_HANDLE;
	m_vkCommandBufferLevel = VK_COMMAND_BUFFER_LEVEL_MAX_ENUM;
	m_vkCommandPool = VK_NULL_HANDLE;
	m_queueFamily = ~0;
	m_isFullyRecorded = false;
	m_isInRenderPass = false;
	m_isRecording = false;
}

void CommandBuffer::Reset(VkCommandBufferResetFlags inFlags)
{
	m_isFullyRecorded = false;
	m_isInRenderPass = false;
	m_isRecording = false;
	VK_CHECK(vkResetCommandBuffer(m_vkCommandBuffer, inFlags));
}

CommandBuffer& CommandBuffer::BeginCommands(VkCommandBufferUsageFlags inFlags, const std::optional<VkCommandBufferInheritanceInfo>& inInheritanceInfo, const void* inNextPtr)
{
	VkCommandBufferBeginInfo beginInfo;
	
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = inNextPtr;
	beginInfo.flags = inFlags;
	beginInfo.pInheritanceInfo = inInheritanceInfo.has_value() ? &inInheritanceInfo.value() : nullptr;

	CHECK_TRUE(!m_isRecording);
	CHECK_TRUE(!m_isInRenderPass);
	CHECK_TRUE(!m_isFullyRecorded);
	VK_CHECK(vkBeginCommandBuffer(this->m_vkCommandBuffer, &beginInfo));
	m_isRecording = true;

	return *this;
}

CommandBuffer& CommandBuffer::EndCommands()
{
	CHECK_TRUE(m_isRecording, "Command buffer is not in recording state!");
	CHECK_TRUE(!m_isInRenderPass);
	CHECK_TRUE(!m_isFullyRecorded);
	VK_CHECK(vkEndCommandBuffer(this->m_vkCommandBuffer));
	m_isRecording = false;
	m_isFullyRecorded = true;

	return *this;
}

CommandBuffer& CommandBuffer::CmdFromExternal(std::function<void(VkCommandBuffer)> inExternalRecord)
{
	_ProcessInCmdScope(
		[this, inExternalRecord]() 
		{ 
			inExternalRecord(this->m_vkCommandBuffer); 
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdBeginRenderPass(const VkRenderPassBeginInfo& inRenderPassBeginInfo, VkSubpassContents inContents)
{
	m_isInRenderPass = true;
	_ProcessInCmdScope(
		[this, &inRenderPassBeginInfo, inContents]()
		{
			vkCmdBeginRenderPass(this->m_vkCommandBuffer, &inRenderPassBeginInfo, inContents);
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdBeginRenderPass(
	VkRenderPass inRenderPass, 
	VkFramebuffer inFramebuffer, 
	int32_t offsetX,
	int32_t offsetY, 
	uint32_t width, 
	uint32_t height, 
	const std::vector<VkClearValue>& inClearValues, 
	VkSubpassContents inContents, 
	const void* inNextPtr)
{
	VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

	renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(inClearValues.size());
	renderPassBeginInfo.pClearValues = inClearValues.data();
	renderPassBeginInfo.framebuffer = inFramebuffer;
	renderPassBeginInfo.renderPass = inRenderPass;
	renderPassBeginInfo.renderArea.offset = { offsetX, offsetY };
	renderPassBeginInfo.renderArea.extent = { width, height };
	renderPassBeginInfo.pNext = inNextPtr;

	return CmdBeginRenderPass(renderPassBeginInfo, inContents);
}

CommandBuffer& CommandBuffer::CmdEndRenderPass()
{
	_ProcessInCmdScope(
		[this]()
		{
			vkCmdEndRenderPass(this->m_vkCommandBuffer);
		});
	m_isInRenderPass = false;

	return *this;
}

CommandBuffer& CommandBuffer::CmdPipelineBarrier(
	VkPipelineStageFlags inSrcStageMask,
	VkPipelineStageFlags inDstStageMask,
	const std::vector<VkMemoryBarrier>& inMemoryBarriers,
	VkDependencyFlags inFlags)
{
	return CmdPipelineBarrier(
		inSrcStageMask,
		inDstStageMask,
		inMemoryBarriers,
		{},
		{},
		inFlags);
}

CommandBuffer& CommandBuffer::CmdPipelineBarrier(
	VkPipelineStageFlags inSrcStageMask,
	VkPipelineStageFlags inDstStageMask,
	const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
	VkDependencyFlags inFlags)
{
	return CmdPipelineBarrier(
		inSrcStageMask,
		inDstStageMask,
		{},
		inBufferBarriers,
		{},
		inFlags);
}

CommandBuffer& CommandBuffer::CmdPipelineBarrier(
	VkPipelineStageFlags inSrcStageMask,
	VkPipelineStageFlags inDstStageMask,
	const std::vector<VkImageMemoryBarrier>& inImageBarriers,
	VkDependencyFlags inFlags)
{
	return CmdPipelineBarrier(
		inSrcStageMask,
		inDstStageMask,
		{},
		{},
		inImageBarriers,
		inFlags);
}

CommandBuffer& CommandBuffer::CmdPipelineBarrier(
	VkPipelineStageFlags inSrcStageMask, 
	VkPipelineStageFlags inDstStageMask, 
	const std::vector<VkMemoryBarrier>& inMemoryBarriers, 
	const std::vector<VkBufferMemoryBarrier>& inBufferBarriers, 
	const std::vector<VkImageMemoryBarrier>& inImageBarriers, 
	VkDependencyFlags inFlags)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdPipelineBarrier(
				m_vkCommandBuffer,
				inSrcStageMask,
				inDstStageMask,
				inFlags,
				static_cast<uint32_t>(inMemoryBarriers.size()),
				inMemoryBarriers.data(),
				static_cast<uint32_t>(inBufferBarriers.size()),
				inBufferBarriers.data(),
				static_cast<uint32_t>(inImageBarriers.size()),
				inImageBarriers.data());
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdClearColorImage(
	VkImage inImage, 
	VkImageLayout inImageLayout, 
	const VkClearColorValue& inClearColor, 
	const std::vector<VkImageSubresourceRange>& inRanges)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdClearColorImage(
				m_vkCommandBuffer,
				inImage,
				inImageLayout,
				&inClearColor,
				static_cast<uint32_t>(inRanges.size()),
				inRanges.data());
		});
	
	return *this;
}

CommandBuffer& CommandBuffer::CmdFillBuffer(VkBuffer inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize, uint32_t inValue)
{
	_ProcessInCmdScope(
		[this, inBuffer, inOffset, inSize, inValue]()
		{
			vkCmdFillBuffer(this->m_vkCommandBuffer, inBuffer, inOffset, inSize, inValue);
		});
	return *this;
}

CommandBuffer& CommandBuffer::CmdBlitImage(
	VkImage inSrcImage, 
	VkImageLayout inSrcLayout, 
	VkImage inDstImage, 
	VkImageLayout inDstLayout, 
	const std::vector<VkImageBlit>& inRegions, 
	VkFilter inFilter)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdBlitImage(
				m_vkCommandBuffer,
				inSrcImage,
				inSrcLayout,
				inDstImage,
				inDstLayout,
				static_cast<uint32_t>(inRegions.size()),
				inRegions.data(),
				inFilter);
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdCopyBufferToImage(
	VkBuffer inSrcBuffer,
	VkImage inDstImage,
	VkImageLayout inDstLayout,
	const std::vector<VkBufferImageCopy>& inRegions)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdCopyBufferToImage(
				m_vkCommandBuffer,
				inSrcBuffer,
				inDstImage,
				inDstLayout,
				static_cast<uint32_t>(inRegions.size()),
				inRegions.data());
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdCopyBuffer(VkBuffer inSrcBuffer, VkBuffer inDstBuffer, const std::vector<VkBufferCopy>& inRegions)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdCopyBuffer(m_vkCommandBuffer, inSrcBuffer, inDstBuffer, static_cast<uint32_t>(inRegions.size()), inRegions.data());
		}
	);
	
	return *this;
}

CommandBuffer& CommandBuffer::CmdBuildAccelerationStructuresKHR(
	const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& inBuildInfos,
	const std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& inBuildRanges)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdBuildAccelerationStructuresKHR(
				m_vkCommandBuffer,
				static_cast<uint32_t>(inBuildInfos.size()),
				inBuildInfos.data(),
				inBuildRanges.data());
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdWriteAccelerationStructuresPropertiesKHR(
	const std::vector<VkAccelerationStructureKHR>& inAccelerationStructures,
	VkQueryType inQueryType,
	VkQueryPool inQueryPool,
	uint32_t inFirstQuery)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdWriteAccelerationStructuresPropertiesKHR(
				m_vkCommandBuffer,
				static_cast<uint32_t>(inAccelerationStructures.size()),
				inAccelerationStructures.data(),
				inQueryType,
				inQueryPool,
				inFirstQuery);
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdCopyAccelerationStructureKHR(
	const VkCopyAccelerationStructureInfoKHR& inCopyInfo)
{
	_ProcessInCmdScope(
		[&]()
		{
			vkCmdCopyAccelerationStructureKHR(
				m_vkCommandBuffer,
				&inCopyInfo);
		});

	return *this;
}

CommandBuffer& CommandBuffer::CmdCopyAccelerationStructureKHR(VkAccelerationStructureKHR inSrcAS, VkAccelerationStructureKHR inDstAS, VkCopyAccelerationStructureModeKHR inMode, const void* inNextPtr)
{
	VkCopyAccelerationStructureInfoKHR copyInfo{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
	copyInfo.src = inSrcAS;
	copyInfo.dst = inDstAS;
	copyInfo.mode = inMode;
	copyInfo.pNext = inNextPtr;
	return CmdCopyAccelerationStructureKHR(copyInfo);
}

CommandBuffer& CommandBuffer::CmdExecuteCommands(const std::vector<VkCommandBuffer>& inCommandBuffers)
{
	CHECK_TRUE(m_vkCommandBufferLevel == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	_ProcessInCmdScope(
		[this, &inCommandBuffers]()
		{
			vkCmdExecuteCommands(this->m_vkCommandBuffer, static_cast<uint32_t>(inCommandBuffers.size()), inCommandBuffers.data());
		});

	return *this;
}

CommandPool::~CommandPool()
{
	CHECK_TRUE(m_vkCommandPool == VK_NULL_HANDLE);
}

void CommandPool::Init(const ICommandPoolInitializer* inInitializerPtr)
{
	CHECK_TRUE(inInitializerPtr != nullptr);

	inInitializerPtr->InitCommandPool(this);

	CHECK_TRUE(m_vkCommandPool != VK_NULL_HANDLE);
	CHECK_TRUE(m_queueFamilyIndex != ~0);
}

CommandPool& CommandPool::FreeCommandBuffer(CommandBuffer* inoutBufferReturnedPtr)
{
	auto& device = MyDevice::GetInstance();

	inoutBufferReturnedPtr->_Uninit();

	return *this;
}

uint32_t CommandPool::GetQueueFamilyIndex() const
{
	return m_queueFamilyIndex;
}

void CommandPool::Uninit()
{
	if (m_vkCommandPool != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		device.DestroyCommandPool(m_vkCommandPool);
		m_vkCommandPool = VK_NULL_HANDLE;
	}
	m_queueFamilyIndex = ~0;
}

void CommandPool::Initializer::InitCommandPool(CommandPool* outPoolPtr) const
{
	auto& device = MyDevice::GetInstance();
	VkCommandPoolCreateInfo createInfo{};

	createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	createInfo.flags = m_createFlags;
	createInfo.queueFamilyIndex = m_queueFamilyIndex;
	if (m_queueFamilyIndex == ~0)
	{
		createInfo.queueFamilyIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
	}

	outPoolPtr->m_vkCommandPool = device.CreateCommandPool(createInfo);
	outPoolPtr->m_queueFamilyIndex = createInfo.queueFamilyIndex;
}

CommandPool::Initializer& CommandPool::Initializer::Reset()
{
	*this = CommandPool::Initializer{};

	return *this;
}

CommandPool::Initializer& CommandPool::Initializer::CustomizeCommandPoolCreateFlags(VkCommandPoolCreateFlags inFlags)
{
	m_createFlags = inFlags;
	
	return *this;
}

CommandPool::Initializer& CommandPool::Initializer::CustomizeQueueFamilyIndex(uint32_t inIndex)
{
	m_queueFamilyIndex = inIndex;

	return *this;
}

VkCommandPool CommandPool::GetVkCommandPool() const
{
	return m_vkCommandPool;
}

CommandPool& CommandPool::ResetPool()
{
	if (m_vkCommandPool != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		device.ResetCommandPool(m_vkCommandPool);
	}

	return *this;
}

CommandPool& CommandPool::AllocateCommandBuffer(CommandBuffer* outCommandBufferPtr, VkCommandBufferLevel inBufferLevel)
{
	CommandBuffer::Initializer bufferInit{};

	bufferInit.CustomizeCommandBufferLevel(inBufferLevel).SetCommandPool(m_vkCommandPool);
	outCommandBufferPtr->_Init(&bufferInit);

	return *this;
}
