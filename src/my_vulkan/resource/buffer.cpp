#include "buffer.h"
#include "device.h"
#include "commandbuffer.h"
#include "memory_allocator.h"
#include "utils.h"

namespace 
{
	void _SubmitToGraphicsQueueAndWait(VkCommandBuffer inCommandBuffer)
	{
		VkSubmitInfo submitInfo{};
		std::vector<VkCommandBuffer> cmdsToSubmit = { inCommandBuffer };
		auto& device = MyDevice::GetInstance();
		VkQueue queueToSubmit = device.GetQueueOfType(QueueFamilyType::GRAPHICS);
		
		submitInfo.commandBufferCount = cmdsToSubmit.size();
		submitInfo.pCommandBuffers = cmdsToSubmit.data();
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		vkQueueSubmit(queueToSubmit, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(queueToSubmit);
	}
}

Buffer::~Buffer()
{
	assert(m_uptrBufferViews.empty());
	assert(m_vkBuffer == VK_NULL_HANDLE);
	assert(m_mappedMemory == nullptr);
}

void Buffer::Create(const BufferCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No buffer create info!");

	auto& device = MyDevice::GetInstance();
	auto& bufferToFill = m_bufferInformation;
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = inCreateInfo->m_bufferSize;
	bufferInfo.usage = inCreateInfo->m_usage;
	bufferInfo.sharingMode = inCreateInfo->m_sharingMode;
	bufferToFill.memoryProperty = inCreateInfo->m_memoryProperty;
	bufferToFill.sharingMode = inCreateInfo->m_sharingMode;
	bufferToFill.size = inCreateInfo->m_bufferSize;
	bufferToFill.usage = inCreateInfo->m_usage;
	bufferToFill.optAlignment = inCreateInfo->m_optAlignment;
	m_vkBuffer = device.CreateBuffer(bufferInfo);
	_AllocateMemory();
}

void Buffer::Destroy()
{
	_DestroyViews();

	if (m_vkBuffer != VK_NULL_HANDLE)
	{
		_FreeMemory();
		MyDevice::GetInstance().DestroyBuffer(m_vkBuffer);
		m_vkBuffer = VK_NULL_HANDLE;
	}
}

MemoryAllocator* Buffer::_GetMemoryAllocator() const
{
	return MyDevice::GetInstance().GetMemoryAllocator();
}

void Buffer::_AllocateMemory()
{	
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_bufferInformation.optAlignment.has_value())
	{
		pAllocator->AllocateForVkBuffer(m_vkBuffer, m_bufferInformation.memoryProperty, m_bufferInformation.optAlignment.value());
	}
	else
	{
		pAllocator->AllocateForVkBuffer(m_vkBuffer, m_bufferInformation.memoryProperty);
	}
}

void Buffer::_FreeMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	if (m_mappedMemory != nullptr)
	{
		pAllocator->UnmapVkBuffer(m_vkBuffer);
		m_mappedMemory = nullptr;
	}
	pAllocator->FreeMemory(m_vkBuffer);
}

void Buffer::_MapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	CHECK_TRUE(CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "Unmappable buffer!");
	pAllocator->MapVkBufferToHost(m_vkBuffer, m_mappedMemory);
}

void Buffer::_UnmapHostMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->UnmapVkBuffer(m_vkBuffer);
}

void Buffer::_CopyFromHostWithMappedMemory(const void* src, size_t bufferOffest, size_t size)
{
	if (m_mappedMemory == nullptr)
	{
		MemoryAllocator* pAllocator = _GetMemoryAllocator();
		pAllocator->MapVkBufferToHost(m_vkBuffer, m_mappedMemory);
	}
	uint8_t* pMapped = (uint8_t*)m_mappedMemory;
	pMapped += bufferOffest;
	memcpy((void*)pMapped, src, size);
}

