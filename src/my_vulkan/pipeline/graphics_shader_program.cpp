#include "graphics_shader_program.h"
#include "device.h"
#include "push_constant_manager.h"
#include "commandbuffer.h"
#include "shader_reflect.h"
#include "utility/hash_util.h"

RenderPass::~RenderPass()
{
	assert(m_vkRenderPass == VK_NULL_HANDLE);
}

void RenderPass::Create(const IRenderPassInitializer* inIntializerPtr)
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

void RenderPass::StartRenderPass(CommandSubmission* pCmd, const Framebuffer* pFramebuffer) const
{
	const auto info = GetRenderPassBeginInfo(this, pFramebuffer);
	pCmd->_BeginRenderPass(info);
}

VkRenderPassBeginInfo RenderPass::GetRenderPassBeginInfo(const RenderPass* inRenderPassPtr, const Framebuffer* inFramebuferrPtr)
{
	VkRenderPassBeginInfo ret{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

	ret.renderPass = inRenderPassPtr->GetVkRenderPass();
	ret.renderArea.offset = { 0, 0 };
	CHECK_TRUE(inRenderPassPtr == inFramebuferrPtr->m_pRenderPass, "This framebuffer doesn't belong to this render pass!");
	ret.framebuffer = inFramebuferrPtr->GetVkFramebuffer();
	ret.renderArea.extent = inFramebuferrPtr->GetImageSize();
	ret.clearValueCount = static_cast<uint32_t>(inRenderPassPtr->m_clearValues.size());
	ret.pClearValues = inRenderPassPtr->m_clearValues.data();

	return ret;
}

void RenderPass::Destroy()
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

void Framebuffer::Create(const IFramebufferInitializer* inInitializerPtr)
{
	inInitializerPtr->InitFramebuffer(this);

	CHECK_TRUE(m_vkFramebuffer != VK_NULL_HANDLE);
	CHECK_TRUE(m_pRenderPass != nullptr);
}

void Framebuffer::Destroy()
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
		vkAttachment.storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE;
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
	case AttachmentPreset::GBUFFER_POSITION:
	case AttachmentPreset::GBUFFER_UV:
	case AttachmentPreset::GBUFFER_ALBEDO:
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
	case AttachmentPreset::GBUFFER_DEPTH:
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
		info.clearValue.color = { 10000.0f, 1.0f, 0.0f, 1.0f };
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
	uint32_t attachmentIndex = static_cast<uint32_t>(m_attachments.size());

	m_attachments.push_back(inAttachment);
	m_subpass.AddColorAttachment(attachmentIndex, attachmentIndex);

	return *this;
}

RenderPass::SingleSubpassInit& RenderPass::SingleSubpassInit::AddDepthStencilAttachment(const Attachment& inAttachment, bool inReadOnly)
{
	uint32_t attachmentIndex = static_cast<uint32_t>(m_attachments.size());

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
	VkAttachmentLoadOp inOperation,
	VkImageLayout inLayout)
{
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
	VkAttachmentStoreOp inOperation,
	VkImageLayout inLayout)
{
	m_storeOp = inOperation;
	m_finalLayout = inLayout;
	return *this;
}

VkAttachmentDescription AttachmentDescriptionBuilder::Build() const
{
	CHECK_TRUE(m_format != VK_FORMAT_MAX_ENUM);

	VkAttachmentDescription desc{};
	desc.format = m_format;
	desc.loadOp = m_loadOp.value_or(VK_ATTACHMENT_LOAD_OP_CLEAR);
	desc.initialLayout = m_initialLayout.value_or(VK_IMAGE_LAYOUT_UNDEFINED);
	desc.storeOp = m_storeOp.value_or(VK_ATTACHMENT_STORE_OP_STORE);
	desc.finalLayout = m_finalLayout.value_or(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	return desc;
}

GraphicsShaderProgramCreateInfo& GraphicsShaderProgramCreateInfo::Reset()
{
	m_shaderModuleInfos.clear();
	m_vkPipelineCache = VK_NULL_HANDLE;
	return *this;
}

GraphicsShaderProgramCreateInfo& GraphicsShaderProgramCreateInfo::AddShaderModuleInfo(ShaderModuleCreateInfo inShaderModule)
{
	CHECK_TRUE(!inShaderModule.m_spirvFile.empty(), "Graphics shader SPIR-V file is unset!");
	CHECK_TRUE(!inShaderModule.m_entries.empty(), "Graphics shader module needs at least one entry!");

	m_shaderModuleInfos.push_back(std::move(inShaderModule));
	return *this;
}

GraphicsShaderProgramCreateInfo& GraphicsShaderProgramCreateInfo::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;
	return *this;
}

