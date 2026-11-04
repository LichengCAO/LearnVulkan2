#pragma once
#include "common.h"
#include "resource/image.h"
#include "resource/vulkan_handle_resource.h"
#include "render_pass_create_info.h"

class ImageView;

namespace VulkanDetail
{
	struct RenderPassSnapshot
	{
		IntrusivePtr<RenderPassResource> resource;
		std::shared_ptr<const RenderPassDescription> description;
		auto GetVkRenderPass() const -> VkRenderPass { return resource ? resource->GetHandle() : VK_NULL_HANDLE; }
		auto GetSubpassCount() const -> uint32_t { return description ? static_cast<uint32_t>(description->subpassInfos.size()) : 0; }
		auto GetClearValues() const -> const std::vector<VkClearValue>&;
		auto IsCompatibleWith(const RenderPassSnapshot& inOther) const -> bool;
	};
}

class FramebufferInfo final
{
	friend class RenderPass;
private:
	std::unordered_map<std::string, ImageView> m_imageViews;
	std::optional<VkExtent2D> m_extent;
	uint32_t m_layers = 1;
public:
	void Reset();
	void SetImageView(std::string_view inTargetAttachmentName, const ImageView& inViewAttached);
	void CustomizeSize(uint32_t inWidth, uint32_t inHeight, uint32_t inLayers = 1);
};

class Framebuffer final
{
	friend class RenderPass;
	friend class CommandBuffer;
	friend class RenderGraphInstance;
	friend struct CommandBufferTestProbe;
	friend struct ResourceLifetimeTestProbe;
private:
	IntrusivePtr<VulkanDetail::FramebufferResource> m_resource;
	VulkanDetail::RenderPassSnapshot m_renderPass;
	VkExtent2D m_extent{};
	uint32_t m_layers = 0;
	auto _GetResourceReference() const -> VulkanDetail::ResourceReference { return m_resource; }
	auto _GetRenderPassSnapshot() const -> VulkanDetail::RenderPassSnapshot { return m_renderPass; }
public:
	Framebuffer() noexcept = default;
	Framebuffer(const Framebuffer&) noexcept = default;
	Framebuffer(Framebuffer&&) noexcept = default;
	auto operator=(const Framebuffer&) noexcept -> Framebuffer& = default;
	auto operator=(Framebuffer&&) noexcept -> Framebuffer& = default;
	~Framebuffer() = default;
	void Reset() noexcept;
	auto IsValid() const noexcept -> bool { return m_resource && m_resource->GetHandle() != VK_NULL_HANDLE; }
	auto operator==(const Framebuffer& inOther) const noexcept -> bool { return m_resource.get() == inOther.m_resource.get(); }
	auto GetSize() const noexcept -> VkExtent2D { return m_extent; }
	auto GetLayerCount() const noexcept -> uint32_t { return m_layers; }
	auto GetVkFramebuffer() const noexcept -> VkFramebuffer { return m_resource ? m_resource->GetHandle() : VK_NULL_HANDLE; }
};

class RenderPass final
{
	friend struct CommandBufferTestProbe;
	friend struct ResourceLifetimeTestProbe;
	friend class CommandBuffer;
	friend class RenderGraphInstance;
	friend class GraphicsShaderProgram;
	friend class GraphicsPipelineStateInfo;
private:
	struct CachedFramebuffer
	{
		std::vector<ImageView> views;
		Framebuffer framebuffer;
	};
	VulkanDetail::RenderPassSnapshot m_snapshot;
	mutable std::vector<CachedFramebuffer> m_framebuffers;
	auto _GetResourceReference() const -> VulkanDetail::ResourceReference { return m_snapshot.resource; }
	auto _GetSnapshot() const -> VulkanDetail::RenderPassSnapshot { return m_snapshot; }

public:
	RenderPass() = default;
	RenderPass(const RenderPass&) = delete;
	RenderPass(RenderPass&&) = delete;
	auto operator=(const RenderPass&) -> RenderPass& = delete;
	auto operator=(RenderPass&&) -> RenderPass& = delete;
	~RenderPass();

	void Create(const RenderPassCreateInfo& inCreateInfo);
	void Destroy();
	auto IsValid() const noexcept -> bool { return GetVkRenderPass() != VK_NULL_HANDLE; }
	auto GetVkRenderPass() const noexcept -> VkRenderPass { return m_snapshot.GetVkRenderPass(); }
	auto GetSubpassCount() const noexcept -> uint32_t { return m_snapshot.GetSubpassCount(); }
	auto GetClearValues() const -> const std::vector<VkClearValue>& { return m_snapshot.GetClearValues(); }
	auto GetAttachmentIndex(std::string_view inName) const -> uint32_t;
	auto GetFramebuffer(const FramebufferInfo& inInfo) const -> Framebuffer;
	void ClearFramebufferCache();
};
