#pragma once
#include <common.h>
#include "resource/private/vulkan_handle_resource.h"

class RenderPassAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
		struct KeyHash
		{
			auto operator()(const std::vector<uint64_t>& inKey) const -> size_t;
		};
		std::unordered_map<std::vector<uint64_t>, IntrusivePtr<VulkanDetail::RenderPassResource>, KeyHash> m_renderPasses;
		friend struct ResourceLifetimeTestProbe;

public:
	RenderPassAllocator() = default;
	RenderPassAllocator(const RenderPassAllocator&) = delete;
		auto operator=(const RenderPassAllocator&) -> RenderPassAllocator& = delete;
	virtual ~RenderPassAllocator();

	void Create(VkDevice inDevice);
		auto AllocateRenderPassWithResult(const VkRenderPassCreateInfo& inCreateInfo) -> std::pair<IntrusivePtr<VulkanDetail::RenderPassResource>, VkResult>;
		auto AllocateRenderPass(const VkRenderPassCreateInfo& inCreateInfo) -> IntrusivePtr<VulkanDetail::RenderPassResource>;
	void Destroy();
};
