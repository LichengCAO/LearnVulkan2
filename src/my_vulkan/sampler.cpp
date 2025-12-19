#include "sampler.h"
#include "device.h"
VkSampler SamplerPool::_GetSampler(const SamplerInfo _info)
{
	SamplerInfo key = _info;
	key.info.pNext = nullptr;
	auto it = m_mapInfoToIndex.find(key);
	uint32_t idx = 0;
	if ( it == m_mapInfoToIndex.end())
	{
		// need new entry
		if (m_currentId == ~0)
		{
			idx = static_cast<uint32_t>(m_vecSamplerEntries.size());
			m_vecSamplerEntries.push_back(SamplerEntry{});
		}
		else
		{
			idx = m_currentId;
			CHECK_TRUE(m_vecSamplerEntries[m_currentId].vkSampler == VK_NULL_HANDLE, "Entry still holds a sampler!");			
		}
		VkSampler* pSampler = &m_vecSamplerEntries[idx].vkSampler;
		
		VK_CHECK(vkCreateSampler(MyDevice::GetInstance().vkDevice, &_info.info, nullptr, pSampler), "Failed to create sampler!");
		m_vecSamplerEntries[idx].info = key;

		m_mapSamplerToIndex.insert({ *pSampler, idx });
		m_mapInfoToIndex.insert({ key, idx });
		
		m_currentId = m_vecSamplerEntries[idx].nextId;
	}
	else
	{
		idx = it->second;
	}
	
	m_vecSamplerEntries[idx].refCount++;
	return m_vecSamplerEntries[idx].vkSampler;
}

VkSampler SamplerPool::GetSampler(const VkSamplerCreateInfo& _createInfo)
{
	SamplerInfo info;
	info.info = _createInfo;
	return _GetSampler(info);
}

VkSampler SamplerPool::GetSampler(VkFilter filter, VkSamplerAddressMode addressMode, VkSamplerMipmapMode mipmapMode, float minLod, float maxLod, float mipLodBias, bool anistrophyEnable, float maxAnistrophy)
{
	VkSamplerCreateInfo createInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	
	createInfo.addressModeU = addressMode;
	createInfo.addressModeV = addressMode;
	createInfo.addressModeW = addressMode;
	createInfo.magFilter = filter;
	createInfo.minFilter = filter;
	createInfo.mipmapMode = mipmapMode;
	createInfo.minLod = minLod;
	createInfo.maxLod = maxLod;
	createInfo.mipLodBias = mipLodBias;
	createInfo.anisotropyEnable = anistrophyEnable ? VK_TRUE : VK_FALSE;
	createInfo.maxAnisotropy = maxAnistrophy;
	createInfo.compareEnable = VK_FALSE;
	createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	return GetSampler(createInfo);
}

void SamplerPool::ReturnSampler(VkSampler* _sampler)
{
	auto it = m_mapSamplerToIndex.find(*_sampler);
	CHECK_TRUE(it != m_mapSamplerToIndex.end(), "Sampler pool doesn't have this sampler!");
	uint32_t idx = it->second;
	SamplerEntry& entry = m_vecSamplerEntries[idx];

	CHECK_TRUE(entry.vkSampler == *_sampler, "Wrong sampler!");
	CHECK_TRUE(entry.refCount > 0, "Wrong reference count!");

	entry.refCount--;

	if (entry.refCount == 0)
	{
		m_mapSamplerToIndex.erase(entry.vkSampler);
		m_mapInfoToIndex.erase(entry.info);
		vkDestroySampler(MyDevice::GetInstance().vkDevice, entry.vkSampler, nullptr);
		entry.vkSampler = VK_NULL_HANDLE;
		uint32_t newEmptyEntry = idx;
		entry.nextId = m_currentId;
		m_currentId = newEmptyEntry;
	}
	*_sampler = VK_NULL_HANDLE;
}

void SamplerPool::ReturnSampler(VkSampler& _sampler)
{
	ReturnSampler(&_sampler);
}
