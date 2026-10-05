#include "pipeline_layout_allocator.h"
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

	void HashPipelineLayoutCreateInfoPNext(size_t& seed, const void* pNext)
	{
		CHECK_TRUE(pNext == nullptr, "Unsupported pipeline layout create info pNext!");
		hash_combine(seed, static_cast<uint32_t>(pNext != nullptr));
	}

	void HashPushConstantRange(size_t& seed, const VkPushConstantRange& range)
	{
		hash_combine(seed, range.stageFlags);
		hash_combine(seed, range.offset);
		hash_combine(seed, range.size);
	}

	uint64_t HashPipelineLayoutCreateInfo(const VkPipelineLayoutCreateInfo& createInfo)
	{
		size_t result = 0;

		HashEnum(result, createInfo.sType);
		HashPipelineLayoutCreateInfoPNext(result, createInfo.pNext);
		hash_combine(result, createInfo.flags);

		hash_combine(result, createInfo.setLayoutCount);
		for (uint32_t i = 0; i < createInfo.setLayoutCount; i++)
		{
			hash_combine(result, createInfo.pSetLayouts[i]);
		}

		hash_combine(result, createInfo.pushConstantRangeCount);
		for (uint32_t i = 0; i < createInfo.pushConstantRangeCount; i++)
		{
			HashPushConstantRange(result, createInfo.pPushConstantRanges[i]);
		}

		return static_cast<uint64_t>(result);
	}
}

PipelineLayoutAllocator::~PipelineLayoutAllocator()
{
	Destroy();
}

void PipelineLayoutAllocator::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid pipeline layout allocator device!");
	CHECK_TRUE(m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice, "Pipeline layout allocator already uses another device!");

	m_vkDevice = inDevice;
}

auto PipelineLayoutAllocator::AllocatePipelineLayoutWithResult(
	const VkPipelineLayoutCreateInfo* inCreateInfo) -> std::pair<VkPipelineLayout, VkResult>
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Pipeline layout allocator is not created!");
	CHECK_TRUE(inCreateInfo != nullptr, "Missing pipeline layout create info!");

	const uint64_t hash = HashPipelineLayoutCreateInfo(*inCreateInfo);
	std::vector<CacheEntry>& entries = m_mapHashToVkPipelineLayout[hash];
	for (const CacheEntry& entry : entries)
	{
		if (entry.structureType != inCreateInfo->sType ||
			entry.flags != inCreateInfo->flags ||
			entry.descriptorSetLayouts.size() != inCreateInfo->setLayoutCount ||
			entry.pushConstantRanges.size() != inCreateInfo->pushConstantRangeCount)
		{
			continue;
		}

		if (!std::equal(
			entry.descriptorSetLayouts.begin(),
			entry.descriptorSetLayouts.end(),
			inCreateInfo->pSetLayouts))
		{
			continue;
		}

		if (!std::equal(
			entry.pushConstantRanges.begin(),
			entry.pushConstantRanges.end(),
			inCreateInfo->pPushConstantRanges,
			[](const VkPushConstantRange& lhs, const VkPushConstantRange& rhs)
			{
				return lhs.stageFlags == rhs.stageFlags &&
					lhs.offset == rhs.offset &&
					lhs.size == rhs.size;
			}))
		{
			continue;
		}

		return { entry.pipelineLayout, VK_SUCCESS };
	}

	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	const VkResult result = vkCreatePipelineLayout(m_vkDevice, inCreateInfo, nullptr, &pipelineLayout);
	if (result != VK_SUCCESS)
	{
		return { VK_NULL_HANDLE, result };
	}

	CacheEntry entry{};
	entry.structureType = inCreateInfo->sType;
	entry.flags = inCreateInfo->flags;
	if (inCreateInfo->setLayoutCount > 0)
	{
		entry.descriptorSetLayouts.assign(
			inCreateInfo->pSetLayouts,
			inCreateInfo->pSetLayouts + inCreateInfo->setLayoutCount);
	}
	if (inCreateInfo->pushConstantRangeCount > 0)
	{
		entry.pushConstantRanges.assign(
			inCreateInfo->pPushConstantRanges,
			inCreateInfo->pPushConstantRanges + inCreateInfo->pushConstantRangeCount);
	}
	entry.pipelineLayout = pipelineLayout;
	entries.push_back(std::move(entry));
	return { pipelineLayout, VK_SUCCESS };
}

auto PipelineLayoutAllocator::AllocatePipelineLayout(const VkPipelineLayoutCreateInfo* inCreateInfo) -> VkPipelineLayout
{
	auto [pipelineLayout, result] = AllocatePipelineLayoutWithResult(inCreateInfo);
	VK_CHECK(result, "Failed to allocate pipeline layout!");

	return pipelineLayout;
}

void PipelineLayoutAllocator::FreePipelineLayout(VkPipelineLayout& inoutPipelineLayout)
{
	inoutPipelineLayout = VK_NULL_HANDLE;
}

void PipelineLayoutAllocator::Destroy()
{
	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (auto& [hash, entries] : m_mapHashToVkPipelineLayout)
		{
			(void)hash;
			for (CacheEntry& entry : entries)
			{
				if (entry.pipelineLayout != VK_NULL_HANDLE)
				{
					vkDestroyPipelineLayout(m_vkDevice, entry.pipelineLayout, nullptr);
					entry.pipelineLayout = VK_NULL_HANDLE;
				}
			}
		}
	}

	m_mapHashToVkPipelineLayout.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
