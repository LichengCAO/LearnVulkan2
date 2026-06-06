#include "compute_pipeline.h"
#include "push_constant_manager.h"
#include "device.h"

ComputePipeline::~ComputePipeline()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void ComputePipeline::Create(const IComputePipelineInitializer* pInitializer)
{
	pInitializer->InitComputePipeline(this);

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
}

void ComputePipeline::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	m_vkPipelineLayout = VK_NULL_HANDLE;
	m_uptrPushConstant.reset();
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
	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE, "Compute pipeline needs a pipeline layout!");

	pPipeline->m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstant : m_pushConstants)
	{
		pPipeline->m_uptrPushConstant->AddConstantRange(pushConstant.stageFlags, pushConstant.offset, pushConstant.size);
	}
	pPipeline->m_vkPipelineLayout = m_vkPipelineLayout;

	pipelineInfo.stage = m_shaderStageInfo;
	pipelineInfo.layout = pPipeline->m_vkPipelineLayout;
	pPipeline->m_vkPipeline = device.CreateComputePipeline(pipelineInfo, m_vkPipelineCache);
}

ComputePipeline::Intializer& ComputePipeline::Intializer::SetShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	m_shaderStageInfo = shaderInfo;
	
	return *this;
}

ComputePipeline::Intializer& ComputePipeline::Intializer::SetPipelineLayout(VkPipelineLayout inPipelineLayout)
{
	CHECK_TRUE(inPipelineLayout != VK_NULL_HANDLE, "Invalid compute pipeline layout!");

	m_vkPipelineLayout = inPipelineLayout;

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
