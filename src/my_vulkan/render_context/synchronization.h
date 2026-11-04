#pragma once

#include "private/submission_frontier.h"

class RenderGraphInstance;

// A linear GPU dependency transferred between queue submissions.
//
// A chain starts with a signal-only submit, may be advanced by wait-and-signal
// submits, and must be terminated by a wait-only submit:
//   first submit  : signal S0
//   middle submit : wait S0, signal S1
//   final submit  : wait S1
//
// SubmissionManager owns the underlying synchronization resources. This
// object only carries a pending dependency and its producer submission frontier.
class QueueDependency final
{
	friend class SubmissionManager;
	friend class RenderGraphInstance;

private:
	bool m_hasPendingSignal = false;
	SubmissionFrontier m_frontier;

public:
	QueueDependency() = default;
	QueueDependency(const QueueDependency&) = delete;
	QueueDependency& operator=(const QueueDependency&) = delete;
	QueueDependency(QueueDependency&&) = delete;
	QueueDependency& operator=(QueueDependency&&) = delete;
	~QueueDependency() noexcept;

private:
	auto _HasPendingSignal() const->bool { return m_hasPendingSignal; }
	auto _GetPendingFrontier() const->const SubmissionFrontier&;
	void _CommitWait() noexcept;
	void _CommitSignal(const SubmissionFrontier& inFrontier) noexcept;
};

// A reusable GPU-to-host completion object.
//
// DeviceContext::Wait() or Poll() must consume the previous submission before
// this object is reused. Destroying an in-flight object is allowed because the
// manager owns its submitted callbacks and synchronization resources. Callbacks
// are one-shot and belong to a submission, not permanently to the fence.
class HostFence final
{
	friend class SubmissionManager;

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
	~HostFence() = default;

	// Registers a one-shot callback for the next submission using this fence.
	// The previous submission must already have been consumed through DeviceContext.
	auto AddCallback(Callback inCallback)->HostFence&;

private:
	void _PrepareForSubmit();
	auto _CommitSubmit(
		size_t inQueueStateIndex,
		const SubmissionFrontier& inSubmissionFrontier) noexcept->std::vector<Callback>;
	void _Complete();
};
