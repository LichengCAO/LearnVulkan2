#pragma once
#include "common.h"

class FramebufferAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkFramebuffer> m_mapHashToVkFramebuffer;

public:
	FramebufferAllocator() = default;
	FramebufferAllocator(const FramebufferAllocator&) = delete;
	FramebufferAllocator& operator=(const FramebufferAllocator&) = delete;
	~FramebufferAllocator();

	void Create(VkDevice inDevice);
	bool AllocateFramebuffer(const VkFramebufferCreateInfo* inCreateInfo, VkFramebuffer& outFramebuffer);
	void FreeFramebuffer(VkFramebuffer& inoutFramebuffer);
	void Destroy();
};