void Buffer::_CopyFromHostWithStaggingBuffer(const void* src, size_t bufferOffest, size_t size)
{
	Buffer stagingBuffer{};
	BufferCreateInfo createInfo{};

	createInfo.SetBufferSize(size).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	createInfo.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	stagingBuffer.Create(&createInfo);
	stagingBuffer.CopyFromHost(src, 0, size);
	
	CopyFromBuffer(&stagingBuffer, 0, bufferOffest, size);

	stagingBuffer.Destroy();
}

Buffer::Buffer()
{
}

Buffer::Buffer(Buffer&& _toMove)
{
	m_vkBuffer = _toMove.m_vkBuffer;
	m_mappedMemory = _toMove.m_mappedMemory;
	m_bufferInformation = _toMove.m_bufferInformation;
	m_uptrBufferViews = std::move(_toMove.m_uptrBufferViews);
	_toMove.m_vkBuffer = VK_NULL_HANDLE;
	_toMove.m_mappedMemory = nullptr;
}

void Buffer::CopyFromHost(const void* src, size_t bufferOffset, size_t size)
{
	CHECK_TRUE(size <= static_cast<size_t>(m_bufferInformation.size), "Try to copy too much data from host!");
	if (CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		&& CONTAIN_BITS(m_bufferInformation.memoryProperty, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		_CopyFromHostWithMappedMemory(src, bufferOffset, size);
	}
	else
	{
		_CopyFromHostWithStaggingBuffer(src, bufferOffset, size);
	}
}

void Buffer::CopyFromBuffer(const Buffer* pOtherBuffer, size_t srcOffset, size_t dstOffset, size_t size)
{
	CommandPool tmpPool{};
	CommandPool::Initializer poolInit{};
	CommandBuffer tmpCmdBuffer{};

	poolInit.CustomizeCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	tmpPool.Create(&poolInit);
	tmpPool.AllocateCommandBuffer(&tmpCmdBuffer);
	tmpCmdBuffer.BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	CopyFromBuffer(pOtherBuffer, srcOffset, dstOffset, size, &tmpCmdBuffer);
	tmpCmdBuffer.EndCommands();
	_SubmitToGraphicsQueueAndWait(tmpCmdBuffer.GetVkCommandBuffer());
	tmpPool.Destroy();
}

void Buffer::Fill(uint32_t inData)
{
	CommandPool tmpPool{};
	CommandPool::Initializer poolInit{};
	CommandBuffer cmd{};

	poolInit.CustomizeCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	tmpPool.Create(&poolInit);
	tmpPool.AllocateCommandBuffer(&cmd);	
	cmd.BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmd.CmdFillBuffer(m_vkBuffer, 0, m_bufferInformation.size, inData);
	cmd.EndCommands();
	_SubmitToGraphicsQueueAndWait(cmd.GetVkCommandBuffer());
	tmpPool.Destroy();
}

const Buffer::Information& Buffer::GetBufferInformation() const
{
	return m_bufferInformation;
}

VkDeviceAddress Buffer::GetDeviceAddress() const
{
	VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	auto& device = MyDevice::GetInstance();
	
	info.pNext = nullptr;
	info.buffer = m_vkBuffer;

	CHECK_TRUE(m_vkBuffer != VK_NULL_HANDLE);
	CHECK_TRUE(
		CONTAIN_BITS(m_bufferInformation.usage, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT), 
		"Maybe VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT need to be added!");

	return device.GetBufferDeviceAddress(info);
}

VkDescriptorBufferInfo Buffer::GetDescriptorBufferInfo() const
{
	VkDescriptorBufferInfo result{};

	result.buffer = m_vkBuffer;
	result.offset = 0;
	result.range = m_bufferInformation.size;

	return result;
}

VkBuffer Buffer::GetVkBuffer() const
{
	return m_vkBuffer;
}

BufferCreateInfo& BufferCreateInfo::Reset()
{
	*this = BufferCreateInfo{};
	return *this;
}

BufferCreateInfo& BufferCreateInfo::SetBufferSize(VkDeviceSize inSize)
{
	m_bufferSize = inSize;
	return *this;
}

BufferCreateInfo& BufferCreateInfo::SetBufferUsage(VkBufferUsageFlags inUsage)
{
	m_usage = inUsage;
	return *this;
}

BufferCreateInfo& BufferCreateInfo::CustomizeSharingMode(VkSharingMode inSharingMode)
{
	m_sharingMode = inSharingMode;
	return *this;
}

BufferCreateInfo& BufferCreateInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperties)
{
	m_memoryProperty = inMemoryProperties;
	return *this;
}

