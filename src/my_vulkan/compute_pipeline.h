#pragma once
#include "common.h"

class PushConstantManager;

class IComputePipelineInitializer
{
public:
	virtual void InitComputePipeline(ComputePipeline* pPipeline) const = 0;
};

class ComputePipeline
{
public:
	struct PipelineInput
	{
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

	class Intializer : public IComputePipelineInitializer
	{
	private:
		VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
		VkPipelineShaderStageCreateInfo m_shaderStageInfo{};
		std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
		std::vector<VkPushConstantRange> m_pushConstants;

	public:
		virtual void InitComputePipeline(ComputePipeline* pPipeline) const override;

		ComputePipeline::Intializer& SetShader(const VkPipelineShaderStageCreateInfo& shaderInfo);
		
		ComputePipeline::Intializer& AddDescriptorSetLayout(VkDescriptorSetLayout _vkLayout);
		
		ComputePipeline::Intializer& AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);
		
		// Optional, if cache is not empty, this will facilitate pipeline creation,
		// if not, cache will be filled with pipeline information for next creation
		ComputePipeline::Intializer& CustomizePipelineCache(VkPipelineCache inCache);
	};

private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

public:
	~ComputePipeline();

	void Init(const IComputePipelineInitializer* pInitializer);
	
	void Uninit();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};