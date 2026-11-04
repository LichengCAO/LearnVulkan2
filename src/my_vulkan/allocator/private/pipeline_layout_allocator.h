#pragma once
#include <common.h>

class PipelineLayoutAllocator final
{
private:
	struct CacheEntry final
	{
		VkStructureType structureType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		VkPipelineLayoutCreateFlags flags = 0;
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
		std::vector<VkPushConstantRange> pushConstantRanges;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	};

	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, std::vector<CacheEntry>> m_mapHashToVkPipelineLayout;

public:
	PipelineLayoutAllocator() = default;
	PipelineLayoutAllocator(const PipelineLayoutAllocator&) = delete;
	auto operator=(const PipelineLayoutAllocator&) -> PipelineLayoutAllocator& = delete;
	~PipelineLayoutAllocator();

	void Create(VkDevice inDevice);
	auto IsCreated() const noexcept -> bool { return m_vkDevice != VK_NULL_HANDLE; }
	// No internal locking. Callers serialize lookup, creation and cache teardown.
	// The cache owns each returned native handle until Destroy().
	auto GetOrCreate(const VkPipelineLayoutCreateInfo& inCreateInfo) -> VkPipelineLayout;
	void Destroy();
};
