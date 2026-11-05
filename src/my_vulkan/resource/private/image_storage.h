#pragma once
#include "allocator/private/memory_allocator.h"
#include "vulkan_handle_resource.h"
namespace VulkanDetail
{
struct ImageStorage final
{
    ImageStorage() = default;
    ImageStorage(const ImageStorage&) = delete;
    ImageStorage(ImageStorage&&) = delete;
    auto operator=(const ImageStorage&) -> ImageStorage& = delete;
    auto operator=(ImageStorage&&) -> ImageStorage& = delete;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    bool ownsNative = false;
    MemoryAllocation allocation;
    IntrusivePtr<SwapchainResource> generation;
    ~ImageStorage() noexcept
    {
        if (ownsNative && image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
    }
};
}
