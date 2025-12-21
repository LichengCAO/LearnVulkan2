#include "pipeline.h"
#include "device.h"
#include "buffer.h"
#include "pipeline_io.h"
#include "utils.h"
#include <fstream>

PushConstantManager::PushConstantManager()
{
}

std::vector<VkShaderStageFlagBits> PushConstantManager::_GetBitsFromStageFlags(VkShaderStageFlags _flags)
{
	std::vector<VkShaderStageFlagBits> result;
	constexpr VkShaderStageFlagBits stagesToCheck[] =
	{
		VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
		VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
		VK_SHADER_STAGE_GEOMETRY_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		VK_SHADER_STAGE_MISS_BIT_KHR,
		VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
		VK_SHADER_STAGE_CALLABLE_BIT_KHR,
		VK_SHADER_STAGE_TASK_BIT_EXT,
		VK_SHADER_STAGE_MESH_BIT_EXT,
		VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI,
		VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI,
	};
	size_t n = sizeof(stagesToCheck) / sizeof(stagesToCheck[0]);

	if (_flags == VK_SHADER_STAGE_ALL)
	{
		result = { VK_SHADER_STAGE_ALL };
	}
	else if (_flags == VK_SHADER_STAGE_ALL_GRAPHICS)
	{
		result = { VK_SHADER_STAGE_ALL_GRAPHICS };
	}
	else
	{
		for (size_t i = 0; i < n; ++i)
		{
			if ((_flags & stagesToCheck[i]) != 0)
			{
				result.push_back(stagesToCheck[i]);
			}
		}
	}

	return result;
}

void PushConstantManager::AddConstantRange(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	uint32_t sizeAligned = common_utils::AlignUp(_size, 4); // Both offset and size are in units of bytes and must be a multiple of 4.
	CHECK_TRUE((m_usedStages & _stages) == 0, "Some stage already used!");
	m_usedStages = m_usedStages | _stages;
	m_currentSize += sizeAligned;
	CHECK_TRUE(m_currentSize <= 128, "Push constant cannot be larger than 128 bytes!");
	m_mapRange.insert({ _stages, { _offset, _size } });
}

void PushConstantManager::PushConstant(VkCommandBuffer _cmd, VkPipelineLayout _layout, VkShaderStageFlags _stage, const void* _data) const
{
	const auto& storedData = m_mapRange.at(_stage);
	uint32_t uSize = storedData.second;
	uint32_t uOffset = storedData.first;

	vkCmdPushConstants(_cmd, _layout, _stage, uOffset, uSize, _data);
}

void PushConstantManager::GetVkPushConstantRanges(std::vector<VkPushConstantRange>& _outRanges) const
{
	for (const auto& p : m_mapRange)
	{
		VkPushConstantRange range{};

		range.offset = p.second.first;
		range.size = p.second.second;
		range.stageFlags = p.first;

		_outRanges.push_back(std::move(range));
	}
}