GraphicsPipelineStateInfo& GraphicsPipelineStateInfo::Reset()
{
	m_vertexBindingDescriptions.clear();
	m_vertexAttributeDescriptions.clear();
	m_renderPassPtr = nullptr;
	m_subpassIndex = 0;
	return *this;
}

GraphicsPipelineStateInfo& GraphicsPipelineStateInfo::AddVertexInputDescription(
	const VkVertexInputBindingDescription& inBindingDescription,
	const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions)
{
	m_vertexBindingDescriptions.push_back(inBindingDescription);
	m_vertexAttributeDescriptions.insert(
		m_vertexAttributeDescriptions.end(),
		inAttributeDescriptions.begin(),
		inAttributeDescriptions.end());

	return *this;
}

GraphicsPipelineStateInfo& GraphicsPipelineStateInfo::SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex)
{
	CHECK_TRUE(inRenderPassPtr != nullptr, "Graphics pipeline state info needs a render pass!");
	inRenderPassPtr->GetSubpass(inSubpassIndex);

	m_renderPassPtr = inRenderPassPtr;
	m_subpassIndex = inSubpassIndex;
	return *this;
}

void GraphicsShaderProgram::_DoCommon(
	VkCommandBuffer cmd,
	const VkExtent2D& imageSize,
	const std::vector<VkDescriptorSet>& pSets,
	const std::vector<uint32_t>& _dynamicOffsets,
	const std::vector<std::pair<VkShaderStageFlags, const void*>>& pushConstants)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline);

	VkViewport viewport;
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(imageSize.width);
	viewport.height = static_cast<float>(imageSize.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;
	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = imageSize;
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	if (pSets.size() > 0)
	{
		vkCmdBindDescriptorSets(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_vkPipelineLayout,
			0,
			static_cast<uint32_t>(pSets.size()),
			pSets.data(),
			static_cast<uint32_t>(_dynamicOffsets.size()),
			_dynamicOffsets.data());
	}

	for (const auto& _pushConst : pushConstants)
	{
		m_uptrPushConstant->PushConstant(cmd, m_vkPipelineLayout, _pushConst.first, _pushConst.second);
	}
}

GraphicsShaderProgram::~GraphicsShaderProgram()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
	assert(m_pipelineLayout.GetVkPipelineLayout() == VK_NULL_HANDLE);
	assert(m_shaderModules.empty());
	assert(m_descriptorSetLayouts.empty());
}

