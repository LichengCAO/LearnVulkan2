#include "../compute_shader_program.h"
#include "push_constant_manager.h"
#include "device.h"

auto ComputeShaderProgramCreateInfo::Reset() -> ComputeShaderProgramCreateInfo&
{
	m_spirvFile.clear();
	m_entry.reset();
	m_vkPipelineCache = VK_NULL_HANDLE;
	return *this;
}

auto ComputeShaderProgramCreateInfo::SetShader(std::string_view file, std::optional<std::string_view> entry) -> ComputeShaderProgramCreateInfo&
{
	auto request = ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_COMPUTE_BIT, entry);
	m_spirvFile = std::move(request.file);
	m_entry = std::move(request.entry);
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
		const auto& entry = m_uptrShaderModule->SelectEntry(VK_SHADER_STAGE_COMPUTE_BIT,
		    inCreateInfo->m_entry ? std::make_optional<std::string_view>(*inCreateInfo->m_entry) : std::nullopt);
		m_shaderProgramLayout.Create(std::vector<ShaderEntryReflection>{
		    m_uptrShaderModule->GetEntryReflection(entry.stage, entry.name)});
		_CreatePushConstantManager(m_shaderProgramLayout.GetPushConstantRanges());
		_CreateVkPipeline(m_uptrShaderModule->GetShaderStageInfo(entry.stage, entry.name), inCreateInfo->m_vkPipelineCache);
	}
	catch (...)
	{
		Destroy();
		throw;
	}

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
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

void ComputeShaderProgram::_CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges)
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
	m_shaderProgramLayout.Destroy();
	m_uptrPushConstant.reset();
}

auto ComputeShaderProgram::GetNameToSetBinding() const
	-> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&
{
	return m_shaderProgramLayout.GetNameToSetBinding();
}

auto ComputeShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_shaderProgramLayout.GetLayout();
}

auto ComputeShaderProgram::GetVkPipeline() const -> VkPipeline
{
	return m_vkPipeline;
}
