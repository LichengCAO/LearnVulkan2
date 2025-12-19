#include "buffer.h"
#include "device.h"
#include "commandbuffer.h"
#include "memory_allocator.h"
#include "utils.h"

//uint32_t Buffer::_FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const
//{
//	VkPhysicalDeviceMemoryProperties memProperties;
//	vkGetPhysicalDeviceMemoryProperties(MyDevice::GetInstance().vkPhysicalDevice, &memProperties);
//	for (uint32_t res = 0; res < memProperties.memoryTypeCount; ++res) {
//		bool suitableType = typeBits & (1 << res);
//		bool suitableMemoryProperty = (memProperties.memoryTypes[res].propertyFlags & properties) == properties;
//		if (suitableType && suitableMemoryProperty) {
//			return res;
//		}
//	}
//	throw std::runtime_error("Failed to find suitable memory type!");
//	return ~0;
//}

Buffer::~Buffer()
{
	assert(vkBuffer == VK_NULL_HANDLE);
	assert(m_mappedMemory == nullptr);
}

void Buffer::Init()
{
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = m_bufferInformation.size;
	bufferInfo.usage = m_bufferInformation.usage;
	bufferInfo.sharingMode = m_bufferInformation.sharingMode;
	CHECK_TRUE(vkBuffer == VK_NULL_HANDLE, "VkBuffer is already created!");
	VK_CHECK(vkCreateBuffer(MyDevice::GetInstance().vkDevice, &bufferInfo, nullptr, &vkBuffer), "Failed to create buffer!");
	_AllocateMemory();
}

void Buffer::PresetCreateInformation(const CreateInformation& _info)
{
	m_bufferInformation.size = _info.size;
	m_bufferInformation.usage = _info.usage;
	m_bufferInformation.sharingMode = _info.optSharingMode.has_value() ? _info.optSharingMode.value() : VK_SHARING_MODE_EXCLUSIVE;
	m_bufferInformation.memoryProperty = _info.optMemoryProperty.has_value() ? _info.optMemoryProperty.value() : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	m_bufferInformation.optAlignment = _info.optAlignment;
}

void Buffer::Uninit()
{
	if (vkBuffer != VK_NULL_HANDLE)
	{
		_FreeMemory();
		vkDestroyBuffer(MyDevice::GetInstance().vkDevice, vkBuffer, nullptr);
		vkBuffer = VK_NULL_HANDLE;
	}
}

MemoryAllocator* Buffer::_GetMemoryAllocator() const
{
	return MyDevice::GetInstance().GetMemoryAllocator();
}

void Buffer::_AllocateMemory()
{
	//CHECK_TRUE(vkBuffer != VK_NULL_HANDLE, "Try to allocate for a uninitialized buffer!");
	//VkMemoryRequirements memRequirements{};
	//VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	//VkMemoryAllocateFlagsInfo allocFlags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	//
	//vkGetBufferMemoryRequirements(MyDevice::GetInstance().vkDevice, vkBuffer, &memRequirements);
	//allocInfo.allocationSize = memRequirements.size; //could be different from size on Host!
	//allocInfo.memoryTypeIndex = _FindMemoryTypeIndex(memRequirements.memoryTypeBits, m_bufferInformation.memoryProperty);
	//if ((m_bufferInformation.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u)
	//{
	//	allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	//	allocInfo.pNext = &allocFlags;
	//}
	//VK_CHECK(vkAllocateMemory(MyDevice::GetInstance().vkDevice, &allocInfo, nullptr, &vkDeviceMemory), "Failed to allocate buffer memory!");
	//VkDeviceSize offset = 0; // should be divisible by memRequirements.alignment
	//vkBindBufferMemory(MyDevice::GetInstance().vkDevice, vkBuffer, vkDeviceMemory, offset);
	
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_bufferInformation.optAlignment.has_value())
	{
		pAllocator->AllocateForVkBuffer(vkBuffer, m_bufferInformation.memoryProperty, m_bufferInformation.optAlignment.value());
	}
	else
	{
		pAllocator->AllocateForVkBuffer(vkBuffer, m_bufferInformation.memoryProperty);
	}
}

void Buffer::_FreeMemory()
{
	//if (vkDeviceMemory != VK_NULL_HANDLE)
	//{
	//	if (m_mappedMemory != nullptr)
	//	{
	//		vkUnmapMemory(MyDevice::GetInstance().vkDevice, vkDeviceMemory);
	//		m_mappedMemory = nullptr;
	//	}
	//	vkFreeMemory(MyDevice::GetInstance().vkDevice, vkDeviceMemory, nullptr);
	//	vkDeviceMemory = VK_NULL_HANDLE;
	//}

	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_mappedMemory != nullptr)
	{
		pAllocator->UnmapVkBuffer(vkBuffer);
		m_mappedMemory = nullptr;
	}
	pAllocator->FreeMemory(vkBuffer);
}

void Buffer::_MapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->MapVkBufferToHost(vkBuffer, m_mappedMemory);
}

void Buffer::_UnmapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->UnmapVkBuffer(vkBuffer);
}

void Buffer::_CopyFromHostWithMappedMemory(const void* src, size_t bufferOffest, size_t size)
{
	if (m_mappedMemory == nullptr)
	{
		MemoryAllocator* pAllocator = _GetMemoryAllocator();
		pAllocator->MapVkBufferToHost(vkBuffer, m_mappedMemory);
	}
	uint8_t* pMapped = (uint8_t*)m_mappedMemory;
	pMapped += bufferOffest;
	memcpy((void*)pMapped, src, size);
}