void GraphicsShaderProgram::Create(const GraphicsShaderProgramCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No graphics shader program create info!");
	CHECK_TRUE(!inCreateInfo->m_shaderModuleInfos.empty(), "Graphics shader program needs shaders!");
	CHECK_TRUE(m_vkPipeline == VK_NULL_HANDLE, "Graphics shader program already has a legacy pipeline!");
	CHECK_TRUE(m_cachedGraphicsPipelines.empty(), "Graphics shader program already has cached pipelines!");
	CHECK_TRUE(m_pipelineLayout.GetVkPipelineLayout() == VK_NULL_HANDLE, "Graphics shader program already created!");

	std::vector<std::string> spirvFiles;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> descriptorSetData;
	std::unordered_map<std::string, uint32_t> pushConstantIndex;
	std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> reflectedPushConstants;
	std::vector<VkPushConstantRange> pushConstantRanges;
	ShaderReflector reflector{};

	for (const auto& shaderModuleInfo : inCreateInfo->m_shaderModuleInfos)
	{
		CHECK_TRUE(!shaderModuleInfo.m_spirvFile.empty(), "Graphics shader SPIR-V file is unset!");
		CHECK_TRUE(!shaderModuleInfo.m_entries.empty(), "Graphics shader module needs at least one entry!");
		spirvFiles.push_back(shaderModuleInfo.m_spirvFile);
	}

	reflector.Create(spirvFiles);
	reflector.ReflectDescriptorSets(m_nameToSetBinding, descriptorSetData);
	reflector.ReflectPushConst(pushConstantIndex, reflectedPushConstants);

	for (const auto& reflectedPushConstant : reflectedPushConstants)
	{
		VkPushConstantRange range{};
		range.stageFlags = reflectedPushConstant.first;
		range.offset = reflectedPushConstant.second.first;
		range.size = reflectedPushConstant.second.second;
		pushConstantRanges.push_back(range);
	}

	_CreateDescriptorSetLayouts(descriptorSetData);
	_CreatePipelineLayout(pushConstantRanges);
	_CreatePushConstantManager(pushConstantRanges);

	m_shaderModules.reserve(inCreateInfo->m_shaderModuleInfos.size());
	for (const auto& shaderModuleInfo : inCreateInfo->m_shaderModuleInfos)
	{
		auto shaderModule = std::make_unique<ShaderModule>();
		shaderModule->Create(shaderModuleInfo);

		for (const auto& [stage, entry] : shaderModuleInfo.m_entries)
		{
			m_shaderStageInfos.push_back(shaderModule->GetShaderStageInfo(stage));
		}

		m_shaderModules.push_back(std::move(shaderModule));
	}

	m_vkPipelineLayout = m_pipelineLayout.GetVkPipelineLayout();
	m_vkPipelineCache = inCreateInfo->m_vkPipelineCache;
	reflector.Destroy();

	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
	CHECK_TRUE(!m_shaderStageInfos.empty());
}

void GraphicsShaderProgram::Create(const IGraphicsShaderProgramInitializer* pInitializer)
{
	pInitializer->InitGraphicsShaderProgram(this);

	// check everything initialized
	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
}

void GraphicsShaderProgram::_CreateDescriptorSetLayouts(
	const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& inDescriptorSetData)
{
	for (const auto& descriptorSetBindings : inDescriptorSetData)
	{
		auto descriptorSetLayout = std::make_unique<DescriptorSetLayout>();
		DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};

		for (const auto& [bindingIndex, layoutBinding] : descriptorSetBindings)
		{
			CHECK_TRUE(
				layoutBinding.pImmutableSamplers == nullptr,
				"Immutable samplers are not supported by DescriptorSetLayoutCreateInfo::SetBinding yet!");

			if (layoutBinding.descriptorCount > 1)
			{
				DescriptorSetLayoutCreateInfo::ArraySizeInfo arraySizeInfo{};
				arraySizeInfo.size = layoutBinding.descriptorCount;
				descriptorSetLayoutCreateInfo.SetBinding(
					layoutBinding.binding,
					layoutBinding.descriptorType,
					layoutBinding.stageFlags,
					&arraySizeInfo);
			}
			else
			{
				descriptorSetLayoutCreateInfo.SetBinding(
					layoutBinding.binding,
					layoutBinding.descriptorType,
					layoutBinding.stageFlags);
			}
		}

		descriptorSetLayout->Create(descriptorSetLayoutCreateInfo);
		m_descriptorSetLayouts.push_back(std::move(descriptorSetLayout));
	}
}

void GraphicsShaderProgram::_CreatePipelineLayout(const std::vector<VkPushConstantRange>& inPushConstantRanges)
{
	PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};

	for (const auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		pipelineLayoutCreateInfo.AddDescriptorSetLayout(descriptorSetLayout->GetVkDescriptorSetLayout());
	}

	for (const auto& pushConstantRange : inPushConstantRanges)
	{
		pipelineLayoutCreateInfo.AddPushConstantRange(
			pushConstantRange.stageFlags,
			pushConstantRange.offset,
			pushConstantRange.size);
	}

	m_pipelineLayout.Create(&pipelineLayoutCreateInfo);
}

