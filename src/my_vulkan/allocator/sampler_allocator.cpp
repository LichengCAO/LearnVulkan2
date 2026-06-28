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

SamplerAllocator::~SamplerAllocator()
{
	Destroy();
}

auto SamplerAllocator::_AllocateSamplerWithResult(const SamplerInfo& inInfo)->std::pair<VkSampler, VkResult>
{
	const SamplerInfo key{ NormalizeSamplerCreateInfo(inInfo.info) };
	const auto iter = m_mapInfoToIndex.find(key);
	uint32_t index = 0;

	if (iter == m_mapInfoToIndex.end())
	{
		if (m_currentId == static_cast<uint32_t>(~0))
		{
			index = static_cast<uint32_t>(m_vecSamplerEntries.size());
			m_vecSamplerEntries.push_back(SamplerEntry{});
		}
		else
		{
			index = m_currentId;
			CHECK_TRUE(m_vecSamplerEntries[index].vkSampler == VK_NULL_HANDLE, "Entry still holds a sampler!");
		}

		VkSampler sampler = VK_NULL_HANDLE;
		const VkResult result = vkCreateSampler(MyDevice::GetInstance().vkDevice, &key.info, nullptr, &sampler);
		if (result != VK_SUCCESS)
		{
			return { VK_NULL_HANDLE, result };
		}

		if (index == m_currentId)
		{
			m_currentId = m_vecSamplerEntries[index].nextId;
		}

		SamplerEntry& entry = m_vecSamplerEntries[index];
		entry.vkSampler = sampler;
		entry.info = key;
		entry.refCount = 0;
		entry.nextId = static_cast<uint32_t>(~0);

		m_mapSamplerToIndex.insert({ sampler, index });
		m_mapInfoToIndex.insert({ key, index });
	}
	else
	{
		index = iter->second;
	}

	m_vecSamplerEntries[index].refCount++;

	return { m_vecSamplerEntries[index].vkSampler, VK_SUCCESS };
}

auto SamplerAllocator::Create()->void
{
}

auto SamplerAllocator::AllocateSamplerWithResult(const SamplerCreateInfo* inCreateInfo)->std::pair<VkSampler, VkResult>
{
	CHECK_TRUE(inCreateInfo != nullptr, "No sampler create info!");

	SamplerInfo info{};
	info.info = NormalizeSamplerCreateInfo(inCreateInfo->GetVkSamplerCreateInfo());

	return _AllocateSamplerWithResult(info);
}

auto SamplerAllocator::AllocateSampler(const SamplerCreateInfo* inCreateInfo)->VkSampler
{
	auto [sampler, result] = AllocateSamplerWithResult(inCreateInfo);
	VK_CHECK(result, "Failed to allocate sampler!");

	return sampler;
}

auto SamplerAllocator::FreeSampler(VkSampler& inoutSampler)->void
{
	if (inoutSampler == VK_NULL_HANDLE)
	{
		return;
	}

	const auto iter = m_mapSamplerToIndex.find(inoutSampler);
	CHECK_TRUE(iter != m_mapSamplerToIndex.end(), "Sampler allocator doesn't have this sampler!");
	const uint32_t index = iter->second;
	SamplerEntry& entry = m_vecSamplerEntries[index];

	CHECK_TRUE(entry.vkSampler == inoutSampler, "Wrong sampler!");
	CHECK_TRUE(entry.refCount > 0, "Wrong sampler reference count!");

	entry.refCount--;
	if (entry.refCount == 0)
	{
		m_mapSamplerToIndex.erase(entry.vkSampler);
		m_mapInfoToIndex.erase(entry.info);
		vkDestroySampler(MyDevice::GetInstance().vkDevice, entry.vkSampler, nullptr);

		entry.vkSampler = VK_NULL_HANDLE;
		entry.refCount = 0;
		entry.nextId = m_currentId;
		m_currentId = index;
	}

	inoutSampler = VK_NULL_HANDLE;
}

auto SamplerAllocator::Destroy()->void
{
	for (SamplerEntry& entry : m_vecSamplerEntries)
	{
		if (entry.vkSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(MyDevice::GetInstance().vkDevice, entry.vkSampler, nullptr);
		}
	}

	m_mapInfoToIndex.clear();
	m_mapSamplerToIndex.clear();
	m_vecSamplerEntries.clear();
	m_currentId = static_cast<uint32_t>(~0);
}
