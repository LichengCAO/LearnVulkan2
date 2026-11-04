#include "../pipeline_layout.h"
#include "device.h"

PipelineLayout::~PipelineLayout()
{
}

void PipelineLayout::Create(const PipelineLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "Pipeline layout create info must be finalized!");
	CHECK_TRUE(m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");
	const auto& data = *inCreateInfo.m_finalized;
	const auto& properties = MyDevice::GetInstance().GetCachedPhysicalDeviceProperties();
	for (const auto& range : data.pushConstants)
	{
		CHECK_TRUE(range.offset <= properties.limits.maxPushConstantsSize &&
			range.size <= properties.limits.maxPushConstantsSize - range.offset,
			"Push constant range exceeds device limits!");
	}

	auto pushConstantRanges = data.pushConstants;
	auto descriptorBindings = data.bindings;
	auto nameToSetBinding = data.names;
	auto ambiguousNames = data.ambiguousNames;
	std::vector<DescriptorSetLayout> layouts(data.layouts.size());

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(layouts.size());
	for (size_t index = 0; index < layouts.size(); ++index)
	{
		// Empty descriptions still create layouts to preserve sparse set indices.
		layouts[index].Create(data.layouts[index]);
		descriptorSetLayouts.push_back(layouts[index].GetVkDescriptorSetLayout());
	}

	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	layoutInfo.pSetLayouts = descriptorSetLayouts.data();
	layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	layoutInfo.pPushConstantRanges = pushConstantRanges.data();

	const VkPipelineLayout pipelineLayout = MyDevice::GetInstance().CreatePipelineLayout(layoutInfo);
	m_descriptorSetLayouts = std::move(layouts);
	m_pushConstantRanges = std::move(pushConstantRanges);
	m_descriptorBindings = std::move(descriptorBindings);
	m_nameToSetBinding = std::move(nameToSetBinding);
	m_ambiguousNames = std::move(ambiguousNames);
	m_vkPipelineLayout = pipelineLayout;
}

void PipelineLayout::Destroy()
{
	m_vkPipelineLayout = VK_NULL_HANDLE;
	m_descriptorSetLayouts.clear();
	m_pushConstantRanges.clear();
	m_nameToSetBinding.clear();
	m_ambiguousNames.clear();
	m_descriptorBindings.clear();
}

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
