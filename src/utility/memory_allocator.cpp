#include "memory_allocator.h"
#include "device.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "utils.h"

VmaMemoryUsage MemoryAllocator::_ToVmaMemoryUsage(VkMemoryPropertyFlags _propertyFlags)
{
	// from nvidia ray tracing tutorial
	if ((_propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
		return VMA_MEMORY_USAGE_GPU_ONLY;
	else if ((_propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		return VMA_MEMORY_USAGE_CPU_ONLY;
	else if ((_propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		return VMA_MEMORY_USAGE_CPU_TO_GPU;
	return VMA_MEMORY_USAGE_UNKNOWN;
}

VkDevice MemoryAllocator::_GetVkDevice() const
{
	return MyDevice::GetInstance().vkDevice;
}

VmaAllocator* MemoryAllocator::_GetPtrVmaAllocator()
{
	return m_uptrVmaAllocator.get();
}

MemoryAllocator::MemoryAllocator()
{
	// https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
	VmaAllocatorCreateInfo createInfo{};
	MyDevice& device = MyDevice::GetInstance();
	VmaVulkanFunctions volkFuncs{};
	
	m_uptrVmaAllocator = std::make_unique<VmaAllocator>();

	volkFuncs.vkAllocateMemory = vkAllocateMemory;
	volkFuncs.vkBindBufferMemory = vkBindBufferMemory;
	volkFuncs.vkBindImageMemory = vkBindImageMemory;
	volkFuncs.vkCreateBuffer = vkCreateBuffer;
	volkFuncs.vkCreateImage = vkCreateImage;
	volkFuncs.vkDestroyBuffer = vkDestroyBuffer;
	volkFuncs.vkDestroyImage = vkDestroyImage;
	volkFuncs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	volkFuncs.vkFreeMemory = vkFreeMemory;
	volkFuncs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
	volkFuncs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
	volkFuncs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	volkFuncs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
	volkFuncs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
	volkFuncs.vkMapMemory = vkMapMemory;
	volkFuncs.vkUnmapMemory = vkUnmapMemory;
	volkFuncs.vkCmdCopyBuffer = vkCmdCopyBuffer;
	volkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	volkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	//"VmaVulkanFunctions::vkGetInstanceProcAddr and vkGetDeviceProcAddr as VmaAllocatorCreateInfo::pVulkanFunctions

	createInfo.device = device.vkDevice;
	createInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/enabling_buffer_device_address.html
	createInfo.instance = device.vkInstance;
	createInfo.physicalDevice = device.vkPhysicalDevice;
	createInfo.pVulkanFunctions = &volkFuncs;

	vmaCreateAllocator(&createInfo, m_uptrVmaAllocator.get());
}

MemoryAllocator::~MemoryAllocator()
{
	if (m_uptrVmaAllocator.get() != nullptr)
	{
		vmaDestroyAllocator(*m_uptrVmaAllocator);
		m_uptrVmaAllocator.reset();
	}
}

void MemoryAllocator::AllocateForVkImage(VkImage _vkImage, VkMemoryPropertyFlags _flags)
{
	VmaAllocationCreateInfo allocCreateInfo{};
	VmaAllocation allocResult = nullptr;
	VmaAllocationInfo allocInfo{};

	allocCreateInfo.usage = _ToVmaMemoryUsage(_flags);

	VK_CHECK(vmaAllocateMemoryForImage(*_GetPtrVmaAllocator(), _vkImage, &allocCreateInfo, &allocResult, &allocInfo), "Failed to allocate memory!");

	vmaBindImageMemory(*_GetPtrVmaAllocator(), allocResult, _vkImage);
	m_mapVkImageToAllocation.insert({ _vkImage, allocResult });
}

void MemoryAllocator::AllocateForVkBuffer(VkBuffer _vkBuffer, VkMemoryPropertyFlags _flags, VkDeviceSize _alignment)
{
	VmaAllocationCreateInfo allocCreateInfo{};
	VmaAllocation allocResult = nullptr;
	VmaAllocationInfo allocInfo{};

	allocCreateInfo.usage = _ToVmaMemoryUsage(_flags);

	if (_alignment > 0)
	{
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(_GetVkDevice(), _vkBuffer, &req);
		req.alignment = std::max(common_utils::AlignUp(req.alignment, _alignment), _alignment);
		VK_CHECK(vmaAllocateMemory(*_GetPtrVmaAllocator(), &req, &allocCreateInfo, &allocResult, &allocInfo), "Failed to allocate memory!");
	}
	else
	{
		VK_CHECK(vmaAllocateMemoryForBuffer(*_GetPtrVmaAllocator(), _vkBuffer, &allocCreateInfo, &allocResult, &allocInfo), "Failed to allocate memory!");
	}

	vmaBindBufferMemory(*_GetPtrVmaAllocator(), allocResult, _vkBuffer);
	m_mapVkBufferToAllocation.insert({ _vkBuffer, allocResult });
}

void MemoryAllocator::MapVkBufferToHost(VkBuffer _vkBuffer, void*& _outHostAddress)
{
	vmaMapMemory(*_GetPtrVmaAllocator(), m_mapVkBufferToAllocation.at(_vkBuffer), &_outHostAddress);
}

void MemoryAllocator::UnmapVkBuffer(VkBuffer _vkBuffer)
{
	vmaUnmapMemory(*_GetPtrVmaAllocator(), m_mapVkBufferToAllocation.at(_vkBuffer));
}

void MemoryAllocator::FreeMemory(VkImage _vkImage)
{
	CHECK_TRUE(m_mapVkImageToAllocation.find(_vkImage) != m_mapVkImageToAllocation.end(), "The image isn't allocate by this allocator!");
	vmaFreeMemory(*_GetPtrVmaAllocator(), m_mapVkImageToAllocation.at(_vkImage));
	m_mapVkImageToAllocation.erase(_vkImage);
}

void MemoryAllocator::FreeMemory(VkBuffer _vkBuffer)
{
	CHECK_TRUE(m_mapVkBufferToAllocation.find(_vkBuffer) != m_mapVkBufferToAllocation.end(), "The buffer isn't allocate by this allocator!");
	vmaFreeMemory(*_GetPtrVmaAllocator(), m_mapVkBufferToAllocation.at(_vkBuffer));
	m_mapVkBufferToAllocation.erase(_vkBuffer);
}
