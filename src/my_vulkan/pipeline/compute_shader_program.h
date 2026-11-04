#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include "resource/descriptor.h"

#include "shader_program_create_info.h"

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

	void Create(const ComputeShaderProgramCreateInfo& inCreateInfo);
	
	void Destroy();

	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline() const -> VkPipeline;
};
