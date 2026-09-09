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

	if (m_submitPrepared)
	{
		if (m_preparedSignal != VK_NULL_HANDLE)
		{
			allocator->Free(m_preparedSignal);
		}
		m_preparedWait = VK_NULL_HANDLE;
		m_preparedSignal = VK_NULL_HANDLE;
		m_submitPrepared = false;
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
	bool inUseWait,
	bool inUseSignal,
	VkSemaphore& outWaitSemaphore,
	VkSemaphore& outSignalSemaphore)
{
	CHECK_TRUE(!m_submitPrepared, "Queue signal chain is already prepared!");
	CHECK_TRUE(inUseWait || inUseSignal, "Queue signal chain submit has no wait or signal operation!");

	const bool hasPendingSignal = m_currentSignal != VK_NULL_HANDLE;
	if (hasPendingSignal)
	{
		CHECK_TRUE(inUseWait, "A pending queue signal chain must be waited before it can be signaled again!");
	}
	else
	{
		CHECK_TRUE(!inUseWait && inUseSignal, "An empty queue signal chain can only be signaled!");
	}

	outWaitSemaphore = inUseWait ? m_currentSignal : VK_NULL_HANDLE;
	outSignalSemaphore = VK_NULL_HANDLE;
	if (inUseSignal)
	{
		SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
		CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
		outSignalSemaphore = allocator->Allocate();
	}

	m_preparedWait = outWaitSemaphore;
	m_preparedSignal = outSignalSemaphore;
	m_submitPrepared = true;
}

auto QueueSignalChain::CommitSubmit(
	bool inUseWait,
	VkSemaphore inWaitSemaphore,
	VkSemaphore inSignalSemaphore)->VkSemaphore
{
	CHECK_TRUE(m_submitPrepared, "Queue signal chain has not been prepared!");
	CHECK_TRUE((m_preparedWait != VK_NULL_HANDLE) == inUseWait, "Queue signal chain wait operation does not match prepare!");
	CHECK_TRUE(m_preparedWait == inWaitSemaphore, "Queue signal chain wait semaphore does not match prepare!");
	CHECK_TRUE(m_preparedSignal == inSignalSemaphore, "Queue signal chain commit does not match prepare!");

	const VkSemaphore consumedSemaphore = inUseWait ? m_currentSignal : VK_NULL_HANDLE;
	m_currentSignal = inSignalSemaphore;
	m_preparedWait = VK_NULL_HANDLE;
	m_preparedSignal = VK_NULL_HANDLE;
	m_submitPrepared = false;
	return consumedSemaphore;
}

void QueueSignalChain::AbortSubmit()
{
	if (!m_submitPrepared)
	{
		return;
	}

	if (m_preparedSignal != VK_NULL_HANDLE)
	{
		SemaphoreAllocator* allocator = MyDevice::GetInstance().GetSemaphoreAllocator();
		CHECK_TRUE(allocator != nullptr, "Semaphore allocator is not created!");
		allocator->Free(m_preparedSignal);
	}
	m_preparedWait = VK_NULL_HANDLE;
	m_preparedSignal = VK_NULL_HANDLE;
	m_submitPrepared = false;
}