void GraphicsPipeline::_InitCreateInfos()
{
	m_dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	
	m_viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	m_viewportStateInfo.viewportCount = 1;
	m_viewportStateInfo.scissorCount = 1;

	m_colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	m_colorBlendStateInfo.logicOpEnable = VK_FALSE;
	m_colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	m_colorBlendStateInfo.blendConstants[0] = 0.0f;
	m_colorBlendStateInfo.blendConstants[1] = 0.0f;
	m_colorBlendStateInfo.blendConstants[2] = 0.0f;
	m_colorBlendStateInfo.blendConstants[3] = 0.0f;

	inputAssemblyStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyStateInfo.primitiveRestartEnable = VK_FALSE;

	rasterizerStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerStateInfo.depthClampEnable = VK_FALSE;
	rasterizerStateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizerStateInfo.depthBiasEnable = VK_FALSE;//use for shadow mapping
	rasterizerStateInfo.depthBiasConstantFactor = 0.0f;
	rasterizerStateInfo.depthBiasClamp = 0.0f;
	rasterizerStateInfo.depthBiasSlopeFactor = 0.0f;
	rasterizerStateInfo.lineWidth = 1.0f; //use values bigger than 1.0, enable wideLines GPU features, extensions
	rasterizerStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizerStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;// TODO: otherwise the image won't be drawn if we apply the transform matrix

	multisampleStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleStateInfo.sampleShadingEnable = VK_FALSE;
	multisampleStateInfo.minSampleShading = 1.0f;
	multisampleStateInfo.pSampleMask = nullptr;
	multisampleStateInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleStateInfo.alphaToOneEnable = VK_FALSE;
	multisampleStateInfo.sampleShadingEnable = VK_TRUE; // enable sample shading in the pipeline
	multisampleStateInfo.minSampleShading = .2f; // min fraction for sample shading; closer to one is smoother
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
	VkPipelineColorBlendAttachmentState colorBlendAttachmentState {};
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

	m_colorBlendStateInfo.attachmentCount = static_cast<uint32_t>(m_colorBlendAttachmentStates.size());
	m_colorBlendStateInfo.pAttachments = m_colorBlendAttachmentStates.data();

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	std::vector<VkPushConstantRange> pushConstantRanges{};
	m_pushConstant.GetVkPushConstantRanges(pushConstantRanges);
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
	CHECK_TRUE(vkPipelineLayout == VK_NULL_HANDLE, "VkPipelineLayout is already created!");
	
	device.CreatePipelineLayout(pipelineLayoutInfo);

	VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStageInfos.size());
	pipelineInfo.pStages = m_shaderStageInfos.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyStateInfo;
	pipelineInfo.pViewportState = &viewportStateInfo;
	pipelineInfo.pRasterizationState = &rasterizerStateInfo;
	pipelineInfo.pMultisampleState = &multisampleStateInfo;
	pipelineInfo.pColorBlendState = &m_colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.renderPass = m_pRenderPass->vkRenderPass;
	pipelineInfo.subpass = m_subpass;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;//only when in graphics pipleininfo VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is set
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.pDepthStencilState = &m_depthStencilInfo;
	CHECK_TRUE(vkPipeline == VK_NULL_HANDLE, "VkPipeline is already created!");
	vkPipeline = device.CreateGraphicsPipeline(pipelineInfo, m_vkPipelineCache);
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

ComputePipeline::~ComputePipeline()
{
	assert(vkPipeline == VK_NULL_HANDLE);
	assert(vkPipelineLayout == VK_NULL_HANDLE);
}

void ComputePipeline::AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_shaderStageInfo = shaderInfo;
}

void ComputePipeline::AddDescriptorSetLayout(const DescriptorSetLayout* pSetLayout)
{
	AddDescriptorSetLayout(pSetLayout->m_vkDescriptorSetLayout);
}

void ComputePipeline::AddDescriptorSetLayout(VkDescriptorSetLayout _vkLayout)
{
	assert(vkPipeline == VK_NULL_HANDLE);
	m_descriptorSetLayouts.push_back(_vkLayout);
}

void ComputePipeline::PreSetPipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;
}

void ComputePipeline::Init()
{
	auto& device = MyDevice::GetInstance();
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	std::vector<VkPushConstantRange> pushConstantRanges{};
	m_pushConstant.GetVkPushConstantRanges(pushConstantRanges);
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	CHECK_TRUE(vkPipelineLayout == VK_NULL_HANDLE, "VkPipelineLayout is already created!");

	device.CreatePipelineLayout(pipelineLayoutInfo);

	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.stage = m_shaderStageInfo;
	pipelineInfo.layout = vkPipelineLayout;
	CHECK_TRUE(vkPipeline == VK_NULL_HANDLE, "VkPipeline is already created!");

	vkPipeline = device.CreateComputePipeline(pipelineInfo, m_vkPipelineCache);
}

void ComputePipeline::Uninit()
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
}

void ComputePipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
	vkCmdBindDescriptorSets(
		commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		vkPipelineLayout,
		0,
		static_cast<uint32_t>(input.vkDescriptorSets.size()),
		input.vkDescriptorSets.data(),
		static_cast<uint32_t>(input.optDynamicOffsets.size()),
		input.optDynamicOffsets.data());

	for (const auto& _pushConst : input.pushConstants)
	{
		m_pushConstant.PushConstant(commandBuffer, vkPipelineLayout, _pushConst.first, _pushConst.second);
	}

	vkCmdDispatch(commandBuffer, input.groupCountX, input.groupCountY, input.groupCountZ);
}

void ComputePipeline::AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	m_pushConstant.AddConstantRange(_stages, _offset, _size);
}

