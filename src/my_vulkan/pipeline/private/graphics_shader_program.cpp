#include <algorithm>
#include <limits>
#include "../graphics_shader_program.h"
#include "device.h"
#include "command_buffer.h"
#include "../render_pass.h"
#include "utility/hash_util.h"

#if 0
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
#endif

auto GraphicsShaderProgramCreateInfo::Reset() -> GraphicsShaderProgramCreateInfo&
{
	m_shaders.clear();
	m_defaultColors.clear();
	m_defaultDepth.reset();
	return *this;
}

auto GraphicsShaderProgramCreateInfo::_AddShader(std::string_view file, VkShaderStageFlagBits stage) -> GraphicsShaderProgramCreateInfo&
{
	CHECK_TRUE(std::none_of(m_shaders.begin(), m_shaders.end(), [stage](const auto& shader) -> bool
	    { return shader.stage == stage; }), "Duplicate graphics shader stage!");
	m_shaders.push_back(ShaderDetail::MakeRequest(file, stage));
	return *this;
}

auto GraphicsShaderProgramCreateInfo::AddVertexShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{
	return _AddShader(file, VK_SHADER_STAGE_VERTEX_BIT);
}

auto GraphicsShaderProgramCreateInfo::AddFragmentShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{
	return _AddShader(file, VK_SHADER_STAGE_FRAGMENT_BIT);
}

auto GraphicsShaderProgramCreateInfo::AddGeometryShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{
	return _AddShader(file, VK_SHADER_STAGE_GEOMETRY_BIT);
}

auto GraphicsShaderProgramCreateInfo::AddTaskShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{
	return _AddShader(file, VK_SHADER_STAGE_TASK_BIT_EXT);
}

auto GraphicsShaderProgramCreateInfo::AddMeshShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{
	return _AddShader(file, VK_SHADER_STAGE_MESH_BIT_EXT);
}

auto GraphicsPipelineStateInfo::Reset() -> GraphicsPipelineStateInfo&
{
	m_vertexBindingDescriptions.clear();
	m_vertexAttributeDescriptions.clear();
	m_renderPassPtr = nullptr;
	m_subpassIndex = 0;
	return *this;
}

auto GraphicsPipelineStateInfo::AddVertexInputDescription(
	const VkVertexInputBindingDescription& inBindingDescription,
	const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions) -> GraphicsPipelineStateInfo&
{
	m_vertexBindingDescriptions.push_back(inBindingDescription);
	m_vertexAttributeDescriptions.insert(
		m_vertexAttributeDescriptions.end(),
		inAttributeDescriptions.begin(),
		inAttributeDescriptions.end());

	return *this;
}

auto GraphicsPipelineStateInfo::SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex) -> GraphicsPipelineStateInfo&
{
	CHECK_TRUE(inRenderPassPtr != nullptr, "Graphics pipeline state info needs a render pass!");

	m_renderPassPtr = inRenderPassPtr;
	m_subpassIndex = inSubpassIndex;
	return *this;
}

GraphicsShaderProgram::GraphicsShaderProgram() = default;

GraphicsShaderProgram::~GraphicsShaderProgram()
{
	assert(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE);
	assert(m_shaderModules.empty());
}

void GraphicsShaderProgram::Create(const GraphicsShaderProgramCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No graphics shader program create info!");
	CHECK_TRUE(!inCreateInfo->m_shaders.empty(), "Graphics shader program needs shaders!");
	CHECK_TRUE(m_cachedGraphicsPipelines.empty(), "Graphics shader program already has cached pipelines!");
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE, "Graphics shader program already created!");

	auto hasStage = [&](VkShaderStageFlagBits stage) -> bool
	{
		return std::any_of(inCreateInfo->m_shaders.begin(), inCreateInfo->m_shaders.end(),
		    [stage](const auto& shader) -> bool { return shader.stage == stage; });
	};
	const bool vertex = hasStage(VK_SHADER_STAGE_VERTEX_BIT);
	const bool geometry = hasStage(VK_SHADER_STAGE_GEOMETRY_BIT);
	const bool mesh = hasStage(VK_SHADER_STAGE_MESH_BIT_EXT);
	const bool task = hasStage(VK_SHADER_STAGE_TASK_BIT_EXT);
	CHECK_TRUE(!(vertex || geometry) || !(mesh || task), "Traditional and mesh graphics stages cannot be mixed!");
	CHECK_TRUE(!task || mesh, "Task shader requires a mesh shader!");
	CHECK_TRUE(vertex || mesh, "Graphics program requires a vertex or mesh shader!");
	try
	{
		auto resolved = ShaderDetail::Resolve(inCreateInfo->m_shaders, m_shaderModules);
		m_shaderStages = std::move(resolved.stages);
		m_pipelineLayout.Create(resolved.reflection);
		_CreateDefaults(*inCreateInfo, resolved.reflection);
	}
	catch (...)
	{
		Destroy();
		throw;
	}
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE);
	CHECK_TRUE(!m_shaderModules.empty());
}

