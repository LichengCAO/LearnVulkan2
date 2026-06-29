#pragma once
#include "pipeline_common.h"
class ShaderModule;

class SimpleShader
{
private:
	std::string m_spvFile;
	std::set<std::string> m_entries;
	std::vector<char> _ReadFile(const std::string& filename) const;
public:
	VkShaderModule vkShaderModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits vkShaderStage = static_cast<VkShaderStageFlagBits>(0);
	~SimpleShader();
	void SetSPVFile(const std::string& file);
	void Create();
	void Destroy();
	VkPipelineShaderStageCreateInfo GetShaderStageInfo(const std::string& entry);
	VkPipelineShaderStageCreateInfo GetShaderStageInfo();
};

class ShaderModule final
{
private:
	std::string m_spirvFile;
	std::unordered_map<VkShaderStageFlagBits, std::string> m_entries;
	VkShaderModule m_vkShaderModule = VK_NULL_HANDLE;

private:
	auto _GetVkPipelineShaderStageCreateInfo(VkShaderStageFlagBits inStage) const->VkPipelineShaderStageCreateInfo;

public:
	ShaderModule() = default;
	ShaderModule(const ShaderModule& inOther) = delete;
	~ShaderModule();
	bool operator==(const ShaderModule& other) const = delete;

	void Create(const ShaderModuleCreateInfo& inCreateInfo);

	auto GetShaderStageInfo(VkShaderStageFlagBits inStage) const->VkPipelineShaderStageCreateInfo;

	void Destroy();
};
