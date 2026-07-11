#pragma once
#include "common.h"
#include "common_enums.h"
#include <deque>

class CommandPoolCreateInfo final
{
private:
	QueueFamilyType m_queueFamilyType = QueueFamilyType::GRAPHICS;
	VkCommandPoolCreateFlags m_createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	const void* m_next = nullptr;

	friend class CommandPool;

public:
	auto Reset()->CommandPoolCreateInfo&;
	auto CustomizeQueueFamilyType(QueueFamilyType inQueueFamilyType)->CommandPoolCreateInfo&;
	auto CustomizeCommandPoolCreateFlags(VkCommandPoolCreateFlags inFlags)->CommandPoolCreateInfo&;
	auto CustomizeNext(const void* inNext)->CommandPoolCreateInfo&;
};

class CommandPool final
{
private:
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	std::deque<VkCommandBuffer> m_usedPrimaryCommandBuffers;
	std::deque<VkCommandBuffer> m_unusedPrimaryCommandBuffers;
	std::deque<VkCommandBuffer> m_usedSecondaryCommandBuffers;
	std::deque<VkCommandBuffer> m_unusedSecondaryCommandBuffers;

private:
	auto _GetUsedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&;
	auto _GetUnusedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&;
	auto _FreeTrackedCommandBuffers()->void;

public:
	using Initializer = CommandPoolCreateInfo;

	CommandPool() = default;
	CommandPool(const CommandPool&) = delete;
	CommandPool& operator=(const CommandPool&) = delete;
	~CommandPool();

	auto Create(const CommandPoolCreateInfo* inCreateInfo)->void;
	auto Destroy()->void;

	auto Reset(VkCommandPoolResetFlags inFlags = 0)->void;
	auto AllocateOrGetCommandBuffer(VkCommandBufferLevel inLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY)->VkCommandBuffer;

	auto GetVkCommandPool() const->VkCommandPool { return m_vkCommandPool; };
	auto GetQueueFamilyIndex() const->uint32_t { return m_queueFamilyIndex; };
};
