#pragma once
#include "common.h"
#include "private/shader_program_layout.h"
#include "private/shader.h"
#include "shader_resources.h"

class PushConstantManager;

class ComputeShaderProgramCreateInfo final
{
private:
	friend class ComputeShaderProgram;

	std::string m_spirvFile;
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	auto Reset() -> ComputeShaderProgramCreateInfo&;
	auto SetShader(std::string_view inSpirv) -> ComputeShaderProgramCreateInfo&;
	auto CustomizePipelineCache(VkPipelineCache inCache) -> ComputeShaderProgramCreateInfo&;
};

class ComputeShaderProgram final
{
	friend class CommandBuffer;
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::unique_ptr<ShaderModule> m_uptrShaderModule;
	ShaderProgramLayout m_shaderProgramLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _CreateVkPipeline(const VkPipelineShaderStageCreateInfo& inShaderStageInfo, VkPipelineCache inPipelineCache);
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);

public:
	ComputeShaderProgram();
	~ComputeShaderProgram();

	void Create(const ComputeShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	
	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline() const -> VkPipeline;
};
