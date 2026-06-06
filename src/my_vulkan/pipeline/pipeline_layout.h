#pragma once
#include "common.h"

class PipelineLayoutCreateInfo final
{
public:
	PipelineLayoutCreateInfo() = default;
	PipelineLayoutCreateInfo(const PipelineLayoutCreateInfo&) = delete;
	PipelineLayoutCreateInfo(PipelineLayoutCreateInfo&&) = delete;
	PipelineLayoutCreateInfo& operator=(const PipelineLayoutCreateInfo&) = delete;
	PipelineLayoutCreateInfo& operator=(PipelineLayoutCreateInfo&&) = delete;
	~PipelineLayoutCreateInfo() = default;

	void AddDescriptorSetLayout(VkDescriptorSetLayout inLayout);
	void AddPushConstantRange(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize);

private:
	friend class PipelineLayout;

	std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<VkPushConstantRange> m_pushConstantRanges;
};

class PipelineLayout final
{
private:
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;

public:
	PipelineLayout() = default;
	PipelineLayout(const PipelineLayout&) = delete;
	PipelineLayout& operator=(const PipelineLayout&) = delete;
	~PipelineLayout();

	void Create(const PipelineLayoutCreateInfo* inCreateInfo);
	void Destroy();
	auto GetVkPipelineLayout() const -> VkPipelineLayout { return m_vkPipelineLayout; };
};
