#pragma once
#include "common.h"

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
	void Init();
	void Uninit();
	VkPipelineShaderStageCreateInfo GetShaderStageInfo(const std::string& entry);
	VkPipelineShaderStageCreateInfo GetShaderStageInfo();
};