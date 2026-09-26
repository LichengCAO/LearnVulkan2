#pragma once

#include "common.h"

class CommandQueueManager;

// A reusable GPU-to-host completion object.
//
// Wait() or Poll() must consume the previous submission before this object is
// reused. Callbacks are one-shot and belong to a submission, not permanently
// to the fence.
class HostFence final
{
	friend class CommandQueueManager;

public:
	using Callback = std::function<void()>;

private:
	VkFence m_vkFence = VK_NULL_HANDLE;
	bool m_isInFlight = false;
	std::vector<Callback> m_callbacks;

public:
	HostFence();
	HostFence(const HostFence&) = delete;
	HostFence& operator=(const HostFence&) = delete;
	HostFence(HostFence&&) = delete;
	HostFence& operator=(HostFence&&) = delete;
	~HostFence();

	// Blocks until the current submission completes and runs its callbacks.
	void Wait();

	// Runs callbacks if the current submission has completed.
	// Returns true when there is no in-flight work or it was completed now.
	auto Poll()->bool;

	// Registers a one-shot callback for the next submission using this fence.
	// The previous submission must already have been consumed by Wait() or Poll().
	auto AddCallback(Callback inCallback)->HostFence&;

private:
	void _PrepareForSubmit();
	void _CommitSubmit();
	void _RunCallbacks();
};

/*
Example:

HostFence frameCompletion;
frameCompletion.AddCallback([]
{
    // Release CPU/GPU resources after the submission is complete.
});

CommandQueue::SubmitInfo submitInfo;
submitInfo
    .SetCommandBuffers({ vkCommandBuffer })
    .SetFence(frameCompletion);
queue.Submit(std::move(submitInfo));

// The application may wait only when it needs the result.
frameCompletion.Wait();
*/
