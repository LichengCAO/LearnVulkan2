#pragma once
#include "common.h"
#include <spirv_reflect.h>
#include <map>

// A borrowed view of one exact entry record; its module must outlive the view.
struct ShaderEntryReflection
{
	const SpvReflectShaderModule* module;
	const SpvReflectEntryPoint* entry;
};

class ShaderReflector final
{
private:
	struct ModuleDeleter
	{
		void operator()(SpvReflectShaderModule* inModule) const;
	};
	std::vector<std::unique_ptr<SpvReflectShaderModule, ModuleDeleter>> m_vecReflectModule;
	std::vector<ShaderEntryReflection> m_entries;

private:
	auto _FindEntry(VkShaderStageFlagBits inStage) const -> const SpvReflectEntryPoint*;

public:
	ShaderReflector() = default;
	ShaderReflector(const ShaderReflector&) = delete;
	auto operator=(const ShaderReflector&) -> ShaderReflector& = delete;
	~ShaderReflector();

	// start reflect shaders of the same pipeline
	void Create(const std::vector<std::string>& _spirvFiles);
	void Create(const std::vector<ShaderEntryReflection>& inEntries);
	auto GetEntries() const -> const std::vector<ShaderEntryReflection>& { return m_entries; }

	// reflect descriptor sets of the pipeline,
	// _mapSetBinding: output, map descriptor set name to {set, binding}, some descriptor sets may not have name
	// _descriptorSetData: output, descriptor bindings of set0, set1, ..., the sets are presented by ascending order
	void ReflectDescriptorSets(
		std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& _mapSetBinding,
		std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& _descriptorSetData,
		std::set<std::string>* outAmbiguousNames = nullptr) const;

	// reflect input of the pipeline stage,
	// _mapLocation: output, map input name to location
	// _mapFormat: output, map location to VkFormat, use map instead of unordered_map so that it can be accessed by ascending order
	void ReflectInput(
		VkShaderStageFlagBits _stage,
		std::unordered_map<std::string, uint32_t>& _mapLocation,
		std::map<uint32_t, VkFormat>& _mapFormat) const;

	// reflect output of the pipeline stage,
	// _mapLocation: output, map output name to location
	// _mapFormat: output, map location to VkFormat, use map instead of unordered_map so that it can be accessed by ascending order
	void ReflectOutput(
		VkShaderStageFlagBits _stage, 
		std::unordered_map<std::string, uint32_t>& _mapLocation,
		std::map<uint32_t, VkFormat>& _mapFormat) const;

	// reflect push constant of the pipeline,
	// _mapIndex: output, map output name to index in _pushConstInfo
	// _pushConstInfo: output, store the stages and size of push constant
	void ReflectPushConst(
		std::unordered_map<std::string, uint32_t>& _mapIndex,
		std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>>& _pushConstInfo) const;

	void PrintReflectResult() const;

	void Destroy();
};
