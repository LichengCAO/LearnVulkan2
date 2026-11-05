#pragma once
#include "../buffer.h"

namespace VulkanDetail
{
struct AccelerationStructureStorage final
{
    VkDevice device = VK_NULL_HANDLE;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> buffer;

    ~AccelerationStructureStorage() noexcept
    {
        if (handle != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device, handle, nullptr);
    }
};
}
