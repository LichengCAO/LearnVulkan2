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

// Owns a native pool and its command buffers; Device must outlive this object.
// Independent pools may be used concurrently without thread affinity. Callers
// externally synchronize each pool and its buffers, and complete their GPU use
// before Reset or destruction. Destruction releases synchronously without waits.
class CommandPool final
{
private:
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	std::deque<VkCommandBuffer> m_usedPrimaryCommandBuffers;
	std::deque<VkCommandBuffer> m_unusedPrimaryCommandBuffers;
	std::deque<VkCommandBuffer> m_usedSecondaryCommandBuffers;
	std::deque<VkCommandBuffer> m_unusedSecondaryCommandBuffers;

private:
	auto _GetUsedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&;
	auto _GetUnusedCommandBufferQueue(VkCommandBufferLevel inLevel)->std::deque<VkCommandBuffer>&;
	void _Destroy() noexcept;

public:
	using Initializer = CommandPoolCreateInfo;

	CommandPool() = default;
	CommandPool(const CommandPool&) = delete;
	auto operator=(const CommandPool&) -> CommandPool& = delete;
	CommandPool(CommandPool&&) = delete;
	auto operator=(CommandPool&&) -> CommandPool& = delete;
	~CommandPool() noexcept;

	// Synchronous creation; keep inputs and Device stable and exclude same-pool access.
	auto Create(const CommandPoolCreateInfo& inCreateInfo)->void;

	// Requires completed GPU work and exclusive access to this pool and its buffers.
	auto Reset(VkCommandPoolResetFlags inFlags = 0)->void;
	// Returns a borrowed buffer, valid only while this pool owns its native storage.
	// Allocation and recording require caller synchronization with same-pool access.
	auto AllocateOrGetCommandBuffer(VkCommandBufferLevel inLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY)->VkCommandBuffer;

	auto GetVkCommandPool() const->VkCommandPool { return m_vkCommandPool; };
	auto GetQueueFamilyIndex() const->uint32_t { return m_queueFamilyIndex; };
};
