#pragma once
#include "common.h"
#include <utility>
#include <variant>

class ImageView;

class RenderTarget final
{
public:
	void CustomizeLoadOperationAndInitialLayout(VkAttachmentLoadOp inOperation, VkImageLayout inLayout);
	void CustomizeStoreOperationAndFinalLayout(VkAttachmentStoreOp inOperation, VkImageLayout inLayout);
	void SetAsColorAttachment(const ImageView* inView);
	void SetAsResolvedColorAttachment(const ImageView* inMultiSampleView, const ImageView* in1SampleView);
	void SetAsDepthStencilAttachment(const ImageView* inView);
};

class RenderPassCreateInfo final
{
public:
	using AttachmentHandle = uint32_t;
	using SubpassHandle = uint32_t;

	class SubpassDescription final
	{
		friend class RenderPassCreateInfo;
		friend class RenderPass;

	private:
		struct Dependency final
		{
			RenderPassCreateInfo::SubpassHandle srcSubpass = VK_SUBPASS_EXTERNAL;
			VkPipelineStageFlags dstStage = 0;
			VkAccessFlags dstAccess = 0;
		};

		std::vector<VkAttachmentReference> m_colorAttachments;
		std::vector<VkAttachmentReference> m_resolveAttachments;
		std::optional<VkAttachmentReference> m_depthStencilAttachment;
		std::vector<Dependency> m_dependencies;
		VkPipelineStageFlags m_availableStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkAccessFlags m_availableAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		VkDependencyFlags m_dependencyFlags = 0;

	public:
		void AddDepthStencilAttachment(
			RenderPassCreateInfo::AttachmentHandle inHandle,
			VkImageLayout inFinalLayout);
		void AddResolvedAttachment(
			uint32_t inOutputSlot,
			RenderPassCreateInfo::AttachmentHandle inMultiSampleAttachmentHandle,
			VkImageLayout inMultiSampleLayout,
			RenderPassCreateInfo::AttachmentHandle in1SampleAttachmentHandle,
			VkImageLayout in1SampleLayout);
		void AddColorAttachment(
			uint32_t inOutputSlot,
			RenderPassCreateInfo::AttachmentHandle inColorAttachmentHandle,
			VkImageLayout inColorLayout);
		void CustomizeAvailableState(
			VkPipelineStageFlags inStage,
			VkAccessFlags inAccess);

		// Adds a dependency from inSrcSubpass to this subpass.
		// inStage/inAccess describe where data must be visible for this subpass.
		// Use CustomizeAvailableState on the source subpass to describe where the data is produced.
		void AddDependencyOnSubpass(
			RenderPassCreateInfo::SubpassHandle inSrcSubpass,
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

public:
	RenderPassCreateInfo() = default;
	RenderPassCreateInfo(const RenderPassCreateInfo&) = delete;
	RenderPassCreateInfo(RenderPassCreateInfo&&) = delete;
	RenderPassCreateInfo& operator=(const RenderPassCreateInfo&) = delete;
	RenderPassCreateInfo& operator=(RenderPassCreateInfo&&) = delete;
	~RenderPassCreateInfo() = default;

	auto AddSubpass(const SubpassDescription& inSubpass)-> RenderPassCreateInfo::SubpassHandle;
	auto AddAttachment(const AttachmentDescription& inAttachment) -> RenderPassCreateInfo::AttachmentHandle;

private:
	friend class RenderPass;

	std::vector<AttachmentDescription> m_attachments;
	std::vector<SubpassDescription> m_subpasses;
};

class RenderPass final
{
private:
	VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;
	std::vector<VkClearValue> m_clearValues;

public:
	RenderPass() = default;
	RenderPass(const RenderPass&) = delete;
	RenderPass& operator=(const RenderPass&) = delete;
	~RenderPass();

	void Create(const RenderPassCreateInfo* inCreateInfo);
	void Destroy();
	auto GetVkRenderPass()const -> VkRenderPass { return m_vkRenderPass; };
	auto GetClearValues() const -> const std::vector<VkClearValue>& { return m_clearValues; };
};

class FramebufferCreateInfo final
{
public:
	void SetImageView(RenderPassCreateInfo::AttachmentHandle inTargetAttachment, const ImageView* inViewAttached);
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