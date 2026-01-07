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
	FrameGraphNodeOutput* GetConnectedOutput() const;
	const FrameGraphNode* GetOwner() const;

	friend class FrameGraphNodeOutput;
};

class FrameGraphNodeOutput
{
private:
	FrameGraphNode* m_owner;
	std::vector<FrameGraphNodeInput*> m_connectedInputs;
	std::variant<BufferResourceHandle, ImageResourceHandle> m_handle;

public:
	void ConnectToInput(FrameGraphNodeInput* inoutFrameGraphInputPtr);
	const std::vector<FrameGraphNodeInput*>& GetConnectedInputs() const;
	const std::variant<BufferResourceHandle, ImageResourceHandle>& GetResourceHandle() const;
	const FrameGraphNode* GetOwner() const;
};

class FrameGraphNode
{
private:
	FrameGraph* m_graph;
	std::vector<std::unique_ptr<FrameGraphNodeInput>> m_inputs;
	std::vector<std::unique_ptr<FrameGraphNodeOutput>> m_outputs;

public:
	void Init();

	// Do things in a frame graph execution, i.e. record command buffer
	void Execute();

	// Normally, dependency based on node input and output will suffice,
	// however, there might be cases where this node represent a secondary buffer
	// recording behavior where we actually record a command buffer for other
	// frame graph node to use
	void AddExecutionDependency(const FrameGraphNode* inNodeDependsOn);

	void GetPreGraphNodes(std::set<FrameGraphNode*>& outFrameGraphNodes) const;

	void GetPostGraphNodes(std::set<FrameGraphNode*>& outFrameGraphNodes) const;

	void GetRequiredInputState() const;

	void GetOutputState() const;

	void Uninit();
};