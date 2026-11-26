#include "buffer.h"
#include "device.h"
#include "allocator/private/memory_allocator.h"
#include "render_context/device_context.h"
#include "utils.h"
#include "private/buffer_storage.h"
#include <cstring>

namespace
{
constexpr VkMemoryPropertyFlags kCoherentHostMemory =
	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

void ValidateInlineTransfer(const Buffer& inBuffer, VkDeviceSize inOffset, const void* inData, size_t inSize)
{
	CHECK_TRUE(inBuffer.IsValid(), "Inline transfer requires a created Buffer!");
	const VkDeviceSize bufferSize = inBuffer.GetBufferInformation().size;
	CHECK_TRUE(inOffset <= bufferSize && inSize <= bufferSize - inOffset, "Buffer transfer range out of bounds!");
	CHECK_TRUE(inSize == 0 || inData != nullptr, "Non-empty Buffer transfer requires a Host pointer!");
}

void RecordTransferBarrier(VkCommandBuffer inCommands, VkBuffer inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize,
	VkPipelineStageFlags2 inSrcStage, VkAccessFlags2 inSrcAccess, VkPipelineStageFlags2 inDstStage, VkAccessFlags2 inDstAccess)
{
	VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
	barrier.srcStageMask = inSrcStage;
	barrier.srcAccessMask = inSrcAccess;
	barrier.dstStageMask = inDstStage;
	barrier.dstAccessMask = inDstAccess;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = inBuffer;
	barrier.offset = inOffset;
	barrier.size = inSize;
	VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(inCommands, &dependency);
}
}

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

Buffer::Buffer()
{
}

Buffer::Buffer(Buffer&& _toMove) noexcept
{
	m_storage = std::move(_toMove.m_storage);
	m_bufferInformation = std::exchange(_toMove.m_bufferInformation, Information{});
	m_uptrBufferViews = std::move(_toMove.m_uptrBufferViews);
}

auto Buffer::_MapForInlineTransfer(VkDeviceSize inOffset, size_t inSize) const -> std::byte*
{
	CHECK_TRUE(inOffset <= SIZE_MAX - inSize, "Mapped Buffer transfer range does not fit the Host address space!");
	return static_cast<std::byte*>(m_storage->allocation.Map()) + static_cast<size_t>(inOffset);
}

void Buffer::_ExecuteTransfer(DeviceContext* inContext, QueueFamilyType inQueue,
	std::function<void(VkCommandBuffer)> inRecord)
{
	if (inContext == nullptr)
	{
		MyDevice::GetInstance()._ExecuteInlineAndWait(std::move(inRecord));
		return;
	}
	std::vector<CommandBuffer> commands(1);
	commands.front().AppendCommand(std::move(inRecord));
	inContext->ExecuteCommandsAndWait(inQueue, std::move(commands));
}

