#pragma once
#include "common.h"
#include <unordered_set>

class SemaphoreAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::vector<VkSemaphore> m_freeList;
	std::unordered_set<VkSemaphore> m_usedList;
	bool m_created = false;

public:
	SemaphoreAllocator() = default;
	SemaphoreAllocator(const SemaphoreAllocator&) = delete;
	SemaphoreAllocator& operator=(const SemaphoreAllocator&) = delete;
	~SemaphoreAllocator();

	auto Create()->void;

	auto Allocate()->VkSemaphore;

	auto Free(VkSemaphore inSemaphore)->void;

	// Destroy an allocated semaphore instead of returning it to the free list.
	// This is required for an unconsumed, signaled binary semaphore because
	// binary semaphores cannot be reset by the host.
	auto Discard(VkSemaphore inSemaphore)->void;

	auto Destroy()->void;
};
