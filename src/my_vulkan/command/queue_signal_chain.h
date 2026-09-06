#pragma once

#include "common.h"

class CommandQueue;

// A linear GPU-to-GPU dependency chain.
//
// Each successful use advances the chain:
//   first submit  : signal S0
//   second submit : wait S0, signal S1
//   third submit  : wait S1, signal S2
//
// The semaphore handles are deliberately hidden from callers. CommandQueue is
// the only class allowed to translate this object into Vulkan submit data.
class QueueSignalChain final
{
	friend class CommandQueue;

private:
	VkSemaphore m_currentSignal = VK_NULL_HANDLE;
	VkSemaphore m_preparedSignal = VK_NULL_HANDLE;

public:
	QueueSignalChain() = default;
	QueueSignalChain(const QueueSignalChain&) = delete;
	QueueSignalChain& operator=(const QueueSignalChain&) = delete;
	QueueSignalChain(QueueSignalChain&&) = delete;
	QueueSignalChain& operator=(QueueSignalChain&&) = delete;
	~QueueSignalChain();

	// Returns whether the next use will have a semaphore to wait on.
	bool HasPendingSignal() const { return m_currentSignal != VK_NULL_HANDLE; }

private:
	void PrepareForSubmit(VkSemaphore& outWaitSemaphore, VkSemaphore& outSignalSemaphore);
	void CommitSubmit(VkSemaphore inSignalSemaphore);
	void AbortSubmit();
};

/*
Example:

QueueSignalChain uploadToGraphics;

// First submit: signal an internal semaphore.
graphicsQueue.Enqueue(&uploadCommands, 1).Submit(
    CommandQueue::SubmitInfo{}.AddQueueSignalChain(uploadToGraphics));

// Later submit: wait for the previous signal at the first consuming stage,
// then produce the next signal in the same chain.
CommandQueue::SubmitInfo submitInfo;
submitInfo.AddQueueSignalChain(
    uploadToGraphics,
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
graphicsQueue.Enqueue(&drawCommands, 1).Submit(std::move(submitInfo));
*/
