#pragma once
#include "common.h"

class SamplerAllocator;

class SamplerCreateInfo final
{
private:
	VkSamplerCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	friend class Sampler;
	friend class SamplerAllocator;

public:
	SamplerCreateInfo();

	auto Reset()->SamplerCreateInfo&;

	auto SetVkSamplerCreateInfo(const VkSamplerCreateInfo& inCreateInfo)->SamplerCreateInfo&;

	auto SetFilter(VkFilter inMinFilter, VkFilter inMagFilter)->SamplerCreateInfo&;

	auto SetAddressMode(VkSamplerAddressMode inAddressMode)->SamplerCreateInfo&;

	auto SetAddressMode(
		VkSamplerAddressMode inAddressModeU,
		VkSamplerAddressMode inAddressModeV,
		VkSamplerAddressMode inAddressModeW)->SamplerCreateInfo&;

	auto SetMipmapMode(VkSamplerMipmapMode inMipmapMode)->SamplerCreateInfo&;

	auto SetLod(float inMinLod, float inMaxLod, float inMipLodBias = 0.0f)->SamplerCreateInfo&;

	auto EnableAnisotropy(float inMaxAnisotropy)->SamplerCreateInfo&;

	auto DisableAnisotropy()->SamplerCreateInfo&;

	auto GetVkSamplerCreateInfo() const->const VkSamplerCreateInfo&;
};

class Sampler final
{
private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;

public:
	Sampler() = default;
	Sampler(const Sampler&) = delete;
	Sampler& operator=(const Sampler&) = delete;
	~Sampler();

	auto Create(const SamplerCreateInfo* inCreateInfo)->void;

	auto Destroy()->void;

	auto GetVkSampler() const->VkSampler;
};
