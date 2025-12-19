#pragma once
#include "common.h"

class Sampler
{
private:
	VkSamplerCreateInfo m_vkSamplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
public:
	VkSampler vkSampler = VK_NULL_HANDLE;
};

class SamplerPool
{
private:
	struct SamplerInfo
	{
		VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		
		SamplerInfo() { memset(this, 0, sizeof(SamplerInfo)); }
		bool operator==(const SamplerInfo& other) const { return memcmp(this, &other, sizeof(SamplerInfo)) == 0; }
	};
	struct SamplerEntry
	{
		VkSampler vkSampler = VK_NULL_HANDLE;
		SamplerInfo info{};
		uint32_t refCount = 0;
		uint32_t nextId = static_cast<uint32_t>(~0);
	};
	struct hash_fn
	{
		std::size_t operator() (const SamplerInfo& node) const
		{
			std::size_t h1 = std::hash<int>()(static_cast<int>(node.info.addressModeU));
			std::size_t h2 = std::hash<int>()(static_cast<int>(node.info.addressModeV));

			return h1 ^ h2;
		}
	};

private:
	std::unordered_map<SamplerInfo, uint32_t, hash_fn> m_mapInfoToIndex;
	std::unordered_map<VkSampler, uint32_t> m_mapSamplerToIndex;
	std::vector<SamplerEntry> m_vecSamplerEntries;
	uint32_t m_currentId = ~0;

private:
	VkSampler _GetSampler(const SamplerInfo _info);

public:
	VkSampler GetSampler(const VkSamplerCreateInfo& _createInfo);
	VkSampler GetSampler(
		VkFilter filter,
		VkSamplerAddressMode addressMode,
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		float minLod = 0.0f,
		float maxLod = VK_LOD_CLAMP_NONE,
		float mipLodBias = 0.0f,
		bool anistrophyEnable = false,
		float maxAnistrophy = 1.0f);
	void ReturnSampler(VkSampler* _sampler);
	void ReturnSampler(VkSampler& _sampler);
};