#pragma once
#include "../descriptor_set.h"

class DescriptorSetAllocator final
{
	// https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/
private:
	struct PoolSizes {
		std::vector<std::pair<VkDescriptorType, uint32_t>> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 500 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 500 }
		};
	};

private:
	PoolSizes m_poolSizes{};
	std::map<VkDescriptorPoolCreateFlags, VkDescriptorPool> m_candidatePool;
	std::map<VkDescriptorPoolCreateFlags, std::vector<VkDescriptorPool>> m_usedPools;
	std::map<VkDescriptorPoolCreateFlags, std::vector<VkDescriptorPool>> m_freePools;

private:
	VkDescriptorPool _CreatePool(VkDescriptorPoolCreateFlags inPoolFlags);
	VkDescriptorPool _GrabPool(VkDescriptorPoolCreateFlags inPoolFlags);

public:
	void Create();

	void ResetPools();

	auto AllocateDescriptorSetWithResult(
		VkDescriptorSetLayout inLayout,
		VkDescriptorPoolCreateFlags inPoolFlags = 0,
		const void* inNextPtr = nullptr) -> std::pair<VkDescriptorSet, VkResult>;

	auto AllocateDescriptorSet(
		VkDescriptorSetLayout inLayout,
		VkDescriptorPoolCreateFlags inPoolFlags = 0,
		const void* inNextPtr = nullptr) -> VkDescriptorSet;

	void Destroy();
};
