#include "../pipeline_layout.h"
#include "shader_program_reflection.h"
#include "device.h"

auto PipelineLayoutCreateInfo::AddDescriptorSetLayout(
	DescriptorSetLayout inLayout) -> PipelineLayoutCreateInfo&
{
	CHECK_TRUE(inLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE, "Invalid descriptor set layout!");

	m_descriptorSetLayouts.push_back(std::move(inLayout));
	return *this;
}

auto PipelineLayoutCreateInfo::AddPushConstantRange(
	VkShaderStageFlags inStages,
	uint32_t inOffset,
	uint32_t inSize) -> PipelineLayoutCreateInfo&
{
	CHECK_TRUE(inStages != 0, "Push constant stages cannot be empty!");
	CHECK_TRUE(inSize > 0, "Push constant size must be greater than zero!");

	VkPushConstantRange range{};
	range.stageFlags = inStages;
	range.offset = inOffset;
	range.size = inSize;

	m_pushConstantRanges.push_back(range);
	return *this;
}

PipelineLayout::~PipelineLayout()
{
}

void PipelineLayout::Create(const PipelineLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");
	auto layouts = inCreateInfo.m_descriptorSetLayouts;
	auto pushConstantRanges = inCreateInfo.m_pushConstantRanges;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> descriptorBindings;

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(layouts.size());
	descriptorBindings.reserve(layouts.size());
	for (const DescriptorSetLayout& descriptorSetLayout : layouts)
	{
		CHECK_TRUE(
			descriptorSetLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE,
			"Pipeline layout contains an invalid descriptor set layout!");
		descriptorSetLayouts.push_back(descriptorSetLayout.GetVkDescriptorSetLayout());
		descriptorBindings.emplace_back(descriptorSetLayout.m_descriptorBindings.begin(), descriptorSetLayout.m_descriptorBindings.end());
	}

	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	layoutInfo.pSetLayouts = descriptorSetLayouts.data();
	layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	layoutInfo.pPushConstantRanges = pushConstantRanges.data();

	m_vkPipelineLayout = MyDevice::GetInstance().CreatePipelineLayout(layoutInfo);
	m_descriptorSetLayouts = std::move(layouts);
	m_pushConstantRanges = std::move(pushConstantRanges);
	m_descriptorBindings = std::move(descriptorBindings);
	m_nameToSetBinding.clear();
	m_ambiguousNames.clear();
}

void PipelineLayout::Create(const ShaderProgramReflection& reflection)
{
	CHECK_TRUE(m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");
	auto nameToSetBinding = reflection.GetNameToSetBinding();
	auto ambiguousNames = reflection.GetAmbiguousNames();
	const auto& reflectedPushConstants = reflection.GetVkPushConstantRanges();
	if (!reflectedPushConstants.empty())
	{
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(MyDevice::GetInstance().vkPhysicalDevice, &properties);
		for (const auto& range : reflectedPushConstants)
			CHECK_TRUE(range.offset <= properties.limits.maxPushConstantsSize &&
			    range.size <= properties.limits.maxPushConstantsSize - range.offset,
			    "Reflected push constant range exceeds device limits!");
	}

	PipelineLayoutCreateInfo createInfo;
	for (const auto& bindings : reflection.GetVkDescriptorBindings())
	{
		DescriptorSetLayoutCreateInfo layoutCreateInfo;
		for (const auto& [bindingIndex, binding] : bindings)
		{
			CHECK_TRUE(binding.pImmutableSamplers == nullptr, "Immutable samplers are not supported by reflection-driven layouts!");
			layoutCreateInfo.SetBinding(bindingIndex, binding.descriptorType, binding.stageFlags, binding.descriptorCount);
		}
		// Empty layouts preserve sparse set indices.
		DescriptorSetLayout layout;
		layout.Create(layoutCreateInfo);
		createInfo.AddDescriptorSetLayout(std::move(layout));
	}
	for (const auto& range : reflectedPushConstants)
		createInfo.AddPushConstantRange(range.stageFlags, range.offset, range.size);

	Create(createInfo);
	m_nameToSetBinding = std::move(nameToSetBinding);
	m_ambiguousNames = std::move(ambiguousNames);
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
