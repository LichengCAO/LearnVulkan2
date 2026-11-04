#pragma once
#include "resource/sampler.h"

// Owns cached native samplers until Destroy. All cache access is externally
// synchronized by the owner; borrowed Sampler wrappers do not retain entries.
class SamplerAllocator final
{
private:
	struct SamplerInfo
	{
		VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		auto operator==(const SamplerInfo& inOther) const->bool;
	};

	struct SamplerInfoHash
	{
		auto operator()(const SamplerInfo& inInfo) const->std::size_t;
	};

private:
	std::unordered_map<SamplerInfo, VkSampler, SamplerInfoHash> m_mapInfoToSampler;

public:
	SamplerAllocator() = default;
	SamplerAllocator(const SamplerAllocator&) = delete;
	auto operator=(const SamplerAllocator&) -> SamplerAllocator& = delete;
	~SamplerAllocator() noexcept;

	void Create();

	auto AllocateSamplerWithResult(const SamplerCreateInfo& inCreateInfo)->std::pair<VkSampler, VkResult>;

	auto AllocateSampler(const SamplerCreateInfo& inCreateInfo)->VkSampler;

	void Destroy() noexcept;
};
