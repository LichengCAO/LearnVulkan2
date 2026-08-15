#pragma once
#include "common.h"
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
		VkPipelineStageFlags dstStage = 0;
		VkAccessFlags dstAccess = 0;
	};

	std::vector<AttachmentReference> m_colorAttachments;
	std::vector<AttachmentReference> m_resolveAttachments;
	std::optional<AttachmentReference> m_depthStencilAttachment;
	std::vector<Dependency> m_dependencies;
	VkPipelineStageFlags m_availableStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkAccessFlags m_availableAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	VkDependencyFlags m_dependencyFlags = 0;

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
	void CustomizeAvailableState(
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess);

	// Adds a dependency from inSrcSubpass to this subpass.
	// inStage/inAccess describe where data must be visible for this subpass.
	// Use CustomizeAvailableState on the source subpass to describe where the data is produced.
	void AddDependencyOnSubpass(
		std::string_view inSrcSubpassName,
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess);
	void AddExternalDependency(
		VkPipelineStageFlags inStage,
		VkAccessFlags inAccess);
	void AllowLocalPipelineBarrier();
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
	RenderPassCreateInfo& operator=(const RenderPassCreateInfo&) = delete;
	RenderPassCreateInfo& operator=(RenderPassCreateInfo&&) = delete;
	~RenderPassCreateInfo() = default;

	void AddSubpass(std::string_view inName, const SubpassDescription& inSubpass);
	void AddAttachment(std::string_view inName, const AttachmentDescription& inAttachment);

private:
	friend class RenderPass;

	auto GetAttachmentIndex(std::string_view inName) const -> uint32_t;
	auto GetSubpassIndex(std::string_view inName) const -> uint32_t;

	std::vector<AttachmentDescription> m_attachments;
	std::vector<SubpassDescription> m_subpasses;
	std::unordered_map<std::string, uint32_t> m_attachmentNameToIndex;
	std::unordered_map<std::string, uint32_t> m_subpassNameToIndex;
};

class RenderPass final
{
private:
	VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;
	std::vector<VkClearValue> m_clearValues;
	std::unordered_map<std::string, uint32_t> m_attachmentNameToIndex;

public:
	RenderPass() = default;
	RenderPass(const RenderPass&) = delete;
	RenderPass& operator=(const RenderPass&) = delete;
	~RenderPass();

	void Create(const RenderPassCreateInfo* inCreateInfo);
	void Destroy();
	auto GetVkRenderPass()const -> VkRenderPass { return m_vkRenderPass; };
	auto GetClearValues() const -> const std::vector<VkClearValue>& { return m_clearValues; };
	auto GetAttachmentIndex(std::string_view inName) const -> uint32_t;
};

class FramebufferCreateInfo final
{
private:
	friend class Framebuffer;

	const RenderPass* m_renderPass = nullptr;
	std::vector<VkImageView> m_imageViews;
	VkExtent2D m_extent{};
	uint32_t m_layers = 1;

public:
	void SetImageView(std::string_view inTargetAttachmentName, const ImageView* inViewAttached);
	void SetRenderPass(const RenderPass* inRenderPass);
};

class Framebuffer final
{
private:
	VkFramebuffer m_vkFramebuffer = VK_NULL_HANDLE;

public:
    Framebuffer() = default;
	~Framebuffer();
	void Create(const FramebufferCreateInfo* inCreateInfo);
	void Destroy();
	VkFramebuffer GetVkFramebuffer() const;
};
