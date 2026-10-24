#include "../compute_shader_program.h"
#include "device.h"

auto ComputeShaderProgramCreateInfo::Reset() -> ComputeShaderProgramCreateInfo&
{
	m_spirvFile.clear();
	m_vkPipelineCache = VK_NULL_HANDLE;
	return *this;
}

auto ComputeShaderProgramCreateInfo::SetShader(std::string_view file) -> ComputeShaderProgramCreateInfo&
{
	CHECK_TRUE(!file.empty(), "Shader SPIR-V file is unset!");
	m_spirvFile = file;
	return *this;
}

auto ComputeShaderProgramCreateInfo::CustomizePipelineCache(VkPipelineCache inCache) -> ComputeShaderProgramCreateInfo&
{
	m_vkPipelineCache = inCache;
	return *this;
}

ComputeShaderProgram::ComputeShaderProgram() = default;

ComputeShaderProgram::~ComputeShaderProgram()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE);
	assert(m_uptrShaderModule == nullptr);
}

void ComputeShaderProgram::Create(const ComputeShaderProgramCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No compute shader program create info!");
	CHECK_TRUE(!inCreateInfo->m_spirvFile.empty(), "Compute shader SPIR-V file is unset!");
	CHECK_TRUE(m_vkPipeline == VK_NULL_HANDLE, "Compute shader program already created!");
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE, "Compute shader program already created!");

	try
	{
		m_uptrShaderModule = std::make_unique<ShaderModule>();
		m_uptrShaderModule->Create(inCreateInfo->m_spirvFile);
		ShaderProgramReflection reflection;
		reflection.Create({m_uptrShaderModule->GetEntryReflection(VK_SHADER_STAGE_COMPUTE_BIT)});
		m_pipelineLayout.Create(reflection);
		_CreateVkPipeline(m_uptrShaderModule->GetShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT), inCreateInfo->m_vkPipelineCache);
	}
	catch (...)
	{
		Destroy();
		throw;
	}

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

void ComputeShaderProgram::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	if (m_uptrShaderModule != nullptr)
	{
		m_uptrShaderModule->Destroy();
		m_uptrShaderModule.reset();
	}
	m_pipelineLayout.Destroy();
}

auto ComputeShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}

auto ComputeShaderProgram::GetVkPipeline() const -> VkPipeline
{
	return m_vkPipeline;
}
