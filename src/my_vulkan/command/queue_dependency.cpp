#include "queue_dependency.h"

#include <utility>

QueueDependency::~QueueDependency() noexcept(false)
{
	CHECK_TRUE(m_semaphore == VK_NULL_HANDLE,
		"Queue dependency must be consumed before destruction!");
}

auto QueueDependency::_Take()->PendingSignal
{
	CHECK_TRUE(m_semaphore != VK_NULL_HANDLE, "Queue dependency has no pending handle!");
	PendingSignal result;
	result.semaphore = std::exchange(m_semaphore, VK_NULL_HANDLE);
	result.frontier = m_frontier;
	m_frontier = {};
	return result;
}

void QueueDependency::_Store(VkSemaphore inSemaphore, const SubmissionFrontier& inFrontier)
{
	CHECK_TRUE(m_semaphore == VK_NULL_HANDLE, "Queue dependency already has a pending handle!");
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Cannot store a null queue dependency handle!");
	m_semaphore = inSemaphore;
	m_frontier = inFrontier;
}
