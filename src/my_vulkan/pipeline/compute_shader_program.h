#pragma once
#include "common.h"
#include "shader_program_layout.h"
#include "shader.h"

class PushConstantManager;

class ComputeShaderProgramCreateInfo final
{
private:
	friend class ComputeShaderProgram;

	std::string m_spirvFile;
	std::string m_entry = "main";
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	auto Reset() -> ComputeShaderProgramCreateInfo&;
	auto SetSpirvFile(std::string inFile) -> ComputeShaderProgramCreateInfo&;
	auto SetEntry(std::string inEntry) -> ComputeShaderProgramCreateInfo&;
	auto CustomizePipelineCache(VkPipelineCache inCache) -> ComputeShaderProgramCreateInfo&;
};

class ComputeShaderProgram final
{
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::unique_ptr<ShaderModule> m_uptrShaderModule;
	ShaderProgramLayout m_shaderProgramLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _CreateVkPipeline(const VkPipelineShaderStageCreateInfo& inShaderStageInfo, VkPipelineCache inPipelineCache);
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);

public:
	~ComputeShaderProgram();

	void Create(const ComputeShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	
	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline() const -> VkPipeline;
};
