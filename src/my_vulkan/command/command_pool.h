#pragma once
#include "common.h"

class MyDevice;

class CommandPool final
{
private:
	MyDevice* m_device = nullptr;
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

public:
	CommandPool() = default;
	CommandPool(const CommandPool&) = delete;
	CommandPool& operator=(const CommandPool&) = delete;
	~CommandPool();

	auto Create(
		MyDevice* inDevice,
		uint32_t inQueueFamilyIndex,
		VkCommandPoolCreateFlags inCreateFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)->void;
	auto Destroy()->void;

	auto Reset(VkCommandPoolResetFlags inFlags = 0)->void;
	auto AllocateCommandBuffer(VkCommandBufferLevel inLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY)->VkCommandBuffer;
	auto FreeCommandBuffer(VkCommandBuffer& inoutCommandBuffer)->void;

	auto GetVkCommandPool() const->VkCommandPool { return m_vkCommandPool; };
	auto GetQueueFamilyIndex() const->uint32_t { return m_queueFamilyIndex; };
};
