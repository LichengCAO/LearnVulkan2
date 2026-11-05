#include "buffer.h"
#include "device.h"
#include "allocator/private/memory_allocator.h"
#include "utils.h"
#include "private/buffer_storage.h"

Buffer::~Buffer() noexcept
{
	_DestroyViews();
}

void Buffer::Create(const BufferCreateInfo& inCreateInfo)
{
	CHECK_TRUE(!IsValid(), "Buffer is already created!");
	CHECK_TRUE(inCreateInfo.m_bufferSize > 0 && inCreateInfo.m_usage != 0, "Buffer size and usage are required!");
	CHECK_TRUE(inCreateInfo.m_sharingMode == VK_SHARING_MODE_EXCLUSIVE,
		"Buffer creation currently supports exclusive sharing only!");
	CHECK_TRUE(!inCreateInfo.m_optAlignment ||
		(*inCreateInfo.m_optAlignment & (*inCreateInfo.m_optAlignment - 1)) == 0, "Invalid buffer alignment!");
	auto& device = MyDevice::GetInstance();
	auto* allocator = device.GetMemoryAllocator();
	CHECK_TRUE(device.vkDevice != VK_NULL_HANDLE && allocator && allocator->IsValid(), "Buffer requires an initialized Device and allocator!");
	auto storage = std::make_unique<VulkanDetail::BufferStorage>();
	storage->device = device.vkDevice;
	Information bufferToFill{};
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = inCreateInfo.m_bufferSize;
	bufferInfo.usage = inCreateInfo.m_usage;
	bufferInfo.sharingMode = inCreateInfo.m_sharingMode;
	bufferToFill.memoryProperty = inCreateInfo.m_memoryProperty;
	bufferToFill.sharingMode = inCreateInfo.m_sharingMode;
	bufferToFill.size = inCreateInfo.m_bufferSize;
	bufferToFill.usage = inCreateInfo.m_usage;
	bufferToFill.optAlignment = inCreateInfo.m_optAlignment;
	VkBuffer native = VK_NULL_HANDLE;
	VK_CHECK(vkCreateBuffer(storage->device, &bufferInfo, nullptr, &native), "Failed to create a VkBuffer!");
	storage->buffer = native;
	storage->allocation = allocator->AllocateForVkBuffer(storage->buffer, bufferToFill.memoryProperty,
		bufferToFill.optAlignment.value_or(0));
	m_bufferInformation = bufferToFill;
	m_storage = std::move(storage);
}

void Buffer::_CopyFromHostWithMappedMemory(const void* src, size_t bufferOffset, size_t size)
{
	CHECK_TRUE(IsValid(), "Cannot copy into an empty Buffer!");
	uint8_t* pMapped = static_cast<uint8_t*>(m_storage->allocation.Map());
	pMapped += bufferOffset;
	memcpy((void*)pMapped, src, size);
}

Buffer::Buffer()
{
}

Buffer::Buffer(Buffer&& _toMove) noexcept
{
	m_storage = std::move(_toMove.m_storage);
	m_bufferInformation = std::exchange(_toMove.m_bufferInformation, Information{});
	m_uptrBufferViews = std::move(_toMove.m_uptrBufferViews);
}

void Buffer::CopyFromHost(const void* src, size_t bufferOffset, size_t size)
{
	CHECK_TRUE(src != nullptr, "No source data!");
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
		"CopyFromHost requires host-visible memory!");
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
		"CopyFromHost requires host-coherent memory!");
	CHECK_TRUE(bufferOffset <= static_cast<size_t>(m_bufferInformation.size), "Copy offset out of range!");
	CHECK_TRUE(
		size <= static_cast<size_t>(m_bufferInformation.size) - bufferOffset,
		"Try to copy too much data from host!");

	_CopyFromHostWithMappedMemory(src, bufferOffset, size);
}

auto Buffer::GetBufferInformation() const -> const Buffer::Information&
{
	return m_bufferInformation;
}

auto Buffer::GetDeviceAddress() const -> VkDeviceAddress
{
	VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	
	info.pNext = nullptr;
	info.buffer = GetVkBuffer();

	CHECK_TRUE(IsValid());
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT), 
		"Maybe VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT need to be added!");

	return vkGetBufferDeviceAddress(m_storage->device, &info);
}

auto Buffer::GetDescriptorBufferInfo() const -> VkDescriptorBufferInfo
{
	VkDescriptorBufferInfo result{};

	result.buffer = GetVkBuffer();
	result.offset = 0;
	result.range = m_bufferInformation.size;

	return result;
}

auto Buffer::GetVkBuffer() const -> VkBuffer
{
	return m_storage ? m_storage->buffer : VK_NULL_HANDLE;
}

auto Buffer::IsValid() const noexcept -> bool { return m_storage && m_storage->buffer != VK_NULL_HANDLE; }

auto BufferCreateInfo::Reset() -> BufferCreateInfo&
{
	*this = BufferCreateInfo{};
	return *this;
}

auto BufferCreateInfo::SetBufferSize(VkDeviceSize inSize) -> BufferCreateInfo&
{
	m_bufferSize = inSize;
	return *this;
}

auto BufferCreateInfo::SetBufferUsage(VkBufferUsageFlags inUsage) -> BufferCreateInfo&
{
	m_usage = inUsage;
	return *this;
}

auto BufferCreateInfo::CustomizeSharingMode(VkSharingMode inSharingMode) -> BufferCreateInfo&
{
	m_sharingMode = inSharingMode;
	return *this;
}

