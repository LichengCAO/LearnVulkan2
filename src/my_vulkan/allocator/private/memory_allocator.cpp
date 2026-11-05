#include "memory_allocator.h"
#include "device.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "utils.h"

auto MemoryAllocator::_ToVmaMemoryUsage(VkMemoryPropertyFlags _propertyFlags) -> VmaMemoryUsage
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

MemoryAllocator::MemoryAllocator()
{
	// https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
	VmaAllocatorCreateInfo createInfo{};
	MyDevice& device = MyDevice::GetInstance();
	VmaVulkanFunctions volkFuncs{};
	
	CHECK_TRUE(device.vkDevice != VK_NULL_HANDLE && device.vkInstance != VK_NULL_HANDLE &&
		device.vkPhysicalDevice != VK_NULL_HANDLE, "MemoryAllocator requires an initialized Device!");

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

	VmaAllocator allocator = nullptr;
	VK_CHECK(vmaCreateAllocator(&createInfo, &allocator), "Failed to create memory allocator!");
	m_vmaAllocator = allocator;
	m_device = device.vkDevice;
}

MemoryAllocator::~MemoryAllocator()
{
	if (m_vmaAllocator != nullptr)
		vmaDestroyAllocator(m_vmaAllocator);
}

auto MemoryAllocator::AllocateForVkImage(VkImage image, VkMemoryPropertyFlags flags) -> VulkanDetail::MemoryAllocation
{
    CHECK_TRUE(IsValid() && image != VK_NULL_HANDLE, "Invalid image allocation dependencies!");
    VmaAllocationCreateInfo info{}; info.usage = _ToVmaMemoryUsage(flags);
    VmaAllocation native = nullptr;
    VK_CHECK(vmaAllocateMemoryForImage(m_vmaAllocator, image, &info, &native, nullptr), "Failed to allocate image memory!");
    VulkanDetail::MemoryAllocation allocation(m_vmaAllocator, native);
    VK_CHECK(vmaBindImageMemory(m_vmaAllocator, native, image), "Failed to bind image memory!");
    return allocation;
}

auto MemoryAllocator::AllocateForVkBuffer(VkBuffer _vkBuffer, VkMemoryPropertyFlags _flags, VkDeviceSize _alignment) -> VulkanDetail::MemoryAllocation
{
	CHECK_TRUE(IsValid() && _vkBuffer != VK_NULL_HANDLE, "Invalid buffer allocation dependencies!");
	VmaAllocationCreateInfo allocCreateInfo{};
	VmaAllocation allocResult = nullptr;
	VmaAllocationInfo allocInfo{};

	allocCreateInfo.usage = _ToVmaMemoryUsage(_flags);

	if (_alignment > 0)
	{
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(m_device, _vkBuffer, &req);
		req.alignment = std::max(common_utils::AlignUp(req.alignment, _alignment), _alignment);
		VK_CHECK(vmaAllocateMemory(m_vmaAllocator, &req, &allocCreateInfo, &allocResult, &allocInfo), "Failed to allocate memory!");
	}
	else
	{
		VK_CHECK(vmaAllocateMemoryForBuffer(m_vmaAllocator, _vkBuffer, &allocCreateInfo, &allocResult, &allocInfo), "Failed to allocate memory!");
	}

	VulkanDetail::MemoryAllocation allocation(m_vmaAllocator, allocResult);
	VK_CHECK(vmaBindBufferMemory(m_vmaAllocator, allocResult, _vkBuffer), "Failed to bind buffer memory!");
	return allocation;
}


namespace VulkanDetail
{
MemoryAllocation::MemoryAllocation(VmaAllocator allocator, VmaAllocation allocation) noexcept
    : m_allocator(allocator), m_allocation(allocation) {}
MemoryAllocation::MemoryAllocation(MemoryAllocation&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_allocation(std::exchange(other.m_allocation, nullptr)),
      m_mappedMemory(std::exchange(other.m_mappedMemory, nullptr)) {}
auto MemoryAllocation::operator=(MemoryAllocation&& other) noexcept -> MemoryAllocation&
{
    if (this != &other)
    {
        Unmap();
        if (m_allocation) vmaFreeMemory(m_allocator, m_allocation);
        m_allocator = std::exchange(other.m_allocator, nullptr);
        m_allocation = std::exchange(other.m_allocation, nullptr);
        m_mappedMemory = std::exchange(other.m_mappedMemory, nullptr);
    }
    return *this;
}
MemoryAllocation::~MemoryAllocation() noexcept
{
    Unmap();
    if (m_allocation) vmaFreeMemory(m_allocator, m_allocation);
}
auto MemoryAllocation::Map() -> void*
{
    if (!m_mappedMemory)
    {
        CHECK_TRUE(IsValid(), "Cannot map an empty allocation!");
        void* mapped = nullptr;
        VK_CHECK(vmaMapMemory(m_allocator, m_allocation, &mapped), "Failed to map buffer memory!");
        m_mappedMemory = mapped;
    }
    return m_mappedMemory;
}
void MemoryAllocation::Unmap() noexcept
{
    if (m_mappedMemory && m_allocation) vmaUnmapMemory(m_allocator, m_allocation);
    m_mappedMemory = nullptr;
}
}
