#include "buffer.h"
#include "device.h"
#include "commandbuffer.h"
#include "memory_allocator.h"
#include "utils.h"

namespace 
{
	void _SubmitToGraphicsQueueAndWait(VkCommandBuffer inCommandBuffer)
	{
		VkSubmitInfo submitInfo{};
		std::vector<VkCommandBuffer> cmdsToSubmit = { inCommandBuffer };
		auto& device = MyDevice::GetInstance();
		VkQueue queueToSubmit = device.GetQueueOfType(QueueFamilyType::GRAPHICS);
		
		submitInfo.commandBufferCount = cmdsToSubmit.size();
		submitInfo.pCommandBuffers = cmdsToSubmit.data();
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		vkQueueSubmit(queueToSubmit, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(queueToSubmit);
	}
}

Buffer::~Buffer()
{
	assert(m_vkBuffer == VK_NULL_HANDLE);
	assert(m_mappedMemory == nullptr);
}

void Buffer::Init(const Buffer::Initializer* inInitializerPtr)
{
	auto& device = MyDevice::GetInstance();
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = inInitializerPtr->m_bufferSize;
	bufferInfo.usage = inInitializerPtr->m_usage;
	bufferInfo.sharingMode = inInitializerPtr->m_sharingMode;
	CHECK_TRUE(m_vkBuffer == VK_NULL_HANDLE);
	m_bufferInformation.memoryProperty = inInitializerPtr->m_memoryProperty;
	m_bufferInformation.sharingMode = inInitializerPtr->m_sharingMode;
	m_bufferInformation.size = inInitializerPtr->m_bufferSize;
	m_bufferInformation.usage = inInitializerPtr->m_usage;
	m_bufferInformation.optAlignment = inInitializerPtr->m_optAlignment;
	m_vkBuffer = device.CreateBuffer(bufferInfo);
	_AllocateMemory();
}

void Buffer::Uninit()
{
	if (m_vkBuffer != VK_NULL_HANDLE)
	{
		_FreeMemory();
		MyDevice::GetInstance().DestroyBuffer(m_vkBuffer);
		m_vkBuffer = VK_NULL_HANDLE;
	}
}

MemoryAllocator* Buffer::_GetMemoryAllocator() const
{
	return MyDevice::GetInstance().GetMemoryAllocator();
}

void Buffer::_AllocateMemory()
{	
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_bufferInformation.optAlignment.has_value())
	{
		pAllocator->AllocateForVkBuffer(m_vkBuffer, m_bufferInformation.memoryProperty, m_bufferInformation.optAlignment.value());
	}
	else
	{
		pAllocator->AllocateForVkBuffer(m_vkBuffer, m_bufferInformation.memoryProperty);
	}
}

void Buffer::_FreeMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_mappedMemory != nullptr)
	{
		pAllocator->UnmapVkBuffer(m_vkBuffer);
		m_mappedMemory = nullptr;
	}
	pAllocator->FreeMemory(m_vkBuffer);
}

void Buffer::_MapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	CHECK_TRUE(CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "Unmappable buffer!");
	pAllocator->MapVkBufferToHost(m_vkBuffer, m_mappedMemory);
}

void Buffer::_UnmapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->UnmapVkBuffer(m_vkBuffer);
}

void Buffer::_CopyFromHostWithMappedMemory(const void* src, size_t bufferOffest, size_t size)
{
	if (m_mappedMemory == nullptr)
	{
		MemoryAllocator* pAllocator = _GetMemoryAllocator();
		pAllocator->MapVkBufferToHost(m_vkBuffer, m_mappedMemory);
	}
	uint8_t* pMapped = (uint8_t*)m_mappedMemory;
	pMapped += bufferOffest;
	memcpy((void*)pMapped, src, size);
}

