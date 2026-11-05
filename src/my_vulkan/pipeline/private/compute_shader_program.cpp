#include "../compute_shader_program.h"
#include "device.h"

ComputeShaderProgram::ComputeShaderProgram() = default;

ComputeShaderProgram::~ComputeShaderProgram()
{
	_Destroy();
}

void ComputeShaderProgram::_Create(const ComputeShaderProgramCreateInfo& inCreateInfo)
{
	// MyDevice validates before creating this temporary. Its destructor rolls
	// back owned resources if any stage throws; cached resources stay cached.
	m_uptrShaderModule = std::make_unique<ShaderModule>();
	MyDevice::GetInstance()._GetOrCreateShaderModule(*m_uptrShaderModule, inCreateInfo.m_finalized->shader);
	MyDevice::GetInstance().GetOrCreatePipelineLayout(m_pipelineLayout, inCreateInfo.m_finalized->layout);
	_CreateVkPipeline(m_uptrShaderModule->GetShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT), inCreateInfo.m_vkPipelineCache);

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE);
}

void ComputeShaderProgram::_CreateVkPipeline(
	const VkPipelineShaderStageCreateInfo& inShaderStageInfo,
	VkPipelineCache inPipelineCache)
{
	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.stage = inShaderStageInfo;
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE, "Compute shader program needs a pipeline layout!");
	pipelineInfo.layout = GetLayout().GetVkPipelineLayout();
	m_vkPipeline = MyDevice::GetInstance().CreateComputePipeline(pipelineInfo, inPipelineCache);
}

void ComputeShaderProgram::_Destroy()
{
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	if (m_uptrShaderModule != nullptr)
	{
		m_uptrShaderModule.reset();
	}
	m_pipelineLayout = {};
}

auto ComputeShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}

auto ComputeShaderProgram::GetVkPipeline() const -> VkPipeline
{
	return m_vkPipeline;
}
