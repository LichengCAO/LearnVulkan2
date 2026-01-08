#pragma once
#include "common.h"
#include "frame_graph_resource.h"
#include "task_scheduler.h"

class FrameGraphNode;
class Image;
class Buffer;
class CommandBuffer;

class FrameGraph
{
private:
	std::vector<std::unique_ptr<FrameGraphNode>> m_nodes;
	std::vector<std::unique_ptr<Image>> m_imageResources;
	std::vector<std::unique_ptr<Buffer>> m_bufferResources;
	std::vector<std::unique_ptr<ISingleThreadTask>> m_hostExecution; // graph node recording tasks

	void _TopologicalSortFrameGraphNodes(std::vector<std::set<size_t>>& outOrderedNodeIndex);

	void _CreateRequiredDeviceRescource();

	void _GenerateExecutionTasksBasedOnSortResult(
		const std::vector<std::set<size_t>>& inOrderedNodeIndex);

	void _GenerateSynchronizeTaskBetween(
		const FrameGraphCompileContext& inPreContext,
		const FrameGraphCompileContext& inPostContext);

	struct ExecutionTask
	{
		TaskThreadType type;
		std::function<void()> task;
	};

public:
	class BufferResourceFetcher
	{

	};

	class ImageResourceFetcher
	{

	};

	BufferResourceHandle FetchBufferResource(const BufferResourceFetcher* pFetcher);
	
	ImageResourceHandle FetchImageResource(const ImageResourceFetcher* pFetcher);
	
	void ReturnBufferResource(BufferResourceHandle inBufferHandle);
	
	void ReturnImageResource(ImageResourceHandle inImageHandle);

	CommandBuffer* GetCommandBuffer();

	ImageResourceHandle RegisterExternalImageResource(Image* inImagePtr, const ImageResourceState& inInitialState);
	
	BufferResourceHandle RegisterExternalBufferResource(Buffer* inBufferPtr, const BufferResourceState& inInitialState);

	void AddFrameGraphNode();

	void SetUp();

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