#include "descriptor_set_allocator.h"
#include "../device.h"

VkDescriptorPool DescriptorSetAllocator::_CreatePool(VkDescriptorPoolCreateFlags inPoolFlags)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	poolSizes.reserve(m_poolSizes.sizes.size());
	for (auto sz : m_poolSizes.sizes)
	{
		poolSizes.push_back({ sz.first, sz.second });
	}

	createInfo.flags = inPoolFlags;
	createInfo.maxSets = 1000;
	createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	createInfo.pPoolSizes = poolSizes.data();
	createInfo.pNext = nullptr;

	descriptorPool = MyDevice::GetInstance().CreateDescriptorPool(createInfo);

	return descriptorPool;
}

VkDescriptorPool DescriptorSetAllocator::_GrabPool(VkDescriptorPoolCreateFlags inPoolFlags)
{
	bool hasThisTypeFreePool =
		m_freePools.find(inPoolFlags) != m_freePools.end()
		&& m_freePools[inPoolFlags].size() > 0;

	if (hasThisTypeFreePool)
	{
		auto& freePools = m_freePools[inPoolFlags];
		VkDescriptorPool pool = freePools.back();
		freePools.pop_back();
		return pool;
	}
	else
	{
		return _CreatePool(inPoolFlags);
	}
}

void DescriptorSetAllocator::ResetPools()
{
	auto& device = MyDevice::GetInstance();

	for (auto& p : m_usedPools)
	{
		auto& freePools = m_freePools[p.first];
		auto& toFreePools = p.second;

		for (VkDescriptorPool poolToFree : toFreePools)
		{
			device.ResetDescriptorPool(poolToFree);
			freePools.push_back(poolToFree);
		}

		toFreePools.clear();
	}

	m_candidatePool.clear();
}

bool DescriptorSetAllocator::Allocate(
	VkDescriptorSetLayout inLayout,
	VkDescriptorSet& outDescriptorSet,
	VkDescriptorPoolCreateFlags inPoolFlags,
	const void* inNextPtr)
{
	auto& device = MyDevice::GetInstance();
	VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorPool candidatePool = VK_NULL_HANDLE;
	VkResult allocResult;
	bool needReallocate = false;

	if (m_candidatePool.find(inPoolFlags) == m_candidatePool.end()
		|| m_candidatePool[inPoolFlags] == VK_NULL_HANDLE)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);
	}
	else
	{
		candidatePool = m_candidatePool[inPoolFlags];
	}

	allocInfo.pNext = nullptr;
	allocInfo.pSetLayouts = &inLayout;
	allocInfo.descriptorPool = candidatePool;
	allocInfo.descriptorSetCount = 1;

	outDescriptorSet = device.AllocateDescriptorSet(
		inLayout,
		candidatePool,
		allocResult);

	switch (allocResult)
	{
	case VK_SUCCESS:
		return true;
	case VK_ERROR_FRAGMENTED_POOL:
	case VK_ERROR_OUT_OF_POOL_MEMORY:
		needReallocate = true;
		break;
	default:
		return false;
	}

	if (needReallocate)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);

		outDescriptorSet = device.AllocateDescriptorSet(
			inLayout,
			candidatePool,
			allocResult);

		if (allocResult == VK_SUCCESS)
		{
			return true;
		}
	}

	return false;
}

auto DescriptorSetAllocator::AllocateDescriptorSet(
	VkDescriptorSetLayout inLayout,
	VkDescriptorPoolCreateFlags inPoolFlags,
	const void* inNextPtr) -> std::pair<VkDescriptorSet, VkResult>
{
	auto& device = MyDevice::GetInstance();
	VkDescriptorPool candidatePool = VK_NULL_HANDLE;
	VkResult allocResult = VK_SUCCESS;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

	if (m_candidatePool.find(inPoolFlags) == m_candidatePool.end()
		|| m_candidatePool[inPoolFlags] == VK_NULL_HANDLE)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);
	}
	else
	{
		candidatePool = m_candidatePool[inPoolFlags];
	}

	descriptorSet = device.AllocateDescriptorSet(
		inLayout,
		candidatePool,
		allocResult,
		inNextPtr);

	if (allocResult == VK_ERROR_FRAGMENTED_POOL || allocResult == VK_ERROR_OUT_OF_POOL_MEMORY)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);

		descriptorSet = device.AllocateDescriptorSet(
			inLayout,
			candidatePool,
			allocResult,
			inNextPtr);
	}

	return { descriptorSet, allocResult };
}

void DescriptorSetAllocator::Create()
{
}

void DescriptorSetAllocator::Destroy()
{
	auto& device = MyDevice::GetInstance();

	for (auto& p : m_candidatePool)
	{
		if (p.second != VK_NULL_HANDLE)
		{
			device.DestroyDescriptorPool(p.second);
		}
	}

	for (auto& p : m_usedPools)
	{
		for (VkDescriptorPool pool : p.second)
		{
			if (pool != VK_NULL_HANDLE)
			{
				device.DestroyDescriptorPool(pool);
			}
		}
		p.second.clear();
	}

	for (auto& p : m_freePools)
	{
		for (VkDescriptorPool pool : p.second)
		{
			if (pool != VK_NULL_HANDLE)
			{
				device.DestroyDescriptorPool(pool);
			}
		}
		p.second.clear();
	}

	m_candidatePool.clear();
	m_usedPools.clear();
	m_freePools.clear();
}
