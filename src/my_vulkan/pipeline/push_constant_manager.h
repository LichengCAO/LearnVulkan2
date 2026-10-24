#pragma once
#include "common.h"

class PushConstantManager
{
private:
	uint32_t m_maxSize = 128;
	std::unordered_map<VkShaderStageFlags, std::pair<uint32_t, uint32_t>> m_mapRange;
	VkShaderStageFlags m_usedStages = 0;

private:
	static auto _GetBitsFromStageFlags(VkShaderStageFlags _flags) -> std::vector<VkShaderStageFlagBits>;

public:
	explicit PushConstantManager(uint32_t inMaxSize = 128);

	// Add a push constant in the input stage, the stage is not allowed to assigned twice
	void AddConstantRange(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);

	// Push constant to the input stage
	void PushConstant(VkCommandBuffer _cmd, VkPipelineLayout _layout, VkShaderStageFlags _stage, const void* _data) const;

	// Get VkPushConstantRanges managed
	void GetVkPushConstantRanges(std::vector<VkPushConstantRange>& _outRanges) const;
};