RayTracingPipeline::RayTracingPipeline()
{
}

RayTracingPipeline::~RayTracingPipeline()
{
	assert(vkPipeline == VK_NULL_HANDLE);
	assert(vkPipelineLayout == VK_NULL_HANDLE);
}

uint32_t RayTracingPipeline::AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	uint32_t uIndex = ~0;
	assert(vkPipeline == VK_NULL_HANDLE);
	uIndex = static_cast<uint32_t>(m_shaderStageInfos.size());
	m_shaderStageInfos.push_back(shaderInfo);
	return uIndex;
}

void RayTracingPipeline::AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout)
{
	m_descriptorSetLayouts.push_back(vkDSetLayout);
}

uint32_t RayTracingPipeline::SetRayGenerationShaderRecord(uint32_t rayGen)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());
	
	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = rayGen;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	m_SBT.SetRayGenRecord(uRet);
	return uRet;
}

uint32_t RayTracingPipeline::AddTriangleHitShaderRecord(uint32_t closestHit, uint32_t anyHit)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	shaderRecord.generalShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.closestHitShader = closestHit;
	shaderRecord.anyHitShader = anyHit;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	m_SBT.AddHitRecord(uRet);
	return uRet;
}

uint32_t RayTracingPipeline::AddProceduralHitShaderRecord(uint32_t closestHit, uint32_t intersection, uint32_t anyHit)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
	shaderRecord.generalShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.closestHitShader = closestHit;
	shaderRecord.anyHitShader = anyHit;
	shaderRecord.intersectionShader = intersection;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	m_SBT.AddHitRecord(uRet);
	return uRet;
}

uint32_t RayTracingPipeline::AddMissShaderRecord(uint32_t miss)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = miss;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	m_SBT.AddMissRecord(uRet);
	return uRet;
}

uint32_t RayTracingPipeline::AddCallableShaderRecord(uint32_t _callable)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = _callable;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	m_SBT.AddCallableRecord(uRet);
	return uRet;
}

void RayTracingPipeline::SetMaxRecursion(uint32_t maxRecur)
{
	m_maxRayRecursionDepth = maxRecur;
}

void RayTracingPipeline::PreSetPipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;
}

void RayTracingPipeline::Init()
{
	auto& device = MyDevice::GetInstance();
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	std::vector<VkPushConstantRange> pushConstantRanges{};
	m_pushConstant.GetVkPushConstantRanges(pushConstantRanges);
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
	CHECK_TRUE(vkPipelineLayout == VK_NULL_HANDLE, "VkPipelineLayout is already created!");

	device.CreatePipelineLayout(pipelineLayoutInfo);

	VkRayTracingPipelineCreateInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
	pipelineInfo.pNext = nullptr;
	pipelineInfo.flags = 0;
	pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStageInfos.size());
	pipelineInfo.pStages = m_shaderStageInfos.data();
	pipelineInfo.groupCount = static_cast<uint32_t>(m_shaderRecords.size());
	pipelineInfo.pGroups = m_shaderRecords.data();
	pipelineInfo.maxPipelineRayRecursionDepth = m_maxRayRecursionDepth;
	pipelineInfo.pLibraryInfo = nullptr;
	pipelineInfo.pLibraryInterface = nullptr;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	CHECK_TRUE(vkPipeline == VK_NULL_HANDLE, "VkPipeline is already created!");
	
	vkPipeline = device.CreateRayTracingPipeline(pipelineInfo);

	m_SBT.Init(this);
}

void RayTracingPipeline::Uninit()
{
	auto& device = MyDevice::GetInstance();
	m_SBT.Uninit();
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
}

void RayTracingPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
	const auto& pSets = input.vkDescriptorSets;
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkPipeline);

	if (pSets.size() > 0)
	{
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
			vkPipelineLayout,
			0,
			static_cast<uint32_t>(pSets.size()),
			pSets.data(),
			static_cast<uint32_t>(input.optDynamicOffsets.size()),
			input.optDynamicOffsets.data());
	}

	for (const auto& _pushConst : input.pushConstants)
	{
		m_pushConstant.PushConstant(commandBuffer, vkPipelineLayout, _pushConst.first, _pushConst.second);
	}

	vkCmdTraceRaysKHR(commandBuffer, &m_SBT.vkRayGenRegion, &m_SBT.vkMissRegion, &m_SBT.vkHitRegion, &m_SBT.vkCallRegion, input.uWidth, input.uHeight, input.uDepth);
}

