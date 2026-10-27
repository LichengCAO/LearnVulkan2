#pragma once
#include "common.h"
#include "resource/image.h"
#include "resource/vulkan_handle_resource.h"
#include <string_view>
#include <utility>
#include <variant>

class ImageView;

class SubpassDescription final
{
	friend class RenderPassCreateInfo;
	friend class RenderPass;

private:
	struct AttachmentReference final
	{
		std::string attachmentName;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool isUsed = false;
	};

	struct Dependency final
	{
		std::string srcSubpassName;
		bool isExternal = false;
		bool isSelf = false;
		VkPipelineStageFlags dstStage = 0;
		VkAccessFlags dstAccess = 0;
		VkDependencyFlags dependencyFlags = 0;
	};

	std::vector<AttachmentReference> m_colorAttachments;
	std::vector<AttachmentReference> m_resolveAttachments;
	std::optional<AttachmentReference> m_depthStencilAttachment;
	std::vector<std::string> m_preserveAttachments;
	std::vector<Dependency> m_dependencies;
	VkPipelineStageFlags m_availableStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkAccessFlags m_availableAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	// VkDependencyFlags m_dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	// Dependency flags belong to individual VkSubpassDependency edges, not to the whole subpass.

public:
	void AddDepthStencilAttachment(
		std::string_view inAttachmentName,
		VkImageLayout inFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	void AddResolvedAttachment(
		uint32_t inOutputSlot,
		std::string_view inMultiSampleAttachmentName,
		std::string_view in1SampleAttachmentName,
		VkImageLayout inMultiSampleLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VkImageLayout in1SampleLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	void AddColorAttachment(
		uint32_t inOutputSlot,
		std::string_view inColorAttachmentName,
		VkImageLayout inColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	void AddPreserveAttachment(std::string_view inAttachmentName);
	void CustomizeAvailableState(
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess);

	// Adds a dependency from inSrcSubpass to this subpass.
	// inStage/inAccess describe where data must be visible for this subpass.
	// Use CustomizeAvailableState on the source subpass to describe where the data is produced.
	// VK_DEPENDENCY_BY_REGION_BIT makes framebuffer-space dependencies local to overlapping framebuffer regions, which can benefit tile-based GPUs.
	void AddDependencyOnSubpass(
		std::string_view inSrcSubpassName,
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess,
		VkDependencyFlags inDependencyFlags = VK_DEPENDENCY_BY_REGION_BIT);
	void AddExternalDependency(
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess,
		VkDependencyFlags inDependencyFlags = 0);
	// Adds a self-dependency that permits vkCmdPipelineBarrier inside this subpass.
	// Any in-render-pass pipeline barrier must use stage/access scopes covered by this self-dependency.
	// The same stage/access masks are used for both source and destination scopes.
	// The default self-dependency is BY_REGION because framebuffer-space barriers inside a render pass must be framebuffer-local.
	void AllowLocalPipelineBarrier(
		VkPipelineStageFlags inStage =
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VkAccessFlags inAccess =
			VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
			VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VkDependencyFlags inDependencyFlags = VK_DEPENDENCY_BY_REGION_BIT);
};

class AttachmentDescription final
{
	friend class RenderPassCreateInfo;
	friend class RenderPass;

private:
	VkAttachmentDescription m_description{};
	VkClearValue m_clearValue{};

public:
	AttachmentDescription();

	void CustomizeFormat(VkFormat inFormat, std::variant<std::pair<float, uint32_t>, glm::vec4> inClearValue);
	void CustomizeFormat(VkFormat inFormat, const VkClearValue& inClearValue);
	void CustomizeSampleCount(VkSampleCountFlagBits inSampleCount);
	void CustomizeLoadOperation(VkAttachmentLoadOp inLoadOp);
	void CustomizeStoreOperation(VkAttachmentStoreOp inStoreOp);
	void CustomizeStencilStoreLoadOperation(VkAttachmentLoadOp inLoadOp, VkAttachmentStoreOp inStoreOp);
	void CustomizeInitialLayout(VkImageLayout inLayout);
	void CustomizeFinalLayout(VkImageLayout inLayout);
};

class RenderPassCreateInfo final
{
public:
	RenderPassCreateInfo() = default;
	RenderPassCreateInfo(const RenderPassCreateInfo&) = delete;
	RenderPassCreateInfo(RenderPassCreateInfo&&) = delete;
	auto operator=(const RenderPassCreateInfo&) -> RenderPassCreateInfo& = delete;
	auto operator=(RenderPassCreateInfo&&) -> RenderPassCreateInfo& = delete;
	~RenderPassCreateInfo() = default;

	void AddSubpass(std::string_view inName, const SubpassDescription& inSubpass);
	void AddAttachment(std::string_view inName, const AttachmentDescription& inAttachment);

private:
	friend class RenderPass;

		auto _GetAttachmentIndex(std::string_view inName) const -> uint32_t;
		auto _GetSubpassIndex(std::string_view inName) const -> uint32_t;

	std::vector<AttachmentDescription> m_attachments;
	std::vector<SubpassDescription> m_subpasses;
	std::unordered_map<std::string, uint32_t> m_attachmentNameToIndex;
	std::unordered_map<std::string, uint32_t> m_subpassNameToIndex;
};

namespace VulkanDetail
{
	class RenderPassDescription final : public RefCounted
	{
	public:
		struct SubpassInfo
		{
			uint32_t colorSlotCount = 0;
			bool hasDepth = false;
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		};
		struct Subpass
		{
			VkSubpassDescriptionFlags flags = 0;
			VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			std::vector<VkAttachmentReference> colors;
			std::vector<VkAttachmentReference> resolves;
			std::optional<VkAttachmentReference> depth;
			std::vector<uint32_t> preserves;
		};
		std::vector<SubpassInfo> subpassInfos;
		std::vector<VkClearValue> clearValues;
		std::unordered_map<std::string, uint32_t> attachmentNameToIndex;
		std::vector<VkAttachmentDescription> attachments;
		std::vector<Subpass> subpasses;
		std::vector<VkSubpassDependency> dependencies;
		std::vector<VkImageUsageFlags> attachmentUsages;
	};
	struct RenderPassSnapshot
	{
		IntrusivePtr<RenderPassResource> resource;
		IntrusivePtr<RenderPassDescription> description;
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
