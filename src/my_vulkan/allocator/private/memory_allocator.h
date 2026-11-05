#pragma once
#include "common.h"
#pragma push_macro("max")
#include "vk_mem_alloc.h"
#pragma pop_macro("max")

class MyDevice;
class MemoryAllocator;
struct CommandBufferTestProbe;
struct ShaderBindingTestProbe;

namespace VulkanDetail
{
// Owns one VMA allocation and borrows its allocator. Independent records may be
// used concurrently; one record and allocator teardown require caller exclusion.
class MemoryAllocation final
{
    friend class ::MemoryAllocator;
    friend struct ::CommandBufferTestProbe;
    friend struct ::ShaderBindingTestProbe;
    VmaAllocator m_allocator = nullptr;
    VmaAllocation m_allocation = nullptr;
    void* m_mappedMemory = nullptr;
    MemoryAllocation(VmaAllocator allocator, VmaAllocation allocation) noexcept;
public:
    MemoryAllocation() noexcept = default;
    MemoryAllocation(const MemoryAllocation&) = delete;
    auto operator=(const MemoryAllocation&) -> MemoryAllocation& = delete;
    MemoryAllocation(MemoryAllocation&& other) noexcept;
    auto operator=(MemoryAllocation&& other) noexcept -> MemoryAllocation&;
    ~MemoryAllocation() noexcept;
    auto Map() -> void*;
    void Unmap() noexcept;
    auto IsValid() const noexcept -> bool { return m_allocation != nullptr; }
};
}

class MemoryAllocator
{
    friend struct BufferAllocationTestProbe;
private:
	VmaAllocator m_vmaAllocator = nullptr;
	VkDevice m_device = VK_NULL_HANDLE;

private:
	static auto _ToVmaMemoryUsage(VkMemoryPropertyFlags _propertyFlags) -> VmaMemoryUsage;

	MemoryAllocator(); // I only want allocator to be created by MyDevice for now

public:
	MemoryAllocator(const MemoryAllocator& _other) = delete;

	virtual ~MemoryAllocator();

	// Allocate memory and then bind it to m_vkImage
	[[nodiscard]] auto AllocateForVkImage(VkImage _vkImage, VkMemoryPropertyFlags _flags) -> VulkanDetail::MemoryAllocation;

	// Allocate memory and then bind it to m_vkBuffer, 
	// the address of the buffer will align with _alignment if it's not 0
	// Completes synchronously; VMA provides internal allocation synchronization.
	// Keep the allocator alive and exclude its teardown through final record release.
	[[nodiscard]] auto AllocateForVkBuffer(VkBuffer _vkBuffer, VkMemoryPropertyFlags _flags, VkDeviceSize _alignment = 0) -> VulkanDetail::MemoryAllocation;
	auto IsValid() const noexcept -> bool { return m_vmaAllocator != nullptr; }



	friend class MyDevice;
};
