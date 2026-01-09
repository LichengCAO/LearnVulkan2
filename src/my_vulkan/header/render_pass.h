#pragma once
#include "common.h"
class RenderPass;
class Framebuffer;
class ImageView;
class CommandSubmission;

class IRenderPassInitializer
{
public:
	virtual void InitRenderPass(RenderPass* pRenderPass) const = 0;
};

class IFramebufferInitializer
{
public:
	virtual void InitFramebuffer(Framebuffer* pFramebuffer) const = 0;
};

class AttachmentDescriptionBuilder
{
private:
	VkFormat m_format = VK_FORMAT_MAX_ENUM;
	std::optional<VkAttachmentLoadOp> m_loadOp;
	std::optional<VkAttachmentStoreOp> m_storeOp;
	std::optional<VkImageLayout> m_initialLayout;
	std::optional<VkImageLayout> m_finalLayout;

public:
	AttachmentDescriptionBuilder& Reset();
	AttachmentDescriptionBuilder& SetFormat(VkFormat inFormat);
	// default VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED
	AttachmentDescriptionBuilder& CustomizeLoadOperationAndInitialLayout(VkAttachmentLoadOp inOperation, VkImageLayout inLayout);
	// default VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	AttachmentDescriptionBuilder& CustomizeStoreOperationAndFinalLayout(VkAttachmentStoreOp inOperation, VkImageLayout inLayout);
	VkAttachmentDescription Build() const;
};

// Render pass can be something like a blueprint of frame buffers
class RenderPass final
{
public:
	// Pipeline only has one subpass to describe the output attachedViews
	enum class AttachmentPreset
	{
		SWAPCHAIN,
		DEPTH,
		GBUFFER_UV,
		GBUFFER_NORMAL,
		GBUFFER_POSITION,
		GBUFFER_ALBEDO,
		GBUFFER_DEPTH,
		COLOR_OUTPUT, //rgba32, shader read-only, clear on load
	};
	struct Attachment
	{
		VkAttachmentDescription attachmentDescription{};
		VkClearValue clearValue{};
	};
	class AttachmentBuilder
	{
	public:
		static RenderPass::Attachment BuildByPreset(RenderPass::AttachmentPreset inPreset);
	};

	class Subpass final
	{
	private:
		std::vector<VkAttachmentReference> colorAttachments;
		std::vector<VkAttachmentReference> resolveAttachments;
		std::optional<VkAttachmentReference> optDepthStencilAttachment;

	public:
		RenderPass::Subpass& AddColorAttachment(
			uint32_t inLocationInShader,
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		RenderPass::Subpass& AddResolveAttachment(
			uint32_t inColorReferenceLocation,
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		RenderPass::Subpass& AddDepthStencilAttachment(
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		VkSubpassDescription GetSubpassDescription() const;

		friend class RenderPass;
	};

	class SingleSubpassInit : public IRenderPassInitializer
	{
	private:
		Subpass m_subpass;
		std::vector<Attachment> m_attachments;

	public:
		virtual void InitRenderPass(RenderPass* pRenderPass) const override;

		// Reset everything into default state
		RenderPass::SingleSubpassInit& Reset();

		RenderPass::SingleSubpassInit& AddColorAttachment(const Attachment& inAttachment);

		RenderPass::SingleSubpassInit& AddDepthStencilAttachment(const Attachment& inAttachment, bool inReadOnly = false);
	};

private:
	std::vector<VkClearValue> m_clearValues;
	std::vector<Subpass> m_subpasses;
	VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;	

public:
	~RenderPass();

	void Init(const IRenderPassInitializer* inIntializerPtr);

	void Uninit();

	VkRenderPass GetVkRenderPass() const;

	const Subpass& GetSubpass(uint32_t index = 0) const;

	static VkRenderPassBeginInfo GetRenderPassBeginInfo(
		const RenderPass* inRenderPassPtr,
		const Framebuffer* inFramebuferrPtr);
};

class Framebuffer final
{
private:
	const RenderPass* m_pRenderPass = nullptr;
	VkFramebuffer m_vkFramebuffer = VK_NULL_HANDLE;
	VkExtent2D m_imageSize;

public:
	class FramebufferInit : public IFramebufferInitializer
	{
	private:
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		std::vector<VkImageView> m_views;
		const RenderPass* m_pRenderPass = nullptr;

	public:
		virtual void InitFramebuffer(Framebuffer* pFramebuffer) const override;

		Framebuffer::FramebufferInit& Reset();

		Framebuffer::FramebufferInit& SetImageView(uint32_t inAttachmentIndex, const ImageView* inViewPtr);

		Framebuffer::FramebufferInit& SetRenderPass(const RenderPass* inRenderPassPtr);
	};

public:
	~Framebuffer();

	void Init(const IFramebufferInitializer* inInitializerPtr);

	void Uninit();

	VkExtent2D GetImageSize() const;

	VkFramebuffer GetVkFramebuffer() const;

	friend class RenderPass;
};