void GraphicsShaderProgram::_CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges)
{
	m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstantRange : inPushConstantRanges)
	{
		m_uptrPushConstant->AddConstantRange(
			pushConstantRange.stageFlags,
			pushConstantRange.offset,
			pushConstantRange.size);
	}
}

size_t GraphicsShaderProgram::_HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const
{
	size_t result = 0;

	hash_combine(result, inStateInfo.m_renderPassPtr != nullptr ? inStateInfo.m_renderPassPtr->GetVkRenderPass() : VK_NULL_HANDLE);
	hash_combine(result, inStateInfo.m_subpassIndex);
	hash_combine(result, inStateInfo.m_vertexBindingDescriptions.size());
	for (const auto& binding : inStateInfo.m_vertexBindingDescriptions)
	{
		hash_combine(result, binding);
	}
	hash_combine(result, inStateInfo.m_vertexAttributeDescriptions.size());
	for (const auto& attribute : inStateInfo.m_vertexAttributeDescriptions)
	{
		hash_combine(result, attribute);
	}

	return result;
}

VkPipeline GraphicsShaderProgram::_CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const
{
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE, "Graphics shader program needs a pipeline layout!");
	CHECK_TRUE(!m_shaderStageInfos.empty(), "Graphics shader program needs shader stages!");
	CHECK_TRUE(inStateInfo.m_renderPassPtr != nullptr, "Graphics pipeline state info needs a render pass!");

	const auto& subpass = inStateInfo.m_renderPassPtr->GetSubpass(inStateInfo.m_subpassIndex);

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(inStateInfo.m_vertexBindingDescriptions.size());
	vertexInputInfo.pVertexBindingDescriptions = inStateInfo.m_vertexBindingDescriptions.data();
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(inStateInfo.m_vertexAttributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = inStateInfo.m_vertexAttributeDescriptions.data();

	std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates = dynamicStates.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyStateInfo.primitiveRestartEnable = VK_FALSE;

	VkPipelineRasterizationStateCreateInfo rasterizerStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizerStateInfo.depthClampEnable = VK_FALSE;
	rasterizerStateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizerStateInfo.depthBiasEnable = VK_FALSE;
	rasterizerStateInfo.depthBiasConstantFactor = 0.0f;
	rasterizerStateInfo.depthBiasClamp = 0.0f;
	rasterizerStateInfo.depthBiasSlopeFactor = 0.0f;
	rasterizerStateInfo.lineWidth = 1.0f;
	rasterizerStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizerStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo multisampleStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleStateInfo.sampleShadingEnable = VK_TRUE;
	multisampleStateInfo.minSampleShading = .2f;
	multisampleStateInfo.pSampleMask = nullptr;
	multisampleStateInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleStateInfo.alphaToOneEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
	if (subpass.optDepthStencilAttachment.has_value())
	{
		VkAttachmentReference depthAtt = subpass.optDepthStencilAttachment.value();
		bool readOnlyDepth = (depthAtt.layout == VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilInfo.depthTestEnable = VK_TRUE;
		depthStencilInfo.depthWriteEnable = readOnlyDepth ? VK_FALSE : VK_TRUE;
		depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
		depthStencilInfo.minDepthBounds = 0.0f;
		depthStencilInfo.maxDepthBounds = 1.0f;
		depthStencilInfo.stencilTestEnable = VK_FALSE;
	}

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
	auto getDefaultColorBlendAttachmentState = []()
	{
		VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};

		colorBlendAttachmentState.blendEnable = VK_TRUE;
		colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;

		return colorBlendAttachmentState;
	};
	colorBlendAttachmentStates.resize(subpass.colorAttachments.size(), getDefaultColorBlendAttachmentState());

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.blendConstants[0] = 0.0f;
	colorBlendStateInfo.blendConstants[1] = 0.0f;
	colorBlendStateInfo.blendConstants[2] = 0.0f;
	colorBlendStateInfo.blendConstants[3] = 0.0f;
	colorBlendStateInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size());
	colorBlendStateInfo.pAttachments = colorBlendAttachmentStates.data();

	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStageInfos.size());
	pipelineInfo.pStages = m_shaderStageInfos.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyStateInfo;
	pipelineInfo.pViewportState = &viewportStateInfo;
	pipelineInfo.pRasterizationState = &rasterizerStateInfo;
	pipelineInfo.pMultisampleState = &multisampleStateInfo;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = m_vkPipelineLayout;
	pipelineInfo.renderPass = inStateInfo.m_renderPassPtr->GetVkRenderPass();
	pipelineInfo.subpass = inStateInfo.m_subpassIndex;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = subpass.optDepthStencilAttachment.has_value() ? &depthStencilInfo : nullptr;

	return MyDevice::GetInstance().CreateGraphicsPipeline(pipelineInfo, m_vkPipelineCache);
}

