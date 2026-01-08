#include "render_pass.h"
#include "device.h"
#include "commandbuffer.h"

RenderPass::~RenderPass()
{
	assert(m_vkRenderPass == VK_NULL_HANDLE);
}

void RenderPass::Init(const IRenderPassInitializer* inIntializerPtr)
{
	inIntializerPtr->InitRenderPass(this);
}

VkRenderPass RenderPass::GetVkRenderPass() const
{
    return m_vkRenderPass;
}

const RenderPass::Subpass& RenderPass::GetSubpass(uint32_t index) const
{
	return m_subpasses.at(index);
}

VkRenderPassBeginInfo RenderPass::GetRenderPassBeginInfo(const RenderPass* inRenderPassPtr, const Framebuffer* inFramebuferrPtr)
{
	VkRenderPassBeginInfo ret{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	
	ret.renderPass = inRenderPassPtr->GetVkRenderPass();
	ret.renderArea.offset = { 0, 0 };
	CHECK_TRUE(inRenderPassPtr == inFramebuferrPtr->m_pRenderPass, "This framebuffer doesn't belong to this render pass!");
	ret.framebuffer = inFramebuferrPtr->GetVkFramebuffer();
	ret.renderArea.extent = inFramebuferrPtr->GetImageSize();
	ret.clearValueCount = static_cast<uint32_t>(inRenderPassPtr->m_clearValues.size()); // should have same length as attachedViews, although index of those who don't clear on load will be ignored
	ret.pClearValues = inRenderPassPtr->m_clearValues.data();
	
	return ret;
}

void RenderPass::Uninit()
{
	m_clearValues.clear();
	if (m_vkRenderPass != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyRenderPass(m_vkRenderPass);
		m_vkRenderPass = VK_NULL_HANDLE;
	}
}

RenderPass::Subpass& RenderPass::Subpass::AddColorAttachment(
	uint32_t inLocationInShader, 
	uint32_t inAttachmentIndexInRenderPass, 
	VkImageLayout inLayout)
{
	static const VkAttachmentReference unusedAttachment{ VK_ATTACHMENT_UNUSED };
	
	if (colorAttachments.size() <= inLocationInShader)
	{	
		colorAttachments.resize(inLocationInShader + 1, unusedAttachment);
	}
	if (resolveAttachments.size() > 0 && resolveAttachments.size() < colorAttachments.size())
	{
		resolveAttachments.resize(colorAttachments.size(), unusedAttachment);
	}
	colorAttachments[inLocationInShader] = { inAttachmentIndexInRenderPass, inLayout };

	return *this;
}

RenderPass::Subpass& RenderPass::Subpass::AddResolveAttachment(
	uint32_t inColorReferenceLocation, 
	uint32_t inAttachmentIndexInRenderPass, 
	VkImageLayout inLayout)
{
	static const VkAttachmentReference unusedAttachment{ VK_ATTACHMENT_UNUSED };
	
	if (resolveAttachments.size() <= inColorReferenceLocation || resolveAttachments.size() < colorAttachments.size())
	{
		size_t aimSize = std::max(colorAttachments.size(), static_cast<size_t>(inColorReferenceLocation + 1));
		resolveAttachments.resize(aimSize, unusedAttachment);
	}
	resolveAttachments[inColorReferenceLocation] = { inAttachmentIndexInRenderPass, inLayout };

	return *this;
}

RenderPass::Subpass& RenderPass::Subpass::AddDepthStencilAttachment(uint32_t inAttachmentIndexInRenderPass, VkImageLayout inLayout)
{
	// ATTACHMENT here means writable
	// VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL -> depth is readonly, stencil is writable
	// VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL -> depth is writable, stencil is readonly
	optDepthStencilAttachment = { inAttachmentIndexInRenderPass, inLayout };
	return *this;
}

VkSubpassDescription RenderPass::Subpass::GetSubpassDescription() const
{
	VkSubpassDescription vkSubpass{};
	
	vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	vkSubpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	vkSubpass.pColorAttachments = colorAttachments.data();
	if (optDepthStencilAttachment.has_value())
	{
		vkSubpass.pDepthStencilAttachment = &optDepthStencilAttachment.value();
	}
	if (resolveAttachments.size() > 0)
	{
		CHECK_TRUE(resolveAttachments.size() == colorAttachments.size());
		vkSubpass.pResolveAttachments = resolveAttachments.data();
	}

	return vkSubpass;
}

Framebuffer::~Framebuffer()
{
	assert(m_vkFramebuffer == VK_NULL_HANDLE);
}

void Framebuffer::Init(const IFramebufferInitializer* inInitializerPtr)
{
	inInitializerPtr->InitFramebuffer(this);

	CHECK_TRUE(m_vkFramebuffer != VK_NULL_HANDLE);
	CHECK_TRUE(m_pRenderPass != nullptr);
}

void Framebuffer::Uninit()
{
	if (m_vkFramebuffer != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyFramebuffer(m_vkFramebuffer);
		m_vkFramebuffer = VK_NULL_HANDLE;
	}
}

VkExtent2D Framebuffer::GetImageSize() const
{
	return m_imageSize;
}

VkFramebuffer Framebuffer::GetVkFramebuffer() const
{
	return m_vkFramebuffer;
}

RenderPass::Attachment RenderPass::AttachmentBuilder::BuildByPreset(RenderPass::AttachmentPreset inPreset)
{
	Attachment info{};
	VkAttachmentDescription& vkAttachment = info.attachmentDescription;
	switch (inPreset)
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

void RenderPass::SingleSubpassInit::InitRenderPass(RenderPass* pRenderPass) const
{
	auto& device = MyDevice::GetInstance();
	std::vector<VkAttachmentDescription> vkAttachments;
	VkSubpassDescription subpass{};
	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	//VkSubpassDependency subpassDependency{};

	// Do things like pipeline memory barrier
	// see https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassDependency.html
	//{
	//	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	//	subpassDependency.dstSubpass = 0;
	//	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	//	subpassDependency.srcAccessMask = 0;
	//	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	//	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	//	if (m_subpass.optDepthStencilAttachment.has_value())
	//	{
	//		subpassDependency.srcStageMask = subpassDependency.srcAccessMask | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	//		subpassDependency.dstStageMask = subpassDependency.dstStageMask | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	//		subpassDependency.dstAccessMask = subpassDependency.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	//	}
	//}
	for (int i = 0; i < m_attachments.size(); ++i)
	{
		vkAttachments.push_back(m_attachments[i].attachmentDescription);
		pRenderPass->m_clearValues.push_back(m_attachments[i].clearValue);
	}
	subpass = m_subpass.GetSubpassDescription();

	renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
	renderPassInfo.pAttachments = vkAttachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 0;
	renderPassInfo.pDependencies = nullptr;

	pRenderPass->m_vkRenderPass = device.CreateRenderPass(renderPassInfo);
	pRenderPass->m_subpasses = { m_subpass };
}

RenderPass::SingleSubpassInit& RenderPass::SingleSubpassInit::Reset()
{
	m_subpass = RenderPass::Subpass{};
	m_attachments.clear();

	return *this;
}

RenderPass::SingleSubpassInit& RenderPass::SingleSubpassInit::AddColorAttachment(const Attachment& inAttachment)
{
	uint32_t attachmentIndex = m_attachments.size();

	m_attachments.push_back(inAttachment);
	m_subpass.AddColorAttachment(attachmentIndex, attachmentIndex);

	return *this;
}

RenderPass::SingleSubpassInit& RenderPass::SingleSubpassInit::AddDepthStencilAttachment(const Attachment& inAttachment, bool inReadOnly)
{
	uint32_t attachmentIndex = m_attachments.size();
	
	m_attachments.push_back(inAttachment);
	m_subpass.AddDepthStencilAttachment(
		attachmentIndex, 
		inReadOnly ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL);

	return *this;
}

void Framebuffer::FramebufferInit::InitFramebuffer(Framebuffer* pFramebuffer) const
{
	CHECK_TRUE(m_pRenderPass != nullptr, "No render pass!");
	VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	auto& device = MyDevice::GetInstance();

	framebufferInfo.renderPass = m_pRenderPass->GetVkRenderPass();
	framebufferInfo.attachmentCount = static_cast<uint32_t>(m_views.size());
	framebufferInfo.pAttachments = m_views.data();
	framebufferInfo.width = m_width;
	framebufferInfo.height = m_height;
	framebufferInfo.layers = 1;

	pFramebuffer->m_pRenderPass = m_pRenderPass;
	pFramebuffer->m_imageSize.width = m_width;
	pFramebuffer->m_imageSize.height = m_height;
	pFramebuffer->m_vkFramebuffer = device.CreateFramebuffer(framebufferInfo);
}

Framebuffer::FramebufferInit& Framebuffer::FramebufferInit::Reset()
{
	*this = Framebuffer::FramebufferInit{};

	return *this;
}

Framebuffer::FramebufferInit& Framebuffer::FramebufferInit::SetImageView(uint32_t inAttachmentIndex, const ImageView* inViewPtr)
{
	const auto& viewInfo = inViewPtr->GetImageViewInformation();
	if (m_views.size() <= inAttachmentIndex)
	{
		m_views.resize(inAttachmentIndex + 1, VK_NULL_HANDLE);
	}
	m_views[inAttachmentIndex] = inViewPtr->GetVkImageView();
	m_width = viewInfo.width;
	m_height = viewInfo.height;
	return *this;
}

Framebuffer::FramebufferInit& Framebuffer::FramebufferInit::SetRenderPass(const RenderPass* inRenderPassPtr)
{
	m_pRenderPass = inRenderPassPtr;

	return *this;
}

AttachmentDescriptionBuilder& AttachmentDescriptionBuilder::Reset()
{
	m_format = VK_FORMAT_MAX_ENUM;
	m_loadOp.reset();
	m_storeOp.reset();
	m_initialLayout.reset();
	m_finalLayout.reset();
	return *this;
}

AttachmentDescriptionBuilder& AttachmentDescriptionBuilder::SetFormat(VkFormat inFormat)
{
	if (inFormat == VK_FORMAT_MAX_ENUM)
	{
		throw std::invalid_argument("Attachment format cannot be VK_FORMAT_MAX_ENUM");
	}
	m_format = inFormat;
	return *this;
}

AttachmentDescriptionBuilder& AttachmentDescriptionBuilder::CustomizeLoadOperationAndInitialLayout(
	VkImageLayout inLayout, 
	VkAttachmentLoadOp inOperation)
{
	// Basic validation (Vulkan spec compliance)
	if (inOperation != VK_ATTACHMENT_LOAD_OP_LOAD &&
		inOperation != VK_ATTACHMENT_LOAD_OP_CLEAR &&
		inOperation != VK_ATTACHMENT_LOAD_OP_DONT_CARE)
	{
		throw std::invalid_argument("Invalid VkAttachmentLoadOp value");
	}

	m_loadOp = inOperation;
	m_initialLayout = inLayout;
	return *this;
}

AttachmentDescriptionBuilder& AttachmentDescriptionBuilder::CustomizeStoreOperationAndFinalLayout(
	VkImageLayout inLayout, 
	VkAttachmentStoreOp inOperation)
{
	m_storeOp = inOperation;
	m_finalLayout = inLayout;
	return *this;
}

VkAttachmentDescription AttachmentDescriptionBuilder::Build() const
{
	CHECK_TRUE(m_format != VK_FORMAT_MAX_ENUM);

	// Assemble the struct with defaults or user-provided values
	VkAttachmentDescription desc{}; // Zero-initialize first (Vulkan best practice)
	desc.format = m_format;

	// Use user value or default for load/initial layout
	desc.loadOp = m_loadOp.value_or(VK_ATTACHMENT_LOAD_OP_CLEAR);
	desc.initialLayout = m_initialLayout.value_or(VK_IMAGE_LAYOUT_UNDEFINED);

	// Use user value or default for store/final layout
	desc.storeOp = m_storeOp.value_or(VK_ATTACHMENT_STORE_OP_STORE);
	desc.finalLayout = m_finalLayout.value_or(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Optional: Set default sample count (common for color/depth attachments)
	// You can add a SetSampleCount() method if needed!
	desc.samples = VK_SAMPLE_COUNT_1_BIT;

	// Optional: Set default stencil ops (only relevant for depth/stencil attachments)
	desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	return desc;
}