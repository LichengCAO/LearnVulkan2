#include "../pipeline_layout.h"

auto PipelineLayout::GetSetBinding(std::string_view inName) const -> std::pair<uint32_t, uint32_t>
{
	const std::string name(inName);
	CHECK_TRUE(!m_ambiguousNames.contains(name), "Ambiguous shader resource: " + name);
	const auto it = m_nameToSetBinding.find(name);
	CHECK_TRUE(it != m_nameToSetBinding.end(), "Unknown shader resource: " + name);
	return it->second;
}

auto PipelineLayout::GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&
{
	CHECK_TRUE(inSet < m_descriptorSetLayouts.size(), "Pipeline layout descriptor set index is out of range!");
	return m_descriptorSetLayouts[inSet];
}
