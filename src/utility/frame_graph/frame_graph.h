#pragma once
#include "common.h"
#include "frame_graph_resource.h"
#include "task_scheduler.h"

class FrameGraphNode;
class Image;
class Buffer;
class CommandBuffer;

// Blueprint that helps to construct a frame graph by
// automatically connect nodes based on names of input and outputs
class FrameGraphBlueprint
{
private:
	std::unordered_map<std::string, FrameGraphNodeOutput*> m_availableOutputs;
	
private:
	FrameGraphNodeOutput* _FindAvailableOutputByName(const std::string& inName);

public:
	FrameGraphBlueprint& AddFrameGraphNode(std::unique_ptr<FrameGraphNode> inFrameGraphNode);
};

// Context that helps us to decide whether to add barrier between execution of frame graph nodes
class FrameGraphCompileContext
{
private:
	std::vector<FrameGraphImageResourceState> m_imageResourceStates;
	std::vector<FrameGraphImageResourceState> m_imageResourceRequireStates;
	std::vector<FrameGraphBufferResourceState> m_bufferResourceStates;
	std::vector<FrameGraphBufferResourceState> m_bufferResourceRequireStates;
	std::unordered_map<uint32_t, size_t> m_imageResourceHandleToIndex;
	std::unordered_map<uint32_t, size_t> m_bufferResourceHandleToIndex;

private:
	auto _GetImageResourceState(const FrameGraphImageResourceHandle& inHandle) -> std::vector<FrameGraphImageSubResourceState>&;
	
	auto _GetBufferResourceState(const FrameGraphBufferResourceHandle& inHandle) -> std::vector<FrameGraphBufferSubResourceState>&;

public:
	// Resource layout, queue ownership may not meet requirement,
	// so we need add barrier, wait semaphore based on these requirements
	void RequireSubResourceStateBeforePass(
		const FrameGraphImageResourceHandle& inHandle, 
		const FrameGraphImageSubResourceState& inState);
	void RequireSubResourceStateBeforePass(
		const FrameGraphBufferResourceHandle& inHandle, 
		const FrameGraphBufferSubResourceState& inState);

	// After pass, resource state changes, we store it for next pass
	void MergeSubResourceStateAfterPass(
		const FrameGraphImageResourceHandle& inHandle, 
		const FrameGraphImageSubResourceState& inState);
	void MergeSubResourceStateAfterPass(
		const FrameGraphBufferResourceHandle& inHandle, 
		const FrameGraphBufferSubResourceState& inState);

	// According to spec, queue ownership transform requires both release operation and acquire operation,
	// see: https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-queue-transfers
	// this requires memory barriers be on both queue.
	// Therefore, we'd better to check every incoming resource state
	// transform ahead and build necessary release operation memory barrier
	// and semaphore in current batch
	void PresageSubResourceStateNextPass(
		const FrameGraphImageResourceHandle& inHandle, 
		const FrameGraphImageSubResourceState& inNewState);
	void PresageSubResourceStateNextPass(
		const FrameGraphBufferResourceHandle& inHandle, 
		const FrameGraphBufferSubResourceState& inNewState);
	
	void AddSubResourceReference(
		const FrameGraphImageResourceHandle& inHandle, 
		const VkImageSubresourceRange& inRange);	
	void AddSubResourceReference(
		const FrameGraphBufferResourceHandle& inHandle, 
		VkDeviceSize inOffset, 
		VkDeviceSize inSize);
	
	void ReleaseSubResourceReference(
		const FrameGraphImageResourceHandle& inHandle,
		const VkImageSubresourceRange& inRange);
	void ReleaseSubResourceReference(
		const FrameGraphBufferResourceHandle& inHandle, 
		VkDeviceSize inOffset, 
		VkDeviceSize inSize);
};

class FrameGraph
{
private:
	std::vector<std::unique_ptr<FrameGraphNode>> m_nodes;
	std::vector<std::unique_ptr<Image>> m_imageResources;
	std::vector<std::unique_ptr<Buffer>> m_bufferResources;
	std::vector<std::unique_ptr<ISingleThreadTask>> m_hostExecution; // graph node recording tasks

	void _TopologicalSortFrameGraphNodes(std::vector<std::set<size_t>>& outOrderedNodeIndex);

	void _CreateRequiredDeviceRescource(const std::set<size_t>& inNodeBatch);

	// Handles barrier insertion for the command buffers in each queue,
	// here, the current thread will hold ownership of all command buffer
	void _GenerateFrameGraphNodeBatchPrologue(const std::set<size_t>& inNodeBatch);

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
	void ReturnBufferResource(FrameGraphBufferResourceHandle inBufferHandle);
	
	void ReturnImageResource(FrameGraphImageResourceHandle inImageHandle);

	CommandBuffer* GetCommandBuffer();

	FrameGraphImageResourceHandle RegisterExternalImageResource(Image* inImagePtr, const FrameGraphImageSubResourceState& inInitialState);
	
	FrameGraphBufferResourceHandle RegisterExternalBufferResource(Buffer* inBufferPtr, const FrameGraphBufferSubResourceState& inInitialState);

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
};