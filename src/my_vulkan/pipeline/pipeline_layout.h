#pragma once
#include "common.h"
#include "resource/descriptor_set.h"

class PipelineLayoutCreateInfo final
{
public:
	PipelineLayoutCreateInfo() = default;
	PipelineLayoutCreateInfo(const PipelineLayoutCreateInfo&) = default;
	PipelineLayoutCreateInfo(PipelineLayoutCreateInfo&&) noexcept = default;
	PipelineLayoutCreateInfo& operator=(const PipelineLayoutCreateInfo&) = default;
	PipelineLayoutCreateInfo& operator=(PipelineLayoutCreateInfo&&) noexcept = default;
	~PipelineLayoutCreateInfo() = default;

	auto AddDescriptorSetLayout(DescriptorSetLayout inLayout) -> PipelineLayoutCreateInfo&;
	auto AddPushConstantRange(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize) -> PipelineLayoutCreateInfo&;

private:
	friend class PipelineLayout;

	std::vector<DescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<VkPushConstantRange> m_pushConstantRanges;
};

class PipelineLayout final
{
private:
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector<DescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<VkPushConstantRange> m_pushConstantRanges;

public:
	PipelineLayout() = default;
	PipelineLayout(const PipelineLayout&) = default;
	PipelineLayout& operator=(const PipelineLayout&) = default;
	PipelineLayout(PipelineLayout&&) noexcept = default;
	PipelineLayout& operator=(PipelineLayout&&) noexcept = default;
	~PipelineLayout();

	void Create(const PipelineLayoutCreateInfo& inCreateInfo);
	void Destroy();
	auto GetVkPipelineLayout() const -> VkPipelineLayout { return m_vkPipelineLayout; };
	auto GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&;
};
