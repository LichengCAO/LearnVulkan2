#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include "resource/descriptor.h"
#include <map>
#include <string_view>
class RenderPass;
class Framebuffer;
class ImageView;
class GraphicsShaderProgram;
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
	auto SetRenderPassSubpass(const RenderPass& inRenderPass, uint32_t inSubpassIndex = 0) -> GraphicsPipelineStateInfo&;
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
	std::vector<ShaderDetail::Request> m_shaders;
	auto _AddShader(std::string_view inSpirv, VkShaderStageFlagBits inStage) -> GraphicsShaderProgramCreateInfo&;
	std::map<uint32_t, ColorAttachment> m_defaultColors;
	std::optional<VkFormat> m_defaultDepth;

public:
	auto SetDefaultColorAttachment(uint32_t inLocation, VkFormat inFormat, VkImageLayout inFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) -> GraphicsShaderProgramCreateInfo&;
	auto SetDefaultDepthAttachment(VkFormat inFormat) -> GraphicsShaderProgramCreateInfo&;
	auto Reset() -> GraphicsShaderProgramCreateInfo&;
	auto AddVertexShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
	auto AddFragmentShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
	auto AddGeometryShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
	auto AddTaskShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
	auto AddMeshShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
};

class GraphicsShaderProgram final
{
	friend class CommandBuffer;
private:
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
	PipelineLayout m_pipelineLayout;
	std::unordered_map<size_t, VkPipeline> m_cachedGraphicsPipelines;
	std::unique_ptr<RenderPass> m_defaultRenderPass;
	GraphicsPipelineStateInfo m_defaultState;
	bool m_automaticVertexInputSupported = true;

private:
	void _CreateDefaults(const GraphicsShaderProgramCreateInfo& inInfo, const ShaderProgramReflection& inReflection);
	auto _ResolveState(const GraphicsPipelineStateInfo& inState) const -> GraphicsPipelineStateInfo;
	auto _CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const -> VkPipeline;
	auto _HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const -> size_t;

public:
	GraphicsShaderProgram();
	~GraphicsShaderProgram();

	void Create(const GraphicsShaderProgramCreateInfo& inCreateInfo);
	void Destroy();
	auto GetLayout() const -> const PipelineLayout&;
	auto GetDefaultRenderPass() const -> const RenderPass&;
	auto GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)->VkPipeline;
};
