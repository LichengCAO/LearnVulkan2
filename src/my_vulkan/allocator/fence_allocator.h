#pragma once
#include "common.h"

class FenceAllocator final
{
private:
	struct FenceEntry
	{
		VkFence vkFence = VK_NULL_HANDLE;
		bool allocated = false;
		uint32_t nextId = static_cast<uint32_t>(~0);
	};

private:
	std::unordered_map<VkFence, uint32_t> m_mapFenceToIndex;
	std::vector<FenceEntry> m_vecFenceEntries;
	uint32_t m_currentId = static_cast<uint32_t>(~0);

private:
	auto _HasVkFence(VkFence inFence) const->bool;
	auto _CreateVkFenceWithResult()->std::pair<VkFence, VkResult>;
	auto _PopFreeFence()->VkFence;
	auto _PushFreeFence(uint32_t inIndex)->void;

public:
	FenceAllocator() = default;
	FenceAllocator(const FenceAllocator&) = delete;
	FenceAllocator& operator=(const FenceAllocator&) = delete;
	~FenceAllocator();

	auto Create()->void;

	auto CreateOrGetVkFenceWithResult()->std::pair<VkFence, VkResult>;

	auto CreateOrGetVkFence()->VkFence;

	auto FreeVkFence(const VkFence* inFences, size_t inCount)->void;

	auto Destroy()->void;
};
