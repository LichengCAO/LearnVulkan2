#include "semaphore_allocator.h"
#include "device.h"

SemaphoreAllocator::~SemaphoreAllocator()
{
	Destroy();
}

auto SemaphoreAllocator::Create()->void
{
	CHECK_TRUE(!m_created, "Semaphore allocator is already created!");
	m_vkDevice = MyDevice::GetInstance().vkDevice;
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Invalid semaphore allocator device!");
	m_created = true;
}

auto SemaphoreAllocator::Allocate()->VkSemaphore
{
	CHECK_TRUE(m_created, "Semaphore allocator is not created!");

	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (!m_freeList.empty())
	{
		semaphore = m_freeList.back();
		m_freeList.pop_back();
	}
	else
	{
		VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VK_CHECK(
			vkCreateSemaphore(m_vkDevice, &createInfo, nullptr, &semaphore),
			"Failed to create semaphore!");
	}

	const auto [iter, inserted] = m_usedList.emplace(semaphore);
	CHECK_TRUE(inserted, "Semaphore is already allocated!");

	return semaphore;
}

auto SemaphoreAllocator::Free(VkSemaphore inSemaphore)->void
{
	CHECK_TRUE(m_created, "Semaphore allocator is not created!");
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Cannot free null semaphore!");

	const auto iter = m_usedList.find(inSemaphore);
	CHECK_TRUE(iter != m_usedList.end(), "Semaphore allocator doesn't have this allocated semaphore!");

	m_usedList.erase(iter);
	m_freeList.push_back(inSemaphore);
}

auto SemaphoreAllocator::Destroy()->void
{
	if (!m_created)
	{
		return;
	}

	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (VkSemaphore semaphore : m_freeList)
		{
			vkDestroySemaphore(m_vkDevice, semaphore, nullptr);
		}

		for (VkSemaphore semaphore : m_usedList)
		{
			vkDestroySemaphore(m_vkDevice, semaphore, nullptr);
		}
	}

	m_freeList.clear();
	m_usedList.clear();
	m_vkDevice = VK_NULL_HANDLE;
	m_created = false;
}
