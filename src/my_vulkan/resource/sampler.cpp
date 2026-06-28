#include "sampler.h"
#include "device.h"
#include "allocator/sampler_allocator.h"

namespace
{
	auto _NormalizeSamplerCreateInfo(const VkSamplerCreateInfo& inCreateInfo)->VkSamplerCreateInfo
	{
		VkSamplerCreateInfo result = inCreateInfo;

		result.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		result.pNext = nullptr;

		return result;
	}
}

SamplerCreateInfo::SamplerCreateInfo()
{
	Reset();
}

auto SamplerCreateInfo::Reset()->SamplerCreateInfo&
{
	m_createInfo = VkSamplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	m_createInfo.magFilter = VK_FILTER_LINEAR;
	m_createInfo.minFilter = VK_FILTER_LINEAR;
	m_createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	m_createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	m_createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	m_createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	m_createInfo.mipLodBias = 0.0f;
	m_createInfo.anisotropyEnable = VK_FALSE;
	m_createInfo.maxAnisotropy = 1.0f;
	m_createInfo.compareEnable = VK_FALSE;
	m_createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	m_createInfo.minLod = 0.0f;
	m_createInfo.maxLod = VK_LOD_CLAMP_NONE;
	m_createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	m_createInfo.unnormalizedCoordinates = VK_FALSE;

	return *this;
}

auto SamplerCreateInfo::SetVkSamplerCreateInfo(const VkSamplerCreateInfo& inCreateInfo)->SamplerCreateInfo&
{
	m_createInfo = _NormalizeSamplerCreateInfo(inCreateInfo);

	return *this;
}

auto SamplerCreateInfo::SetFilter(VkFilter inMinFilter, VkFilter inMagFilter)->SamplerCreateInfo&
{
	m_createInfo.minFilter = inMinFilter;
	m_createInfo.magFilter = inMagFilter;

	return *this;
}

auto SamplerCreateInfo::SetAddressMode(VkSamplerAddressMode inAddressMode)->SamplerCreateInfo&
{
	return SetAddressMode(inAddressMode, inAddressMode, inAddressMode);
}

auto SamplerCreateInfo::SetAddressMode(
	VkSamplerAddressMode inAddressModeU,
	VkSamplerAddressMode inAddressModeV,
	VkSamplerAddressMode inAddressModeW)->SamplerCreateInfo&
{
	m_createInfo.addressModeU = inAddressModeU;
	m_createInfo.addressModeV = inAddressModeV;
	m_createInfo.addressModeW = inAddressModeW;

	return *this;
}

auto SamplerCreateInfo::SetMipmapMode(VkSamplerMipmapMode inMipmapMode)->SamplerCreateInfo&
{
	m_createInfo.mipmapMode = inMipmapMode;

	return *this;
}

auto SamplerCreateInfo::SetLod(float inMinLod, float inMaxLod, float inMipLodBias)->SamplerCreateInfo&
{
	m_createInfo.minLod = inMinLod;
	m_createInfo.maxLod = inMaxLod;
	m_createInfo.mipLodBias = inMipLodBias;

	return *this;
}

auto SamplerCreateInfo::EnableAnisotropy(float inMaxAnisotropy)->SamplerCreateInfo&
{
	m_createInfo.anisotropyEnable = VK_TRUE;
	m_createInfo.maxAnisotropy = inMaxAnisotropy;

	return *this;
}

auto SamplerCreateInfo::DisableAnisotropy()->SamplerCreateInfo&
{
	m_createInfo.anisotropyEnable = VK_FALSE;
	m_createInfo.maxAnisotropy = 1.0f;

	return *this;
}

auto SamplerCreateInfo::GetVkSamplerCreateInfo() const->const VkSamplerCreateInfo&
{
	return m_createInfo;
}

Sampler::~Sampler()
{
	assert(m_vkSampler == VK_NULL_HANDLE);
}

auto Sampler::Create(const SamplerCreateInfo* inCreateInfo)->void
{
	CHECK_TRUE(inCreateInfo != nullptr, "No sampler create info!");
	CHECK_TRUE(m_vkSampler == VK_NULL_HANDLE, "Sampler already created!");

	m_vkSampler = MyDevice::GetInstance().GetSamplerAllocator()->AllocateSampler(inCreateInfo);
}

auto Sampler::Destroy()->void
{
	if (m_vkSampler != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().GetSamplerAllocator()->FreeSampler(m_vkSampler);
	}
}

auto Sampler::GetVkSampler() const->VkSampler
{
	return m_vkSampler;
}
