#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include "resource/descriptor.h"
#include "render_pass.h"
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
	VulkanDetail::RenderPassSnapshot m_renderPass;
	uint32_t m_subpassIndex = 0;

public:
	void Reset();
	void AddVertexInputDescription(
		const VkVertexInputBindingDescription& inBindingDescription,
		const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions);
	void SetRenderPassSubpass(const RenderPass& inRenderPass, uint32_t inSubpassIndex = 0);
};

#include "shader_program_create_info.h"

class GraphicsShaderProgram final
{
	friend class MyDevice;
	friend class CommandBuffer;
private:
	void _Create(const GraphicsShaderProgramCreateInfo& inInfo);
	void _Destroy();
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
	PipelineLayout m_pipelineLayout;
	struct CachedGraphicsPipeline
	{
		VkPipeline pipeline = VK_NULL_HANDLE;
		VulkanDetail::RenderPassSnapshot renderPass;
	};
	std::unordered_map<size_t, CachedGraphicsPipeline> m_cachedGraphicsPipelines;
	std::unique_ptr<RenderPass> m_defaultRenderPass;
	GraphicsPipelineStateInfo m_defaultState;
	bool m_automaticVertexInputSupported = true;

private:
	void _CreateDefaults(const GraphicsShaderProgramCreateInfo& inInfo);
	auto _ResolveState(const GraphicsPipelineStateInfo& inState) const -> GraphicsPipelineStateInfo;
	auto _CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const -> VkPipeline;
	auto _HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const -> size_t;

public:
	GraphicsShaderProgram();
	~GraphicsShaderProgram();
	GraphicsShaderProgram(const GraphicsShaderProgram&) = delete;
	auto operator=(const GraphicsShaderProgram&) -> GraphicsShaderProgram& = delete;
	GraphicsShaderProgram(GraphicsShaderProgram&&) = delete;
	auto operator=(GraphicsShaderProgram&&) -> GraphicsShaderProgram& = delete;

	// MyDevice creates this object synchronously. Callers serialize this instance,
	// its lazy GetVkPipeline calls and related shared cache/dependency accesses.
	auto GetLayout() const -> const PipelineLayout&;
	auto GetDefaultRenderPass() const -> const RenderPass&;
	auto GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)->VkPipeline;
};
