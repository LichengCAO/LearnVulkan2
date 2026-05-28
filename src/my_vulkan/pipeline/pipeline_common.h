#pragma once
#include <common.h>

class ComputePipeline;
class RayTracingPipeline;
class GraphicsPipeline;
class GraphicsProgram;

class ShaderModuleCreateInfo final
{
	friend class ShaderModule;
	friend class GraphicsProgram;
private:
	std::string m_spirvFile;
	std::unordered_map<VkShaderStageFlagBits, std::string> m_entries;

public:
	ShaderModuleCreateInfo() = default;
	~ShaderModuleCreateInfo() = default;

	void SetSpirvFile(std::string inString) { m_spirvFile = std::move(inString); };
	void AddEntry(VkShaderStageFlagBits inStage, const std::string& inEntry) { m_entries[inStage] = inEntry; };
};