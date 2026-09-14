#pragma once

#include "common.h"

class CommandQueue;

// A reusable GPU-to-host completion object.
//
// Reusing the object waits for the previous submission, runs its callbacks,
// resets the VkFence, and binds the fence to the next submission. Callbacks are
// one-shot and therefore belong to a submission, not permanently to the fence.
class HostFence final
{
	friend class CommandQueue;

public:
	using Callback = std::function<void()>;

private:
	VkFence m_vkFence = VK_NULL_HANDLE;
	bool m_hasSubmittedWork = false;
	bool m_isInFlight = false;
	std::vector<Callback> m_pendingCallbacks;
	std::vector<Callback> m_activeCallbacks;

public:
	HostFence();
	HostFence(const HostFence&) = delete;
	HostFence& operator=(const HostFence&) = delete;
	HostFence(HostFence&&) = delete;
	HostFence& operator=(HostFence&&) = delete;
	~HostFence();

	bool HasSubmittedWork() const { return m_hasSubmittedWork; }
	bool IsInFlight() const { return m_isInFlight; }
	bool IsComplete() const;

	// Blocks until the current submission completes and runs its callbacks.
	void Wait();

	// Runs callbacks if the current submission has completed.
	// Returns true when there is no in-flight work or it was completed now.
	bool Poll();

	// Registers a one-shot callback for the next submission using this fence.
	HostFence& AddCallback(Callback inCallback);

private:
	void PrepareForSubmit();
	void CommitSubmit();
	void ForceComplete();
	void RunCallbacks();
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