void GraphicsShaderProgram::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	for (auto& [stateHash, pipeline] : m_cachedGraphicsPipelines)
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			device.DestroyPipeline(pipeline);
		}
	}
	m_cachedGraphicsPipelines.clear();
	for (auto& shaderModule : m_shaderModules)
	{
		shaderModule->Destroy();
	}
	m_shaderModules.clear();
	m_shaderStageInfos.clear();
	m_pipelineLayout.Destroy();
	for (auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		descriptorSetLayout->Destroy();
	}
	m_descriptorSetLayouts.clear();
	m_nameToSetBinding.clear();
	m_vkPipelineCache = VK_NULL_HANDLE;
	m_vkPipelineLayout = VK_NULL_HANDLE;
	m_uptrPushConstant.reset();
}

const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& GraphicsShaderProgram::GetNameToSetBinding() const
{
	return m_nameToSetBinding;
}

VkPipeline GraphicsShaderProgram::GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)
{
	const size_t stateHash = _HashGraphicsPipelineStateInfo(inStateInfo);
	if (const auto cacheIt = m_cachedGraphicsPipelines.find(stateHash); cacheIt != m_cachedGraphicsPipelines.end())
	{
		return cacheIt->second;
	}

	VkPipeline pipeline = _CreateVkPipeline(inStateInfo);
	m_cachedGraphicsPipelines[stateHash] = pipeline;
	return pipeline;
}

void GraphicsShaderProgram::Do(VkCommandBuffer commandBuffer, const PipelineInput_DrawIndexed& input)
{
	// TODO: check m_subpass should match number of vkCmdNextSubpass calls after vkCmdBeginRenderPass
	_DoCommon(commandBuffer, input.imageSize, input.vkDescriptorSets, input.optDynamicOffsets, input.pushConstants);

	CHECK_TRUE(input.vertexBuffers.size() > 0, "Index draw must have vertex buffers."); // I'm not sure about it, check specification someday
	CHECK_TRUE(input.indexBuffer != VK_NULL_HANDLE, "Index buffer must be assigned here.");

	if (input.optVertexBufferOffsets.has_value())
	{
		vkCmdBindVertexBuffers(
			commandBuffer,
			0,
			static_cast<uint32_t>(input.vertexBuffers.size()),
			input.vertexBuffers.data(),
			input.optVertexBufferOffsets.value().data());
	}
	else
	{
		std::vector<VkDeviceSize> vecDummyOffset(input.vertexBuffers.size(), 0);
		vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<uint32_t>(input.vertexBuffers.size()), input.vertexBuffers.data(), vecDummyOffset.data());
	}

	if (input.optIndexBufferOffset.has_value())
	{
		vkCmdBindIndexBuffer(commandBuffer, input.indexBuffer, input.optIndexBufferOffset.value(), input.vkIndexType);
	}
	else
	{
		vkCmdBindIndexBuffer(commandBuffer, input.indexBuffer, 0, input.vkIndexType);
	}

	vkCmdDrawIndexed(commandBuffer, input.indexCount, 1, 0, 0, 0);
}

