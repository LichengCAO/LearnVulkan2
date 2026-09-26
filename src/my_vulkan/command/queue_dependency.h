#pragma once

#include "common.h"
#include "submission_frontier.h"

class CommandQueueManager;
class RenderGraphInstance;

// A linear GPU dependency transferred between queue submissions.
//
// A chain starts with a signal-only submit, may be advanced by wait-and-signal
// submits, and must be terminated by a wait-only submit:
//   first submit  : signal S0
//   middle submit : wait S0, signal S1
//   final submit  : wait S1
//
// CommandQueueManager owns the underlying synchronization resources. This
// object only carries a pending dependency and its producer submission frontier.
class QueueDependency final
{
	friend class CommandQueueManager;
	friend class RenderGraphInstance;

private:
	struct PendingSignal final
	{
		SubmissionFrontier frontier;
	};

	bool m_hasPendingSignal = false;
	SubmissionFrontier m_frontier;

public:
	QueueDependency() = default;
	QueueDependency(const QueueDependency&) = delete;
	QueueDependency& operator=(const QueueDependency&) = delete;
	QueueDependency(QueueDependency&&) = delete;
	QueueDependency& operator=(QueueDependency&&) = delete;
	~QueueDependency() noexcept(false);

private:
	auto _HasPendingSignal() const->bool { return m_hasPendingSignal; }
	auto _Take()->PendingSignal;
	void _Store(const SubmissionFrontier& inFrontier);
};
