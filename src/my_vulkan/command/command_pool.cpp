#include "command_pool.h"
#include "device.h"

auto CommandPoolCreateInfo::Reset()->CommandPoolCreateInfo&
{
	*this = CommandPoolCreateInfo{};
	return *this;
}

auto CommandPoolCreateInfo::CustomizeQueueFamilyType(QueueFamilyType inQueueFamilyType)->CommandPoolCreateInfo&
{
	m_queueFamilyType = inQueueFamilyType;
	return *this;
}

auto CommandPoolCreateInfo::CustomizeCommandPoolCreateFlags(VkCommandPoolCreateFlags inFlags)->CommandPoolCreateInfo&
{
	m_createFlags = inFlags;
	return *this;
}

auto CommandPoolCreateInfo::CustomizeNext(const void* inNext)->CommandPoolCreateInfo&
{
	m_next = inNext;
	return *this;
}

CommandPool::~CommandPool()
{
	Destroy();
}

auto CommandPool::_GetUsedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&
{
	switch (inLevel)
	{
	case VK_COMMAND_BUFFER_LEVEL_PRIMARY:
		return m_usedPrimaryCommandBuffers;
	case VK_COMMAND_BUFFER_LEVEL_SECONDARY:
		return m_usedSecondaryCommandBuffers;
	default:
		CHECK_TRUE(false, "Unsupported command buffer level!");
		return m_usedPrimaryCommandBuffers;
	}
}

auto CommandPool::_GetUnusedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&
{
	switch (inLevel)
	{
	case VK_COMMAND_BUFFER_LEVEL_PRIMARY:
		return m_unusedPrimaryCommandBuffers;
	case VK_COMMAND_BUFFER_LEVEL_SECONDARY:
		return m_unusedSecondaryCommandBuffers;
	default:
		CHECK_TRUE(false, "Unsupported command buffer level!");
		return m_unusedPrimaryCommandBuffers;
	}
}

auto CommandPool::_FreeTrackedCommandBuffers()->void
{
	auto& device = MyDevice::GetInstance();
	auto freeQueue = [this, &device](std::deque<VkCommandBuffer>& inoutQueue)
	{
		for (VkCommandBuffer commandBuffer : inoutQueue)
		{
			if (commandBuffer != VK_NULL_HANDLE)
			{
				device.FreeCommandBuffer(m_vkCommandPool, commandBuffer);
			}
		}
		inoutQueue.clear();
	};

	freeQueue(m_usedPrimaryCommandBuffers);
	freeQueue(m_unusedPrimaryCommandBuffers);
	freeQueue(m_usedSecondaryCommandBuffers);
	freeQueue(m_unusedSecondaryCommandBuffers);
}

auto CommandPool::Create(const CommandPoolCreateInfo* inCreateInfo)->void
{
	CHECK_TRUE(inCreateInfo != nullptr, "No command pool create info!");
	CHECK_TRUE(m_vkCommandPool == VK_NULL_HANDLE, "Command pool already created!");

	auto& device = MyDevice::GetInstance();
	m_queueFamilyIndex = device.GetQueueFamilyIndexOfType(inCreateInfo->m_queueFamilyType);
	CHECK_TRUE(m_queueFamilyIndex != VK_QUEUE_FAMILY_IGNORED, "Invalid command pool queue family index!");

	VkCommandPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	createInfo.pNext = inCreateInfo->m_next;
	createInfo.flags = inCreateInfo->m_createFlags;
	createInfo.queueFamilyIndex = m_queueFamilyIndex;

	m_vkCommandPool = device.CreateCommandPool(createInfo);
}

auto CommandPool::Destroy()->void
{
	if (m_vkCommandPool == VK_NULL_HANDLE)
	{
		m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		return;
	}

	_FreeTrackedCommandBuffers();
	MyDevice::GetInstance().DestroyCommandPool(m_vkCommandPool);
	m_vkCommandPool = VK_NULL_HANDLE;
	m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
}

auto CommandPool::Reset(VkCommandPoolResetFlags inFlags)->void
{
	CHECK_TRUE(m_vkCommandPool != VK_NULL_HANDLE, "Command pool is not created!");
	VK_CHECK(MyDevice::GetInstance().ResetCommandPool(m_vkCommandPool, inFlags), "Failed to reset command pool!");

	m_unusedPrimaryCommandBuffers.insert(
		m_unusedPrimaryCommandBuffers.end(),
		m_usedPrimaryCommandBuffers.begin(),
		m_usedPrimaryCommandBuffers.end());
	m_usedPrimaryCommandBuffers.clear();

	m_unusedSecondaryCommandBuffers.insert(
		m_unusedSecondaryCommandBuffers.end(),
		m_usedSecondaryCommandBuffers.begin(),
		m_usedSecondaryCommandBuffers.end());
	m_usedSecondaryCommandBuffers.clear();
}

auto CommandPool::AllocateOrGetCommandBuffer(VkCommandBufferLevel inLevel)->VkCommandBuffer
{
	CHECK_TRUE(m_vkCommandPool != VK_NULL_HANDLE, "Command pool is not created!");

	auto& usedQueue = _GetUsedCommandBufferQueue(inLevel);
	auto& unusedQueue = _GetUnusedCommandBufferQueue(inLevel);
	VkCommandBuffer result = VK_NULL_HANDLE;

	if (!unusedQueue.empty())
	{
		result = unusedQueue.front();
		unusedQueue.pop_front();
	}
	else
	{
		result = MyDevice::GetInstance().AllocateCommandBuffer(m_vkCommandPool, inLevel);
	}

	usedQueue.push_back(result);
	return result;
}
