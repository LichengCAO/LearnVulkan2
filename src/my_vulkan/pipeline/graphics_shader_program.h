#pragma once
#include "pipeline_common.h"
#include "shader_program_layout.h"
#include "shader.h"
#include <pipeline_state.h>
class RenderPass;
class Framebuffer;
class ImageView;
class GraphicsShaderProgram;
class PushConstantManager;
class CommandSubmission;
class CommandBuffer;

class GraphicsPipelineStateInfo final
{
private:
	friend class GraphicsShaderProgram;

	std::vector<VkVertexInputBindingDescription> m_vertexBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> m_vertexAttributeDescriptions;
	const RenderPass* m_renderPassPtr = nullptr;
	uint32_t m_subpassIndex = 0;

public:
	auto Reset() -> GraphicsPipelineStateInfo&;
	auto AddVertexInputDescription(
		const VkVertexInputBindingDescription& inBindingDescription,
		const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions) -> GraphicsPipelineStateInfo&;
	auto SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex = 0) -> GraphicsPipelineStateInfo&;
};

class GraphicsShaderProgramCreateInfo final
{
	friend class GraphicsShaderProgram;

private:
	std::vector<ShaderModuleCreateInfo> m_shaderModuleInfos;

public:
	auto Reset() -> GraphicsShaderProgramCreateInfo&;
	auto AddShaderModuleInfo(ShaderModuleCreateInfo inShaderModule) -> GraphicsShaderProgramCreateInfo&;
};

class GraphicsShaderProgram final
{
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	ShaderProgramLayout m_shaderProgramLayout;
	std::unordered_map<size_t, VkPipeline> m_cachedGraphicsPipelines;

private:
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);
	auto _CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const -> VkPipeline;
	auto _HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const -> size_t;

public:
	~GraphicsShaderProgram();

	void Create(const GraphicsShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	
	auto GetLayout() const -> const PipelineLayout&;
	
	auto GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)->VkPipeline;
};
