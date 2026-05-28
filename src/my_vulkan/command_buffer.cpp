#include "command_buffer.h"
#include "device.h"
#include "image.h"
#include "buffer.h"

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

void CommandBuffer::Create(const ICommandBufferInitializer* inInitializerPtr)
{
	inInitializerPtr->InitCommandBuffer(this);
}

void CommandBuffer::Destroy()
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

CommandBuffer& CommandBuffer::BeginCommands(
	VkCommandBufferUsageFlags inFlags, 
	const std::optional<VkCommandBufferInheritanceInfo>& inInheritanceInfo, 
	const void* inNextPtr)
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
	const std::vector<const VkAccelerationStructureBuildRangeInfoKHR*>& inBuildRanges)
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

void CommandPool::Create(const ICommandPoolInitializer* inInitializerPtr)
{
	CHECK_TRUE(inInitializerPtr != nullptr);

	inInitializerPtr->InitCommandPool(this);

	CHECK_TRUE(m_vkCommandPool != VK_NULL_HANDLE);
	CHECK_TRUE(m_queueFamilyIndex != ~0);
}

CommandPool& CommandPool::FreeCommandBuffer(CommandBuffer* inoutBufferReturnedPtr)
{
	auto& device = MyDevice::GetInstance();

	inoutBufferReturnedPtr->Destroy();

	return *this;
}

uint32_t CommandPool::GetQueueFamilyIndex() const
{
	return m_queueFamilyIndex;
}

void CommandPool::Destroy()
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
