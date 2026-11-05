#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include "resource/descriptor.h"

#include "shader_program_create_info.h"

class ComputeShaderProgram final
{
	friend class MyDevice;
	friend class CommandBuffer;
private:
	std::unique_ptr<ShaderModule> m_uptrShaderModule;
	PipelineLayout m_pipelineLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _Create(const ComputeShaderProgramCreateInfo& inCreateInfo);
	void _Destroy();
	void _CreateVkPipeline(const VkPipelineShaderStageCreateInfo& inShaderStageInfo, VkPipelineCache inPipelineCache);

public:
	ComputeShaderProgram();
	ComputeShaderProgram(const ComputeShaderProgram&) = delete;
	auto operator=(const ComputeShaderProgram&) -> ComputeShaderProgram& = delete;
	ComputeShaderProgram(ComputeShaderProgram&&) = delete;
	auto operator=(ComputeShaderProgram&&) -> ComputeShaderProgram& = delete;
	// Owns its reflection snapshot and borrows Device-cached module/pipeline/layout handles.
	// Destroy before Device teardown and after completing native uses. Destruction
	// does not wait for the GPU; see MyDevice::CreateComputeShaderProgram for the
	// shared caches accessed by creation and the indirect pipeline release path.
	~ComputeShaderProgram();

	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline() const -> VkPipeline;
};
