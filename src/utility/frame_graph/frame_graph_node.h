#pragma once
#include "common.h"
#include "frame_graph_resource.h"
#include <variant>

class FrameGraph;
class FrameGraphNode;
class FrameGraphNodeInput;
class FrameGraphNodeOutput;

#define FRAME_GRAPH_RESOURCE_HANDLE std::variant<FrameGraphBufferResourceHandle, FrameGraphImageResourceHandle>
#define FRAME_GRAPH_RESOURCE_STATE std::variant<FrameGraphBufferSubResourceState, FrameGraphImageSubResourceState>

#pragma region Initializer

class IFrameGraphNodeInputInitializer
{
public:
	virtual void InitializeInput(FrameGraphNodeInput* inoutNodeInput) const = 0;
};

class IFrameGraphNodeOutputInitializer
{
public:
	virtual void InitializeOutput(FrameGraphNodeOutput* inoutNodeOutput) const = 0;
};

class IFrameGraphNodeInoutInitializer
{
public:
	virtual void InitializeInout(
		FrameGraphNodeInput* inoutNodeInput, 
		FrameGraphNodeOutput* inoutNodeOutput) const = 0;
};

class FrameGraphNodeInputInitializer : public IFrameGraphNodeInputInitializer
{
private:
	std::string m_name;
	VkAccessFlags m_access;
	VkPipelineStageFlags m_stages;
	std::optional<VkImageLayout> m_layout;
	std::optional<uint32_t> m_queueFamilyIndex;

public:
	FrameGraphNodeInputInitializer& Reset();
	FrameGraphNodeInputInitializer& SetName(const std::string& inName);
	FrameGraphNodeInputInitializer& SetAccessState(VkAccessFlags inAccessFlags);
	FrameGraphNodeInputInitializer& SetVisiblePipelineStage(VkPipelineStageFlags inPipelineStageFlags);
	FrameGraphNodeInputInitializer& SetImageLayout(VkImageLayout inImageLayout);
	FrameGraphNodeInputInitializer& CustomizeQueueFamilyIndex(uint32_t inQueueFamilyIndex);
	virtual void InitializeInput(FrameGraphNodeInput* inoutNodeInput) const override;
};

class FrameGraphNodeOutputInitializer : public IFrameGraphNodeOutputInitializer
{
private:
	std::string m_name;
	VkAccessFlags m_access;
	VkPipelineStageFlags m_stages;
	std::optional<VkImageLayout> m_layout;
	std::optional<uint32_t> m_queueFamilyIndex;
	FRAME_GRAPH_RESOURCE_HANDLE m_resourceHandle;

public:
	FrameGraphNodeOutputInitializer& Reset();
	FrameGraphNodeOutputInitializer& SetName(const std::string& inName);
	FrameGraphNodeOutputInitializer& SetAccessState(VkAccessFlags inAccessFlags);
	FrameGraphNodeOutputInitializer& SetAvailablePipelineStage(VkPipelineStageFlags inPipelineStageFlags);
	FrameGraphNodeOutputInitializer& SetResourceHandle(FRAME_GRAPH_RESOURCE_HANDLE inResourceHandle);
	FrameGraphNodeOutputInitializer& SetImageLayout(VkImageLayout inImageLayout);
	FrameGraphNodeOutputInitializer& CustomizeQueueFamilyIndex(uint32_t inQueueFamilyIndex);
	virtual void InitializeOutput(FrameGraphNodeOutput* inoutNodeOutput) const override;
};

class FrameGraphNodeInoutInitializer : public IFrameGraphNodeInoutInitializer
{
private:
	std::string m_name;
	VkAccessFlags m_inAccess;
	VkAccessFlags m_outAccess;
	VkPipelineStageFlags m_inStages;
	VkPipelineStageFlags m_outStages;
	std::optional<VkImageLayout> m_initialLayout;
	std::optional<VkImageLayout> m_finalLayout;
	std::optional<uint32_t> m_initialQueueFamilyIndex;
	std::optional<uint32_t> m_finalQueueFamilyIndex;

public:
	FrameGraphNodeInoutInitializer& Reset();
	FrameGraphNodeInoutInitializer& SetName(const std::string& inName);
	FrameGraphNodeInoutInitializer& SetInputAccessState(const VkAccessFlags inAccessFlags);
	FrameGraphNodeInoutInitializer& SetInputPipelineStage(const VkPipelineStageFlags inPipelineStageFlags);
	FrameGraphNodeInoutInitializer& SetOutputAccessState(const VkAccessFlags inAccessFlags);
	FrameGraphNodeInoutInitializer& SetOutputPipelineStage(const VkPipelineStageFlags inPipelineStageFlags);
	FrameGraphNodeInoutInitializer& SetImageLayouts(const VkImageLayout inInitialImageLayout, const VkImageLayout inFinalImageLayout);
	FrameGraphNodeInoutInitializer& CustomizeQueueFamilyIndices(uint32_t inInitialQueueFamilyIndex, uint32_t inFinalQueueFamilyIndex);
	virtual void InitializeInout(FrameGraphNodeInput* inoutNodeInput, FrameGraphNodeOutput* inoutNodeOutput) const override;
};