void GraphicsShaderProgram::Do(VkCommandBuffer commandBuffer, const PipelineInput_Mesh& input)
{
	_DoCommon(commandBuffer, input.imageSize, input.vkDescriptorSets, input.optDynamicOffsets, input.pushConstants);

	vkCmdDrawMeshTasksEXT(commandBuffer, input.groupCountX, input.groupCountY, input.groupCountZ);
}

void GraphicsShaderProgram::Do(VkCommandBuffer commandBuffer, const PipelineInput_Draw& input)
{
	_DoCommon(commandBuffer, input.imageSize, input.vkDescriptorSets, input.optDynamicOffsets, input.pushConstants);

	if (input.vertexBuffers.size() > 0)
	{
		vkCmdBindVertexBuffers(
			commandBuffer,
			0,
			static_cast<uint32_t>(input.vertexBuffers.size()),
			input.vertexBuffers.data(),
			(input.optVertexBufferOffsets.has_value() ? input.optVertexBufferOffsets.value().data() : nullptr)
		);
	}
	vkCmdDraw(commandBuffer, input.vertexCount, 1, 0, 0);
}

void GraphicsShaderProgram::BaseInit::_InitCreateInfos()
{
	m_dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	m_viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	m_viewportStateInfo.viewportCount = 1;
	m_viewportStateInfo.scissorCount = 1;

	m_inputAssemblyStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	m_inputAssemblyStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	m_inputAssemblyStateInfo.primitiveRestartEnable = VK_FALSE;
	
	m_rasterizerStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	m_rasterizerStateInfo.depthClampEnable = VK_FALSE;
	m_rasterizerStateInfo.rasterizerDiscardEnable = VK_FALSE;
	m_rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	m_rasterizerStateInfo.depthBiasEnable = VK_FALSE;//use for shadow mapping
	m_rasterizerStateInfo.depthBiasConstantFactor = 0.0f;
	m_rasterizerStateInfo.depthBiasClamp = 0.0f;
	m_rasterizerStateInfo.depthBiasSlopeFactor = 0.0f;
	m_rasterizerStateInfo.lineWidth = 1.0f; //use values bigger than 1.0, enable wideLines GPU features, extensions
	m_rasterizerStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	m_rasterizerStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;// TODO: otherwise the image won't be drawn if we apply the transform matrix
	
	m_multisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	m_multisampleStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_multisampleStateInfo.sampleShadingEnable = VK_FALSE;
	m_multisampleStateInfo.minSampleShading = 1.0f;
	m_multisampleStateInfo.pSampleMask = nullptr;
	m_multisampleStateInfo.alphaToCoverageEnable = VK_FALSE;
	m_multisampleStateInfo.alphaToOneEnable = VK_FALSE;
	m_multisampleStateInfo.sampleShadingEnable = VK_TRUE; // enable sample shading in the pipeline
	m_multisampleStateInfo.minSampleShading = .2f; // min fraction for sample shading; closer to one is smoother
}

VkPipelineColorBlendAttachmentState GraphicsShaderProgram::BaseInit::_GetDefaultColorBlendAttachmentState() const
{
	VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};
	
	colorBlendAttachmentState.blendEnable = VK_TRUE;
	colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.colorWriteMask = 
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	return colorBlendAttachmentState;
}

GraphicsShaderProgram::BaseInit::BaseInit()
{
	_InitCreateInfos();
}

