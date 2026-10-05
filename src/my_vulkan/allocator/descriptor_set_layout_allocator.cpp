#include "descriptor_set_layout_allocator.h"

#include "../device.h"
#include "utility/hash_util.h"

#include <algorithm>
#include <type_traits>

namespace
{
	template <class Enum>
	void HashEnum(size_t& seed, Enum value)
	{
		hash_combine(seed, static_cast<std::underlying_type_t<Enum>>(value));
	}

	void HashBinding(size_t& seed, const VkDescriptorSetLayoutBinding& binding)
	{
		hash_combine(seed, binding.binding);
		HashEnum(seed, binding.descriptorType);
		hash_combine(seed, binding.descriptorCount);
		hash_combine(seed, binding.stageFlags);
		// Immutable samplers are currently rejected by DescriptorSetLayout::SetBinding.
		CHECK_TRUE(binding.pImmutableSamplers == nullptr, "Immutable samplers are not supported by the layout allocator!");
	}

}

auto DescriptorSetLayoutAllocator::_GetSortedBindings(
	const DescriptorSetLayout& inLayout) const
	-> std::vector<VkDescriptorSetLayoutBinding>
{
	std::vector<VkDescriptorSetLayoutBinding> sortedBindings;
	sortedBindings.reserve(inLayout.m_descriptorBindings.size());
	for (const auto& entry : inLayout.m_descriptorBindings)
	{
		sortedBindings.push_back(entry.second);
	}
	std::sort(
		sortedBindings.begin(),
		sortedBindings.end(),
		[](const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs)
		{
			return lhs.binding < rhs.binding;
		});
	return sortedBindings;
}

auto DescriptorSetLayoutAllocator::_HashBindings(
	const std::vector<VkDescriptorSetLayoutBinding>& layoutBindings) const
	-> uint64_t
{
	size_t result = 0;
	hash_combine(result, layoutBindings.size());
	for (const VkDescriptorSetLayoutBinding& binding : layoutBindings)
	{
		HashBinding(result, binding);
	}
	return static_cast<uint64_t>(result);
}

auto DescriptorSetLayoutAllocator::_IsEqual(
	const std::vector<VkDescriptorSetLayoutBinding>& lhs,
	const std::vector<VkDescriptorSetLayoutBinding>& rhs) const
	-> bool
{
	if (lhs.size() != rhs.size())
	{
		return false;
	}

	for (size_t index = 0; index < lhs.size(); ++index)
	{
		const VkDescriptorSetLayoutBinding& left = lhs[index];
		const VkDescriptorSetLayoutBinding& right = rhs[index];
		if (left.binding != right.binding ||
			left.descriptorType != right.descriptorType ||
			left.descriptorCount != right.descriptorCount ||
			left.stageFlags != right.stageFlags ||
			left.pImmutableSamplers != right.pImmutableSamplers)
		{
			return false;
		}
	}

	return true;
}

DescriptorSetLayoutAllocator::~DescriptorSetLayoutAllocator()
{
	Destroy();
}

void DescriptorSetLayoutAllocator::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid descriptor set layout allocator device!");
	CHECK_TRUE(
		m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice,
		"Descriptor set layout allocator already uses another device!");
	m_vkDevice = inDevice;
}

auto DescriptorSetLayoutAllocator::Allocate(
	const DescriptorSetLayout& inLayout) -> VkDescriptorSetLayout
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Descriptor set layout allocator is not created!");

	const std::vector<VkDescriptorSetLayoutBinding> layoutBindings = _GetSortedBindings(inLayout);
	const uint64_t hash = _HashBindings(layoutBindings);
	std::vector<CacheEntry>& entries = m_cache[hash];
	for (const CacheEntry& entry : entries)
	{
		if (_IsEqual(entry.layoutBindings, layoutBindings))
		{
			return entry.descriptorSetLayout;
		}
	}

	VkDescriptorSetLayoutCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	createInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
	createInfo.pBindings = layoutBindings.data();

	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VK_CHECK(
		vkCreateDescriptorSetLayout(m_vkDevice, &createInfo, nullptr, &descriptorSetLayout),
		"Failed to create descriptor set layout!");
	entries.push_back(CacheEntry{ layoutBindings, descriptorSetLayout });
	return descriptorSetLayout;
}

void DescriptorSetLayoutAllocator::Destroy()
{
	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (auto& [hash, entries] : m_cache)
		{
			(void)hash;
			for (CacheEntry& entry : entries)
			{
				if (entry.descriptorSetLayout != VK_NULL_HANDLE)
				{
					vkDestroyDescriptorSetLayout(m_vkDevice, entry.descriptorSetLayout, nullptr);
					entry.descriptorSetLayout = VK_NULL_HANDLE;
				}
			}
		}
	}

	m_cache.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
