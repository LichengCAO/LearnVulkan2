#pragma once
#include "common.h"

class Buffer;
class CommandBuffer;

class AsyncDeviceDataLoader
{
private:
	VkFence m_vkFence = VK_NULL_HANDLE;
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
	std::unique_ptr<Buffer> m_uptrBuffer;
	std::unique_ptr<CommandBuffer> m_uptrCommandBuffer;

private:
	CommandBuffer* _GetCommandBuffer();
	Buffer* _GetStagingBuffer();
	void _CopyToStagingBuffer(const void* inSrcData, size_t inSize);

public:
	void Init();

	void Process();

	void Uninit();
};

