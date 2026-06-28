#pragma once
#include "command_list.h"
#include "common_enums.h"

class MyDevice;

namespace CommandFramework
{

class CommandSyncObject
{
public:
	virtual ~CommandSyncObject() = default;

	virtual auto GetWaitSemaphores() const->const std::vector<VkSemaphore>& = 0;
	virtual auto GetWaitStages() const->const std::vector<VkPipelineStageFlags>& = 0;
	virtual auto GetSignalSemaphores() const->const std::vector<VkSemaphore>& = 0;
	virtual auto GetFence() const->VkFence = 0;
};

class CommandQueue final
{
public:
	struct CreateInfo final
	{
		MyDevice* device = nullptr;
		VkQueue vkQueue = VK_NULL_HANDLE;
		uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		QueueFamilyType queueFamilyType = QueueFamilyType::UNSET;
		uint32_t threadCount = 1;
		uint32_t frameInFlightCount = 1;
		VkCommandPoolCreateFlags commandPoolFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	};

	struct RecordedCommandBuffer final
	{
		VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
		uint32_t sourceCommandBufferIndex = 0;
		uint32_t threadIndex = 0;
	};

private:
	MyDevice* m_device = nullptr;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	QueueFamilyType m_queueFamilyType = QueueFamilyType::UNSET;
	uint32_t m_threadCount = 1;
	uint32_t m_frameInFlightCount = 1;
	uint32_t m_currentFrameIndex = 0;
	std::vector<VkCommandPool> m_vkCommandPools;
	std::vector<std::vector<VkFence>> m_frameFences;
	std::vector<const CommandBuffer*> m_pendingCommandBuffers;
	std::vector<RecordedCommandBuffer> m_recordedCommandBuffers;

private:
	auto _GetCommandPoolIndex(uint32_t inFrameIndex, uint32_t inThreadIndex) const->uint32_t;
	auto _GetCommandPool(uint32_t inFrameIndex, uint32_t inThreadIndex) const->VkCommandPool;

public:
	CommandQueue() = default;
	CommandQueue(const CommandQueue&) = delete;
	CommandQueue& operator=(const CommandQueue&) = delete;
	~CommandQueue();

	auto Create(const CreateInfo* inCreateInfo)->void;
	auto Destroy()->void;

	auto StartFrame(uint32_t inFrameIndex)->void;
	auto Enqueue(const CommandBuffer* inCommandBuffer)->CommandQueue&;
	auto Enqueue(const CommandBuffer* const* inCommandBuffers, uint32_t inCount)->CommandQueue&;
	auto RecordPendingCommandBuffers()->const std::vector<RecordedCommandBuffer>&;
	auto Submit(const CommandSyncObject* inSyncObject)->void;

	auto GetVkQueue() const->VkQueue { return m_vkQueue; };
	auto GetQueueFamilyIndex() const->uint32_t { return m_queueFamilyIndex; };
	auto GetQueueFamilyType() const->QueueFamilyType { return m_queueFamilyType; };
	auto GetThreadCount() const->uint32_t { return m_threadCount; };
	auto GetFrameInFlightCount() const->uint32_t { return m_frameInFlightCount; };
	auto GetCurrentFrameIndex() const->uint32_t { return m_currentFrameIndex; };
	auto GetPendingCommandBufferCount() const->uint32_t { return static_cast<uint32_t>(m_pendingCommandBuffers.size()); };
	auto GetRecordedCommandBuffers() const->const std::vector<RecordedCommandBuffer>& { return m_recordedCommandBuffers; };
};

}
