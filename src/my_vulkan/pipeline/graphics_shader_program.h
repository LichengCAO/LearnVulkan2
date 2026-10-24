#pragma once
#include "pipeline_common.h"
#include "shader_program_layout.h"
#include "shader.h"
#include "shader_resources.h"
#include <map>
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
	friend class CommandBuffer;

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
	friend class CommandBuffer;

private:
	struct ColorAttachment
	{
		VkFormat format;
		VkImageLayout finalLayout;
	};
	std::vector<ShaderModuleCreateInfo> m_shaderModuleInfos;
	std::map<uint32_t, ColorAttachment> m_defaultColors;
	std::optional<VkFormat> m_defaultDepth;

public:
	auto SetDefaultColorAttachment(uint32_t inLocation, VkFormat inFormat,
		VkImageLayout inFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) -> GraphicsShaderProgramCreateInfo&;
	auto SetDefaultDepthAttachment(VkFormat inFormat) -> GraphicsShaderProgramCreateInfo&;
	auto Reset() -> GraphicsShaderProgramCreateInfo&;
	auto AddShaderModuleInfo(ShaderModuleCreateInfo inShaderModule) -> GraphicsShaderProgramCreateInfo&;
};

class GraphicsShaderProgram final
{
	friend class CommandBuffer;
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	ShaderProgramLayout m_shaderProgramLayout;
	std::unordered_map<size_t, VkPipeline> m_cachedGraphicsPipelines;
	std::unique_ptr<RenderPass> m_defaultRenderPass;
	GraphicsPipelineStateInfo m_defaultState;
	bool m_automaticVertexInputSupported = true;

private:
	void _CreateDefaults(const GraphicsShaderProgramCreateInfo& inInfo);
	auto _ResolveState(const GraphicsPipelineStateInfo& inState) const -> GraphicsPipelineStateInfo;
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);
	auto _CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const -> VkPipeline;
	auto _HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const -> size_t;

public:
	GraphicsShaderProgram();
	~GraphicsShaderProgram();

	void Create(const GraphicsShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	
	auto GetLayout() const -> const PipelineLayout&;
	auto GetDefaultRenderPass() const -> const RenderPass&;
	
	auto GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)->VkPipeline;
};
