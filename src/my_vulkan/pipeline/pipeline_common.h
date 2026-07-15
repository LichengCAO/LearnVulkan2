#pragma once
#include <common.h>

class ComputeShaderProgram;
class RayTracingShaderProgram;
class GraphicsShaderProgram;

class ShaderModuleCreateInfo final
{
	friend class ShaderModule;
	friend class GraphicsShaderProgram;
	friend class GraphicsShaderProgramCreateInfo;
private:
	std::string m_spirvFile;
	std::unordered_map<VkShaderStageFlagBits, std::string> m_entries;

public:
	ShaderModuleCreateInfo() = default;
	~ShaderModuleCreateInfo() = default;

	void SetSpirvFile(std::string inString) { m_spirvFile = std::move(inString); };
	void AddEntry(VkShaderStageFlagBits inStage, const std::string& inEntry) { m_entries[inStage] = inEntry; };
};
