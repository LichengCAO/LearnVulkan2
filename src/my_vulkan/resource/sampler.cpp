#include "sampler.h"
#include "device.h"
#include "allocator/sampler_allocator.h"

SamplerCreateInfo::SamplerCreateInfo()
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
}

auto SamplerCreateInfo::CustomizeFilter(VkFilter inMinFilter, VkFilter inMagFilter)->SamplerCreateInfo&
{
	m_createInfo.minFilter = inMinFilter;
	m_createInfo.magFilter = inMagFilter;

	return *this;
}

auto SamplerCreateInfo::CustomizeAddressMode(VkSamplerAddressMode inAddressMode)->SamplerCreateInfo&
{
	return CustomizeAddressMode(inAddressMode, inAddressMode, inAddressMode);
}

auto SamplerCreateInfo::CustomizeAddressMode(
	VkSamplerAddressMode inAddressModeU,
	VkSamplerAddressMode inAddressModeV,
	VkSamplerAddressMode inAddressModeW)->SamplerCreateInfo&
{
	m_createInfo.addressModeU = inAddressModeU;
	m_createInfo.addressModeV = inAddressModeV;
	m_createInfo.addressModeW = inAddressModeW;

	return *this;
}

auto SamplerCreateInfo::CustomizeMipmapMode(VkSamplerMipmapMode inMipmapMode)->SamplerCreateInfo&
{
	m_createInfo.mipmapMode = inMipmapMode;

	return *this;
}

auto SamplerCreateInfo::CustomizeLod(float inMinLod, float inMaxLod, float inMipLodBias)->SamplerCreateInfo&
{
	m_createInfo.minLod = inMinLod;
	m_createInfo.maxLod = inMaxLod;
	m_createInfo.mipLodBias = inMipLodBias;

	return *this;
}

auto SamplerCreateInfo::CustomizeAnisotropy(float inMaxAnisotropy)->SamplerCreateInfo&
{
	m_createInfo.anisotropyEnable = VK_TRUE;
	m_createInfo.maxAnisotropy = inMaxAnisotropy;

	return *this;
}

auto SamplerCreateInfo::CustomizeCompare(VkCompareOp inCompareOp)->SamplerCreateInfo&
{
	m_createInfo.compareEnable = VK_TRUE;
	m_createInfo.compareOp = inCompareOp;

	return *this;
}

auto SamplerCreateInfo::CustomizeBorderColor(VkBorderColor inBorderColor)->SamplerCreateInfo&
{
	m_createInfo.borderColor = inBorderColor;

	return *this;
}

auto SamplerCreateInfo::CustomizeUnnormalizedCoordinates()->SamplerCreateInfo&
{
	m_createInfo.unnormalizedCoordinates = VK_TRUE;

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
