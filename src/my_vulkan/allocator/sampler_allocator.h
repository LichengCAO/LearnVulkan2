#pragma once
#include "resource/sampler.h"

class SamplerAllocator final
{
private:
	struct SamplerInfo
	{
		VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		auto operator==(const SamplerInfo& inOther) const->bool;
	};

	struct SamplerEntry
	{
		VkSampler vkSampler = VK_NULL_HANDLE;
		SamplerInfo info{};
		uint32_t refCount = 0;
		uint32_t nextId = static_cast<uint32_t>(~0);
	};

	struct SamplerInfoHash
	{
		auto operator()(const SamplerInfo& inInfo) const->std::size_t;
	};

private:
	std::unordered_map<SamplerInfo, uint32_t, SamplerInfoHash> m_mapInfoToIndex;
	std::unordered_map<VkSampler, uint32_t> m_mapSamplerToIndex;
	std::vector<SamplerEntry> m_vecSamplerEntries;
	uint32_t m_currentId = static_cast<uint32_t>(~0);

private:
	auto _AllocateSamplerWithResult(const SamplerInfo& inInfo)->std::pair<VkSampler, VkResult>;

public:
	SamplerAllocator() = default;
	SamplerAllocator(const SamplerAllocator&) = delete;
	SamplerAllocator& operator=(const SamplerAllocator&) = delete;
	~SamplerAllocator();

	auto Create()->void;

	auto AllocateSamplerWithResult(const SamplerCreateInfo* inCreateInfo)->std::pair<VkSampler, VkResult>;

	auto AllocateSampler(const SamplerCreateInfo* inCreateInfo)->VkSampler;

	auto FreeSampler(VkSampler& inoutSampler)->void;

	auto Destroy()->void;
};
