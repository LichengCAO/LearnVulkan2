#include "fence_allocator.h"
#include "device.h"

FenceAllocator::~FenceAllocator()
{
	Destroy();
}

auto FenceAllocator::_HasVkFence(VkFence inFence) const->bool
{
	return m_mapFenceToIndex.find(inFence) != m_mapFenceToIndex.end();
}

auto FenceAllocator::_CreateVkFenceWithResult()->std::pair<VkFence, VkResult>
{
	VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence = VK_NULL_HANDLE;

	const VkResult result = vkCreateFence(MyDevice::GetInstance().vkDevice, &fenceInfo, nullptr, &fence);
	if (result != VK_SUCCESS)
	{
		return { VK_NULL_HANDLE, result };
	}

	const uint32_t index = static_cast<uint32_t>(m_vecFenceEntries.size());
	FenceEntry entry{};
	entry.vkFence = fence;
	entry.allocated = true;
	m_vecFenceEntries.push_back(entry);
	m_mapFenceToIndex.insert({ fence, index });

	return { fence, VK_SUCCESS };
}

auto FenceAllocator::_PopFreeFence()->VkFence
{
	if (m_currentId == static_cast<uint32_t>(~0))
	{
		return VK_NULL_HANDLE;
	}

	const uint32_t index = m_currentId;
	FenceEntry& entry = m_vecFenceEntries[index];

	CHECK_TRUE(entry.vkFence != VK_NULL_HANDLE, "Free fence entry is invalid!");
	CHECK_TRUE(!entry.allocated, "Free fence entry is still allocated!");

	m_currentId = entry.nextId;
	entry.nextId = static_cast<uint32_t>(~0);
	entry.allocated = true;

	return entry.vkFence;
}

auto FenceAllocator::_PushFreeFence(uint32_t inIndex)->void
{
	FenceEntry& entry = m_vecFenceEntries[inIndex];

	CHECK_TRUE(entry.vkFence != VK_NULL_HANDLE, "Invalid fence entry!");
	CHECK_TRUE(entry.allocated, "Fence is already free!");

	entry.allocated = false;
	entry.nextId = m_currentId;
	m_currentId = inIndex;
}

auto FenceAllocator::Create()->void
{
}

auto FenceAllocator::CreateOrGetVkFenceWithResult()->std::pair<VkFence, VkResult>
{
	VkFence fence = _PopFreeFence();
	if (fence != VK_NULL_HANDLE)
	{
		const VkResult result = vkResetFences(MyDevice::GetInstance().vkDevice, 1, &fence);
		if (result != VK_SUCCESS)
		{
			const uint32_t index = m_mapFenceToIndex.at(fence);
			_PushFreeFence(index);
			return { VK_NULL_HANDLE, result };
		}

		return { fence, VK_SUCCESS };
	}

	return _CreateVkFenceWithResult();
}

auto FenceAllocator::CreateOrGetVkFence()->VkFence
{
	auto [fence, result] = CreateOrGetVkFenceWithResult();
	VK_CHECK(result, "Failed to create or get fence!");

	return fence;
}

auto FenceAllocator::FreeVkFence(const VkFence* inFences, size_t inCount)->void
{
	if (inCount == 0)
	{
		return;
	}

	CHECK_TRUE(inFences != nullptr, "No fences to free!");

	std::vector<uint32_t> indices;
	std::vector<VkFence> fencesToReset;
	std::set<VkFence> uniqueFences;
	indices.reserve(inCount);
	fencesToReset.reserve(inCount);

	for (size_t i = 0; i < inCount; ++i)
	{
		const VkFence fence = inFences[i];

		CHECK_TRUE(fence != VK_NULL_HANDLE, "Cannot free null fence!");
		CHECK_TRUE(uniqueFences.insert(fence).second, "Cannot free the same fence twice!");
		CHECK_TRUE(_HasVkFence(fence), "Fence allocator doesn't have this fence!");

		const uint32_t index = m_mapFenceToIndex.at(fence);
		FenceEntry& entry = m_vecFenceEntries[index];

		CHECK_TRUE(entry.vkFence == fence, "Wrong fence!");
		CHECK_TRUE(entry.allocated, "Fence is already free!");

		indices.push_back(index);
		fencesToReset.push_back(fence);
	}

	VK_CHECK(
		vkResetFences(
			MyDevice::GetInstance().vkDevice,
			static_cast<uint32_t>(fencesToReset.size()),
			fencesToReset.data()),
		"Failed to reset fences!");

	for (uint32_t index : indices)
	{
		_PushFreeFence(index);
	}
}

auto FenceAllocator::Destroy()->void
{
	for (FenceEntry& entry : m_vecFenceEntries)
	{
		if (entry.vkFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(MyDevice::GetInstance().vkDevice, entry.vkFence, nullptr);
		}
	}

	m_mapFenceToIndex.clear();
	m_vecFenceEntries.clear();
	m_currentId = static_cast<uint32_t>(~0);
}
