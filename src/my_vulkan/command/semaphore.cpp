#include "semaphore.h"
#include "allocator/semaphore_allocator.h"
#include "device.h"

Semaphore::Semaphore()
{
	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
	m_vkSemaphore = allocator->Allocate();
}

Semaphore::~Semaphore()
{
	if (m_vkSemaphore == VK_NULL_HANDLE)
	{
		return;
	}

	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
	allocator->Free(m_vkSemaphore);
	m_vkSemaphore = VK_NULL_HANDLE;
}
