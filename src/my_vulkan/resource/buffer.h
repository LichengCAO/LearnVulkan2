#pragma once
#include "common.h"
#include "../ref_counted.h"

// ref: https://stackoverflow.com/questions/73512602/using-vulkan-memory-allocator-with-volk
class Buffer;
class BufferView;
class CommandBuffer;
class MemoryAllocator;
namespace VulkanDetail { struct BufferStorage; }

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
	VkDevice m_device = VK_NULL_HANDLE;

private:
	void _Create(const Buffer& inBuffer, const BufferViewInfo& inCreateInfo);
	void _Destroy() noexcept;

public:
	BufferView() = default;
	BufferView(const BufferView&) = delete;
	auto operator=(const BufferView&) -> BufferView& = delete;
	BufferView(BufferView&&) = delete;
	auto operator=(BufferView&&) -> BufferView& = delete;
	~BufferView() noexcept;

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
	VkDeviceSize m_bufferSize = 0;
	VkBufferUsageFlags m_usage = 0;

	friend class Buffer;

public:
	// Reset optional values to default, clear other values to zero
	auto Reset() -> BufferCreateInfo&;

	// Set buffer size, mandatory
	auto SetBufferSize(VkDeviceSize inSize) -> BufferCreateInfo&;

	// Set buffer usage, mandatory
	auto SetBufferUsage(VkBufferUsageFlags inUsage) -> BufferCreateInfo&;

	// Optional, default: VK_SHARING_MODE_EXCLUSIVE
	auto CustomizeSharingMode(VkSharingMode inSharingMode) -> BufferCreateInfo&;

	// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	auto CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties) -> BufferCreateInfo&;

	// Optional, for buffers that have alignment requirement
	auto CustomizeAlignment(VkDeviceSize inAlignment) -> BufferCreateInfo&;
};

class Buffer final : public RefCounted
{
	friend struct CommandBufferTestProbe;
	friend struct ShaderBindingTestProbe;
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
	std::unique_ptr<VulkanDetail::BufferStorage> m_storage;
	std::vector<std::unique_ptr<BufferView>> m_uptrBufferViews;

private:
	// Map the memory and copy from host-visible, host-coherent memory.
	void _CopyFromHostWithMappedMemory(const void* src, size_t bufferOffset, size_t size);

	auto _FindView(const BufferViewInfo& inCreateInfo) const -> BufferView*;

	void _DestroyViews();

public:
	// After Device initialization, independent Buffers may be created, mapped,
	// viewed and released concurrently. Callers exclude conflicting access to
	// one Buffer and keep its Device/allocator alive through final release.
	// Native GPU uses must complete before release; destruction never waits.
	Buffer();
	
	Buffer(Buffer&& _toMove) noexcept;
	
	Buffer(const Buffer& _toCopy) = delete;
	auto operator=(const Buffer&) -> Buffer& = delete;
	auto operator=(Buffer&&) -> Buffer& = delete;
	
	~Buffer() noexcept override;

	void Create(const BufferCreateInfo& inCreateInfo);
	
	auto IsValid() const noexcept -> bool;

	// Copy from host memory. Requires host-visible, host-coherent memory.
	void CopyFromHost(const void* src, size_t bufferOffset, size_t size);

	// Borrowed cached view; remains valid until this Buffer's final release.
	auto View(const BufferViewInfo& inCreateInfo)->const BufferView*;

	// Get buffer information
	auto GetBufferInformation() const -> const Buffer::Information&;
	
	// Get device address of the buffer, require VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	auto GetDeviceAddress() const -> VkDeviceAddress;

	// Get default VkDescriptorBufferInfo, with offset = 0, range = bufferSize
	auto GetDescriptorBufferInfo() const -> VkDescriptorBufferInfo;

	// Get device handle of this buffer
	auto GetVkBuffer() const -> VkBuffer;
	friend class BufferView;
};