void RayTracingPipeline::AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	m_pushConstant.AddConstantRange(_stages, _offset, _size);
}

RayTracingPipeline::ShaderBindingTable::ShaderBindingTable()
{
}

void RayTracingPipeline::ShaderBindingTable::SetRayGenRecord(uint32_t index)
{
	m_uRayGenerationIndex = index;
}

void RayTracingPipeline::ShaderBindingTable::AddMissRecord(uint32_t index)
{
	m_uMissIndices.push_back(index);
}

void RayTracingPipeline::ShaderBindingTable::AddHitRecord(uint32_t index)
{
	m_uHitIndices.push_back(index);
}

void RayTracingPipeline::ShaderBindingTable::AddCallableRecord(uint32_t index)
{
	m_uCallableIndices.push_back(index);
}

void RayTracingPipeline::ShaderBindingTable::Init(const RayTracingPipeline* pRayTracingPipeline)
{
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{};
	uint32_t uRecordCount = static_cast<uint32_t>(pRayTracingPipeline->m_shaderRecords.size()); // This is because the inner (nested) class A is considered part of B and has access to all private, protected, and public members of B
	uint32_t uHandleSizeHost = 0u;
	uint32_t uHandleSizeDevice = 0u;
	size_t uGroupDataSize = 0u;
	std::vector<uint8_t> groupData;
	auto& device = MyDevice::GetInstance();
	static auto funcAlignUp = [](uint32_t uToAlign, uint32_t uAlignRef)
	{
		return (uToAlign + uAlignRef - 1) & ~(uAlignRef - 1);
	};
	
	device.GetPhysicalDeviceRayTracingProperties(rayTracingProperties);
	
	uHandleSizeHost = rayTracingProperties.shaderGroupHandleSize;
	uHandleSizeDevice = funcAlignUp(uHandleSizeHost, rayTracingProperties.shaderGroupHandleAlignment);

	vkRayGenRegion.size = funcAlignUp(uHandleSizeDevice * 1u, rayTracingProperties.shaderGroupBaseAlignment);
	vkMissRegion.size = funcAlignUp(uHandleSizeDevice * static_cast<uint32_t>(m_uMissIndices.size()), rayTracingProperties.shaderGroupBaseAlignment);
	vkHitRegion.size = funcAlignUp(uHandleSizeDevice * static_cast<uint32_t>(m_uHitIndices.size()), rayTracingProperties.shaderGroupBaseAlignment);
	vkCallRegion.size = funcAlignUp(uHandleSizeDevice * static_cast<uint32_t>(m_uCallableIndices.size()), rayTracingProperties.shaderGroupBaseAlignment);

	vkRayGenRegion.stride = vkRayGenRegion.size; // The size member of pRayGenShaderBindingTable must be equal to its stride member
	vkMissRegion.stride = uHandleSizeDevice;
	vkHitRegion.stride = uHandleSizeDevice;
	vkCallRegion.stride = uHandleSizeDevice;

	uGroupDataSize = static_cast<size_t>(uRecordCount * uHandleSizeHost);
	groupData.resize(uGroupDataSize);

	VK_CHECK(
		device.GetRayTracingShaderGroupHandles(
			pRayTracingPipeline->vkPipeline, 
			0, 
			uRecordCount, 
			uGroupDataSize, 
			groupData.data()), 
		"Failed to get group handle data!");

	// bind shader group(shader record) handle to table on device
	{
		Buffer::CreateInformation bufferInfo{};
		VkDeviceAddress SBTAddress = 0;
		std::vector<uint8_t> dataToDevice;
		auto funcGetGroupHandle = [&](int i) { return groupData.data() + i * static_cast<int>(uHandleSizeHost); };
		m_uptrBuffer = std::make_unique<Buffer>();

		bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
		bufferInfo.size = vkRayGenRegion.size + vkMissRegion.size + vkHitRegion.size + vkCallRegion.size;
		m_uptrBuffer->PresetCreateInformation(bufferInfo);
		m_uptrBuffer->Init();
		
		SBTAddress = m_uptrBuffer->GetDeviceAddress();
		dataToDevice.resize(bufferInfo.size, 0u);

		// set rayGen
		uint8_t* pEntryPoint = dataToDevice.data();
		vkRayGenRegion.deviceAddress = SBTAddress;
		memcpy(pEntryPoint, funcGetGroupHandle(m_uRayGenerationIndex), static_cast<size_t>(uHandleSizeHost));

		// set Miss
		vkMissRegion.deviceAddress = SBTAddress + vkRayGenRegion.size;
		pEntryPoint = dataToDevice.data() + vkRayGenRegion.size;
		for (const auto index : m_uMissIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}

		// set Hit
		vkHitRegion.deviceAddress = SBTAddress + vkRayGenRegion.size + vkMissRegion.size;
		pEntryPoint = dataToDevice.data() + vkRayGenRegion.size + vkMissRegion.size;
		for (const auto index : m_uHitIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}

		// set Callable
		vkCallRegion.deviceAddress = SBTAddress + vkRayGenRegion.size + vkMissRegion.size + vkHitRegion.size;
		pEntryPoint = dataToDevice.data() + vkRayGenRegion.size + vkMissRegion.size + vkHitRegion.size;
		for (const auto index : m_uCallableIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}
		m_uptrBuffer->CopyFromHost(dataToDevice.data());
	}
}

