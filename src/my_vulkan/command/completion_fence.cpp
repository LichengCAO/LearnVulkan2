#include "completion_fence.h"

#include <utility>

HostFence::~HostFence() noexcept(false)
{
	CHECK_TRUE(
		!m_isInFlight,
		"Host fence must be consumed through DeviceContext before destruction!");
}

auto HostFence::AddCallback(Callback inCallback)->HostFence&
{
	CHECK_TRUE(static_cast<bool>(inCallback), "Completion callback is empty!");
	CHECK_TRUE(
		!m_isInFlight,
		"Cannot add a callback while the previous submission is unconsumed!");
	m_callbacks.push_back(std::move(inCallback));
	return *this;
}

void HostFence::_PrepareForSubmit()
{
	CHECK_TRUE(
		!m_isInFlight,
		"Host fence is still bound to an unconsumed submission!");
}

auto HostFence::_TakeCallbacks()->std::vector<Callback>
{
	std::vector<Callback> callbacks = std::move(m_callbacks);
	m_callbacks.clear();
	return callbacks;
}

void HostFence::_RestoreCallbacks(std::vector<Callback> inCallbacks)
{
	CHECK_TRUE(m_callbacks.empty(), "Host fence already has callbacks!");
	m_callbacks = std::move(inCallbacks);
}

void HostFence::_CommitSubmit(
	size_t inQueueStateIndex,
	const SubmissionFrontier& inSubmissionFrontier)
{
	CHECK_TRUE(!m_isInFlight, "Host fence is already in flight!");
	CHECK_TRUE(
		inQueueStateIndex < SubmissionFrontier::MAX_QUEUE_COUNT,
		"Host fence queue state index is out of range!");
	m_queueStateIndex = inQueueStateIndex;
	m_submissionFrontier = inSubmissionFrontier;
	m_isInFlight = true;
}

void HostFence::_AbortSubmit(std::vector<Callback> inCallbacks)
{
	CHECK_TRUE(m_isInFlight, "Host fence submit was not committed!");
	m_isInFlight = false;
	m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	m_submissionFrontier = {};
	_RestoreCallbacks(std::move(inCallbacks));
}

void HostFence::_Complete()
{
	CHECK_TRUE(m_isInFlight, "Host fence has no in-flight submission!");
	m_isInFlight = false;
	m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	m_submissionFrontier = {};
}
