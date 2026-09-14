#pragma once

#include "common.h"

class CommandQueue;

// A linear GPU-to-GPU dependency chain.
//
// A chain starts with a signal-only submit, may be advanced by wait-and-signal
// submits, and must be terminated by a wait-only submit:
//   first submit  : signal S0
//   middle submit : wait S0, signal S1
//   final submit  : wait S1
//
// The semaphore handles are deliberately hidden from callers. CommandQueue is
// the only class allowed to translate this object into Vulkan submit data.
class QueueSemaphore final
{
	friend class CommandQueue;

private:
	VkSemaphore m_currentSignal = VK_NULL_HANDLE;
	VkSemaphore m_preparedWait = VK_NULL_HANDLE;
	VkSemaphore m_preparedSignal = VK_NULL_HANDLE;
	bool m_submitPrepared = false;

public:
	QueueSemaphore() = default;
	QueueSemaphore(const QueueSemaphore&) = delete;
	QueueSemaphore& operator=(const QueueSemaphore&) = delete;
	QueueSemaphore(QueueSemaphore&&) = delete;
	QueueSemaphore& operator=(QueueSemaphore&&) = delete;
	~QueueSemaphore();

	// Returns whether the next use will have a semaphore to wait on.
	bool HasPendingSignal() const { return m_currentSignal != VK_NULL_HANDLE; }

private:
	void PrepareForSubmit(
		bool inUseWait,
		bool inUseSignal,
		VkSemaphore& outWaitSemaphore,
		VkSemaphore& outSignalSemaphore);
	auto CommitSubmit(
		bool inUseWait,
		VkSemaphore inWaitSemaphore,
		VkSemaphore inSignalSemaphore)->VkSemaphore;
	void AbortSubmit();
};

/*
Example:

QueueSignalChain uploadToGraphics;

// First submit: create and signal the first internal semaphore.
graphicsQueue.Enqueue(&uploadCommands, 1).Submit(
    CommandQueue::SubmitInfo{}.AddSignalQueueSignalChain(uploadToGraphics));

// Middle submit: consume the previous signal and produce the next one.
CommandQueue::SubmitInfo submitInfo;
submitInfo.AddWaitQueueSignalChain(
    uploadToGraphics,
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)
    .AddSignalQueueSignalChain(uploadToGraphics);
graphicsQueue.Enqueue(&drawCommands, 1).Submit(std::move(submitInfo));

// Final submit: consume the tail without producing another signal.
graphicsQueue.Enqueue(&finishCommands, 1).Submit(
    CommandQueue::SubmitInfo{}.AddWaitQueueSignalChain(uploadToGraphics));
*/
