#pragma once
#include "common.h"

class Semaphore final
{
private:
	VkSemaphore m_vkSemaphore = VK_NULL_HANDLE;

public:
	Semaphore();
	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;
	~Semaphore();

	auto GetVkSemaphore() const->VkSemaphore { return m_vkSemaphore; };
};
