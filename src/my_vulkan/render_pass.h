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

	struct Subpass
	{
		std::vector<VkAttachmentReference> colorAttachments;
		std::optional<VkAttachmentReference> optDepthStencilAttachment;
		std::vector<VkAttachmentReference> resolveAttachments;

		RenderPass::Subpass& AddColorAttachment(uint32_t inAttachmentIndex);

		RenderPass::Subpass& SetDepthStencilAttachment(uint32_t inAttachmentIndex, bool inReadOnly = false);

		RenderPass::Subpass& AddResolveAttachment(uint32_t inAttachmentIndex);

		VkSubpassDescription GetSubpassDescription() const;
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