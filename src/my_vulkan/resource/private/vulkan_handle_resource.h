#pragma once

#include "common.h"
#include "my_vulkan/intrusive_ptr.h"
#include <functional>
#include <utility>
#include <vector>

namespace VulkanDetail
{
using ResourceReference = IntrusivePtr<RefCounted>;

// Dependencies describe native parent objects, never facade-owned caches.
template <typename Handle>
class VulkanHandleResource final : public RefCounted
{
private:
	Handle m_handle{};
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
};

using ImageResource = VulkanHandleResource<VkImage>;
using ImageViewResource = VulkanHandleResource<VkImageView>;
using RenderPassResource = VulkanHandleResource<VkRenderPass>;
using FramebufferResource = VulkanHandleResource<VkFramebuffer>;
using SwapchainResource = VulkanHandleResource<VkSwapchainKHR>;
}
