#pragma once
#include "common.h"

// ref: https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
class BufferView;
class CommandSubmission;
class MemoryAllocator;

class Buffer final
{
public:
	struct CreateInformation
	{
		VkDeviceSize size;
		VkBufferUsageFlags usage;
		std::optional<VkSharingMode>			optSharingMode;		//optional, default: VK_SHARING_MODE_EXCLUSIVE;
		std::optional<VkMemoryPropertyFlags> optMemoryProperty; // optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		std::optional<VkDeviceSize>				optAlignment;			// buffer may have alignment requirements, i.e. Scratch Buffer
	};
	struct Information
	{
		VkDeviceSize size;
		VkBufferUsageFlags usage;
		VkSharingMode sharingMode;
		VkMemoryPropertyFlags	memoryProperty;// = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		std::optional<VkDeviceSize> optAlignment; // buffer may have alignment requirements, i.e. Scratch Buffer
	};

private:
	Information m_bufferInformation{};
	void* m_mappedMemory = nullptr; // we store this value, since mapping is not free

public:
	VkBuffer	   vkBuffer = VK_NULL_HANDLE;
	//VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;

private:
	//uint32_t _FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

	MemoryAllocator* _GetMemoryAllocator() const;
	
	void _AllocateMemory();
	
	void _FreeMemory();

	void _MapHostMemory();

	void _UnmapHostMemory();

	// Map the memory and copy, if this buffer is host coherent
	void _CopyFromHostWithMappedMemory(const void* src, size_t bufferOffset, size_t size);
	// Create a staging buffer to copy from host and then copy from staging buffer
	void _CopyFromHostWithStaggingBuffer(const void* src, size_t bufferOffest, size_t size);

public:
	Buffer();
	Buffer(Buffer&& _toMove);
	Buffer(Buffer& _toCopy) = delete;
	~Buffer();

	void PresetCreateInformation(const CreateInformation& _info);

	void Init();
	
	void Uninit();

	// Copy from host, will use stagging buffer if necessary, use buffer's size as length
	void CopyFromHost(const void* src);
	// Copy from host, will use stagging buffer if necessary
	void CopyFromHost(const void* src, size_t bufferOffset, size_t size);

	// Copy from buffer, will wait until copy is done, use buffer's size as length
	void CopyFromBuffer(const Buffer& otherBuffer);
	// Copy from buffer, will wait until copy is done, use buffer's size as length
	void CopyFromBuffer(const Buffer* pOtherBuffer);
	// Copy from buffer,
	// if pCmd is nullptr it will create a command buffer and wait this action to finish,
	// if command buffer is provided, then when does this command finish depends on user
	void CopyFromBuffer(const Buffer* pOtherBuffer, size_t srcOffset, size_t dstOffset, size_t size, CommandSubmission* pCmd = nullptr);

	// Fill buffer with input data,
	// if pCmd is nullptr it will create a command buffer and wait this action to finish,
	// if command buffer is provided, then when does this command finish depends on user
	void Fill(uint32_t _data, CommandSubmission* pCmd = nullptr);

	const Information& GetBufferInformation() const;
	
	VkDeviceAddress GetDeviceAddress() const;

	VkDescriptorBufferInfo GetDescriptorInfo() const;

	BufferView NewBufferView(VkFormat _format);
};

class BufferView // use for texel buffer
{
private:
	VkFormat m_format = VkFormat::VK_FORMAT_UNDEFINED;

public:
	const Buffer* pBuffer = nullptr;
	VkBufferView vkBufferView = VK_NULL_HANDLE;

private:

public:
	~BufferView();
	void Init();
	void Uninit();

	friend class Buffer;
};

//Resource Type :
//
//Storage Texel Buffer : Operates on buffer resources; ideal for 1D data that can benefit from format interpretation(e.g., integer or floating - point conversion).
//Storage Image : Operates on image resources; enables multi - dimensional access and supports a broader set of operations specific to images.
//Shader Access :
//
//Storage Texel Buffer : Accessed in shaders using imageLoadand imageStore functions with buffer access semantics.
//Storage Image : Fully supports imageLoad, imageStore, and atomic operations, providing more flexibility for image - oriented tasks.
//Setup Complexity :
//
//Storage Texel Buffer : Generally simpler to set up and use due to buffer - based nature.
//Storage Image : Involves more setup(memory layout transitions, view configurations) but offers richer functionality for image processing.