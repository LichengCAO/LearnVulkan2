#pragma once

#include "common.h"
#include "common_enums.h"
#include <variant>

class MyDevice;
class Buffer;
class Image;
class CommandBuffer;
class Fence;
class Semaphore;
class Queue
{
private:
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	QueueFamilyType m_queueFamilyType = QueueFamilyType::UNSET;

private:
	Queue() = default;

	void Create(VkQueue inVkQueue, uint32_t inFamilyIndex, QueueFamilyType inFamilyType)
	{
		m_vkQueue = inVkQueue;
		m_queueFamilyIndex = inFamilyIndex;
		m_queueFamilyType = inFamilyType;
	}

public:
	class SubmitInfo
	{
	public:
		void AddSemaphoreToWait(Semaphore* inSemaphore, VkPipelineStageFlags inWaitStage);
		void AddCommandBufferToSubmit(CommandBuffer* inCmd);
		void AddSemaphoreToSignal(Semaphore* inSemaphore);
	};

public:
	auto GetQueueFamilyIndex()const->uint32_t { return m_queueFamilyIndex; };
	
	auto GetQueueFamilyType()const->QueueFamilyType { return m_queueFamilyType; };

	auto GetVkQueue() const->VkQueue { return m_vkQueue; };

	void Submit(const Queue::SubmitInfo* inSubmitInfos, size_t inCount, std::optional<Fence*> inFence = {});

	friend class MyDevice;
};

class QueueContext final
{
private:
	enum class ResourceType
	{
		MEMORY,
		IMAGE,
		BUFFER,
	};

	struct BufferState
	{
		const Buffer* buffer;
		VkDeviceSize size;
		VkDeviceSize offset;
	};

	struct ImageState
	{
		const Image* image;
		VkImageLayout layout;
		VkImageAspectFlags aspect;
		uint32_t baseMipLevel;
		uint32_t mipLevelCount;
		uint32_t baseArrayLayer;
		uint32_t arrayLayerCount;
	};

	class Resource
	{
		friend class QueueContext;

	protected:
		std::variant<std::monostate, BufferState, ImageState> m_state;
		ResourceType m_type = ResourceType::MEMORY;
		uint32_t m_queueFamily = VK_QUEUE_FAMILY_IGNORED;
		VkAccessFlags m_access = 0;
		VkPipelineStageFlags m_stage = 0;

	public:
		void SetBufferMemory(
			const Buffer* inBuffer,
			VkDeviceSize inSize,
			VkDeviceSize inOffset);
		void SetBufferMemoryConcise(const Buffer* inBuffer);
		void SetImageMemory(
			const Image* inImage,
			VkImageLayout inLayout,
			uint32_t inBaseMipLevel,
			uint32_t inMipLevelCount,
			uint32_t inBaseArrayLayer,
			uint32_t inArrayLayerCount);
		void SetImageMemoryConcise(const Image* inImage, VkImageLayout inLayout);
		void PresetQueueFamily(uint32_t inQueueFamily) { m_queueFamily = inQueueFamily; };
	};

	struct BarrierBatch
	{
		std::vector<VkBufferMemoryBarrier> bufferBarriers;
		std::vector<VkImageMemoryBarrier> imageBarriers;
		VkPipelineStageFlags srcStages = 0;
		VkPipelineStageFlags dstStages = 0;

		auto HasBarriers() const -> bool
		{
			return !bufferBarriers.empty() || !imageBarriers.empty();
		}
	};

public:
	class ResourceReleased : public Resource
	{
	public:
		void SetReleaseTime(VkAccessFlags inAccess, VkPipelineStageFlags inStage) { m_access = inAccess; m_stage = inStage; };
	};

	class ResourceAcquired : public Resource
	{
	public:
		void SetAcquiredTime(VkAccessFlags inAccess, VkPipelineStageFlags inStage) { m_access = inAccess; m_stage = inStage; };
	};

private:
	const Queue* m_queue;
	std::vector<Resource> m_resources;

private:
	static auto ResolveWholeBufferSize(const Resource& inResource) -> VkDeviceSize;
	static auto BufferEndOffset(const Resource& inResource) -> VkDeviceSize;
	static auto IsSameResource(const Resource& inLeft, const Resource& inRight) -> bool;
	static auto FindResourceIndex(const std::vector<Resource>& inResources, const Resource& inTarget) -> size_t;
	void UpsertResource(const Resource& inResource);
	static void AppendBarrier(
		BarrierBatch& inoutBatch,
		const Resource& inSource,
		const Resource& inDestination,
		uint32_t inSourceQueueFamily,
		uint32_t inDestinationQueueFamily,
		VkAccessFlags inSourceAccess,
		VkAccessFlags inDestinationAccess,
		VkPipelineStageFlags inSourceStage,
		VkPipelineStageFlags inDestinationStage);
	static void BuildProcess(const BarrierBatch& inBatch, std::function<void(CommandBuffer*)>& outProcess);

public:
	QueueContext(const Queue* inQueue) : m_queue(inQueue) {};

	void PushResources(const ResourceReleased* inResources, size_t inCount);

	void PullResources(const ResourceAcquired* inResources, size_t inCount, std::function<void(CommandBuffer*)>& outSyncProcess);

	void GrabResourcesFromOther(
		QueueContext* inSourceContext,
		const ResourceAcquired* inResources, 
		size_t inCount, 
		std::function<void(CommandBuffer*)>& outReleaseProcess, 
		std::function<void(CommandBuffer*)>& outAcquireProcess);
};