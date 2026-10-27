#pragma once
#include "common.h"
#include "resource/descriptor.h"
#include <string_view>

class ShaderProgramReflection;

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
	friend struct CommandBufferTestProbe;
	friend struct ShaderBindingTestProbe;
private:
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector<DescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<VkPushConstantRange> m_pushConstantRanges;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;
	std::set<std::string> m_ambiguousNames;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> m_descriptorBindings;

public:
	PipelineLayout() = default;
	PipelineLayout(const PipelineLayout&) = default;
	auto operator=(const PipelineLayout&) -> PipelineLayout& = default;
	PipelineLayout(PipelineLayout&&) noexcept = default;
	auto operator=(PipelineLayout&&) noexcept -> PipelineLayout& = default;
	~PipelineLayout();

	void Create(const PipelineLayoutCreateInfo& inCreateInfo);
	void Create(const ShaderProgramReflection& inReflection);
	void Destroy();
	auto GetVkPipelineLayout() const -> VkPipelineLayout { return m_vkPipelineLayout; };
	auto GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&;
	auto GetVkPushConstantRanges() const -> const std::vector<VkPushConstantRange>& { return m_pushConstantRanges; }
	auto GetVkDescriptorBindings() const -> const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& { return m_descriptorBindings; }
	auto GetSetBinding(std::string_view inName) const -> std::pair<uint32_t, uint32_t>;
};
