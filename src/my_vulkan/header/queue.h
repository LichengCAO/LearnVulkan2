#pragma once

#include "common.h"
#include "common_enums.h"
#include <variant>

class MyDevice;
class Buffer;
class Image;
class CommandBuffer;
class Queue
{
private:
	enum class ResourceType
	{
		UNSET,
		IMAGE,
		BUFFER,
	};

	struct BufferResourceState
	{
		const Buffer* buffer;
		VkDeviceSize size;
		VkDeviceSize offset;
	};

	struct ImageResourceState
	{
		const Image* image;
		VkImageLayout layout;
		uint32_t baseMipLevel;
		uint32_t mipLevelCount;
		uint32_t baseArrayLayer;
		uint32_t arrayLayerCount;
	};

public:
	class ResourceDescriptor
	{
	protected:
		ResourceType m_type;
		std::variant<std::monostate, BufferResourceState, ImageResourceState> m_state;
		VkAccessFlags m_accesses;
		VkPipelineStageFlags m_stages;

	public:
		void SetBufferMemory(const Buffer* inBuffer, VkDeviceSize inSize, VkDeviceSize inOffset);
		void SetBufferMemoryConcise(const Buffer* inBuffer);
		void SetImageMemory(const Image* inImage, VkImageLayout inLayout, uint32_t inBaseMipLevel, uint32_t inMipLevelCount, uint32_t inBaseArrayLayer, uint32_t inArrayLayerCount);
		void SetImageMemoryConcise(const Image* inImage, VkImageLayout inLayout);
	};

	class AvailableResourceDescriptor : public ResourceDescriptor
	{
	public:
		void SetAvailableAccess(VkAccessFlags inAccess) { m_accesses = inAccess; };
		void SetAvailableStage(VkPipelineStageFlags inStages) { m_stages = inStages; };
	};

	class RequiredResourceDescriptor : public ResourceDescriptor
	{
	public:
		void SetRequiredAccess(VkAccessFlags inAccess) { m_accesses = inAccess; };
		void SetRequiredStage(VkPipelineStageFlags inStages) { m_stages = inStages; };
	};

private:
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	QueueFamilyType m_queueFamilyType = QueueFamilyType::UNSET;

private:
	Queue() = default;

	void Init(VkQueue inVkQueue, uint32_t inFamilyIndex, QueueFamilyType inFamilyType)
	{
		m_vkQueue = inVkQueue;
		m_queueFamilyIndex = inFamilyIndex;
		m_queueFamilyType = inFamilyType;
	}

public:
	auto GetQueueFamilyIndex()const->uint32_t { return m_queueFamilyIndex; };
	
	auto GetQueueFamilyType()const->QueueFamilyType { return m_queueFamilyType; };

	auto GetVkQueue() const->VkQueue { return m_vkQueue; };

	void PushResources(const AvailableResourceDescriptor* inDescriptors, size_t inCount);

	void PullResources(const RequiredResourceDescriptor* inDescriptors, size_t inCount, CommandBuffer* inoutCmd);

	void GrabResourcesFromOtherQueue(Queue* inSourceQueue, const RequiredResourceDescriptor* inDescriptors, size_t inCount, CommandBuffer* inoutCmd);

	friend class MyDevice;
};