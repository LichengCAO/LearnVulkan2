#pragma once

#include "common.h"

class DescriptorSetLayoutAllocator final
{
private:
	struct CacheEntry final
	{
		std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	};

	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, std::vector<CacheEntry>> m_cache;

	auto _HashBindings(
		const std::vector<VkDescriptorSetLayoutBinding>& inLayoutBindings) const
		-> uint64_t;
	auto _IsEqual(
		const std::vector<VkDescriptorSetLayoutBinding>& inLhs,
		const std::vector<VkDescriptorSetLayoutBinding>& inRhs) const
		-> bool;

public:
	DescriptorSetLayoutAllocator() = default;
	DescriptorSetLayoutAllocator(const DescriptorSetLayoutAllocator&) = delete;
	auto operator=(const DescriptorSetLayoutAllocator&) -> DescriptorSetLayoutAllocator& = delete;
	~DescriptorSetLayoutAllocator();

	void Create(VkDevice inDevice);
	// Input bindings are sorted; callers serialize access to this cache, including
	// its destruction. Returned native handles remain owned by the cache.
	auto GetOrCreate(const std::vector<VkDescriptorSetLayoutBinding>& inSortedBindings) -> VkDescriptorSetLayout;
	void Destroy();
};
