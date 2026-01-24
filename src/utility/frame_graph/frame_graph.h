#pragma once
#include "common.h"
#include "frame_graph_resource.h"
#include "frame_graph_compile_context.h"
#include "task_scheduler.h"

class FrameGraphNode;
class Image;
class Buffer;
class CommandBuffer;
class FrameGraphBuilder;

class FrameGraph
{
private:
	std::vector<std::unique_ptr<FrameGraphNode>> m_nodes;
	std::vector<std::unique_ptr<Image>> m_internalImages;
	std::vector<std::unique_ptr<Buffer>> m_internalBuffers;
	std::vector<Image*> m_externalImages;
	std::vector<Buffer*> m_externalBuffers;
	std::vector<std::unique_ptr<ISingleThreadTask>> m_hostExecution; // graph node recording tasks
	FrameGraphCompileContext m_currentContext;

	std::unordered_map<uint32_t, Image*> m_handleToImage;
	std::unordered_map<uint32_t, Buffer*> m_handleToBuffer;

	void _TopologicalSortFrameGraphNodes(std::vector<std::set<FrameGraphNode*>>& outOrderedNodeIndex);

	void _CreateRequiredDeviceRescource(const std::set<FrameGraphNode*>& inNodeBatch);

	// Handles barrier insertion for the command buffers in each queue,
	// here, the current thread will hold ownership of all command buffer
	void _GenerateFrameGraphNodeBatchPrologue(const std::set<FrameGraphNode*>& inNodeBatch);

	// Once barrier is recorded, the current thread can release the ownership
	// of command buffers, and different threads can record command buffers
	// separately
	void _GenerateFrameGraphNodeBatchExecutionTasks(const std::set<size_t>& inNodeBatch);

	// Wait till all command buffer recording done on different threads, and then:
	// If we have cross queue data, we need to submit it as soon as possible,
	// and we also need to provide a new command buffer to record new command for
	// the queue that submit command buffer
	void _GenerateFrameGraphNodeBatchEpilogue(const std::set<size_t>& inNodeBatch);
	
	struct ExecutionTask
	{
		FrameGraphQueueType type;
		std::function<void()> task;
	};

public:	
	void ReturnBufferResource(FrameGraphBufferHandle inBufferHandle);
	
	void ReturnImageResource(FrameGraphImageHandle inImageHandle);

	CommandBuffer* GetCommandBuffer();

	Image* GetImageResource(const FrameGraphImageHandle& inHandle);

	Buffer* GetBufferResource(const FrameGraphBufferHandle& inHandle);

	void SetUp(FrameGraphBlueprint* inBlueprint);

	// Decide static process based in input, only do once,
	// unless window resized, or other dramatic change
	// This should include: 
	// 1. order frame graph nodes
	// 2. synchronize them(barrier, semaphore), maybe layout transfer
	// 3. allocate resource for each nodes(include memory aliasing)
	void Compile();

	void StartFrame();

	void Execute();

	void EndFrame();

	friend class FrameGraphBuilder;
};