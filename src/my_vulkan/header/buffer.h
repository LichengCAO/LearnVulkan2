#pragma once
#include "common.h"

// ref: https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
class Buffer;
class BufferView;
class CommandBuffer;
class MemoryAllocator;

class IBufferInitializer
{
public:
	virtual void InitBuffer(Buffer* outBufferPtr) const = 0;
};

class IBufferViewInitializer
{
public:
	virtual void InitBufferView(BufferView* outViewPtr) const = 0;
};

class Buffer final
{
public:
	class Initializer : public IBufferInitializer
	{
	private:
		std::optional<VkDeviceSize> m_optAlignment;
		VkMemoryPropertyFlags m_memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		VkSharingMode m_sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkDeviceSize m_bufferSize;
		VkBufferUsageFlags m_usage;

	public:
		virtual void InitBuffer(Buffer* outBufferPtr) const override;

		// Reset optional values to default, clear other values to zero
		Buffer::Initializer& Reset();

		// Set buffer size, mandatory
		Buffer::Initializer& SetBufferSize(VkDeviceSize inSize);

		// Set buffer usage, mandatory
		Buffer::Initializer& SetBufferUsage(VkBufferUsageFlags inUsage);

		// Optional, default: VK_SHARING_MODE_EXCLUSIVE
		Buffer::Initializer& CustomizeSharingMode(VkSharingMode inSharingMode);
		
		// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		Buffer::Initializer& CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties);
		
		// Optional, for buffers that have alignment requirement
		Buffer::Initializer& CustomizeAlignment(VkDeviceSize inAlignment);

		friend class Buffer;
	};

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

public:
	Buffer();
	
	Buffer(Buffer&& _toMove);
	
	Buffer(Buffer& _toCopy) = delete;
	
	~Buffer();

	void Init(const IBufferInitializer* inInitializerPtr);
	
	void Uninit();

	// Copy from host, will use stagging buffer if necessary, use buffer's size as length
	void CopyFromHost(const void* src);
	
	// Copy from host, will use stagging buffer if necessary
	void CopyFromHost(const void* src, size_t bufferOffset, size_t size);
	
	// Copy from source buffer, will wait until copy is done, use dist buffer's size as length
	void CopyFromBuffer(const Buffer* pOtherBuffer);
	
	// Copy from buffer on graphics queue, wait till copy finish
	void CopyFromBuffer(
		const Buffer* inSrcBufferPtr, 
		size_t inSrcOffset, 
		size_t inDstOffset, 
		size_t inSize);

	// Record copy command to command buffer
	void CopyFromBuffer(
		const Buffer* inSrcBufferPtr,
		size_t inSrcOffset,
		size_t inDstOffset,
		size_t inSize,
		CommandBuffer* outCmdPtr);

	// Fill buffer with input data by graphics queue, wait till finish
	void Fill(uint32_t inData);

	// Record fill data command into command buffer
	void Fill(uint32_t inData, CommandBuffer* outCmdPtr);

	// Get buffer information
	const Buffer::Information& GetBufferInformation() const;
	
	// Get device address of the buffer, require VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	VkDeviceAddress GetDeviceAddress() const;

	// Get default VkDescriptorBufferInfo, with offset = 0, range = bufferSize
	VkDescriptorBufferInfo GetDescriptorBufferInfo() const;

	// Get device handle of this buffer
	VkBuffer GetVkBuffer() const;
};

class BufferView // use for texel buffer
{
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
private:
	VkBufferView m_vkBufferView = VK_NULL_HANDLE;
	VkFormat m_format = VK_FORMAT_UNDEFINED;

public:
	class Initializer : public IBufferViewInitializer
	{
	private:
		VkFormat m_format = VK_FORMAT_UNDEFINED;
		VkBuffer m_buffer = VK_NULL_HANDLE;
		VkDeviceSize m_offset = 0;
		VkDeviceSize m_range = 0;

	public:
		virtual void InitBufferView(BufferView* outViewPtr) const override;
		BufferView::Initializer& Reset();
		BufferView::Initializer& SetFormat(VkFormat inFormat);
		BufferView::Initializer& SetBuffer(const Buffer* inBufferPtr);
	};

public:
	~BufferView();
	
	void Init(const IBufferViewInitializer* inInitPtr);
	
	void Uninit();
};