void RayTracingPipeline::ShaderBindingTable::Uninit()
{
	m_uptrBuffer->Uninit();
}

void PipelineCache::_LoadBinary(std::vector<uint8_t>& outData) const
{
	// code from https://mysvac.github.io/vulkan-hpp-tutorial/md/04/11_pipelinecache/
	outData.clear();
	if (std::ifstream in("pipeline_cache.data", std::ios::binary | std::ios::ate); in) 
	{
		const std::size_t size = in.tellg();
		in.seekg(0);
		outData.resize(size);
		in.read(reinterpret_cast<char*>(outData.data()), size);
	}
}

PipelineCache::~PipelineCache()
{
	CHECK_TRUE(m_vkPipelineCache == VK_NULL_HANDLE);
}

PipelineCache& PipelineCache::PreSetSourceFilePath(const std::string& inFilePath)
{
	m_filePath = inFilePath;

	return *this;
}

PipelineCache& PipelineCache::PreSetCreateFlags(VkPipelineCacheCreateFlags inFlags)
{
	m_createFlags = inFlags;

	return *this;
}

void PipelineCache::Init()
{
	auto& device = MyDevice::GetInstance();
	VkPipelineCacheCreateInfo createInfo{};
	std::vector<uint8_t> binaryData{};
	VkPipelineCacheHeaderVersionOne* pCacheHeader;

	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.flags = m_createFlags;
	
	// now, load data from file
	_LoadBinary(binaryData);
	pCacheHeader = reinterpret_cast<VkPipelineCacheHeaderVersionOne*>(binaryData.data());
	if (device.IsPipelineCacheValid(pCacheHeader))
	{
		createInfo.initialDataSize = binaryData.size();
		createInfo.pInitialData = binaryData.data();
		m_fileCacheValid = true;
	}
	// we no longer need source file path
	m_filePath = {};

	m_vkPipelineCache = device.CreatePipelineCache(createInfo);
}

VkPipelineCache PipelineCache::GetVkPipelineCache() const
{
	return m_vkPipelineCache;
}

bool PipelineCache::IsEmptyCache() const
{
	return (!m_fileCacheValid) && m_vkPipelineCache != VK_NULL_HANDLE;
}

void PipelineCache::Uninit()
{
	auto& device = MyDevice::GetInstance();

	device.DestroyPipelineCache(m_vkPipelineCache);

	m_vkPipelineCache = VK_NULL_HANDLE;
	m_createFlags = 0;
	m_filePath = {};
	m_fileCacheValid = false;
}

void PipelineCache::SaveCacheToFile(VkPipelineCache inCacheToStore, const std::string& inDstFilePath)
{
	// code from https://mysvac.github.io/vulkan-hpp-tutorial/md/04/11_pipelinecache/
	auto& device = MyDevice::GetInstance();
	std::vector<uint8_t> cacheData;
	std::ofstream out(inDstFilePath, std::ios::binary);

	VK_CHECK(device.GetPipelineCacheData(inCacheToStore, cacheData));
	
	out.write(reinterpret_cast<const char*>(cacheData.data()), cacheData.size());
}
