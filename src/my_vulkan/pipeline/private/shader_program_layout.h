#pragma once
#include "../pipeline_layout.h"
#include "shader_program_reflection.h"

class ShaderProgramLayout final
{
	friend struct ShaderBindingTestProbe;
private:
	PipelineLayout m_pipelineLayout;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;
	std::set<std::string> m_ambiguousNames;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> m_descriptorBindings;

public:
	void Create(const ShaderProgramReflection& inReflection);
	void Destroy();
	auto GetDescriptorBindings() const -> const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& { return m_descriptorBindings; }
	auto GetAmbiguousNames() const -> const std::set<std::string>& { return m_ambiguousNames; }

	auto GetLayout() const -> const PipelineLayout&;
	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	auto GetPushConstantRanges() const -> const std::vector<VkPushConstantRange>&;
};
