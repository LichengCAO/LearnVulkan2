#include <algorithm>
#include <limits>
#include "../graphics_shader_program.h"
#include "device.h"
#include "command_buffer.h"
#include "../render_pass_desc.h"
#include "utility/hash_util.h"


void GraphicsPipelineStateInfo::Reset()
{
	m_vertexBindingDescriptions.clear();
	m_vertexAttributeDescriptions.clear();
	m_renderPass = {};
	m_subpassIndex = 0;
}

void GraphicsPipelineStateInfo::AddVertexInputDescription(
	const VkVertexInputBindingDescription& inBindingDescription,
	const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions)
{
	m_vertexBindingDescriptions.push_back(inBindingDescription);
	m_vertexAttributeDescriptions.insert(
		m_vertexAttributeDescriptions.end(),
		inAttributeDescriptions.begin(),
		inAttributeDescriptions.end());
}

void GraphicsPipelineStateInfo::SetRenderPassSubpass(const RenderPassDesc& inRenderPass, uint32_t inSubpassIndex)
{
	const auto snapshot = inRenderPass._GetSnapshot();
	CHECK_TRUE(inSubpassIndex < snapshot.GetSubpassCount(),
	    "Invalid render pass/subpass!");
	m_renderPass = snapshot;
	m_subpassIndex = inSubpassIndex;
}

GraphicsShaderProgram::GraphicsShaderProgram() = default;

GraphicsShaderProgram::~GraphicsShaderProgram()
{
	_Destroy();
}

void GraphicsShaderProgram::_Create(const GraphicsShaderProgramCreateInfo& inCreateInfo)
{
	const auto& data = *inCreateInfo.m_finalized;
	ShaderDetail::CreateStages(data.shaders, m_shaderModules, m_shaderStages);
	MyDevice::GetInstance().GetOrCreatePipelineLayout(m_pipelineLayout, data.layout);
	_CreateDefaults(inCreateInfo);
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE);
	CHECK_TRUE(!m_shaderModules.empty());
}

auto GraphicsShaderProgram::_HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo, VkRenderPass inRenderPass) const -> size_t
{
	size_t result = 0;

	hash_combine(result, inRenderPass);
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

auto GraphicsShaderProgram::_CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo, VkRenderPass inRenderPass) const -> VkPipeline
{
	const VkPipelineLayout pipelineLayout = GetLayout().GetVkPipelineLayout();
	CHECK_TRUE(pipelineLayout != VK_NULL_HANDLE, "Graphics shader program needs a pipeline layout!");
	CHECK_TRUE(!m_shaderModules.empty(), "Graphics shader program needs shader stages!");
	CHECK_TRUE(inRenderPass != VK_NULL_HANDLE && inStateInfo.m_renderPass.description, "Graphics pipeline state info needs a render pass!");
    CHECK_TRUE(inStateInfo.m_subpassIndex < inStateInfo.m_renderPass.GetSubpassCount(), "Invalid graphics subpass index!");
    const auto& subpass = inStateInfo.m_renderPass.description->subpassInfos[inStateInfo.m_subpassIndex];

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
	pipelineInfo.renderPass = inRenderPass;
	pipelineInfo.subpass = inStateInfo.m_subpassIndex;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = subpass.hasDepth ? &depthStencilInfo : nullptr;

	return MyDevice::GetInstance().CreateGraphicsPipeline(pipelineInfo, VK_NULL_HANDLE);
}

void GraphicsShaderProgram::_Destroy()
{
	if (!m_cachedGraphicsPipelines.empty())
	{
		auto& device = MyDevice::GetInstance();
		for (auto& [stateHash, entry] : m_cachedGraphicsPipelines)
		{
			if (entry.pipeline != VK_NULL_HANDLE) device.DestroyPipeline(entry.pipeline);
		}
	}
	m_cachedGraphicsPipelines.clear();
	m_defaultState.Reset();
	m_defaultRenderPass.reset();
    m_automaticVertexInputSupported = true;
	m_shaderStages.clear();
	m_shaderModules.clear();
	m_pipelineLayout = {};
}

auto GraphicsShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}

auto GraphicsShaderProgram::GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) -> VkPipeline
{
	const auto state = _ResolveState(inStateInfo);
	const auto renderPass = MyDevice::GetInstance()._GetOrCreateRenderPass(state.m_renderPass);
	const size_t stateHash = _HashGraphicsPipelineStateInfo(state, renderPass);
	if (const auto cacheIt = m_cachedGraphicsPipelines.find(stateHash); cacheIt != m_cachedGraphicsPipelines.end())
	{
		return cacheIt->second.pipeline;
	}

	VkPipeline pipeline = _CreateVkPipeline(state, renderPass);
	m_cachedGraphicsPipelines[stateHash] = {pipeline, state.m_renderPass};
	return pipeline;
}

void GraphicsShaderProgram::_CreateDefaults(const GraphicsShaderProgramCreateInfo& info)
{
    const auto& data = *info.m_finalized;
    m_defaultState.m_vertexBindingDescriptions = data.vertexBindings;
    m_defaultState.m_vertexAttributeDescriptions = data.vertexAttributes;
    m_automaticVertexInputSupported = data.automaticVertexInputSupported;
    if (!data.defaultRenderPass) return;
    m_defaultRenderPass = data.defaultRenderPass;
    m_defaultState.m_renderPass = m_defaultRenderPass->_GetSnapshot();
    if (m_automaticVertexInputSupported) GetVkPipeline({});
}

auto GraphicsShaderProgram::_ResolveState(const GraphicsPipelineStateInfo& input) const -> GraphicsPipelineStateInfo
{
	auto state = input;
	if (!state.m_renderPass.description)
		state.m_renderPass = m_defaultState.m_renderPass;
	CHECK_TRUE(state.m_renderPass.description,
	    "Provide an explicit render pass or configure default attachments!");
	if (state.m_vertexBindingDescriptions.empty() && state.m_vertexAttributeDescriptions.empty())
	{
		CHECK_TRUE(m_automaticVertexInputSupported, "Shader inputs require an explicit vertex layout!");
		state.m_vertexBindingDescriptions = m_defaultState.m_vertexBindingDescriptions;
		state.m_vertexAttributeDescriptions = m_defaultState.m_vertexAttributeDescriptions;
	}
	return state;
}
auto GraphicsShaderProgram::GetDefaultRenderPassDesc() const -> const RenderPassDesc&
{
	CHECK_TRUE(m_defaultRenderPass != nullptr, "No default render pass was configured!");
	return *m_defaultRenderPass;
}
