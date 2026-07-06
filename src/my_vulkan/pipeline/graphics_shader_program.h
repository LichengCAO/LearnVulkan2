#pragma once
#include "pipeline_common.h"
#include "pipeline_layout.h"
#include "shader.h"
#include "descriptor_set.h"
#include <pipeline_state.h>
#include <map>
class RenderPass;
class Framebuffer;
class ImageView;
class GraphicsShaderProgram;
class GraphicsProgram;
class PushConstantManager;
class CommandSubmission;
class CommandBuffer;
class DescriptorSetLayout;

class GraphicsPipelineStateInfo final
{
private:
	friend class GraphicsShaderProgram;

	std::vector<VkVertexInputBindingDescription> m_vertexBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> m_vertexAttributeDescriptions;
	const RenderPass* m_renderPassPtr = nullptr;
	uint32_t m_subpassIndex = 0;

public:
	GraphicsPipelineStateInfo& Reset();
	GraphicsPipelineStateInfo& AddVertexInputDescription(
		const VkVertexInputBindingDescription& inBindingDescription,
		const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions);
	GraphicsPipelineStateInfo& SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex = 0);
};

class GraphicsProgramCreateInfo final
{
	friend class GraphicsProgram;
private:
	std::set<std::string> m_spirvFiles;
	std::unordered_map<VkShaderStageFlagBits, std::string> m_mapStageToEntry;
	VkSampleCountFlagBits m_sampleCount = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
	bool m_depthStencil = false;

public:
	void AddShaderModuleInfo(const ShaderModuleCreateInfo& inShaderModule);
	void AddDepthStencilAttachment() { m_depthStencil = true; };
	void CustomizeMultiSample(VkSampleCountFlagBits inSample) { m_sampleCount = inSample; };
};

class GraphicsShaderProgram final
{
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	std::vector<std::unique_ptr<DescriptorSetLayout>> m_descriptorSetLayouts;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;
	std::unique_ptr<PipelineLayout> m_pipelineLayout;
	std::unordered_map<size_t, VkPipeline> m_cachedGraphicsPipelines;

private:
	void _CreateDescriptorSetLayouts(const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& inDescriptorSetData);
	void _CreatePipelineLayout(const std::vector<VkPushConstantRange>& inPushConstantRanges);
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);
	VkPipeline _CreateVkPipeline(const GraphicsPipelineStateInfo& inStateInfo) const;
	size_t _HashGraphicsPipelineStateInfo(const GraphicsPipelineStateInfo& inStateInfo) const;
	VkPipelineLayout _GetVkPipelineLayout() const;

public:
	~GraphicsShaderProgram();

	void Create(const GraphicsShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const->const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	
	auto GetVkPipeline(const GraphicsPipelineStateInfo& inStateInfo)->VkPipeline;
};
