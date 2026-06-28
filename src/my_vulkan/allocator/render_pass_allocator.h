#pragma once
#include <common.h>

class RenderPassAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkRenderPass> m_mapHashToVkRenderPass;

public:
	RenderPassAllocator() = default;
	RenderPassAllocator(const RenderPassAllocator&) = delete;
	RenderPassAllocator& operator=(const RenderPassAllocator&) = delete;
	virtual ~RenderPassAllocator();

	void Create(VkDevice inDevice);
	auto AllocateRenderPassWithResult(const VkRenderPassCreateInfo* inCreateInfo) -> std::pair<VkRenderPass, VkResult>;
	auto AllocateRenderPass(const VkRenderPassCreateInfo* inCreateInfo) -> VkRenderPass;
	void FreeRenderPass(VkRenderPass& inoutRenderPass);
	void Destroy();
};
