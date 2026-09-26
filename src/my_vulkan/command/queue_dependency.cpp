#include "queue_dependency.h"

QueueDependency::~QueueDependency() noexcept(false)
{
	CHECK_TRUE(!m_hasPendingSignal,
		"Queue dependency must be consumed before destruction!");
}

auto QueueDependency::_Take()->PendingSignal
{
	CHECK_TRUE(m_hasPendingSignal, "Queue dependency has no pending signal!");
	PendingSignal result;
	result.frontier = m_frontier;
	m_hasPendingSignal = false;
	m_frontier = {};
	return result;
}

void QueueDependency::_Store(const SubmissionFrontier& inFrontier)
{
	CHECK_TRUE(!m_hasPendingSignal, "Queue dependency already has a pending signal!");
	m_hasPendingSignal = true;
	m_frontier = inFrontier;
}
