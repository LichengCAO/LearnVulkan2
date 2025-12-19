#include "render_pass.h"
#include "device.h"
#include "commandbuffer.h"

RenderPass::~RenderPass()
{
	assert(vkRenderPass == VK_NULL_HANDLE);
}

RenderPass::Attachment RenderPass::GetPresetAttachment(AttachmentPreset _preset)
{
	Attachment info{};
	VkAttachmentDescription& vkAttachment = info.attachmentDescription;
	switch (_preset)
	{
	case AttachmentPreset::SWAPCHAIN:
	{
		vkAttachment.format = MyDevice::GetInstance().GetSwapchainFormat();
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE; // https://www.reddit.com/r/vulkan/comments/d8meej/gridlike_pattern_over_a_basic_clearcolor_vulkan/?rdt=33712
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::DEPTH:
	{
		vkAttachment.format = MyDevice::GetInstance().GetDepthFormat();
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.depthStencil = { 1.0f, 0 };
		break;
	}
	case AttachmentPreset::GBUFFER_NORMAL:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE; // https://www.reddit.com/r/vulkan/comments/d8meej/gridlike_pattern_over_a_basic_clearcolor_vulkan/?rdt=33712
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::GBUFFER_POSITION:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE; // https://www.reddit.com/r/vulkan/comments/d8meej/gridlike_pattern_over_a_basic_clearcolor_vulkan/?rdt=33712
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::GBUFFER_UV:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE; // https://www.reddit.com/r/vulkan/comments/d8meej/gridlike_pattern_over_a_basic_clearcolor_vulkan/?rdt=33712
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::GBUFFER_ALBEDO:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::GBUFFER_DEPTH:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;// positive depth (larger farer), depth attachment value, 0, 1.0 
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 10000.0f, 1.0f, 0.0f, 1.0f };
		break;
	}
	case AttachmentPreset::COLOR_OUTPUT:
	{
		vkAttachment.format = VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		vkAttachment.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		vkAttachment.loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR;
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
		vkAttachment.stencilLoadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		vkAttachment.stencilStoreOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE;
		vkAttachment.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		vkAttachment.finalLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.clearValue = VkClearValue{};
		info.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
		break;
	}
	default:
	{
		CHECK_TRUE(false, "No such attachment preset!");
		break;
	}
	}
	return info;
}

uint32_t RenderPass::PreAddAttachment(const Attachment& _info)
{
	uint32_t ret = static_cast<uint32_t>(attachments.size());
	assert(vkRenderPass == VK_NULL_HANDLE);
	attachments.push_back(_info);
	m_clearValues.push_back(_info.clearValue);
	return ret;
}

uint32_t RenderPass::PreAddAttachment(AttachmentPreset _preset)
{
	return PreAddAttachment(GetPresetAttachment(_preset));
}

