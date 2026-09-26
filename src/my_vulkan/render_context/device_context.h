#include "common.h"
#include "common_enums.h"
#include "command/command_buffer.h"
#include "command/command_queue.h"
#include "render_context/frame_context.h"

struct QueueSubmitInfo
{
	std::vector<QueueDependency*> queueSignals;
	HostFence* hostFence = nullptr;
};

// Thread-affine. Public methods must be called from the owning thread.
class DeviceContext
{
private:
	struct QueueRecordingState final
	{
		std::vector<FrameContext::RecordingTicket> pendingTickets;
	};

	static auto _GetQueueIndex(QueueFamilyType inQueue) -> size_t;
	auto _GetCurrentFrameContext() -> FrameContext&;
	std::unique_ptr<CommandQueueManager> m_uptrCommandQueueManager;
	std::vector<std::unique_ptr<FrameContext>> m_frameContexts;
	size_t m_currentFrameIndex = SIZE_MAX;
	bool m_frameActive = false;
	std::array<QueueRecordingState, SubmissionFrontier::MAX_QUEUE_COUNT> m_queueRecordingStates;

public:
	explicit DeviceContext(size_t inFrameCount);
	DeviceContext(const DeviceContext&) = delete;
	DeviceContext& operator=(const DeviceContext&) = delete;
	~DeviceContext() noexcept(false);

	void StartFrame();

	void EndFrame();

	// Dispatches recording and returns immediately. Requests stay ordered per queue.
	void CommitCommandsToQueue(QueueFamilyType inQueue, std::vector<CommandBuffer> inBuffers);

	// Waits for and consumes every recording request issued to this queue since
	// its previous submit, then submits their command buffers in commit order.
	void SubmitQueue(QueueFamilyType inQueue, const QueueSubmitInfo& inSubmitInfo);

	// Records, submits, and waits until the specified queue completes the commands.
	// This operation is independent of the active frame lifecycle.
	void ExecuteCommandsAndWait(QueueFamilyType inQueue, std::vector<CommandBuffer> inBuffers);

	// Consumes the fence's current logical submission. A fence whose Vulkan
	// resource was already reclaimed by a later wait completes immediately.
	void Wait(HostFence& inFence);

	// Returns false only while the fence's current submission is incomplete.
	auto Poll(HostFence& inFence)->bool;

	// Runs once after all queues used by the current frame have completed.
	void AddCurrentFrameCompletionCallback(HostFence::Callback inCallback);
};

