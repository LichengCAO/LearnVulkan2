#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include "resource/descriptor.h"

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
	std::unique_ptr<ShaderModule> m_uptrShaderModule;
	PipelineLayout m_pipelineLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _CreateVkPipeline(const VkPipelineShaderStageCreateInfo& inShaderStageInfo, VkPipelineCache inPipelineCache);

public:
	ComputeShaderProgram();
	~ComputeShaderProgram();

	void Create(const ComputeShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline() const -> VkPipeline;
};
