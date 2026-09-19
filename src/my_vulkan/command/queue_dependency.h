#pragma once

#include "common.h"
#include "submission_frontier.h"

class CommandQueue;
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
	friend class CommandQueue;
	friend class CommandQueueManager;
	friend class RenderGraphInstance;

private:
	struct PendingSignal final
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		SubmissionFrontier frontier;
	};

	VkSemaphore m_semaphore = VK_NULL_HANDLE;
	SubmissionFrontier m_frontier;

public:
	QueueDependency() = default;
	QueueDependency(const QueueDependency&) = delete;
	QueueDependency& operator=(const QueueDependency&) = delete;
	QueueDependency(QueueDependency&&) = delete;
	QueueDependency& operator=(QueueDependency&&) = delete;
	~QueueDependency() noexcept(false);

private:
	auto _HasSemaphore() const->bool { return m_semaphore != VK_NULL_HANDLE; }
	auto _Take()->PendingSignal;
	void _Store(VkSemaphore inSemaphore, const SubmissionFrontier& inFrontier);
};

/*
Example:

QueueDependency uploadToGraphics;

// First submit: create and signal the first internal semaphore.
graphicsQueue.Enqueue(&uploadCommands, 1).Submit(
    CommandQueue::SubmitInfo{}.AddSignalQueueDependency(uploadToGraphics));

// Middle submit: consume the previous signal and produce the next one.
CommandQueue::SubmitInfo submitInfo;
submitInfo.AddWaitQueueDependency(
    uploadToGraphics,
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)
    .AddSignalQueueDependency(uploadToGraphics);
graphicsQueue.Enqueue(&drawCommands, 1).Submit(std::move(submitInfo));

// Final submit: consume the tail without producing another signal.
graphicsQueue.Enqueue(&finishCommands, 1).Submit(
    CommandQueue::SubmitInfo{}.AddWaitQueueDependency(uploadToGraphics));
*/
