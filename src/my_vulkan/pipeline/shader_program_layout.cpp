#include "shader_program_layout.h"
#include "shader_reflect.h"

void ShaderProgramLayout::Create(const std::vector<std::string>& inSpirvFiles)
{
	CHECK_TRUE(m_pipelineLayout.GetVkPipelineLayout() == VK_NULL_HANDLE, "Shader program layout already created!");
	CHECK_TRUE(!inSpirvFiles.empty(), "Shader program layout needs shaders!");
	for (const auto& spirvFile : inSpirvFiles)
	{
		CHECK_TRUE(!spirvFile.empty(), "Shader SPIR-V file is unset!");
	}

	ShaderReflector reflector;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> nameToSetBinding;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> descriptorSetData;
	std::unordered_map<std::string, uint32_t> pushConstantIndex;
	std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> reflectedPushConstants;

	reflector.Create(inSpirvFiles);
	reflector.ReflectDescriptorSets(nameToSetBinding, descriptorSetData);
	reflector.ReflectPushConst(pushConstantIndex, reflectedPushConstants);

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

	for (const auto& [stages, range] : reflectedPushConstants)
	{
		pipelineLayoutCreateInfo.AddPushConstantRange(stages, range.first, range.second);
	}

	PipelineLayout pipelineLayout;
	pipelineLayout.Create(pipelineLayoutCreateInfo);
	m_pipelineLayout = std::move(pipelineLayout);
	m_nameToSetBinding = std::move(nameToSetBinding);
}

void ShaderProgramLayout::Create(const PipelineLayout& inPipelineLayout)
{
	CHECK_TRUE(m_pipelineLayout.GetVkPipelineLayout() == VK_NULL_HANDLE, "Shader program layout already created!");
	CHECK_TRUE(inPipelineLayout.GetVkPipelineLayout() != VK_NULL_HANDLE, "Invalid shader program pipeline layout!");

	m_pipelineLayout = inPipelineLayout;
}

void ShaderProgramLayout::Destroy()
{
	m_pipelineLayout.Destroy();
	m_nameToSetBinding.clear();
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
