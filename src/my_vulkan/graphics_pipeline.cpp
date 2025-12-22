#include "graphics_pipeline.h"
#include "device.h"
#include "push_constant_manager.h"

void GraphicsPipeline::_InitCreateInfos()
{
}

void GraphicsPipeline::_DoCommon(
	VkCommandBuffer cmd,
	const VkExtent2D& imageSize,
	const std::vector<VkDescriptorSet>& pSets,
	const std::vector<uint32_t>& _dynamicOffsets,
	const std::vector<std::pair<VkShaderStageFlags, const void*>>& pushConstants)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);

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
			vkPipelineLayout,
			0,
			static_cast<uint32_t>(pSets.size()),
			pSets.data(),
			static_cast<uint32_t>(_dynamicOffsets.size()),
			_dynamicOffsets.data());
	}

	for (const auto& _pushConst : pushConstants)
	{
		m_pushConstant.PushConstant(cmd, vkPipelineLayout, _pushConst.first, _pushConst.second);
	}
}

GraphicsPipeline::GraphicsPipeline()
{
	_InitCreateInfos();
}

GraphicsPipeline::~GraphicsPipeline()
{
	assert(vkPipeline == VK_NULL_HANDLE);
	assert(vkPipelineLayout == VK_NULL_HANDLE);
}

void GraphicsPipeline::AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_shaderStageInfos.push_back(shaderInfo);
}

void GraphicsPipeline::AddVertexInputLayout(const VertexInputLayout* pVertLayout)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	uint32_t binding = static_cast<uint32_t>(m_vertBindingDescriptions.size());
	m_vertBindingDescriptions.push_back(pVertLayout->GetVertexInputBindingDescription(binding));
	auto attributeDescriptions = pVertLayout->GetVertexInputAttributeDescriptions(binding);
	for (int i = 0; i < attributeDescriptions.size(); ++i)
	{
		m_vertAttributeDescriptions.push_back(attributeDescriptions[i]);
	}
}

void GraphicsPipeline::AddVertexInputLayout(const VkVertexInputBindingDescription& _bindingDescription, const std::vector<VkVertexInputAttributeDescription>& _attributeDescriptions)
{
	m_vertBindingDescriptions.push_back(_bindingDescription);
	m_vertAttributeDescriptions.insert(m_vertAttributeDescriptions.end(), _attributeDescriptions.begin(), _attributeDescriptions.end());
}

void GraphicsPipeline::AddDescriptorSetLayout(const DescriptorSetLayout* pSetLayout)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_descriptorSetLayouts.push_back(pSetLayout->m_vkDescriptorSetLayout);
}

void GraphicsPipeline::AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_descriptorSetLayouts.push_back(vkDSetLayout);
}

void GraphicsPipeline::BindToSubpass(const RenderPass* pRenderPass, uint32_t subpass)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_pRenderPass = pRenderPass;
	m_subpass = subpass;
	if (pRenderPass->subpasses[subpass].colorAttachments.size() > 0)
	{
		uint32_t attachmentId = pRenderPass->subpasses[subpass].colorAttachments[0].attachment;
		multisampleStateInfo.rasterizationSamples = pRenderPass->attachments[attachmentId].attachmentDescription.samples;
	}
	if (pRenderPass->subpasses[subpass].optDepthStencilAttachment.has_value())
	{
		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		VkAttachmentReference depthAtt = pRenderPass->subpasses[subpass].optDepthStencilAttachment.value();
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
	VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};
	colorBlendAttachmentState.blendEnable = VK_TRUE;
	colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;
	int colorAttachmentCount = m_pRenderPass->subpasses[m_subpass].colorAttachments.size();
	m_colorBlendAttachmentStates.reserve(colorAttachmentCount);
	for (int i = 0; i < colorAttachmentCount; ++i)
	{
		m_colorBlendAttachmentStates.push_back(colorBlendAttachmentState);
	}
}

void GraphicsPipeline::SetColorAttachmentAsAdd(int idx)
{
	m_colorBlendAttachmentStates[idx].dstAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachmentStates[idx].srcAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachmentStates[idx].dstColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachmentStates[idx].srcColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
}

void GraphicsPipeline::PreSetPipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;
}

void GraphicsPipeline::Init()
{
	
}

void GraphicsPipeline::Uninit()
{
	auto& device = MyDevice::GetInstance();
	if (vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(vkPipeline);
		vkPipeline = VK_NULL_HANDLE;
	}
	if (vkPipelineLayout != VK_NULL_HANDLE)
	{
		device.DestroyPipelineLayout(vkPipelineLayout);
		vkPipelineLayout = VK_NULL_HANDLE;
	}
	m_pRenderPass = nullptr;
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

void GraphicsPipeline::AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	m_pushConstant.AddConstantRange(_stages, _offset, _size);
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
	for (const auto& pushConstInfo : m_pushConstantInfos)
	{
		pPipeline->m_pushConstant->AddConstantRange(pushConstInfo.stages, pushConstInfo.offset, pushConstInfo.size);
	}
	pPipeline->m_pushConstant->GetVkPushConstantRanges(pushConstantRanges);
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

	pPipeline->vkPipelineLayout = device.CreatePipelineLayout(pipelineLayoutInfo);

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
	pipelineInfo.layout = pPipeline->vkPipelineLayout;
	pipelineInfo.renderPass = m_pRenderPass->m_vkRenderPass;
	pipelineInfo.subpass = m_subpass;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;//only when in graphics pipleininfo VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is set
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = &m_depthStencilInfo;
	pPipeline->vkPipeline = device.CreateGraphicsPipeline(pipelineInfo, m_cache);
}