void Buffer::_CopyFromHostWithStaggingBuffer(const void* src, size_t bufferOffest, size_t size)
{
	Buffer stagingBuffer{};
	Buffer::Initializer initializer{};

	initializer.SetBufferSize(size).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	initializer.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	stagingBuffer.Init(&initializer);
	stagingBuffer.CopyFromHost(src);
	
	CopyFromBuffer(&stagingBuffer, 0, bufferOffest, size);

	stagingBuffer.Uninit();
}

Buffer::Buffer()
{
}

Buffer::Buffer(Buffer&& _toMove)
{
	m_vkBuffer = _toMove.m_vkBuffer;
	m_mappedMemory = _toMove.m_mappedMemory;
	m_bufferInformation = _toMove.m_bufferInformation;
	_toMove.m_vkBuffer = VK_NULL_HANDLE;
	_toMove.m_mappedMemory = nullptr;
}

void Buffer::CopyFromHost(const void* src)
{
	CopyFromHost(src, 0, static_cast<size_t>(m_bufferInformation.size));
}

void Buffer::CopyFromHost(const void* src, size_t bufferOffset, size_t size)
{
	CHECK_TRUE(size <= static_cast<size_t>(m_bufferInformation.size), "Try to copy too much data from host!");
	if (CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		&& CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		_CopyFromHostWithMappedMemory(src, bufferOffset, size);
	}
	else
	{
		_CopyFromHostWithStaggingBuffer(src, bufferOffset, size);
	}
}

void Buffer::CopyFromBuffer(const Buffer* pOtherBuffer)
{
	CopyFromBuffer(pOtherBuffer, 0, 0, m_bufferInformation.size);
}

void Buffer::CopyFromBuffer(const Buffer* pOtherBuffer, size_t srcOffset, size_t dstOffset, size_t size)
{
	CommandPool tmpPool{};
	CommandPool::Initializer poolInit{};
	CommandBuffer tmpCmdBuffer{};

	poolInit.CustomizeCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	tmpPool.Init(&poolInit);
	tmpPool.AllocateCommandBuffer(&tmpCmdBuffer);
	tmpCmdBuffer.BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	CopyFromBuffer(pOtherBuffer, srcOffset, dstOffset, size, &tmpCmdBuffer);
	tmpCmdBuffer.EndCommands();
	_SubmitToGraphicsQueueAndWait(tmpCmdBuffer.GetVkCommandBuffer());
	tmpPool.Uninit();
}

void Buffer::CopyFromBuffer(
	const Buffer* pOtherBuffer, 
	size_t srcOffset, 
	size_t dstOffset, 
	size_t size, 
	CommandBuffer* inCmdPtr)
{
	VkBufferCopy copyInfo{};

	CHECK_TRUE(pOtherBuffer != nullptr);
	CHECK_TRUE(inCmdPtr != nullptr);
	CHECK_TRUE(m_vkBuffer != VK_NULL_HANDLE);
	CHECK_TRUE(CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_TRANSFER_DST_BIT));

	copyInfo.size = size;
	copyInfo.dstOffset = dstOffset;
	copyInfo.srcOffset = srcOffset;

	inCmdPtr->CmdCopyBuffer(pOtherBuffer->m_vkBuffer, m_vkBuffer, { copyInfo });
}

void Buffer::Fill(uint32_t inData)
{
	CommandPool tmpPool{};
	CommandPool::Initializer poolInit{};
	CommandBuffer cmd{};

	poolInit.CustomizeCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	tmpPool.Init(&poolInit);
	tmpPool.AllocateCommandBuffer(&cmd);	
	cmd.BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmd.CmdFillBuffer(m_vkBuffer, 0, m_bufferInformation.size, inData);
	cmd.EndCommands();
	_SubmitToGraphicsQueueAndWait(cmd.GetVkCommandBuffer());
	tmpPool.Uninit();
}

void Buffer::Fill(uint32_t inData, CommandBuffer* inCmdPtr)
{
	CHECK_TRUE(inCmdPtr != nullptr);
	inCmdPtr->CmdFillBuffer(m_vkBuffer, 0, m_bufferInformation.size, inData);
}