auto GraphicsShaderProgram::_HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const -> size_t
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

auto GraphicsShaderProgram::_CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const -> VkPipeline
{
	const VkPipelineLayout pipelineLayout = GetLayout().GetVkPipelineLayout();
	CHECK_TRUE(pipelineLayout != VK_NULL_HANDLE, "Graphics shader program needs a pipeline layout!");
	CHECK_TRUE(!m_shaderModules.empty(), "Graphics shader program needs shader stages!");
	CHECK_TRUE(inStateInfo.m_renderPassPtr != nullptr, "Graphics pipeline state info needs a render pass!");
    CHECK_TRUE(inStateInfo.m_subpassIndex < inStateInfo.m_renderPassPtr->m_subpassInfos.size(), "Invalid graphics subpass index!");
    const auto& subpass = inStateInfo.m_renderPassPtr->m_subpassInfos[inStateInfo.m_subpassIndex];

	const auto& shaderStageInfos = m_shaderStages;
	CHECK_TRUE(!shaderStageInfos.empty(), "Graphics shader program needs shader stages!");

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
	multisampleStateInfo.rasterizationSamples = subpass.samples;
	multisampleStateInfo.sampleShadingEnable = VK_FALSE;
	multisampleStateInfo.minSampleShading = .2f;
	multisampleStateInfo.pSampleMask = nullptr;
	multisampleStateInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleStateInfo.alphaToOneEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportStateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencilInfo.depthTestEnable = subpass.hasDepth;
    depthStencilInfo.depthWriteEnable = subpass.hasDepth;
    depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
	auto getDefaultColorBlendAttachmentState = []()
	{
		VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};

		colorBlendAttachmentState.blendEnable = VK_FALSE;
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
	colorBlendAttachmentStates.resize(subpass.colorSlotCount, getDefaultColorBlendAttachmentState());

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
	pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageInfos.size());
	pipelineInfo.pStages = shaderStageInfos.data();
	const bool mesh = std::any_of(m_shaderStages.begin(), m_shaderStages.end(), [](const auto& stage) -> bool
	    { return stage.stage == VK_SHADER_STAGE_MESH_BIT_EXT; });
	pipelineInfo.pVertexInputState = mesh ? nullptr : &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = mesh ? nullptr : &inputAssemblyStateInfo;
	pipelineInfo.pViewportState = &viewportStateInfo;
	pipelineInfo.pRasterizationState = &rasterizerStateInfo;
	pipelineInfo.pMultisampleState = &multisampleStateInfo;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = inStateInfo.m_renderPassPtr->GetVkRenderPass();
	pipelineInfo.subpass = inStateInfo.m_subpassIndex;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = subpass.hasDepth ? &depthStencilInfo : nullptr;

	return MyDevice::GetInstance().CreateGraphicsPipeline(pipelineInfo, VK_NULL_HANDLE);
}

void GraphicsShaderProgram::Destroy()
{
	auto& device = MyDevice::GetInstance();
	for (auto& [stateHash, pipeline] : m_cachedGraphicsPipelines)
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			device.DestroyPipeline(pipeline);
		}
	}
	m_cachedGraphicsPipelines.clear();
    if (m_defaultRenderPass) { m_defaultRenderPass->Destroy(); m_defaultRenderPass.reset(); }
    m_defaultState.Reset();
    m_automaticVertexInputSupported = true;
	for (auto& shaderModule : m_shaderModules)
	{
		shaderModule->Destroy();
	}
	m_shaderStages.clear();
	m_shaderModules.clear();
	m_pipelineLayout.Destroy();
}

