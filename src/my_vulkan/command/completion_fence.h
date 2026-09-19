#pragma once

#include "common.h"
#include "submission_frontier.h"

class CommandQueue;
class CommandQueueManager;
class RenderGraphInstance;

// A reusable GPU-to-host completion object.
//
// Reusing the object waits for the previous submission, runs its callbacks,
// resets the VkFence, and binds the fence to the next submission. Callbacks are
// one-shot and therefore belong to a submission, not permanently to the fence.
class HostFence final
{
	friend class CommandQueue;
	friend class CommandQueueManager;
	friend class RenderGraphInstance;

public:
	using Callback = std::function<void()>;

private:
	VkFence m_vkFence = VK_NULL_HANDLE;
	bool m_hasSubmittedWork = false;
	bool m_isInFlight = false;
	CommandQueueManager* m_commandQueueManager = nullptr;
	const void* m_completionObserver = nullptr;
	SubmissionFrontier m_submissionFrontier;
	std::vector<Callback> m_pendingCallbacks;
	std::vector<Callback> m_activeCallbacks;

public:
	HostFence();
	HostFence(const HostFence&) = delete;
	HostFence& operator=(const HostFence&) = delete;
	HostFence(HostFence&&) = delete;
	HostFence& operator=(HostFence&&) = delete;
	~HostFence();

	auto HasSubmittedWork() const->bool { return m_hasSubmittedWork; }
	auto IsInFlight() const->bool { return m_isInFlight; }
	auto IsComplete() const->bool;

	// Blocks until the current submission completes and runs its callbacks.
	void Wait();

	// Runs callbacks if the current submission has completed.
	// Returns true when there is no in-flight work or it was completed now.
	auto Poll()->bool;

	// Registers a one-shot callback for the next submission using this fence.
	auto AddCallback(Callback inCallback)->HostFence&;

private:
	void _AcquireCompletionObserver(const void* inObserver);
	void _ReleaseCompletionObserver(const void* inObserver);
	void _PrepareForSubmit(const void* inCompletionObserver);
	void _CommitSubmit(CommandQueueManager& inCommandQueueManager, const SubmissionFrontier& inSubmissionFrontier);
	void _ForceComplete();
	void _RunCallbacks();
};

/*
Example:

HostFence frameCompletion;
frameCompletion.AddCallback([]
{
    // Release CPU/GPU resources after the submission is complete.
});

queue.Enqueue(&commands, 1).Submit(
    CommandQueue::SubmitInfo{}.SetFence(frameCompletion));

// The application may wait only when it needs the result.
frameCompletion.Wait();
*/