void Buffer::_CopyFromHostWithStaggingBuffer(const void* src, size_t bufferOffest, size_t size)
{
	CreateInformation stagBufInfo{};
	Buffer stagBuf{};

	stagBufInfo.optMemoryProperty = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	stagBufInfo.size = static_cast<VkDeviceSize>(size);
	stagBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagBuf.PresetCreateInformation(stagBufInfo);
	stagBuf.Init();
	stagBuf.CopyFromHost(src);

	CopyFromBuffer(&stagBuf, 0, bufferOffest, size);

	stagBuf.Uninit();
}

Buffer::Buffer()
{
}

Buffer::Buffer(Buffer&& _toMove)
{
	vkBuffer = _toMove.vkBuffer;
	m_mappedMemory = _toMove.m_mappedMemory;
	m_bufferInformation = _toMove.m_bufferInformation;
	_toMove.vkBuffer = VK_NULL_HANDLE;
	_toMove.m_mappedMemory = nullptr;
}

void Buffer::CopyFromHost(const void* src)
{
	CopyFromHost(src, 0, static_cast<size_t>(m_bufferInformation.size));
}

void Buffer::CopyFromHost(const void* src, size_t bufferOffset, size_t size)
{
	CHECK_TRUE(size <= static_cast<size_t>(m_bufferInformation.size), "Try to copy too much data from host!");
	if ((m_bufferInformation.memoryProperty & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	{
		_CopyFromHostWithMappedMemory(src, bufferOffset, size);
	}
	else
	{
		_CopyFromHostWithStaggingBuffer(src, bufferOffset, size);
	}
}

void Buffer::CopyFromBuffer(const Buffer& otherBuffer)
{
	CopyFromBuffer(&otherBuffer, 0, 0, m_bufferInformation.size);
}

void Buffer::CopyFromBuffer(const Buffer* pOtherBuffer)
{
	CopyFromBuffer(pOtherBuffer, 0, 0, m_bufferInformation.size);
}

void Buffer::CopyFromBuffer(const Buffer* pOtherBuffer, size_t srcOffset, size_t dstOffset, size_t size, CommandSubmission* pCmd)
{
	VkBufferCopy copyInfo{};

	CHECK_TRUE(vkBuffer != VK_NULL_HANDLE, "This buffer is not initialized!");
	CHECK_TRUE(((m_bufferInformation.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0), "This buffer must have VK_BUFFER_USAGE_TRANSFER_DST_BIT to copy from other buffer!");

	copyInfo.size = size;
	copyInfo.dstOffset = dstOffset;
	copyInfo.srcOffset = srcOffset;

	if (pCmd)
	{
		pCmd->CopyBuffer(pOtherBuffer->vkBuffer, vkBuffer, { copyInfo });
	}
	else
	{
		CommandSubmission cmdSubmit{};
		cmdSubmit.Init();
		cmdSubmit.StartOneTimeCommands({});
		cmdSubmit.CopyBuffer(pOtherBuffer->vkBuffer, vkBuffer, { copyInfo });
		cmdSubmit.SubmitCommands();
		cmdSubmit.Uninit();
	}
}

void Buffer::Fill(uint32_t _data, CommandSubmission* pCmd)
{
	if (pCmd != nullptr)
	{
		pCmd->FillBuffer(vkBuffer, 0u, m_bufferInformation.size, _data);
	}
	else
	{
		CommandSubmission cmdSubmit{};
		cmdSubmit.Init();
		cmdSubmit.StartOneTimeCommands({});
		cmdSubmit.FillBuffer(vkBuffer, 0u, m_bufferInformation.size, _data);
		cmdSubmit.SubmitCommands();
		cmdSubmit.Uninit();
	}
}

const Buffer::Information& Buffer::GetBufferInformation() const
{
	return m_bufferInformation;
}

VkDeviceAddress Buffer::GetDeviceAddress() const
{
	VkDeviceAddress vkDeviceAddr = 0;
	VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	
	assert(vkBuffer != VK_NULL_HANDLE);
	info.pNext = nullptr;
	info.buffer = vkBuffer;

	CHECK_TRUE(((m_bufferInformation.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0), "Maybe VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT need to be added!");

	vkDeviceAddr = vkGetBufferDeviceAddress(MyDevice::GetInstance().vkDevice, &info);

	return vkDeviceAddr;
}

VkDescriptorBufferInfo Buffer::GetDescriptorInfo() const
{
	VkDescriptorBufferInfo info{};
	info.buffer = vkBuffer;
	info.offset = 0;
	info.range = m_bufferInformation.size;
	return info;
}

BufferView Buffer::NewBufferView(VkFormat _format)
{
	CHECK_TRUE((m_bufferInformation.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) || (m_bufferInformation.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT), "This buffer cannot have buffer views!");
	BufferView bufferview{};
	bufferview.m_format = _format;
	bufferview.pBuffer = this;
	return bufferview;
}

BufferView::~BufferView()
{
	assert(vkBufferView == VK_NULL_HANDLE);
}

void BufferView::Init()
{
	VkBufferViewCreateInfo createInfo{ VkStructureType::VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
	createInfo.buffer = pBuffer->vkBuffer;
	createInfo.format = m_format;
	createInfo.offset = 0;
	createInfo.range = pBuffer->GetBufferInformation().size;
	CHECK_TRUE(vkBufferView == VK_NULL_HANDLE, "VkBufferView is already created!");
	VK_CHECK(vkCreateBufferView(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &vkBufferView), "Failed to create buffer view!");
}

void BufferView::Uninit()
{
	if (vkBufferView != VK_NULL_HANDLE)
	{
		vkDestroyBufferView(MyDevice::GetInstance().vkDevice, vkBufferView, nullptr);
		vkBufferView = VK_NULL_HANDLE;
	}
	pBuffer = nullptr;
}
