#pragma once
#include "common.h"
#include "resource/descriptor_set.h"

class PipelineLayoutCreateInfo final
{
public:
	PipelineLayoutCreateInfo() = default;
	PipelineLayoutCreateInfo(const PipelineLayoutCreateInfo&) = default;
	PipelineLayoutCreateInfo(PipelineLayoutCreateInfo&&) noexcept = default;
	auto operator=(const PipelineLayoutCreateInfo&) -> PipelineLayoutCreateInfo& = default;
	auto operator=(PipelineLayoutCreateInfo&&) noexcept -> PipelineLayoutCreateInfo& = default;
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
	auto operator=(const PipelineLayout&) -> PipelineLayout& = default;
	PipelineLayout(PipelineLayout&&) noexcept = default;
	auto operator=(PipelineLayout&&) noexcept -> PipelineLayout& = default;
	~PipelineLayout();

	void Create(const PipelineLayoutCreateInfo& inCreateInfo);
	void Destroy();
	auto GetVkPipelineLayout() const -> VkPipelineLayout { return m_vkPipelineLayout; };
	auto GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&;
	auto GetPushConstantRanges() const -> const std::vector<VkPushConstantRange>& { return m_pushConstantRanges; }
};
