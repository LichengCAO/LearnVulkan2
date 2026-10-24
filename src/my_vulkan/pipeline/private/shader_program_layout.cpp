#include "shader_program_layout.h"
#include "device.h"

void ShaderProgramLayout::Create(const ShaderProgramReflection& reflection)
{
	CHECK_TRUE(m_pipelineLayout.GetVkPipelineLayout() == VK_NULL_HANDLE, "Shader program layout already created!");
	auto nameToSetBinding = reflection.GetNameToSetBinding();
	auto descriptorSetData = reflection.GetDescriptorBindings();
	auto ambiguousNames = reflection.GetAmbiguousNames();
	const auto& reflectedPushConstants = reflection.GetPushConstantRanges();
	if (!reflectedPushConstants.empty())
	{
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(MyDevice::GetInstance().vkPhysicalDevice, &properties);
		for (const auto& range : reflectedPushConstants)
			CHECK_TRUE(range.offset <= properties.limits.maxPushConstantsSize &&
			    range.size <= properties.limits.maxPushConstantsSize - range.offset,
			    "Reflected push constant range exceeds device limits!");
	}

	PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	for (const auto& descriptorSetBindings : descriptorSetData)
	{
		DescriptorSetLayoutCreateInfo layoutCreateInfo;
		for (const auto& [bindingIndex, layoutBinding] : descriptorSetBindings)
		{
			CHECK_TRUE(layoutBinding.pImmutableSamplers == nullptr, "Immutable samplers are not supported by reflection-driven layouts!");
			layoutCreateInfo.SetBinding(
				bindingIndex,
				layoutBinding.descriptorType,
				layoutBinding.stageFlags,
				layoutBinding.descriptorCount);
		}

		// Empty entries preserve the reflected set indices for sparse descriptor sets.
		DescriptorSetLayout descriptorSetLayout;
		descriptorSetLayout.Create(layoutCreateInfo);
		pipelineLayoutCreateInfo.AddDescriptorSetLayout(std::move(descriptorSetLayout));
	}

	for (const auto& range : reflectedPushConstants)
	{
		pipelineLayoutCreateInfo.AddPushConstantRange(range.stageFlags, range.offset, range.size);
	}

	PipelineLayout pipelineLayout;
	pipelineLayout.Create(pipelineLayoutCreateInfo);
	m_pipelineLayout = std::move(pipelineLayout);
	m_nameToSetBinding = std::move(nameToSetBinding);
	m_ambiguousNames = std::move(ambiguousNames);
	m_descriptorBindings = std::move(descriptorSetData);
}

void ShaderProgramLayout::Destroy()
{
	m_pipelineLayout.Destroy();
	m_nameToSetBinding.clear();
	m_ambiguousNames.clear();
	m_descriptorBindings.clear();
}

auto ShaderProgramLayout::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}

auto ShaderProgramLayout::GetNameToSetBinding() const
	-> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&
{
	return m_nameToSetBinding;
}

auto ShaderProgramLayout::GetPushConstantRanges() const -> const std::vector<VkPushConstantRange>&
{
	return m_pipelineLayout.GetPushConstantRanges();
}
