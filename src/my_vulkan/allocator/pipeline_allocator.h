#pragma once
#include <common.h>

class GraphicsPipelineAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipeline> m_mapHashToVkPipeline;

public:
	GraphicsPipelineAllocator() = default;
	GraphicsPipelineAllocator(const GraphicsPipelineAllocator&) = delete;
	GraphicsPipelineAllocator& operator=(const GraphicsPipelineAllocator&) = delete;
	~GraphicsPipelineAllocator();

	void Create(VkDevice inDevice);
	bool AllocateGraphicsPipeline(
		const VkGraphicsPipelineCreateInfo* inCreateInfo,
		VkPipelineCache inCache,
		VkPipeline& outPipeline);
	void FreeGraphicsPipeline(VkPipeline& inoutPipeline);
	void Destroy();
};
