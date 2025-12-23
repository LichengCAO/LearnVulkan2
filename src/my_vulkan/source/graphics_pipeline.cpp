#include "graphics_pipeline.h"
#include "device.h"
#include "push_constant_manager.h"
#include "render_pass.h"

void GraphicsPipeline::_DoCommon(
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

GraphicsPipeline::~GraphicsPipeline()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void GraphicsPipeline::Init(const IGraphicsPipelineInitializer* pInitializer)
{
	pInitializer->InitGraphicsPipeline(this);

	// check everything initialized
	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
}

void GraphicsPipeline::Uninit()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	if (m_vkPipelineLayout != VK_NULL_HANDLE)
	{
		device.DestroyPipelineLayout(m_vkPipelineLayout);
		m_vkPipelineLayout = VK_NULL_HANDLE;
	}
	m_uptrPushConstant.reset();
}

void GraphicsPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput_DrawIndexed& input)
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

void GraphicsPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput_Mesh& input)
{
	_DoCommon(commandBuffer, input.imageSize, input.vkDescriptorSets, input.optDynamicOffsets, input.pushConstants);

	vkCmdDrawMeshTasksEXT(commandBuffer, input.groupCountX, input.groupCountY, input.groupCountZ);
}

void GraphicsPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput_Draw& input)
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

void GraphicsPipeline::BaseInit::_InitCreateInfos()
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

VkPipelineColorBlendAttachmentState GraphicsPipeline::BaseInit::_GetDefaultColorBlendAttachmentState() const
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

GraphicsPipeline::BaseInit::BaseInit()
{
	_InitCreateInfos();
}

void GraphicsPipeline::BaseInit::InitGraphicsPipeline(GraphicsPipeline* pPipeline) const
{
	auto& device = MyDevice::GetInstance();

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

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	std::vector<VkPushConstantRange> pushConstantRanges{};
	pPipeline->m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstInfo : m_pushConstantInfos)
	{
		pPipeline->m_uptrPushConstant->AddConstantRange(pushConstInfo.stageFlags, pushConstInfo.offset, pushConstInfo.size);
	}
	pPipeline->m_uptrPushConstant->GetVkPushConstantRanges(pushConstantRanges);
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

	pPipeline->m_vkPipelineLayout = device.CreatePipelineLayout(pipelineLayoutInfo);

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

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::Reset()
{
	*this = GraphicsPipeline::BaseInit();
	return *this;
}

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::AddShader(const VkPipelineShaderStageCreateInfo& inShaderInfo)
{
	m_shaderStageInfos.push_back(inShaderInfo);

	return *this;
}

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::AddVertexInputDescription(
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

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::AddDescriptorSetLayout(VkDescriptorSetLayout inDescriptorSetLayout)
{
	m_descriptorSetLayouts.push_back(inDescriptorSetLayout);

	return *this;
}

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::CustomizeColorAttachmentAsAdd(uint32_t inAttachmentIndex)
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

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::AddPushConstant(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize)
{
	VkPushConstantRange info{};

	info.stageFlags = inStages;
	info.offset = inOffset;
	info.size = inSize;

	m_pushConstantInfos.push_back(info);

	return *this;
}

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_cache = inCache;

	return *this;
}

GraphicsPipeline::BaseInit& GraphicsPipeline::BaseInit::SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex)
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
