#include "common.h"
#include "common_enums.h"
#include "command/command_buffer.h"
#include "command/queue_dependency.h"
#include "render_context/frame_context.h"

struct QueueSubmitInfo
{
	std::vector<QueueDependency*> queueSignals;
	HostFence* hostFence = nullptr;
};

class DeviceContext
{
private:
	struct QueueRecordingState final
	{
		std::vector<FrameContext::RecordingTicket> pendingTickets;
	};

	static auto _GetQueueIndex(QueueFamilyType inQueue) -> size_t;
	auto _GetCurrentFrameContext() -> FrameContext&;
	std::vector<std::unique_ptr<FrameContext>> m_frameContexts;
	size_t m_currentFrameIndex = SIZE_MAX;
	bool m_frameActive = false;
	std::array<QueueRecordingState, SubmissionFrontier::MAX_QUEUE_COUNT> m_queueRecordingStates;

public:
	explicit DeviceContext(size_t inFrameCount);

	void StartFrame();

	void EndFrame();

	// Dispatches recording and returns immediately. Requests stay ordered per queue.
	void CommitCommandsToQueue(QueueFamilyType inQueue, std::vector<CommandBuffer> inBuffers);

	// Waits for and consumes every recording request issued to this queue since
	// its previous submit, then submits their command buffers in commit order.
	void SubmitQueue(QueueFamilyType inQueue, const QueueSubmitInfo& inSubmitInfo);

	// Records, submits, and waits until the specified queue completes the commands.
	// This operation is independent of the active frame lifecycle.
	void ExecuteCommandsAndWait(
		QueueFamilyType inQueue,
		std::vector<CommandBuffer> inBuffers);

	// Runs once after all queues used by the current frame have completed.
	void AddCurrentFrameCompletionCallback(HostFence::Callback inCallback);
};

