#include "compute_pipeline.h"
#include "push_constant_manager.h"
#include "device.h"

ComputePipeline::~ComputePipeline()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void ComputePipeline::Init(const IComputePipelineInitializer* pInitializer)
{
	pInitializer->InitComputePipeline(this);

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
}

void ComputePipeline::Uninit()
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
}

void ComputePipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_vkPipeline);
	vkCmdBindDescriptorSets(
		commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		m_vkPipelineLayout,
		0,
		static_cast<uint32_t>(input.vkDescriptorSets.size()),
		input.vkDescriptorSets.data(),
		static_cast<uint32_t>(input.optDynamicOffsets.size()),
		input.optDynamicOffsets.data());

	for (const auto& _pushConst : input.pushConstants)
	{
		m_uptrPushConstant->PushConstant(commandBuffer, m_vkPipelineLayout, _pushConst.first, _pushConst.second);
	}

	vkCmdDispatch(commandBuffer, input.groupCountX, input.groupCountY, input.groupCountZ);
}

void ComputePipeline::Intializer::InitComputePipeline(ComputePipeline* pPipeline) const
{
	auto& device = MyDevice::GetInstance();
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	std::vector<VkPushConstantRange> pushConstantRanges{};

	pPipeline->m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstant : m_pushConstants)
	{
		pPipeline->m_uptrPushConstant->AddConstantRange(pushConstant.stageFlags, pushConstant.offset, pushConstant.size);
	}
	pPipeline->m_uptrPushConstant->GetVkPushConstantRanges(pushConstantRanges);

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pPipeline->m_vkPipelineLayout = device.CreatePipelineLayout(pipelineLayoutInfo);

	pipelineInfo.stage = m_shaderStageInfo;
	pipelineInfo.layout = pPipeline->m_vkPipelineLayout;
	pPipeline->m_vkPipeline = device.CreateComputePipeline(pipelineInfo, m_vkPipelineCache);
}

ComputePipeline::Intializer& ComputePipeline::Intializer::SetShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	m_shaderStageInfo = shaderInfo;
	
	return *this;
}

ComputePipeline::Intializer& ComputePipeline::Intializer::AddDescriptorSetLayout(VkDescriptorSetLayout inLayout)
{
	m_descriptorSetLayouts.push_back(inLayout);

	return *this;
}

ComputePipeline::Intializer& ComputePipeline::Intializer::AddPushConstant(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize)
{
	VkPushConstantRange pushConstant{};

	pushConstant.stageFlags = inStages;
	pushConstant.offset = inOffset;
	pushConstant.size = inSize;

	m_pushConstants.push_back(pushConstant);

	return *this;
}

ComputePipeline::Intializer& ComputePipeline::Intializer::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;

	return *this;
}
