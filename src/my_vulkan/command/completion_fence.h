#pragma once

#include "common.h"
#include "submission_frontier.h"

class CommandQueueManager;

// A reusable GPU-to-host completion object.
//
// DeviceContext::Wait() or Poll() must consume the previous submission before
// this object is reused. Callbacks are one-shot and belong to a submission,
// not permanently to the fence. The Vulkan fence is owned by
// CommandQueueManager; this object only records its logical submission state.
class HostFence final
{
	friend class CommandQueueManager;

public:
	using Callback = std::function<void()>;

private:
	bool m_isInFlight = false;
	size_t m_queueStateIndex = SubmissionFrontier::MAX_QUEUE_COUNT;
	SubmissionFrontier m_submissionFrontier;
	std::vector<Callback> m_callbacks;

public:
	HostFence() = default;
	HostFence(const HostFence&) = delete;
	HostFence& operator=(const HostFence&) = delete;
	HostFence(HostFence&&) = delete;
	HostFence& operator=(HostFence&&) = delete;
	~HostFence() noexcept(false);

	// Registers a one-shot callback for the next submission using this fence.
	// The previous submission must already have been consumed through DeviceContext.
	auto AddCallback(Callback inCallback)->HostFence&;

private:
	void _PrepareForSubmit();
	auto _TakeCallbacks()->std::vector<Callback>;
	void _RestoreCallbacks(std::vector<Callback> inCallbacks);
	void _CommitSubmit(
		size_t inQueueStateIndex,
		const SubmissionFrontier& inSubmissionFrontier);
	void _AbortSubmit(std::vector<Callback> inCallbacks);
	void _Complete();
};
