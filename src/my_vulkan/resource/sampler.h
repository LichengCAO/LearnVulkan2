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

	auto CustomizeFilter(VkFilter inMinFilter, VkFilter inMagFilter)->SamplerCreateInfo&;

	auto CustomizeAddressMode(VkSamplerAddressMode inAddressMode)->SamplerCreateInfo&;

	auto CustomizeAddressMode(
		VkSamplerAddressMode inAddressModeU,
		VkSamplerAddressMode inAddressModeV,
		VkSamplerAddressMode inAddressModeW)->SamplerCreateInfo&;

	auto CustomizeMipmapMode(VkSamplerMipmapMode inMipmapMode)->SamplerCreateInfo&;

	auto CustomizeLod(float inMinLod, float inMaxLod, float inMipLodBias = 0.0f)->SamplerCreateInfo&;

	auto CustomizeAnisotropy(float inMaxAnisotropy)->SamplerCreateInfo&;

	auto CustomizeCompare(VkCompareOp inCompareOp)->SamplerCreateInfo&;

	auto CustomizeBorderColor(VkBorderColor inBorderColor)->SamplerCreateInfo&;

	auto CustomizeUnnormalizedCoordinates()->SamplerCreateInfo&;

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
