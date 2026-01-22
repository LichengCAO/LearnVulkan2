#pragma once
#include "frame_graph_resource.h"
#include "frame_graph_node.h"

class Image;
class Buffer;
class FrameGraphBuilder;
#define FRAME_GRAPH_RESOURCE_HANDLE std::variant<std::monostate, FrameGraphBufferHandle, FrameGraphImageHandle>
#define FRAME_GRAPH_SUBRESOURCE_STATE std::variant<std::monostate, FrameGraphBufferSubResourceState, FrameGraphImageSubResourceState>

class FrameGraphBufferResourceAllocator
{
public:
	FrameGraphBufferResourceAllocator& SetSize(VkDeviceSize inSize);
	// If this enabled, we do not apply memory aliasing and create
	// a new device object during compilation instead
	FrameGraphBufferResourceAllocator& CustomizeAsDedicated();
	FrameGraphBufferResourceAllocator& CustomizeLoadOperationAsClear(uint32_t inClearValue);
};

class FrameGraphImageResourceAllocator
{
private:

public:
	FrameGraphImageResourceAllocator& SetSize2D(uint32_t inWidth, uint32_t inHeight);
	FrameGraphImageResourceAllocator& SetFormat(VkFormat inFormat);
	// If this enabled, we do not apply memory aliasing and create
	// a new device object during compilation instead
	FrameGraphImageResourceAllocator& CustomizeAsDedicated();
	FrameGraphImageResourceAllocator& CustomizeArrayLayer(uint32_t inArrayLength);
	FrameGraphImageResourceAllocator& CustomizeMipLevel(uint32_t inLevelCount);
};

struct FrameGraphNodeHandle
{
	uint32_t index = ~0u;
};

struct FrameGraphPassBind;

class FrameGraphBuilder
{
private:
	std::vector<Image*> m_externalImages;
	std::vector<Buffer*> m_externalBuffers;
	std::vector<std::function<void(FrameGraph*)>> m_initProcesses;

	auto _MapHandleToResource(const FrameGraphImageHandle& inHandle) -> Image*;
	auto _MapHandleToResource(const FrameGraphBufferHandle& inHandle) -> Buffer*;
	auto _CreateNewImageResourceHandle() -> FrameGraphImageHandle;
	auto _CreateNewBufferResourceHandle() -> FrameGraphBufferHandle;
	
	struct NodeBlueprint;
	struct NodeOutput;
	struct NodeInput
	{
		NodeBlueprint* owner;
		NodeOutput* prev;
		NodeOutput* next;
		FRAME_GRAPH_RESOURCE_HANDLE handle;
		FRAME_GRAPH_SUBRESOURCE_STATE state;
		std::string name;
	};
	struct NodeOutput
	{
		NodeBlueprint* owner;
		NodeInput* prev;
		std::vector<NodeInput*> nexts;
		FRAME_GRAPH_RESOURCE_HANDLE handle;
		FRAME_GRAPH_SUBRESOURCE_STATE state;
		std::string name;
	};
	struct NodeBlueprint
	{
		std::vector<std::unique_ptr<NodeInput>> inputs;
		std::vector<std::unique_ptr<NodeOutput>> outputs;
		FrameGraphQueueType type;
	};

	std::unordered_map<std::string, NodeOutput*> m_nameToOutput;
	std::vector<std::unique_ptr<NodeBlueprint>> m_nodeBlueprints;

public:
	struct ExternalImageResourceRegisterInfo
	{
		Image* image;
		FrameGraphImageResourceState initialState;
	};
	struct ExternalBufferResourceRegisterInfo
	{
		Buffer* buffer;
		FrameGraphBufferResourceState initialState;
	};
	auto RegisterExternalResource(const ExternalImageResourceRegisterInfo& inRegisterInfo) -> FrameGraphImageHandle;
	auto RegisterExternalResource(const ExternalBufferResourceRegisterInfo& inRegisterInfo) -> FrameGraphBufferHandle;
	auto PromiseInternalResource(const FrameGraphImageResourceAllocator& inAllocator) -> FrameGraphImageHandle;
	auto PromiseInternalResource(const FrameGraphBufferResourceAllocator& inAllocator) -> FrameGraphBufferHandle;
	auto AddFrameGraphPass(const FrameGraphPassBind* inPassBind) -> FrameGraphNodeHandle;
	void AddExtraDependency(FrameGraphNodeHandle inSooner, FrameGraphNodeHandle inLater);
};


struct FrameGraphPassBind
{
private:
	struct Data
	{
		std::string name;
		uint32_t id;
		FRAME_GRAPH_RESOURCE_HANDLE handle;
	};
	std::vector<Data> m_data;
	FrameGraphQueueType m_type;
	const FrameGraphPass* m_pass;

public:
	FrameGraphPassBind(const FrameGraphBuilder* inBuilder, const FrameGraphPass* inPassToBind) : m_pass(inPassToBind) {};
	FrameGraphPassBind& BindInAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName);
	FrameGraphPassBind& BindInoutAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName);
	FrameGraphPassBind& BindOutAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName,
		const FrameGraphBufferHandle& inHandle);
	FrameGraphPassBind& BindOutAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName,
		const FrameGraphImageHandle& inHandle);
	FrameGraphPassBind& BindTransientAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName,
		const FrameGraphBufferHandle& inHandle);
	FrameGraphPassBind& BindTransientAttachment(
		uint32_t inAttachmentIndex,
		const std::string& inName,
		const FrameGraphImageHandle& inHandle);
	FrameGraphPassBind& SetQueueType(FrameGraphQueueType inType);

	friend class FrameGraphBuilder;
};