const Buffer::Information& Buffer::GetBufferInformation() const
{
	return m_bufferInformation;
}

VkDeviceAddress Buffer::GetDeviceAddress() const
{
	VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	auto& device = MyDevice::GetInstance();
	
	info.pNext = nullptr;
	info.buffer = m_vkBuffer;

	CHECK_TRUE(m_vkBuffer != VK_NULL_HANDLE);
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT), 
		"Maybe VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT need to be added!");

	return device.GetBufferDeviceAddress(info);
}

VkDescriptorBufferInfo Buffer::GetDescriptorBufferInfo() const
{
	VkDescriptorBufferInfo result{};

	result.buffer = m_vkBuffer;
	result.offset = 0;
	result.range = m_bufferInformation.size;

	return result;
}

VkBuffer Buffer::GetVkBuffer() const
{
	return m_vkBuffer;
}

BufferView Buffer::NewBufferView(VkFormat _format)
{
	CHECK_TRUE((m_bufferInformation.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) || (m_bufferInformation.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT), "This buffer cannot have buffer views!");
	BufferView bufferview{};
	bufferview.m_format = _format;
	bufferview.pBuffer = this;
	return bufferview;
}

Buffer::Initializer& Buffer::Initializer::Reset()
{
	*this = Buffer::Initializer{};
	return *this;
}

Buffer::Initializer& Buffer::Initializer::SetBufferSize(VkDeviceSize inSize)
{
	m_bufferSize = inSize;
	return *this;
}

Buffer::Initializer& Buffer::Initializer::SetBufferUsage(VkBufferUsageFlags inUsage)
{
	m_usage = inUsage;
	return *this;
}

Buffer::Initializer& Buffer::Initializer::CustomizeSharingMode(VkSharingMode inSharingMode)
{
	m_sharingMode = inSharingMode;
	return *this;
}

Buffer::Initializer& Buffer::Initializer::CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties)
{
	m_memoryProperty = inMemoryProperties;
	return *this;
}

Buffer::Initializer& Buffer::Initializer::CustomizeAlignment(VkDeviceSize inAlignment)
{
	m_optAlignment = inAlignment;
	return *this;
}

BufferView::Initializer& BufferView::Initializer::Reset()
{
	*this = BufferView::Initializer{};

	return *this;
}

BufferView::Initializer& BufferView::Initializer::SetFormat(VkFormat inFormat)
{
	m_format = inFormat;
	return *this;
}

BufferView::Initializer& BufferView::Initializer::SetBuffer(const Buffer* inBufferPtr)
{
	auto& bufferInfo = inBufferPtr->GetBufferInformation();

	CHECK_TRUE(
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) || 
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT), 
		"This buffer cannot have buffer views!");

	m_buffer = inBufferPtr->GetVkBuffer();
	m_offset = 0;
	m_range = bufferInfo.size;

	return *this;
}

BufferView::~BufferView()
{
	CHECK_TRUE(m_vkBufferView == VK_NULL_HANDLE);
}

void BufferView::Init(const BufferView::Initializer* inInitPtr)
{
	CHECK_TRUE(m_vkBufferView == VK_NULL_HANDLE, "VkBufferView is already created!");
	auto& device = MyDevice::GetInstance();
	VkBufferViewCreateInfo createInfo{ VkStructureType::VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };

	m_format = inInitPtr->m_format;
	createInfo.buffer = inInitPtr->m_buffer;
	createInfo.format = m_format;
	createInfo.offset = inInitPtr->m_offset;
	createInfo.range = inInitPtr->m_range;
	
	m_vkBufferView = device.CreateBufferView(createInfo);
}

void BufferView::Uninit()
{
	if (m_vkBufferView != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		device.DestroyBufferView(m_vkBufferView);
		m_vkBufferView = VK_NULL_HANDLE;
	}
	m_format = VK_FORMAT_UNDEFINED;
}