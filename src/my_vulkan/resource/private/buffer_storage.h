#pragma once
#include "allocator/private/memory_allocator.h"

namespace VulkanDetail
{
struct BufferStorage final
{
    BufferStorage() = default;
    BufferStorage(const BufferStorage&) = delete;
    auto operator=(const BufferStorage&) -> BufferStorage& = delete;
    BufferStorage(BufferStorage&&) = delete;
    auto operator=(BufferStorage&&) -> BufferStorage& = delete;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    MemoryAllocation allocation;
    ~BufferStorage() noexcept
    {
        allocation.Unmap();
        if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        // allocation is freed after the native buffer has been destroyed.
    }
};
}