auto BufferCreateInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties) -> BufferCreateInfo&
{
	m_memoryProperty = inMemoryProperties;
	return *this;
}

auto BufferCreateInfo::CustomizeAlignment(VkDeviceSize inAlignment) -> BufferCreateInfo&
{
	m_optAlignment = inAlignment;
	return *this;
}

auto BufferViewInfo::Reset()->BufferViewInfo&
{
	*this = BufferViewInfo{};

	return *this;
}

auto BufferViewInfo::SetFormat(VkFormat inFormat)->BufferViewInfo&
{
	m_format = inFormat;

	return *this;
}

auto BufferViewInfo::CustomizeBufferRange(VkDeviceSize inOffset, VkDeviceSize inRange)->BufferViewInfo&
{
	m_offset = inOffset;
	m_range = inRange;

	return *this;
}

auto Buffer::_FindView(const BufferViewInfo& inCreateInfo) const -> BufferView*
{
	CHECK_TRUE(IsValid(), "Buffer must be created before requesting a buffer view!");
	CHECK_TRUE(inCreateInfo.m_format != VK_FORMAT_UNDEFINED, "Buffer view format must be set!");
	CHECK_TRUE(inCreateInfo.m_offset < m_bufferInformation.size, "Buffer view offset out of range!");

	const VkDeviceSize resolvedRange = (inCreateInfo.m_range == VK_WHOLE_SIZE)
		? (m_bufferInformation.size - inCreateInfo.m_offset)
		: inCreateInfo.m_range;

	CHECK_TRUE(resolvedRange > 0, "Buffer view range must be greater than 0!");
	CHECK_TRUE(resolvedRange <= m_bufferInformation.size - inCreateInfo.m_offset, "Buffer view range out of range!");
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) ||
		CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT),
		"This buffer cannot have buffer views!");

	for (const auto& uptrView : m_uptrBufferViews)
	{
		const auto& viewInfo = uptrView->GetBufferViewInformation();
		if (viewInfo.format == inCreateInfo.m_format
			&& viewInfo.offset == inCreateInfo.m_offset
			&& viewInfo.range == resolvedRange)
		{
			return uptrView.get();
		}
	}

	return nullptr;
}

void Buffer::_DestroyViews()
{
	for (auto& uptrView : m_uptrBufferViews)
	{
		if (uptrView != nullptr)
		{
			uptrView->_Destroy();
		}
	}
	m_uptrBufferViews.clear();
}

auto Buffer::View(const BufferViewInfo& inCreateInfo)->const BufferView*
{
	if (BufferView* existingView = _FindView(inCreateInfo))
	{
		return existingView;
	}

	auto uptrView = std::make_unique<BufferView>();
	uptrView->_Create(*this, inCreateInfo);
	BufferView* result = uptrView.get();
	m_uptrBufferViews.push_back(std::move(uptrView));

	return result;
}

BufferView::~BufferView() noexcept
{
	_Destroy();
}

void BufferView::_Create(const Buffer& inBuffer, const BufferViewInfo& inCreateInfo)
{
	CHECK_TRUE(m_vkBufferView == VK_NULL_HANDLE, "VkBufferView is already created!");

	const auto& bufferInfo = inBuffer.GetBufferInformation();
	CHECK_TRUE(inCreateInfo.m_offset < bufferInfo.size, "Buffer view offset out of range!");

	const VkDeviceSize resolvedRange = (inCreateInfo.m_range == VK_WHOLE_SIZE)
		? (bufferInfo.size - inCreateInfo.m_offset)
		: inCreateInfo.m_range;

	CHECK_TRUE(inCreateInfo.m_format != VK_FORMAT_UNDEFINED, "Buffer view format must be set!");
	CHECK_TRUE(resolvedRange > 0, "Buffer view range must be greater than 0!");
	CHECK_TRUE(resolvedRange <= bufferInfo.size - inCreateInfo.m_offset, "Buffer view range out of range!");
	CHECK_TRUE(
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) ||
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT),
		"This buffer cannot have buffer views!");

	m_device = inBuffer.m_storage->device;
	VkBufferViewCreateInfo createInfo{ VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };

	m_viewInformation.vkBuffer = inBuffer.GetVkBuffer();
	m_viewInformation.format = inCreateInfo.m_format;
	m_viewInformation.offset = inCreateInfo.m_offset;
	m_viewInformation.range = resolvedRange;

	createInfo.buffer = m_viewInformation.vkBuffer;
	createInfo.format = m_viewInformation.format;
	createInfo.offset = m_viewInformation.offset;
	createInfo.range = m_viewInformation.range;

	VkBufferView native = VK_NULL_HANDLE;
	VK_CHECK(vkCreateBufferView(m_device, &createInfo, nullptr, &native), "Failed to create buffer view!");
	m_vkBufferView = native;
}

void BufferView::_Destroy() noexcept
{
	if (m_vkBufferView != VK_NULL_HANDLE)
	{
		vkDestroyBufferView(m_device, m_vkBufferView, nullptr);
		m_vkBufferView = VK_NULL_HANDLE;
	}
	m_viewInformation = BufferView::Information{};
}

auto BufferView::GetBufferViewInformation() const->const Information&
{
	return m_viewInformation;
}

auto BufferView::GetVkBufferView() const->VkBufferView
{
	return m_vkBufferView;
}
