#pragma once
#include "common.h"
#include "resource/descriptor.h"
#include <string_view>

class ShaderProgramReflection;

#include "pipeline_layout_create_info.h"

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
	void Destroy();
	auto GetVkPipelineLayout() const -> VkPipelineLayout { return m_vkPipelineLayout; };
	auto GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&;
	auto GetVkPushConstantRanges() const -> const std::vector<VkPushConstantRange>& { return m_pushConstantRanges; }
	auto GetVkDescriptorBindings() const -> const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& { return m_descriptorBindings; }
	auto GetSetBinding(std::string_view inName) const -> std::pair<uint32_t, uint32_t>;
};
