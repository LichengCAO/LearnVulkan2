#pragma once

#include "../resource/descriptor_set.h"

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

	auto _GetSortedBindings(
		const DescriptorSetLayout& inLayout) const
		-> std::vector<VkDescriptorSetLayoutBinding>;
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
	DescriptorSetLayoutAllocator& operator=(const DescriptorSetLayoutAllocator&) = delete;
	~DescriptorSetLayoutAllocator();

	void Create(VkDevice inDevice);
	auto Allocate(const DescriptorSetLayout& inLayout) -> VkDescriptorSetLayout;
	void Destroy();
};
