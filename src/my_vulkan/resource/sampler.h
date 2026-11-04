#pragma once
#include "common.h"

class SamplerAllocator;
class MyDevice;

class SamplerCreateInfo final
{
private:
	VkSamplerCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

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

// Borrowed handle value. Copy, move, assignment, and destruction do not access
// the device or release native resources. The handle is usable only while its
// owning MyDevice sampler cache remains alive.
class Sampler final
{
	friend class MyDevice;
private:
	VkSampler m_vkSampler = VK_NULL_HANDLE;

public:
	Sampler() noexcept = default;
	Sampler(const Sampler&) noexcept = default;
	Sampler(Sampler&&) noexcept = default;
	auto operator=(const Sampler&) noexcept -> Sampler& = default;
	auto operator=(Sampler&&) noexcept -> Sampler& = default;
	~Sampler() noexcept = default;

	auto GetVkSampler() const noexcept -> VkSampler;
};
