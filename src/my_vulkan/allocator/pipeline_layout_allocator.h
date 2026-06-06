#pragma once
#include <common.h>

class PipelineLayoutAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipelineLayout> m_mapHashToVkPipelineLayout;

public:
	PipelineLayoutAllocator() = default;
	PipelineLayoutAllocator(const PipelineLayoutAllocator&) = delete;
	PipelineLayoutAllocator& operator=(const PipelineLayoutAllocator&) = delete;
	~PipelineLayoutAllocator();

	void Create(VkDevice inDevice);
	bool AllocatePipelineLayout(const VkPipelineLayoutCreateInfo* inCreateInfo, VkPipelineLayout& outPipelineLayout);
	void FreePipelineLayout(VkPipelineLayout& inoutPipelineLayout);
	void Destroy();
};
