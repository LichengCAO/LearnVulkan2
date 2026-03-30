#pragma once
#include "common.h"
#include "common_enums.h"

class Buffer;
class CommandBuffer;
class CommandPool;

class StagingBufferUploader
{
private:
	std::unique_ptr<Buffer> m_uptrStagingBuffer;
	std::unique_ptr<CommandBuffer> m_uptrCommandBuffer;
	std::unique_ptr<CommandPool> m_uptrCommandPool;
	VkDeviceSize m_stagingBufferSize = 0;
	VkFence m_vkUploadFence = VK_NULL_HANDLE;
    bool m_hasPendingUpload = false;

private:
	void _Submit(VkCommandBuffer inVkCommandBuffer, bool inWaitForCompletion);

public:
    class Creator
    {
    private:
        std::optional<VkDeviceSize> m_stagingBufferSize;
        std::optional<QueueFamilyType> m_queueFamilyType;

    public:
        auto CustomizeStagingBufferSizeBudget(VkDeviceSize inSize) -> Creator&;
        auto CustomizeQueueFamilyType(QueueFamilyType inType) -> Creator&;
    };

    struct UploadRequest
    {
        Buffer* dstBuffer;
        VkDeviceSize dstOffset;
        const void* srcData;
        size_t size;
    };

public:
	~StagingBufferUploader();

	// Create a persistent host-visible staging buffer and command resources for uploads.
	void Init(const Creator* inCreator);

    void UploadToBuffers(const UploadRequest* inRequests, size_t inRequestCount);

	// Wait for the latest submitted upload to complete.
	void WaitForUpload();

	void Uninit();
};