void Buffer::_UploadToDeviceInline(DeviceContext* inContext, QueueFamilyType inQueue, Buffer& inDst,
	VkDeviceSize inOffset, const void* inSrc, size_t inSize)
{
	ValidateInlineTransfer(inDst, inOffset, inSrc, inSize);
	if (inSize == 0)
		return;
	if (CONTAIN_BITS(inDst.m_bufferInformation.memoryProperty, kCoherentHostMemory))
	{
		std::memcpy(inDst._MapForInlineTransfer(inOffset, inSize), inSrc, inSize);
		return;
	}
	CHECK_TRUE(CONTAIN_BITS(inDst.m_bufferInformation.usage, VK_BUFFER_USAGE_TRANSFER_DST_BIT),
		"Staging upload requires VK_BUFFER_USAGE_TRANSFER_DST_BIT!");

	Buffer staging;
	BufferCreateInfo info;
	info.SetBufferSize(inSize).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).CustomizeMemoryProperty(kCoherentHostMemory);
	staging.Create(info);
	std::memcpy(staging._MapForInlineTransfer(0, inSize), inSrc, inSize);
	const VkBuffer src = staging.GetVkBuffer();
	const VkBuffer dst = inDst.GetVkBuffer();
	const VkBufferCopy region{0, inOffset, inSize};
	_ExecuteTransfer(inContext, inQueue, [src, dst, region](VkCommandBuffer commands)
	{
		RecordTransferBarrier(commands, dst, region.dstOffset, region.size,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		// Submission makes writes to the coherent staging allocation available to the copy.
		vkCmdCopyBuffer(commands, src, dst, 1, &region);
		RecordTransferBarrier(commands, dst, region.dstOffset, region.size,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
	});
}

void Buffer::_DownloadFromDeviceInline(DeviceContext* inContext, QueueFamilyType inQueue, const Buffer& inSrc,
	VkDeviceSize inOffset, void* outDst, size_t inSize)
{
	ValidateInlineTransfer(inSrc, inOffset, outDst, inSize);
	if (inSize == 0)
		return;
	if (CONTAIN_BITS(inSrc.m_bufferInformation.memoryProperty, kCoherentHostMemory))
	{
		std::memcpy(outDst, inSrc._MapForInlineTransfer(inOffset, inSize), inSize);
		return;
	}
	CHECK_TRUE(CONTAIN_BITS(inSrc.m_bufferInformation.usage, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
		"Staging download requires VK_BUFFER_USAGE_TRANSFER_SRC_BIT!");

	Buffer staging;
	BufferCreateInfo info;
	info.SetBufferSize(inSize).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_DST_BIT).CustomizeMemoryProperty(kCoherentHostMemory);
	staging.Create(info);
	const VkBuffer src = inSrc.GetVkBuffer();
	const VkBuffer dst = staging.GetVkBuffer();
	const VkBufferCopy region{inOffset, 0, inSize};
	_ExecuteTransfer(inContext, inQueue, [src, dst, region](VkCommandBuffer commands)
	{
		RecordTransferBarrier(commands, src, region.srcOffset, region.size,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
		vkCmdCopyBuffer(commands, src, dst, 1, &region);
		RecordTransferBarrier(commands, dst, region.dstOffset, region.size,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
	});
	std::memcpy(outDst, staging._MapForInlineTransfer(0, inSize), inSize);
}

void Buffer::UploadToDeviceInline(Buffer& inDst, VkDeviceSize inOffset, const void* inSrc, size_t inSize)
{
	_UploadToDeviceInline(nullptr, QueueFamilyType::COMPUTE, inDst, inOffset, inSrc, inSize);
}

void Buffer::DownloadFromDeviceInline(const Buffer& inSrc, VkDeviceSize inOffset, void* outDst, size_t inSize)
{
	_DownloadFromDeviceInline(nullptr, QueueFamilyType::COMPUTE, inSrc, inOffset, outDst, inSize);
}

void Buffer::UploadToDeviceInline(DeviceContext& inContext, QueueFamilyType inQueue, Buffer& inDst,
	VkDeviceSize inOffset, const void* inSrc, size_t inSize)
{
	_UploadToDeviceInline(&inContext, inQueue, inDst, inOffset, inSrc, inSize);
}

void Buffer::DownloadFromDeviceInline(DeviceContext& inContext, QueueFamilyType inQueue, const Buffer& inSrc,
	VkDeviceSize inOffset, void* outDst, size_t inSize)
{
	_DownloadFromDeviceInline(&inContext, inQueue, inSrc, inOffset, outDst, inSize);
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
auto Buffer::GetResourceId() const noexcept -> uint64_t { return m_storage ? m_storage->resourceId : 0; }

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
	const auto id = VulkanDetail::_AllocateResourceId();
	VK_CHECK(vkCreateBufferView(m_device, &createInfo, nullptr, &native), "Failed to create buffer view!");
	m_vkBufferView = native;
	m_resourceId = id;
}

void BufferView::_Destroy() noexcept
{
	if (m_vkBufferView != VK_NULL_HANDLE)
	{
		vkDestroyBufferView(m_device, m_vkBufferView, nullptr);
		m_vkBufferView = VK_NULL_HANDLE;
	}
	m_viewInformation = BufferView::Information{};
	m_resourceId = 0;
}

auto BufferView::GetBufferViewInformation() const->const Information&
{
	return m_viewInformation;
}

auto BufferView::GetVkBufferView() const->VkBufferView
{
	return m_vkBufferView;
}