uint32_t RenderPass::PreAddSubpass(Subpass _subpass)
{
	uint32_t ret = static_cast<uint32_t>(subpasses.size());
	assert(vkRenderPass == VK_NULL_HANDLE);
	VkSubpassDependency subpassDependency{};
	subpassDependency.srcSubpass = subpasses.empty() ? VK_SUBPASS_EXTERNAL : static_cast<uint32_t>(subpasses.size() - 1);
	subpassDependency.dstSubpass = static_cast<uint32_t>(subpasses.size());
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	if (_subpass.optDepthStencilAttachment.has_value())
	{
		subpassDependency.srcStageMask = subpassDependency.srcAccessMask | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		subpassDependency.dstStageMask = subpassDependency.dstStageMask | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		subpassDependency.dstAccessMask = subpassDependency.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	m_vkSubpassDependencies.push_back(subpassDependency);
	subpasses.push_back(_subpass);

	return ret;
}

void RenderPass::Init()
{
	std::vector<VkAttachmentDescription> vkAttachments;
	for (int i = 0; i < attachments.size(); ++i)
	{
		vkAttachments.push_back(attachments[i].attachmentDescription);
	}
	std::vector<VkSubpassDescription> vkSubpasses;
	for (int i = 0; i < subpasses.size(); ++i)
	{
		VkSubpassDescription vkSubpass{};
		vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		vkSubpass.colorAttachmentCount = static_cast<uint32_t>(subpasses[i].colorAttachments.size());
		vkSubpass.pColorAttachments = subpasses[i].colorAttachments.data();
		if (subpasses[i].optDepthStencilAttachment.has_value())
		{
			vkSubpass.pDepthStencilAttachment = &subpasses[i].optDepthStencilAttachment.value();
		}
		if (subpasses[i].resolveAttachments.size() > 0)
		{
			vkSubpass.pResolveAttachments = subpasses[i].resolveAttachments.data();
		}
		vkSubpasses.push_back(vkSubpass);
	}
	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
	renderPassInfo.pAttachments = vkAttachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(vkSubpasses.size());
	renderPassInfo.pSubpasses = vkSubpasses.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(m_vkSubpassDependencies.size());
	renderPassInfo.pDependencies = m_vkSubpassDependencies.data();
	CHECK_TRUE(vkRenderPass == VK_NULL_HANDLE, "VkRenderPass is already created!");
	VK_CHECK(vkCreateRenderPass(MyDevice::GetInstance().vkDevice, &renderPassInfo, nullptr, &vkRenderPass), "Failed to create render pass!");
}

VkRenderPassBeginInfo RenderPass::_GetVkRenderPassBeginInfo(const Framebuffer* pFramebuffer) const
{
	VkRenderPassBeginInfo ret{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	ret.renderPass = vkRenderPass;
	ret.renderArea.offset = { 0, 0 };
	if (pFramebuffer != nullptr)
	{
		CHECK_TRUE(this == pFramebuffer->pRenderPass, "This framebuffer doesn't belong to this render pass!");
		ret.framebuffer = pFramebuffer->vkFramebuffer;
		ret.renderArea.extent = pFramebuffer->GetImageSize();
		ret.clearValueCount = static_cast<uint32_t>(m_clearValues.size()); // should have same length as attachedViews, although index of those who don't clear on load will be ignored
		ret.pClearValues = m_clearValues.data();
	}
	return ret;
}

void RenderPass::StartRenderPass(CommandSubmission* pCmd, const Framebuffer* pFramebuffer) const
{
	pCmd->_BeginRenderPass(_GetVkRenderPassBeginInfo(pFramebuffer), VK_SUBPASS_CONTENTS_INLINE);
	if (pFramebuffer != nullptr)
	{
		int n = pFramebuffer->attachedViews.size();
		std::vector<VkImage> images;
		std::vector<VkImageSubresourceRange> ranges;
		std::vector<VkImageLayout> layouts;
		images.reserve(n);
		ranges.reserve(n);
		layouts.reserve(n);
		for (int i = 0; i < n; ++i)
		{
			auto info = pFramebuffer->attachedViews[i]->GetImageViewInformation();
			VkImageSubresourceRange range{};
			range.aspectMask = info.aspectMask;
			range.baseArrayLayer = info.baseArrayLayer;
			range.baseMipLevel = info.baseMipLevel;
			range.layerCount = info.layerCount;
			range.levelCount = info.levelCount;

			images.push_back(info.vkImage);
			ranges.push_back(range);
			layouts.push_back(attachments[i].attachmentDescription.finalLayout);
		}
		pCmd->BindCallback(
			CommandSubmission::CALLBACK_BINDING_POINT::END_RENDER_PASS,
			[images, ranges, layouts](CommandSubmission* pCmd)
			{
				for (int i = 0; i < images.size(); ++i)
				{
					pCmd->_UpdateImageLayout(images[i], ranges[i], layouts[i]);
				}
			}
		);
	}
}

void RenderPass::Uninit()
{
	m_vkSubpassDependencies.clear();
	m_clearValues.clear();
	attachments.clear();
	subpasses.clear();
	if (vkRenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(MyDevice::GetInstance().vkDevice, vkRenderPass, nullptr);
		vkRenderPass = VK_NULL_HANDLE;
	}
}

Framebuffer RenderPass::NewFramebuffer(const std::vector<const ImageView*>& _imageViews) const
{
	CHECK_TRUE(vkRenderPass != VK_NULL_HANDLE, "Render pass is not initialized!");
	CHECK_TRUE(_imageViews.size() == attachments.size(), "Number of image views is not the same as number of attachments");
	for (int i = 0; i < _imageViews.size(); ++i)
	{
		CHECK_TRUE(attachments[i].attachmentDescription.format == _imageViews[i]->pImage->GetImageInformation().format, "Format of the imageview is not the same as the format of attachment");
	}
	return Framebuffer{ this ,_imageViews };
}

void RenderPass::NewFramebuffer(const std::vector<const ImageView*>& _imageViews, Framebuffer*& _pFramebuffer) const
{
	CHECK_TRUE(vkRenderPass != VK_NULL_HANDLE, "Render pass is not initialized!");
	CHECK_TRUE(_imageViews.size() == attachments.size(), "Number of image views is not the same as number of attachments");
	for (int i = 0; i < _imageViews.size(); ++i)
	{
		CHECK_TRUE(attachments[i].attachmentDescription.format == _imageViews[i]->pImage->GetImageInformation().format, "Format of the imageview is not the same as the format of attachment");
	}
	_pFramebuffer = new Framebuffer(this, _imageViews);
}

void RenderPass::Subpass::AddColorAttachment(uint32_t _binding)
{
	colorAttachments.push_back({ _binding, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
}

void RenderPass::Subpass::SetDepthStencilAttachment(uint32_t _binding, bool _readOnly)
{
	// ATTACHMENT here means writable
	// VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL -> depth is readonly, stencil is writable
	// VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL -> depth is writable, stencil is readonly
	if (_readOnly)
	{
		optDepthStencilAttachment = { _binding, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	}
	else
	{
		optDepthStencilAttachment = { _binding, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	}
}

void RenderPass::Subpass::AddResolveAttachment(uint32_t _binding)
{
	resolveAttachments.push_back({ _binding, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
}

Framebuffer::~Framebuffer()
{
	assert(vkFramebuffer == VK_NULL_HANDLE);
}

void Framebuffer::Init()
{
	CHECK_TRUE(pRenderPass != nullptr, "No render pass!");
	CHECK_TRUE(attachedViews.size() > 0, "No attachment!");
	VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	std::vector<VkImageView> vkAttachments;
	framebufferInfo.renderPass = pRenderPass->vkRenderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachedViews.size());
	for (int i = 0; i < attachedViews.size(); ++i)
	{
		CHECK_TRUE(attachedViews[i]->vkImageView != VK_NULL_HANDLE, "Image view is not initialized!");
		vkAttachments.push_back(attachedViews[i]->vkImageView);
	}
	framebufferInfo.pAttachments = vkAttachments.data();
	framebufferInfo.width = attachedViews[0]->pImage->GetImageInformation().width;
	framebufferInfo.height = attachedViews[0]->pImage->GetImageInformation().height;
	framebufferInfo.layers = 1;
	CHECK_TRUE(vkFramebuffer == VK_NULL_HANDLE, "VkFramebuffer is already created!");
	VK_CHECK(vkCreateFramebuffer(MyDevice::GetInstance().vkDevice, &framebufferInfo, nullptr, &vkFramebuffer), "Failed to create framebuffer!");
}

void Framebuffer::Uninit()
{
	if (vkFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(MyDevice::GetInstance().vkDevice, vkFramebuffer, nullptr);
		vkFramebuffer = VK_NULL_HANDLE;
	}
}

VkExtent2D Framebuffer::GetImageSize() const
{
	CHECK_TRUE(attachedViews.size() > 0, "No image in this framebuffer!");
	VkExtent2D ret{};
	ret.width = attachedViews[0]->pImage->GetImageSize().width;
	ret.height = attachedViews[0]->pImage->GetImageSize().height;
	return ret;
}
