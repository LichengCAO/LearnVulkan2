#pragma once
#include "pipeline_layout.h"

class ShaderProgramLayout final
{
private:
	PipelineLayout m_pipelineLayout;
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_nameToSetBinding;

public:
	void Create(const std::vector<std::string>& inSpirvFiles);
	void Create(const PipelineLayout& inPipelineLayout);
	void Destroy();

	auto GetLayout() const -> const PipelineLayout&;
	auto GetNameToSetBinding() const -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;
	auto GetPushConstantRanges() const -> const std::vector<VkPushConstantRange>&;
};
