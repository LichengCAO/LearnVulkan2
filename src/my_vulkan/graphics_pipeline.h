#pragma once
#include "common.h"
class RenderPass;
class GraphicsPipeline;
class PushConstantManager;

class IGraphicsPipelineInitializer
{
public:
	virtual void InitGraphicsPipeline(GraphicsPipeline* pPipeline) const = 0;
};

class GraphicsPipeline
{
public:
	// For pipelines that don't bind vertex buffers, e.g. pass through
	struct PipelineInput_Draw
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t vertexCount = 0u;

		std::vector<VkBuffer>						vertexBuffers;			// can be empty
		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
	};

	// For general graphics pipelines that use vertex buffers
	struct PipelineInput_DrawIndexed
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		std::vector<VkBuffer>	vertexBuffers;								// not sure, but I think order matters
		VkBuffer				indexBuffer = VK_NULL_HANDLE;
		VkIndexType				vkIndexType = VK_INDEX_TYPE_UINT32;
		uint32_t				indexCount = 0u;

		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
		std::optional<VkDeviceSize>					optIndexBufferOffset;
	};

	// For mesh shader pipelines
	struct PipelineInput_Mesh
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

private:
	std::unique_ptr<PushConstantManager> m_pushConstant;

public:
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline vkPipeline = VK_NULL_HANDLE;

public:
	class BaseInit : public IGraphicsPipelineInitializer
	{
	private:
		struct PushConstantInfo
		{
			VkShaderStageFlags stages;
			uint32_t offset;
			uint32_t size;
		};

		std::vector<VkPipelineShaderStageCreateInfo> m_shaderStageInfos;
		std::vector<VkVertexInputBindingDescription> m_vertBindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> m_vertAttributeDescriptions;
		std::vector<VkDynamicState> m_dynamicStates;
		std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachmentStates;
		std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
		std::vector<PushConstantInfo> m_pushConstantInfos;
		VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyStateInfo{};
		VkPipelineRasterizationStateCreateInfo m_rasterizerStateInfo{};
		VkPipelineMultisampleStateCreateInfo m_multisampleStateInfo{};
		VkPipelineViewportStateCreateInfo m_viewportStateInfo{};
		VkPipelineDepthStencilStateCreateInfo m_depthStencilInfo{};
		VkPipelineCache m_cache = VK_NULL_HANDLE;
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		uint32_t m_subpassIndex = 0;

		void _InitCreateInfos();

	public:
		GraphicsPipeline::BaseInit();

		virtual void InitGraphicsPipeline(GraphicsPipeline* pPipeline) const override;

		GraphicsPipeline::BaseInit& Reset();

		GraphicsPipeline::BaseInit& AddShader(const VkPipelineShaderStageCreateInfo& inShaderInfo);

		GraphicsPipeline::BaseInit& AddVertexInputLayout(
			const VkVertexInputBindingDescription& inBindingDescription,
			const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions);

		GraphicsPipeline::BaseInit& AddDescriptorSetLayout(VkDescriptorSetLayout inDescriptorSetLayout);

		GraphicsPipeline::BaseInit& CustomizeColorAttachmentAsAdd(uint32_t inAttachmentIndex);

		GraphicsPipeline::BaseInit& AddPushConstant(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize);

		// Optional, if cache is not empty, this will facilitate pipeline creation,
		// if not, cache will be filled with pipeline information for next creation
		GraphicsPipeline::BaseInit& CustomizePipelineCache(VkPipelineCache inCache);

		GraphicsPipeline::BaseInit& SetRenderPassSubpass(VkRenderPass inRenderPass, uint32_t inSubpassIndex = 0);
	};

private:
	void _InitCreateInfos();
	void _DoCommon(
		VkCommandBuffer _cmd,
		const VkExtent2D& _imageSize,
		const std::vector<VkDescriptorSet>& _vkDescriptorSets,
		const std::vector<uint32_t>& _dynamicOffsets,
		const std::vector<std::pair<VkShaderStageFlags, const void*>>& _pushConstants);

public:
	GraphicsPipeline();
	~GraphicsPipeline();

	void Init(const IGraphicsPipelineInitializer* pInitializer);
	
	void Uninit();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput_DrawIndexed& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Mesh& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Draw& input);
};