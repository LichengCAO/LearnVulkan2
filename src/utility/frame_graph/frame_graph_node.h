#pragma once
#include "common.h"
#include "frame_graph_resource.h"
#include <variant>

class FrameGraph;
class FrameGraphNode;

class FrameGraphNodeInput
{
private:
	FrameGraphNode* m_owner;
	FrameGraphNodeOutput* m_connectedOutput;

public:
	auto GetConnectedOutput() const -> FrameGraphNodeOutput*;
	auto GetResourceHandle() const -> const std::variant<BufferResourceHandle, ImageResourceHandle>&;
	auto GetRequiredResourceState() const -> const std::variant<BufferResourceState, ImageResourceState>&;
	auto GetOwner() const -> const FrameGraphNode*;

	friend class FrameGraphNodeOutput;
};

class FrameGraphNodeOutput
{
private:
	FrameGraphNode* m_owner;
	std::vector<FrameGraphNodeInput*> m_connectedInputs;
	std::variant<BufferResourceHandle, ImageResourceHandle> m_handle;

public:
	auto GetConnectedInputs() const -> const std::vector<FrameGraphNodeInput*>&;
	auto GetResourceHandle() const -> const std::variant<BufferResourceHandle, ImageResourceHandle>&;
	auto GetProcessedResourceState() const -> const std::variant<BufferResourceState, ImageResourceState>&;
	auto GetOwner() const -> const FrameGraphNode*;
	void ConnectToInput(FrameGraphNodeInput* inoutFrameGraphInputPtr);
};

class FrameGraphNode
{
private:
	FrameGraph* m_graph;
	std::vector<std::unique_ptr<FrameGraphNodeInput>> m_inputs;
	std::vector<std::unique_ptr<FrameGraphNodeOutput>> m_outputs;
	std::set<FrameGraphNode*> m_extraDependencies;

public:
	void Init();

	// Normally, dependency based on node input and output will suffice,
	// however, there might be cases where this node represent a secondary buffer
	// recording behavior where we actually record a command buffer for other
	// frame graph node to use
	void AddExecutionDependency(FrameGraphNode* inNodeThisDependsOn);

	// Get frame graph nodes this nodes depends on, I consider 3 cases: 
	// 1. if they in the same queue, and record the same command buffer, 
	//    pre-nodes' commands appear before this node's, and there will be barriers between them
	// 2. if they are in different queues (which implies different command buffers), 
	//    pre-nodes' command buffer will be submit first, and there will be 
	//    semaphore added
	// 3. if they are in the same queue, but pre-nodes record a secondary buffer
	void GetPreGraphNodes(std::set<FrameGraphNode*>& outFrameGraphNodes) const;

	//========================================================================
	// Process in Compile()
	//------------------------------------------------------------------------

	// Since frame graph handles the resource management, we just ask it to
	// give us resource if it is not external texture
	void RequireGraphInternalResources();

	// This graph nodes may need resource to be in certain state before
	// the resource can safely run through recorded command, e.g. image
	// layout transform
	void MergeInputResourceState(
		const FrameGraphCompileContext& inPrevContext, 
		FrameGraphCompileContext& inoutRequiredContext) const;

	// Frame graph resource will change state after execution of device commands
	// this node records, therefore we need to update it, e.g. after render pass
	// image may transfer to color attachment layout
	void MergeOutputResourceState(FrameGraphCompileContext& inoutResultContext) const;

	// After command execution, 
	// some resources may no longer be useful, we can
	// return it to graph
	void ReleaseGraphInternalResources();

	//========================================================================

	// Do things in a frame graph execution, i.e. record command buffer
	void Execute();

	void Uninit();
};