#pragma once
#include "../buffer.h"
#include "vulkan_handle_resource.h"

namespace VulkanDetail
{
struct AccelerationStructureStorage final
{
    VkDevice device = VK_NULL_HANDLE;
    const uint64_t resourceId = _AllocateResourceId();
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> buffer;

    ~AccelerationStructureStorage() noexcept
    {
        if (handle != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device, handle, nullptr);
    }
};
}
