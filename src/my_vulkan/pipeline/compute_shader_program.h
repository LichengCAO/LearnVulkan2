#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "shader.h"
#include "descriptor_set.h"

class PushConstantManager;

class ComputeShaderProgramCreateInfo final
{
private:
	friend class ComputeShaderProgram;

	std::string m_spirvFile;
	std::string m_entry = "main";
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	ComputeShaderProgramCreateInfo& Reset();
	ComputeShaderProgramCreateInfo& SetSpirvFile(std::string inFile);
	ComputeShaderProgramCreateInfo& SetEntry(std::string inEntry);
	ComputeShaderProgramCreateInfo& CustomizePipelineCache(VkPipelineCache inCache);
};

class ComputeShaderProgram final
{
private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::unique_ptr<ShaderModule> m_uptrShaderModule;
	std::vector<std::unique_ptr<DescriptorSetLayout>> m_descriptorSetLayouts;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;
	std::unique_ptr<PipelineLayout> m_pipelineLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _CreateDescriptorSetLayouts(const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& inDescriptorSetData);
	void _CreatePipelineLayout(const std::vector<VkPushConstantRange>& inPushConstantRanges);
	void _CreateVkPipeline(const VkPipelineShaderStageCreateInfo& inShaderStageInfo, VkPipelineCache inPipelineCache);
	void _CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges);

public:
	~ComputeShaderProgram();

	void Create(const ComputeShaderProgramCreateInfo* inCreateInfo);
	
	void Destroy();

	auto GetNameToSetBinding() const->const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& ;
	
	VkPipeline GetVkPipeline() const;
};
