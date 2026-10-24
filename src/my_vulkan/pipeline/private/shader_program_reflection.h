#pragma once
#include "utility/shader_reflection_data.h"
#include <map>

class ShaderProgramReflection final
{
private:
	struct StageInterfaces
	{
		uint32_t entryCount = 0;
		std::vector<ShaderEntryReflection::InterfaceVariable> inputs;
		std::vector<ShaderEntryReflection::InterfaceVariable> outputs;
	};
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;
	std::set<std::string> m_ambiguousNames;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> m_descriptorBindings;
	std::vector<VkPushConstantRange> m_pushConstantRanges;
	std::map<VkShaderStageFlagBits, StageInterfaces> m_interfaces;

	auto _GetInterface(VkShaderStageFlagBits inStage, bool inInput) const -> const std::vector<ShaderEntryReflection::InterfaceVariable>&;
public:
	void Create(const std::vector<ShaderEntryReflection>& inEntries);
	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& { return m_nameToSetBinding; }
	auto GetAmbiguousNames() const -> const std::set<std::string>& { return m_ambiguousNames; }
	auto GetDescriptorBindings() const -> const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& { return m_descriptorBindings; }
	auto GetPushConstantRanges() const -> const std::vector<VkPushConstantRange>& { return m_pushConstantRanges; }
	auto GetInput(VkShaderStageFlagBits inStage) const -> const std::vector<ShaderEntryReflection::InterfaceVariable>& { return _GetInterface(inStage, true); }
	auto GetOutput(VkShaderStageFlagBits inStage) const -> const std::vector<ShaderEntryReflection::InterfaceVariable>& { return _GetInterface(inStage, false); }
};