void GraphicsShaderProgram::BaseInit::InitGraphicsShaderProgram(GraphicsShaderProgram* pPipeline) const
{
	auto& device = MyDevice::GetInstance();
	CHECK_TRUE(m_pipelineLayout != VK_NULL_HANDLE, "Graphics pipeline needs a pipeline layout!");

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertBindingDescriptions.size());
	vertexInputInfo.pVertexBindingDescriptions = m_vertBindingDescriptions.data();
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertAttributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = m_vertAttributeDescriptions.data();

	VkPipelineDynamicStateCreateInfo dynamicStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(m_dynamicStates.size());
	dynamicStateInfo.pDynamicStates = m_dynamicStates.data();

	VkPipelineViewportStateCreateInfo viewportStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.blendConstants[0] = 0.0f;
	colorBlendStateInfo.blendConstants[1] = 0.0f;
	colorBlendStateInfo.blendConstants[2] = 0.0f;
	colorBlendStateInfo.blendConstants[3] = 0.0f;
	colorBlendStateInfo.attachmentCount = static_cast<uint32_t>(m_colorBlendAttachmentStates.size());
	colorBlendStateInfo.pAttachments = m_colorBlendAttachmentStates.data();

	pPipeline->m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstInfo : m_pushConstantInfos)
	{
		pPipeline->m_uptrPushConstant->AddConstantRange(pushConstInfo.stageFlags, pushConstInfo.offset, pushConstInfo.size);
	}
	pPipeline->m_vkPipelineLayout = m_pipelineLayout;

	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStageInfos.size());
	pipelineInfo.pStages = m_shaderStageInfos.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &m_inputAssemblyStateInfo;
	pipelineInfo.pViewportState = &viewportStateInfo;
	pipelineInfo.pRasterizationState = &m_rasterizerStateInfo;
	pipelineInfo.pMultisampleState = &m_multisampleStateInfo;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = pPipeline->m_vkPipelineLayout;
	pipelineInfo.renderPass = m_renderPass;
	pipelineInfo.subpass = m_subpassIndex;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;//only when in graphics pipleininfo VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is set
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = &m_depthStencilInfo;
	pPipeline->m_vkPipeline = device.CreateGraphicsPipeline(pipelineInfo, m_cache);
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::Reset()
{
	*this = GraphicsShaderProgram::BaseInit();
	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::AddShader(const VkPipelineShaderStageCreateInfo& inShaderInfo)
{
	m_shaderStageInfos.push_back(inShaderInfo);

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::AddVertexInputDescription(
	const VkVertexInputBindingDescription& inBindingDescription, 
	const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions)
{
	m_vertBindingDescriptions.push_back(inBindingDescription);
	m_vertAttributeDescriptions.insert(
		m_vertAttributeDescriptions.end(), 
		inAttributeDescriptions.begin(), 
		inAttributeDescriptions.end());

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::SetPipelineLayout(VkPipelineLayout inPipelineLayout)
{
	CHECK_TRUE(inPipelineLayout != VK_NULL_HANDLE, "Invalid graphics pipeline layout!");

	m_pipelineLayout = inPipelineLayout;

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::CustomizeColorAttachmentAsAdd(uint32_t inAttachmentIndex)
{
	if (m_colorBlendAttachmentStates.size() <= inAttachmentIndex)
	{
		m_colorBlendAttachmentStates.resize(inAttachmentIndex + 1, _GetDefaultColorBlendAttachmentState());
	}
	auto& toUpdate = m_colorBlendAttachmentStates[inAttachmentIndex];

	toUpdate.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	toUpdate.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	toUpdate.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	toUpdate.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::AddPushConstant(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize)
{
	VkPushConstantRange info{};

	info.stageFlags = inStages;
	info.offset = inOffset;
	info.size = inSize;

	m_pushConstantInfos.push_back(info);

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_cache = inCache;

	return *this;
}

GraphicsShaderProgram::BaseInit& GraphicsShaderProgram::BaseInit::SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex)
{
	const auto& subpass = inRenderPassPtr->GetSubpass(inSubpassIndex);

	m_colorBlendAttachmentStates.resize(subpass.colorAttachments.size(), _GetDefaultColorBlendAttachmentState());

	if (subpass.optDepthStencilAttachment.has_value())
	{
		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		VkAttachmentReference depthAtt = subpass.optDepthStencilAttachment.value();
		bool readOnlyDepth = (depthAtt.layout == VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = readOnlyDepth ? VK_FALSE : VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.minDepthBounds = 0.0f; // Optional
		depthStencil.maxDepthBounds = 1.0f; // Optional
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = {}; // Optional
		depthStencil.back = {}; // Optional
		m_depthStencilInfo = depthStencil;
	}

	m_subpassIndex = inSubpassIndex;
	m_renderPass = inRenderPassPtr->GetVkRenderPass();

	return *this;
}
