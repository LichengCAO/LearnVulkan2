#pragma once
#include <common.h>

namespace VulkanDetail
{
struct ResolvedFramebufferDesc;
struct RenderPassSnapshot;

// The owning Device serializes every access, including teardown. Entries own
// exactly one native creation and hold only borrowed attachment identities.
class RenderTargetCache final
{
private:
	struct KeyHash
	{
		auto operator()(const std::vector<uint64_t>& inKey) const -> size_t;
	};
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<std::vector<uint64_t>, VkRenderPass, KeyHash> m_renderPasses;
	std::unordered_map<std::vector<uint64_t>, VkFramebuffer, KeyHash> m_framebuffers;

public:
	RenderTargetCache() = default;
	RenderTargetCache(const RenderTargetCache&) = delete;
	RenderTargetCache(RenderTargetCache&&) = delete;
	auto operator=(const RenderTargetCache&) -> RenderTargetCache& = delete;
	auto operator=(RenderTargetCache&&) -> RenderTargetCache& = delete;
	~RenderTargetCache() noexcept;

	void Create(VkDevice inDevice);
	auto IsCreated() const noexcept -> bool { return m_vkDevice != VK_NULL_HANDLE; }
	auto GetOrCreateRenderPass(const VkRenderPassCreateInfo& inCreateInfo) -> VkRenderPass;
	auto GetOrCreateRenderPass(const RenderPassSnapshot& inSnapshot) -> VkRenderPass;
	auto GetOrCreateFramebuffer(VkRenderPass inRenderPass,
		const ResolvedFramebufferDesc& inDescription) -> VkFramebuffer;
	void Destroy() noexcept;
};
}
