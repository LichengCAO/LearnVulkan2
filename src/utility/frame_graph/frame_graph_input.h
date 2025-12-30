#pragma once
#include "common.h"
#include <variant>

class FrameGraphInput;
class FrameGraphOutput;
class FrameGraphResource;
class FrameGraphNode;
class Image;
class Buffer;
class ImageView;
class BufferView;

class FrameGraphInput
{
private:
	FrameGraphNode* m_owner;
	std::vector<FrameGraphOutput*> m_connectedOutputs;

public:
	const std::vector<FrameGraphOutput*>& GetConnectedOutputs() const;
	
	friend class FrameGraphOutput;
};

class FrameGraphOutput
{
private:
	FrameGraphNode* m_owner;
	std::vector<FrameGraphInput*> m_connectedInputs;
	FrameGraphResource* m_pResource;

public:
	FrameGraphOutput& ConnectToInput(FrameGraphInput* inoutFrameGraphInputPtr);
	const std::vector<FrameGraphInput*>& GetConnectedInputs() const;
};

class FrameGraphNode
{
private:
	std::vector<std::unique_ptr<FrameGraphInput>> m_inputs;
	std::vector<std::unique_ptr<FrameGraphOutput>> m_outputs;

public:
};

class FrameGraphBlueprint
{
private:
	std::vector<std::unique_ptr<FrameGraphNode>> m_nodes;

public:
	FrameGraphBlueprint& AddFrameGraphNode(std::unique_ptr<FrameGraphNode> inNode);
};

enum class FrameGraphResourceType
{
	BUFFER,
	IMAGE,
	ACCELERATION_STRUCTURE
};

class FrameGraphResource
{
private:
	FrameGraphResourceType m_type;

public:
};

struct ImageResourceReference
{
	VkImageSubresourceRange range;
	VkImageLayout layout;
	uint32_t queueFamily;
	VkAccessFlags availableAccess; // when does output ready
};

struct BufferResourceReference
{
	VkDeviceSize offset;
	VkDeviceSize range;
	uint32_t queueFamily;
	VkAccessFlags availableAccess;
};

class FrameGraph
{
public:
	struct CompileContext
	{

	};
	// Decide static process based in input, only do once,
	// unless window resized, or other dramatic change
	// This should include: 
	// 1. order frame graph nodes
	// 2. synchronize them(barrier, semaphore), maybe layout transfer
	// 3. allocate resource for each nodes(include memory aliasing)
	void Compile();

	void StartFrame();

	void EndFrame();
};