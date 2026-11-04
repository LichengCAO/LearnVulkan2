#include "sampler_allocator.h"
#include "device.h"
#include "utility/hash_util.h"

#include <type_traits>

namespace
{
	auto NormalizeSamplerCreateInfo(const VkSamplerCreateInfo& inCreateInfo)->VkSamplerCreateInfo
	{
		VkSamplerCreateInfo result = inCreateInfo;

		result.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		result.pNext = nullptr;

		return result;
	}
}

auto SamplerAllocator::SamplerInfo::operator==(const SamplerInfo& inOther) const->bool
{
	return info.flags == inOther.info.flags
		&& info.magFilter == inOther.info.magFilter
		&& info.minFilter == inOther.info.minFilter
		&& info.mipmapMode == inOther.info.mipmapMode
		&& info.addressModeU == inOther.info.addressModeU
		&& info.addressModeV == inOther.info.addressModeV
		&& info.addressModeW == inOther.info.addressModeW
		&& info.mipLodBias == inOther.info.mipLodBias
		&& info.anisotropyEnable == inOther.info.anisotropyEnable
		&& info.maxAnisotropy == inOther.info.maxAnisotropy
		&& info.compareEnable == inOther.info.compareEnable
		&& info.compareOp == inOther.info.compareOp
		&& info.minLod == inOther.info.minLod
		&& info.maxLod == inOther.info.maxLod
		&& info.borderColor == inOther.info.borderColor
		&& info.unnormalizedCoordinates == inOther.info.unnormalizedCoordinates;
}

auto SamplerAllocator::SamplerInfoHash::operator()(const SamplerInfo& inInfo) const->std::size_t
{
	size_t result = 0;

	hash_combine(result, inInfo.info.flags);
	hash_combine(result, static_cast<std::underlying_type_t<VkFilter>>(inInfo.info.magFilter));
	hash_combine(result, static_cast<std::underlying_type_t<VkFilter>>(inInfo.info.minFilter));
	hash_combine(result, static_cast<std::underlying_type_t<VkSamplerMipmapMode>>(inInfo.info.mipmapMode));
	hash_combine(result, static_cast<std::underlying_type_t<VkSamplerAddressMode>>(inInfo.info.addressModeU));
	hash_combine(result, static_cast<std::underlying_type_t<VkSamplerAddressMode>>(inInfo.info.addressModeV));
	hash_combine(result, static_cast<std::underlying_type_t<VkSamplerAddressMode>>(inInfo.info.addressModeW));
	hash_combine(result, inInfo.info.mipLodBias);
	hash_combine(result, inInfo.info.anisotropyEnable == VK_TRUE);
	hash_combine(result, inInfo.info.maxAnisotropy);
	hash_combine(result, inInfo.info.compareEnable == VK_TRUE);
	hash_combine(result, static_cast<std::underlying_type_t<VkCompareOp>>(inInfo.info.compareOp));
	hash_combine(result, inInfo.info.minLod);
	hash_combine(result, inInfo.info.maxLod);
	hash_combine(result, static_cast<std::underlying_type_t<VkBorderColor>>(inInfo.info.borderColor));
	hash_combine(result, inInfo.info.unnormalizedCoordinates == VK_TRUE);

	return result;
}

SamplerAllocator::~SamplerAllocator() noexcept
{
	Destroy();
}

auto SamplerAllocator::AllocateSamplerWithResult(const SamplerCreateInfo& inCreateInfo)->std::pair<VkSampler, VkResult>
{
	const SamplerInfo key{ NormalizeSamplerCreateInfo(inCreateInfo.GetVkSamplerCreateInfo()) };
	const auto iter = m_mapInfoToSampler.find(key);
	if (iter != m_mapInfoToSampler.end()) return { iter->second, VK_SUCCESS };

	VkSampler sampler = VK_NULL_HANDLE;
	const auto device = MyDevice::GetInstance().vkDevice;
	const VkResult result = vkCreateSampler(device, &key.info, nullptr, &sampler);
	if (result != VK_SUCCESS) return { VK_NULL_HANDLE, result };

	try
	{
		const auto [iterator, inserted] = m_mapInfoToSampler.emplace(key, sampler);
		CHECK_TRUE(inserted, "Sampler description is already cached!");
	}
	catch (...)
	{
		vkDestroySampler(device, sampler, nullptr);
		throw;
	}
	return { sampler, VK_SUCCESS };
}

void SamplerAllocator::Create()
{
}

auto SamplerAllocator::AllocateSampler(const SamplerCreateInfo& inCreateInfo)->VkSampler
{
	auto [sampler, result] = AllocateSamplerWithResult(inCreateInfo);
	VK_CHECK(result, "Failed to allocate sampler!");

	return sampler;
}

void SamplerAllocator::Destroy() noexcept
{
	if (m_mapInfoToSampler.empty()) return;
	const auto device = MyDevice::GetInstance().vkDevice;
	// Each entry owns one successful native creation, even when handles are equal.
	for (const auto& [info, sampler] : m_mapInfoToSampler)
		vkDestroySampler(device, sampler, nullptr);
	m_mapInfoToSampler.clear();
}
