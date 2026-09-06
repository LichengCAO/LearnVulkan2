#include "queue_signal_chain.h"

#include "allocator/semaphore_allocator.h"
#include "device.h"

QueueSignalChain::~QueueSignalChain()
{
	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	if (allocator == nullptr)
	{
		return;
	}

	if (m_preparedSignal != VK_NULL_HANDLE)
	{
		allocator->Free(m_preparedSignal);
		m_preparedSignal = VK_NULL_HANDLE;
	}
	if (m_currentSignal != VK_NULL_HANDLE)
	{
		// The tail has been signaled but never consumed by a later wait. Waiting
		// for the device makes destruction legal; it still cannot be reused as a
		// fresh binary semaphore because the host cannot reset its signal state.
		if (MyDevice::GetInstance().GetVkDevice() != VK_NULL_HANDLE)
		{
			MyDevice::GetInstance().WaitIdle();
		}
		allocator->Discard(m_currentSignal);
		m_currentSignal = VK_NULL_HANDLE;
	}
}

void QueueSignalChain::PrepareForSubmit(
	VkSemaphore& outWaitSemaphore,
	VkSemaphore& outSignalSemaphore)
{
	CHECK_TRUE(m_preparedSignal == VK_NULL_HANDLE, "Queue signal chain is already prepared!");
	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");

	outWaitSemaphore = m_currentSignal;
	outSignalSemaphore = allocator->Allocate();
	m_preparedSignal = outSignalSemaphore;
}

void QueueSignalChain::CommitSubmit(VkSemaphore inSignalSemaphore)
{
	CHECK_TRUE(inSignalSemaphore != VK_NULL_HANDLE, "Invalid queue signal semaphore!");
	CHECK_TRUE(m_preparedSignal == inSignalSemaphore, "Queue signal chain commit does not match prepare!");
	m_currentSignal = inSignalSemaphore;
	m_preparedSignal = VK_NULL_HANDLE;
}

void QueueSignalChain::AbortSubmit()
{
	if (m_preparedSignal == VK_NULL_HANDLE)
	{
		return;
	}

	SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
	CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
	allocator->Free(m_preparedSignal);
	m_preparedSignal = VK_NULL_HANDLE;
}
