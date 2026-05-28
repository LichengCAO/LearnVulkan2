#include "staging_buffer_uploader.h"
#include "buffer.h"
#include "command_buffer.h"
#include "device.h"
#include <algorithm>

StagingBufferUploader::~StagingBufferUploader()
{
	assert(m_uptrStagingBuffer == nullptr);
	assert(m_uptrCommandBuffer == nullptr);
	assert(m_uptrCommandPool == nullptr);
	assert(m_stagingBufferSize == 0);
	assert(m_vkUploadFence == VK_NULL_HANDLE);
	assert(!m_hasPendingUpload);
}

void StagingBufferUploader::Create(
	VkDeviceSize inStagingBufferSize,
	QueueFamilyType inQueueType)
{
	CHECK_TRUE(inStagingBufferSize > 0);
	CHECK_TRUE(m_uptrStagingBuffer == nullptr);
	CHECK_TRUE(m_uptrCommandBuffer == nullptr);
	CHECK_TRUE(m_uptrCommandPool == nullptr);
	CHECK_TRUE(m_stagingBufferSize == 0);
	CHECK_TRUE(m_vkUploadFence == VK_NULL_HANDLE);
	CHECK_TRUE(!m_hasPendingUpload);

	auto& device = MyDevice::GetInstance();
	Buffer::Initializer stagingInit{};
	CommandPool::Initializer poolInit{};
	CommandBuffer::Initializer cmdInit{};

	m_stagingBufferSize = inStagingBufferSize;

	m_uptrCommandPool = std::make_unique<CommandPool>();
	poolInit.CustomizeCommandPoolCreateFlags(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	poolInit.CustomizeQueueFamilyIndex(device.GetQueueFamilyIndexOfType(inQueueType));
	m_uptrCommandPool->Create(&poolInit);

	m_uptrCommandBuffer = std::make_unique<CommandBuffer>();
	cmdInit.SetCommandPool(m_uptrCommandPool->GetVkCommandPool());
	m_uptrCommandBuffer->Create(&cmdInit);

	m_uptrStagingBuffer = std::make_unique<Buffer>();
	stagingInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	stagingInit.SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	stagingInit.SetBufferSize(m_stagingBufferSize);
	m_uptrStagingBuffer->Create(&stagingInit);

	m_vkUploadFence = device.CreateVkFence(0);
}

void StagingBufferUploader::UploadToBuffer(
	Buffer* inDstBuffer,
	VkDeviceSize inDstOffset,
	const void* inSrcData,
	VkDeviceSize inSize,
	bool inWaitForCompletion)
{
	CHECK_TRUE(inDstBuffer != nullptr);
	CHECK_TRUE(inSrcData != nullptr);
	CHECK_TRUE(inSize > 0);
	CHECK_TRUE(m_uptrStagingBuffer != nullptr);
	CHECK_TRUE(m_uptrCommandBuffer != nullptr);
	CHECK_TRUE(m_uptrCommandPool != nullptr);
	CHECK_TRUE(m_stagingBufferSize > 0);
	CHECK_TRUE(!m_hasPendingUpload, "Upload already pending. Call WaitForUpload() before submitting another upload.");

	const auto& dstInfo = inDstBuffer->GetBufferInformation();
	CHECK_TRUE(CONTAIN_BITS(dstInfo.usage, VK_BUFFER_USAGE_TRANSFER_DST_BIT), "Destination buffer must have VK_BUFFER_USAGE_TRANSFER_DST_BIT.");
	CHECK_TRUE(inDstOffset <= dstInfo.size, "Destination offset exceeds destination buffer size.");
	CHECK_TRUE(inSize <= dstInfo.size - inDstOffset, "Upload range exceeds destination buffer size.");

	if (!inWaitForCompletion)
	{
		CHECK_TRUE(inSize <= m_stagingBufferSize, "Async upload size exceeds staging buffer size. Use larger staging buffer or call with inWaitForCompletion=true.");

		m_uptrStagingBuffer->CopyFromHost(inSrcData, 0, static_cast<size_t>(inSize));
		m_uptrCommandBuffer->Reset();
		m_uptrCommandBuffer->BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = inDstOffset;
		copyRegion.size = inSize;
		m_uptrCommandBuffer->CmdCopyBuffer(
			m_uptrStagingBuffer->GetVkBuffer(),
			inDstBuffer->GetVkBuffer(),
			{ copyRegion });

		m_uptrCommandBuffer->EndCommands();
		_Submit(m_uptrCommandBuffer->GetVkCommandBuffer(), false);
		return;
	}

	const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(inSrcData);
	VkDeviceSize srcOffset = 0;
	VkDeviceSize dstOffset = inDstOffset;
	VkDeviceSize remain = inSize;

	while (remain > 0)
	{
		const VkDeviceSize chunkSize = std::min(remain, m_stagingBufferSize);

		m_uptrStagingBuffer->CopyFromHost(srcBytes + srcOffset, 0, static_cast<size_t>(chunkSize));

		m_uptrCommandBuffer->Reset();
		m_uptrCommandBuffer->BeginCommands(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = dstOffset;
		copyRegion.size = chunkSize;
		m_uptrCommandBuffer->CmdCopyBuffer(
			m_uptrStagingBuffer->GetVkBuffer(),
			inDstBuffer->GetVkBuffer(),
			{ copyRegion });

		m_uptrCommandBuffer->EndCommands();
		_Submit(m_uptrCommandBuffer->GetVkCommandBuffer(), true);

		srcOffset += chunkSize;
		dstOffset += chunkSize;
		remain -= chunkSize;
	}
}

void StagingBufferUploader::WaitForUpload()
{
	if (!m_hasPendingUpload)
	{
		return;
	}

	auto& device = MyDevice::GetInstance();
	VK_CHECK(vkWaitForFences(device.vkDevice, 1, &m_vkUploadFence, VK_TRUE, UINT64_MAX), "Failed to wait for staging upload completion.");
	m_hasPendingUpload = false;
}

void StagingBufferUploader::Destroy()
{
	WaitForUpload();

	auto& device = MyDevice::GetInstance();
	if (m_vkUploadFence != VK_NULL_HANDLE)
	{
		device.DestroyVkFence(m_vkUploadFence);
	}

	if (m_uptrCommandBuffer != nullptr)
	{
		m_uptrCommandBuffer->Destroy();
		m_uptrCommandBuffer.reset();
	}
	if (m_uptrCommandPool != nullptr)
	{
		m_uptrCommandPool->Destroy();
		m_uptrCommandPool.reset();
	}
	if (m_uptrStagingBuffer != nullptr)
	{
		m_uptrStagingBuffer->Destroy();
		m_uptrStagingBuffer.reset();
	}
	m_stagingBufferSize = 0;
	m_hasPendingUpload = false;
}

void StagingBufferUploader::_Submit(VkCommandBuffer inVkCommandBuffer, bool inWaitForCompletion)
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrCommandPool != nullptr);
	CHECK_TRUE(m_vkUploadFence != VK_NULL_HANDLE);

	auto& device = MyDevice::GetInstance();
	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkQueue queue = device.GetQueueByQueueFamilyIndex(m_uptrCommandPool->GetQueueFamilyIndex());

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &inVkCommandBuffer;
	VK_CHECK(vkResetFences(device.vkDevice, 1, &m_vkUploadFence), "Failed to reset staging upload fence.");
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, m_vkUploadFence), "Failed to submit staging upload command.");
	m_hasPendingUpload = true;

	if (inWaitForCompletion)
	{
		WaitForUpload();
	}
}
