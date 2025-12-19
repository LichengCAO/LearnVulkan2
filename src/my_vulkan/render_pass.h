#pragma once
#include "common.h"
class Framebuffer;
class ImageView;
class CommandSubmission;

// Render pass can be something like a blueprint of frame buffers
class RenderPass
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

	struct Subpass
	{
		std::vector<VkAttachmentReference> colorAttachments;
		std::optional<VkAttachmentReference> optDepthStencilAttachment;
		std::vector<VkAttachmentReference> resolveAttachments;

		void AddColorAttachment(uint32_t _binding);

		void SetDepthStencilAttachment(uint32_t _binding, bool _readOnly = false);

		void AddResolveAttachment(uint32_t _binding);
	};

private:
	std::vector<VkSubpassDependency> m_vkSubpassDependencies;
	std::vector<VkClearValue> m_clearValues;

private:
	// Return begin info of this render pass
	VkRenderPassBeginInfo _GetVkRenderPassBeginInfo(const Framebuffer* pFramebuffer = nullptr) const;

public:
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;
	std::vector<Attachment> attachments;
	std::vector<Subpass> subpasses;

public:
	static RenderPass::Attachment GetPresetAttachment(AttachmentPreset _preset);

	~RenderPass();

	uint32_t PreAddAttachment(const Attachment& _info);

	uint32_t PreAddAttachment(AttachmentPreset _preset);

	uint32_t PreAddSubpass(Subpass _subpass);

	void Init();

	Framebuffer NewFramebuffer(const std::vector<const ImageView*>& _imageViews) const;

	void NewFramebuffer(const std::vector<const ImageView*>& _imageViews, Framebuffer*& _pFramebuffer) const;

	// record vkCmdBeginRenderPass command in command buffer, also bind callback for image layout management
	void StartRenderPass(CommandSubmission* pCmd, const Framebuffer* pFramebuffer = nullptr) const;

	void Uninit();
};

class Framebuffer final
{
public:
	VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
	const RenderPass* const pRenderPass = nullptr;
	const std::vector<const ImageView*> attachedViews;

private:
	Framebuffer(
		const RenderPass* _pRenderPass,
		const std::vector<const ImageView*> _imageViews)
		: pRenderPass(_pRenderPass),  
		attachedViews(_imageViews) {};

public:
	Framebuffer() {};

	~Framebuffer();

	void Init();

	void Uninit();

	VkExtent2D GetImageSize() const;

	friend class RenderPass;
};