auto GraphicsShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}

auto GraphicsShaderProgram::GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) -> VkPipeline
{
	const auto state = _ResolveState(inStateInfo);
	const size_t stateHash = _HashGraphicsPipelineStateInfo(state);
	if (const auto cacheIt = m_cachedGraphicsPipelines.find(stateHash); cacheIt != m_cachedGraphicsPipelines.end())
	{
		return cacheIt->second;
	}

	VkPipeline pipeline = _CreateVkPipeline(state);
	m_cachedGraphicsPipelines[stateHash] = pipeline;
	return pipeline;
}

namespace
{
	auto VertexFormatSize(VkFormat format) -> uint32_t
	{
		if (format >= VK_FORMAT_R16_UINT && format <= VK_FORMAT_R16_SFLOAT)
			return 2;
		if (format >= VK_FORMAT_R16G16_UINT && format <= VK_FORMAT_R16G16_SFLOAT)
			return 4;
		if (format >= VK_FORMAT_R16G16B16_UINT && format <= VK_FORMAT_R16G16B16_SFLOAT)
			return 6;
		if (format >= VK_FORMAT_R16G16B16A16_UINT && format <= VK_FORMAT_R16G16B16A16_SFLOAT)
			return 8;
		if (format >= VK_FORMAT_R32_UINT && format <= VK_FORMAT_R32G32B32A32_SFLOAT)
			return 4 * (1 + (static_cast<uint32_t>(format) - VK_FORMAT_R32_UINT) / 3);
		// 64-bit inputs have extra location rules: use an explicit vertex layout.
		return 0;
	}
	auto IsDepthFormat(VkFormat format) -> bool
	{
		return format >= VK_FORMAT_D16_UNORM && format <= VK_FORMAT_D32_SFLOAT_S8_UINT && format != VK_FORMAT_S8_UINT;
	}
	auto NumericCategory(VkFormat format) -> uint32_t
	{
		switch (format)
		{
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_A8B8G8R8_UINT_PACK32:
		case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16G16_UINT:
		case VK_FORMAT_R16G16B16_UINT:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32G32_UINT:
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32A32_UINT:
			return 1;
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8G8_SINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_A8B8G8R8_SINT_PACK32:
		case VK_FORMAT_A2R10G10B10_SINT_PACK32:
		case VK_FORMAT_A2B10G10R10_SINT_PACK32:
		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16G16_SINT:
		case VK_FORMAT_R16G16B16_SINT:
		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32G32_SINT:
		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32A32_SINT:
			return 2;
		default:
			return 0;
		}
	}
} // namespace
auto GraphicsShaderProgramCreateInfo::SetDefaultColorAttachment(
    uint32_t location, VkFormat format, VkImageLayout finalLayout) -> GraphicsShaderProgramCreateInfo&
{
	CHECK_TRUE(format != VK_FORMAT_UNDEFINED && format != VK_FORMAT_MAX_ENUM && !IsDepthFormat(format) &&
	               format != VK_FORMAT_S8_UINT,
	    "Invalid color attachment format!");
	CHECK_TRUE(finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
	               finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR || finalLayout == VK_IMAGE_LAYOUT_GENERAL ||
	               finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
	               finalLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	    "Unsupported default color final layout!");
	m_defaultColors[location] = {format, finalLayout};
	return *this;
}
auto GraphicsShaderProgramCreateInfo::SetDefaultDepthAttachment(VkFormat format) -> GraphicsShaderProgramCreateInfo&
{
	CHECK_TRUE(IsDepthFormat(format), "Invalid depth attachment format!");
	m_defaultDepth = format;
	return *this;
}
void GraphicsShaderProgram::_CreateDefaults(const GraphicsShaderProgramCreateInfo& info,
    const ShaderProgramReflection& reflection)
{
	const auto byLocation = [](const auto& left, const auto& right) -> bool { return left.location < right.location; };
	auto inputs = reflection.GetInput(VK_SHADER_STAGE_VERTEX_BIT);
	std::sort(inputs.begin(), inputs.end(), byLocation);
	uint32_t stride = 0;
	for (size_t i = 0; i < inputs.size(); ++i)
	{
		const auto& [name, location, format] = inputs[i];
		const uint32_t size = VertexFormatSize(format);
		if (!size || (i > 0 && inputs[i - 1].location == location))
		{
			m_automaticVertexInputSupported = false;
			break;
		}
		m_defaultState.m_vertexAttributeDescriptions.push_back({location, 0, format, stride});
		stride += size;
	}
	if (stride && m_automaticVertexInputSupported)
		m_defaultState.m_vertexBindingDescriptions.push_back({0, stride, VK_VERTEX_INPUT_RATE_VERTEX});
	if (info.m_defaultColors.empty() && !info.m_defaultDepth)
		return;

	auto outputs = reflection.GetOutput(VK_SHADER_STAGE_FRAGMENT_BIT);
	std::sort(outputs.begin(), outputs.end(), byLocation);
	CHECK_TRUE(std::adjacent_find(outputs.begin(), outputs.end(),
	    [](const auto& left, const auto& right) -> bool { return left.location == right.location; }) == outputs.end(),
	    "Default render pass requires unique fragment output locations!");
	CHECK_TRUE(
	    outputs.size() == info.m_defaultColors.size(), "Default color attachments must cover all fragment outputs!");
	RenderPassCreateInfo passInfo;
	SubpassDescription subpass;
	for (const auto& output : outputs)
	{
		const auto location = output.location;
		const auto format = output.format;
		const auto it = info.m_defaultColors.find(location);
		CHECK_TRUE(it != info.m_defaultColors.end(), "Missing default color attachment location!");
		CHECK_TRUE(format != VK_FORMAT_UNDEFINED, "Default render pass requires simple fragment outputs!");
		CHECK_TRUE(NumericCategory(format) == NumericCategory(it->second.format),
		    "Fragment output and attachment numeric types differ!");
		const std::string name = output.name.empty() ? "color" + std::to_string(location) : output.name;
		AttachmentDescription attachment;
		VkClearValue clear{};
		attachment.CustomizeFormat(it->second.format, clear);
		attachment.CustomizeFinalLayout(it->second.finalLayout);
		passInfo.AddAttachment(name, attachment);
		subpass.AddColorAttachment(location, name);
	}
	if (info.m_defaultDepth)
	{
		AttachmentDescription attachment;
		VkClearValue clear{};
		clear.depthStencil = {1.0f, 0};
		attachment.CustomizeFormat(*info.m_defaultDepth, clear);
		attachment.CustomizeFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		passInfo.AddAttachment("__depth", attachment);
		subpass.AddDepthStencilAttachment("__depth");
	}
	VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkAccessFlags access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	if (info.m_defaultDepth)
	{
		stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	subpass.CustomizeAvailableState(stages, access);
	subpass.AddExternalDependency(stages, access);
	passInfo.AddSubpass("default", subpass);
	m_defaultRenderPass = std::make_unique<RenderPass>();
	m_defaultRenderPass->Create(&passInfo);
	m_defaultState.m_renderPassPtr = m_defaultRenderPass.get();
	// Complex vertex layouts remain available through the explicit pipeline-state path.
	if (m_automaticVertexInputSupported)
		GetVkPipeline({});
}
auto GraphicsShaderProgram::_ResolveState(const GraphicsPipelineStateInfo& input) const -> GraphicsPipelineStateInfo
{
	auto state = input;
	if (!state.m_renderPassPtr)
		state.m_renderPassPtr = m_defaultRenderPass.get();
	CHECK_TRUE(state.m_renderPassPtr != nullptr, "Provide an explicit render pass or configure default attachments!");
	if (state.m_vertexBindingDescriptions.empty() && state.m_vertexAttributeDescriptions.empty())
	{
		CHECK_TRUE(m_automaticVertexInputSupported, "Shader inputs require an explicit vertex layout!");
		state.m_vertexBindingDescriptions = m_defaultState.m_vertexBindingDescriptions;
		state.m_vertexAttributeDescriptions = m_defaultState.m_vertexAttributeDescriptions;
	}
	return state;
}
auto GraphicsShaderProgram::GetDefaultRenderPass() const -> const RenderPass&
{
	CHECK_TRUE(m_defaultRenderPass != nullptr, "No default render pass was configured!");
	return *m_defaultRenderPass;
}