class FrameGraphNodeInitializer
{
private:
	std::vector<std::unique_ptr<FrameGraphNodeInput>> m_tmpInput;
	std::vector<std::unique_ptr<FrameGraphNodeOutput>> m_tmpOutput;
	FrameGraphQueueType m_queueType;
	FrameGraph* m_frameGraph;

public:
	FrameGraphNodeInitializer& Reset();
	FrameGraphNodeInitializer& AddInput(const IFrameGraphNodeInputInitializer* inInitializer);
	FrameGraphNodeInitializer& AddOutput(const IFrameGraphNodeOutputInitializer* inInitializer);
	FrameGraphNodeInitializer& AddInout(const IFrameGraphNodeInoutInitializer* inInitializer);
	FrameGraphNodeInitializer& SetQueueType(FrameGraphQueueType inQueueType);
	FrameGraphNodeInitializer& SetOwner(FrameGraph* inFrameGraph);
	void InitializeFrameGraphNode(FrameGraphNode* inFrameGraphNode);
};

#pragma endregion

#pragma region InputOutput
class FrameGraphNodeInput
{
private:
	FrameGraphNode* m_owner;
	std::string m_name;
	FrameGraphNodeOutput* m_connectedOutput;
	FrameGraphNodeOutput* m_correlatedOutput; // for resource that also serve as output
	FRAME_GRAPH_RESOURCE_STATE m_resourceState;

public:
	auto GetConnectedOutput() const->FrameGraphNodeOutput*;
	auto GetResourceHandle() const -> const FRAME_GRAPH_RESOURCE_HANDLE&;
	auto GetRequiredResourceState() const -> const FRAME_GRAPH_RESOURCE_STATE&;
	auto GetOwner() const -> const FrameGraphNode*;
	auto GetOwner() -> FrameGraphNode*;
	auto IsMutableReference() const -> bool;

	friend class FrameGraphNodeOutput;
	friend class FrameGraphNodeInputInitializer;
	friend class FrameGraphNodeInoutInitializer;
	friend class FrameGraphNodeInitializer;
};

class FrameGraphNodeOutput
{
private:
	FrameGraphNode* m_owner;
	std::string m_name;
	FrameGraphNodeInput* m_correlatedInput; // for output that modifies input
	std::vector<FrameGraphNodeInput*> m_connectedInputs;
	FRAME_GRAPH_RESOURCE_HANDLE m_resourceHandle;
	FRAME_GRAPH_RESOURCE_STATE m_resourceState;

public:
	auto GetConnectedInputs() const -> const std::vector<FrameGraphNodeInput*>&;
	auto GetResourceHandle() const -> const FRAME_GRAPH_RESOURCE_HANDLE&;
	auto GetProcessedResourceState() const -> const FRAME_GRAPH_RESOURCE_STATE&;
	auto GetOwner() const -> const FrameGraphNode*;
	auto GetOwner() -> FrameGraphNode*;
	auto IsReference() const -> bool;

	// Connect this output to an input, if input will also serve as output, this output can only has one input connected
	void ConnectToInput(FrameGraphNodeInput* inoutFrameGraphInputPtr);

	friend class FrameGraphNodeOutputInitializer;
	friend class FrameGraphNodeInoutInitializer;
	friend class FrameGraphNodeInitializer;
};

#pragma endregion

class FrameGraphNode
{
private:
	FrameGraph* m_graph;
	std::vector<std::unique_ptr<FrameGraphNodeInput>> m_inputs;
	std::vector<std::unique_ptr<FrameGraphNodeOutput>> m_outputs;
	std::set<FrameGraphNode*> m_extraDependencies;
	FrameGraphQueueType m_queueType = FrameGraphQueueType::GRAPHICS_ONLY;

public:
	void Init(FrameGraphNodeInitializer* inInitializer);

	// Normally, dependency based on node input and output will suffice,
	// however, there might be cases where this node represent a secondary buffer
	// recording behavior where we actually record a command buffer for other
	// frame graph node to use
	void AddExtraDependency(FrameGraphNode* inNodeThisDependsOn);

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
	void RequireResource();

	// This graph nodes may need resource to be in certain state before
	// the resource can safely run through recorded command, e.g. image
	// layout transform
	void RequireInputResourceState() const;

	// Frame graph resource will change state after execution of device commands
	// this node records, therefore we need to update it, e.g. after render pass
	// image may transfer to color attachment layout
	void MergeOutputResourceState() const;

	// After command execution, 
	// decrease reference count of resource required by this node's inputs
	// increase reference count of resource produced by this node's outputs based on how many inputs it connected to
	void UpdateResourceReferenceCount() const;

	//========================================================================

	// Do things in a frame graph execution, i.e. record command buffer
	void Execute();

	void Uninit();

	friend class FrameGraphNodeInitializer;
};

#undef FRAME_GRAPH_RESOURCE_STATE
#undef FRAME_GRAPH_RESOURCE_HANDLE