BufferCreateInfo& BufferCreateInfo::CustomizeAlignment(VkDeviceSize inAlignment)
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

BufferView* Buffer::_FindView(const BufferViewInfo& inCreateInfo) const
{
	CHECK_TRUE(m_vkBuffer != VK_NULL_HANDLE, "Buffer must be created before requesting a buffer view!");
	CHECK_TRUE(inCreateInfo.m_format != VK_FORMAT_UNDEFINED, "Buffer view format must be set!");
	CHECK_TRUE(inCreateInfo.m_offset < m_bufferInformation.size, "Buffer view offset out of range!");

	const VkDeviceSize resolvedRange = (inCreateInfo.m_range == VK_WHOLE_SIZE)
		? (m_bufferInformation.size - inCreateInfo.m_offset)
		: inCreateInfo.m_range;

	CHECK_TRUE(resolvedRange > 0, "Buffer view range must be greater than 0!");
	CHECK_TRUE((inCreateInfo.m_offset + resolvedRange) <= m_bufferInformation.size, "Buffer view range out of range!");
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
			uptrView->Destroy();
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
	uptrView->Create(this, &inCreateInfo);
	BufferView* result = uptrView.get();
	m_uptrBufferViews.push_back(std::move(uptrView));

	return result;
}

BufferView::~BufferView()
{
	assert(m_vkBufferView == VK_NULL_HANDLE);
}

auto BufferView::Create(const Buffer* inBuffer, const BufferViewInfo* inCreateInfo)->void
{
	CHECK_TRUE(inCreateInfo != nullptr, "No buffer view create info!");
	CHECK_TRUE(inBuffer != nullptr, "No buffer!");
	CHECK_TRUE(m_vkBufferView == VK_NULL_HANDLE, "VkBufferView is already created!");

	const auto& bufferInfo = inBuffer->GetBufferInformation();
	CHECK_TRUE(inCreateInfo->m_offset < bufferInfo.size, "Buffer view offset out of range!");

	const VkDeviceSize resolvedRange = (inCreateInfo->m_range == VK_WHOLE_SIZE)
		? (bufferInfo.size - inCreateInfo->m_offset)
		: inCreateInfo->m_range;

	CHECK_TRUE(inCreateInfo->m_format != VK_FORMAT_UNDEFINED, "Buffer view format must be set!");
	CHECK_TRUE(resolvedRange > 0, "Buffer view range must be greater than 0!");
	CHECK_TRUE((inCreateInfo->m_offset + resolvedRange) <= bufferInfo.size, "Buffer view range out of range!");
	CHECK_TRUE(
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) ||
		CONTAIN_BITS(bufferInfo.usage, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT),
		"This buffer cannot have buffer views!");

	auto& device = MyDevice::GetInstance();
	VkBufferViewCreateInfo createInfo{ VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };

	m_viewInformation.vkBuffer = inBuffer->GetVkBuffer();
	m_viewInformation.format = inCreateInfo->m_format;
	m_viewInformation.offset = inCreateInfo->m_offset;
	m_viewInformation.range = resolvedRange;

	createInfo.buffer = m_viewInformation.vkBuffer;
	createInfo.format = m_viewInformation.format;
	createInfo.offset = m_viewInformation.offset;
	createInfo.range = m_viewInformation.range;

	m_vkBufferView = device.CreateBufferView(createInfo);
}

auto BufferView::Destroy()->void
{
	if (m_vkBufferView != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		device.DestroyBufferView(m_vkBufferView);
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
