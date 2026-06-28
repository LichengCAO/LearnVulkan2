#pragma once
#include "common.h"

// ref: https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
class Buffer;
class BufferView;
class CommandBuffer;
class MemoryAllocator;

class BufferViewInfo final
{
private:
	friend class Buffer;
	friend class BufferView;

	VkFormat m_format = VK_FORMAT_UNDEFINED;
	VkDeviceSize m_offset = 0;
	VkDeviceSize m_range = VK_WHOLE_SIZE;

public:
	auto Reset()->BufferViewInfo&;
	auto SetFormat(VkFormat inFormat)->BufferViewInfo&;
	auto CustomizeBufferRange(VkDeviceSize inOffset, VkDeviceSize inRange = VK_WHOLE_SIZE)->BufferViewInfo&;
};

class BufferView final
{
public:
	struct Information
	{
		VkBuffer vkBuffer = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkDeviceSize offset = 0;
		VkDeviceSize range = VK_WHOLE_SIZE;
	};

private:
	Information m_viewInformation{};
	VkBufferView m_vkBufferView = VK_NULL_HANDLE;

private:
	auto Create(const Buffer* inBuffer, const BufferViewInfo* inCreateInfo)->void;
	auto Destroy()->void;

public:
	~BufferView();

	auto GetBufferViewInformation() const->const Information&;
	auto GetVkBufferView() const->VkBufferView;

	friend class Buffer;
};

class BufferCreateInfo final
{
private:
	std::optional<VkDeviceSize> m_optAlignment;
	VkMemoryPropertyFlags m_memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	VkSharingMode m_sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkDeviceSize m_bufferSize;
	VkBufferUsageFlags m_usage;

	friend class Buffer;

public:
	// Reset optional values to default, clear other values to zero
	BufferCreateInfo& Reset();

	// Set buffer size, mandatory
	BufferCreateInfo& SetBufferSize(VkDeviceSize inSize);

	// Set buffer usage, mandatory
	BufferCreateInfo& SetBufferUsage(VkBufferUsageFlags inUsage);

	// Optional, default: VK_SHARING_MODE_EXCLUSIVE
	BufferCreateInfo& CustomizeSharingMode(VkSharingMode inSharingMode);

	// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	BufferCreateInfo& CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties);

	// Optional, for buffers that have alignment requirement
	BufferCreateInfo& CustomizeAlignment(VkDeviceSize inAlignment);
};

class Buffer final
{
public:
	struct Information
	{
		VkDeviceSize size;
		VkBufferUsageFlags usage;
		VkSharingMode sharingMode;
		VkMemoryPropertyFlags	memoryProperty;
		std::optional<VkDeviceSize> optAlignment; // buffer may have alignment requirements, i.e. Scratch Buffer
	};

private:
	Information m_bufferInformation{};
	void* m_mappedMemory = nullptr; // we store this value, since mapping is not free
	VkBuffer m_vkBuffer = VK_NULL_HANDLE;
	std::vector<std::unique_ptr<BufferView>> m_uptrBufferViews;

private:
	MemoryAllocator* _GetMemoryAllocator() const;
	
	void _AllocateMemory();
	
	void _FreeMemory();

	void _MapHostMemory();

	void _UnmapHostMemory();

	// Map the memory and copy, if this buffer is host coherent
	void _CopyFromHostWithMappedMemory(const void* src, size_t bufferOffset, size_t size);
	
	// Create a staging buffer to copy from host and then copy from staging buffer
	void _CopyFromHostWithStaggingBuffer(const void* src, size_t bufferOffest, size_t size);

	BufferView* _FindView(const BufferViewInfo& inCreateInfo) const;

	void _DestroyViews();

public:
	Buffer();
	
	Buffer(Buffer&& _toMove);
	
	Buffer(Buffer& _toCopy) = delete;
	
	~Buffer();

	void Create(const BufferCreateInfo* inCreateInfo);
	
	void Destroy();

	// Copy from host wait till finish, will use stagging buffer if necessary
	void CopyFromHost(const void* src, size_t bufferOffset, size_t size);

	// Copy from buffer on graphics queue, wait till copy finish
	void CopyFromBuffer(
		const Buffer* inSrcBufferPtr, 
		size_t inSrcOffset, 
		size_t inDstOffset, 
		size_t inSize);

	// Fill buffer with input data by graphics queue, wait till finish
	void Fill(uint32_t inData);

	auto View(const BufferViewInfo& inCreateInfo)->const BufferView*;

	// Get buffer information
	const Buffer::Information& GetBufferInformation() const;
	
	// Get device address of the buffer, require VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	VkDeviceAddress GetDeviceAddress() const;

	// Get default VkDescriptorBufferInfo, with offset = 0, range = bufferSize
	VkDescriptorBufferInfo GetDescriptorBufferInfo() const;

	// Get device handle of this buffer
	VkBuffer GetVkBuffer() const;
};
