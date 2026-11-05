#pragma once

#include "common.h"
#include "my_vulkan/intrusive_ptr.h"
#include <functional>
#include <utility>
#include <vector>
#include <atomic>
#include <cstdint>
#include <limits>

namespace VulkanDetail
{
using ResourceReference = IntrusivePtr<RefCounted>;

inline auto _AllocateResourceId() -> uint64_t
{
	static std::atomic<uint64_t> lastAssigned{ 0 };
	auto current = lastAssigned.load(std::memory_order_relaxed);
	for (;;)
	{
		CHECK_TRUE(current != std::numeric_limits<uint64_t>::max(), "Vulkan resource identity space is exhausted!");
		if (lastAssigned.compare_exchange_weak(current, current + 1,
			std::memory_order_relaxed, std::memory_order_relaxed)) return current + 1;
	}
}

// Dependencies describe native parent objects, never facade-owned caches.
template <typename Handle>
class VulkanHandleResource final : public RefCounted
{
private:
	Handle m_handle{};
	const uint64_t m_resourceId = _AllocateResourceId();
	std::function<void(Handle)> m_destroy;
	std::vector<ResourceReference> m_dependencies;

public:
	VulkanHandleResource(Handle inHandle, std::function<void(Handle)> inDestroy,
		std::vector<ResourceReference> inDependencies = {})
		: m_handle(inHandle), m_destroy(std::move(inDestroy)), m_dependencies(std::move(inDependencies))
	{
	}

	VulkanHandleResource(const VulkanHandleResource&) = delete;
	VulkanHandleResource(VulkanHandleResource&&) = delete;
	auto operator=(const VulkanHandleResource&) -> VulkanHandleResource& = delete;
	auto operator=(VulkanHandleResource&&) -> VulkanHandleResource& = delete;

	~VulkanHandleResource() noexcept override
	{
		// The parent's native handle remains valid throughout child destruction.
		if (m_handle != Handle{} && m_destroy)
		{
			m_destroy(m_handle);
		}
	}

	auto GetHandle() const noexcept -> Handle { return m_handle; }
	auto GetResourceId() const noexcept -> uint64_t { return m_resourceId; }
};

using ImageViewResource = VulkanHandleResource<VkImageView>;
using SwapchainResource = VulkanHandleResource<VkSwapchainKHR>;
struct ImageStorage;
class ImageResource final : public RefCounted
{
    std::unique_ptr<ImageStorage> m_storage;
    const uint64_t m_resourceId = _AllocateResourceId();
public:
    explicit ImageResource(std::unique_ptr<ImageStorage> storage);
    ~ImageResource() noexcept override;
    ImageResource(const ImageResource&) = delete;
    ImageResource(ImageResource&&) = delete;
    auto operator=(const ImageResource&) -> ImageResource& = delete;
    auto operator=(ImageResource&&) -> ImageResource& = delete;
    auto GetHandle() const noexcept -> VkImage;
    auto GetDevice() const noexcept -> VkDevice;
    auto GetResourceId() const noexcept -> uint64_t { return m_resourceId; }
};
}
