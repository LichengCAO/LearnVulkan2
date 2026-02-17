#pragma once
#include "frame_graph_resource.h"
#include "frame_graph_node.h"
#include "image.h"
#include "buffer.h"
#include "command_buffer.h"

class FrameGraphBuilder;

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

class FrameGraphCommandBufferAllocator
{
public:
	FrameGraphCommandBufferAllocator& AddGraphicsCommandPool(VkCommandPool inPool);
	FrameGraphCommandBufferAllocator& AddComputeCommandPool(VkCommandPool inPool);
};

class FrameGraphSemaphoreAllocator
{
public:
	VkSemaphore AllocateSemaphore();
	void FreeSemaphore(VkSemaphore inSemaphore);
};

class FrameGraphFenceAllocator
{
public:
	VkFence GetFence();
	void FreeFence(VkFence inFence);
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
	FrameGraphPassBind(
		const FrameGraphBuilder* inBuilder, 
		const FrameGraphPass* inPassToBind) : m_pass(inPassToBind) {};
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

class FrameGraphBuilder
{
private:
	std::vector<Image*> m_externalImages;
	std::vector<Buffer*> m_externalBuffers;
	std::vector<std::function<void(FrameGraph*)>> m_initProcesses;

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
		std::set<NodeInput*> nexts;
		FRAME_GRAPH_RESOURCE_HANDLE handle;
		FRAME_GRAPH_SUBRESOURCE_STATE state;
		std::string name;
	};
	struct NodeTransient
	{
		NodeBlueprint* owner;
		FRAME_GRAPH_RESOURCE_HANDLE handle;
		FRAME_GRAPH_SUBRESOURCE_STATE initialState;
		FRAME_GRAPH_SUBRESOURCE_STATE finalState;
		std::string name;
	};
	struct NodeBlueprint
	{
		std::vector<std::unique_ptr<NodeInput>> inputs;
		std::vector<std::unique_ptr<NodeOutput>> outputs;
		std::vector<std::unique_ptr<NodeTransient>> transients;
		std::set<NodeBlueprint*> extraNexts;
		std::set<NodeBlueprint*> extraPrevs;
		FrameGraphQueueType type;
		void GetFullNext(std::set<NodeBlueprint*>& output);
		void GetFullPrev(std::set<NodeBlueprint*>& output);
	};
	struct ImageBlueprint
	{
		bool external;
		std::unique_ptr<FrameGraphImageResourceState> initialState;
		std::unique_ptr<IImageInitializer> initializer;

		//============= instance ==============
		std::unordered_map<FrameGraphImageHandle, size_t> handleToIndex; // index of 'refCounts' 'states'
		std::vector<uint32_t> refCounts;
		std::vector<std::unique_ptr<FrameGraphImageResourceState>> states;
		
		bool HaveResourceAssigned(FrameGraphImageHandle inHandle);
	};
	struct BufferBlueprint
	{
		bool external;
		std::unique_ptr<FrameGraphBufferResourceState> initialState;
		std::unique_ptr<IBufferInitializer> initializer;

		// ============= instance =============
		std::unordered_map<FrameGraphBufferHandle, size_t> handleToIndex; // index of 'refCounts' 'states'
		std::vector<uint32_t> refCounts;
		std::vector<std::unique_ptr<FrameGraphBufferResourceState>> states;
		
		bool HaveResourceAssigned(FrameGraphBufferHandle inHandle);
	};
	struct FenceBlueprint
	{
		enum class State
		{
			SIGNALED,
			UNSIGNALED
		};
		std::vector<FenceBlueprint::State> state;
	};
	struct SemaphoreBlueprint
	{
		enum class State
		{
			FREE,
			PENDING,
		};
		std::vector<SemaphoreBlueprint::State> state;
	};

	std::unordered_map<std::string, NodeOutput*> m_nameToOutput;
	std::vector<std::unique_ptr<NodeBlueprint>> m_nodeBlueprints;
	std::vector<std::unique_ptr<ImageBlueprint>> m_imageBlueprints;
	std::vector<std::unique_ptr<BufferBlueprint>> m_bufferBlueprints;
	std::vector<std::unique_ptr<FenceBlueprint>> m_fenceBlueprints;
	std::vector<std::unique_ptr<SemaphoreBlueprint>> m_semaphoreBlueprints;
	std::unordered_map<FrameGraphImageHandle, size_t> m_handleToImageBlueprint;
	std::unordered_map<FrameGraphBufferHandle, size_t> m_handleToBufferBlueprint;

	auto _GetNodeBlueprint(FrameGraphNodeHandle inHandle) -> NodeBlueprint*;
	auto _GetImageBlueprint(FrameGraphImageHandle inHandle) -> ImageBlueprint*;
	auto _GetBufferBlueprint(FrameGraphBufferHandle inHandle) -> BufferBlueprint*;
	auto _GetResourceState(FrameGraphImageHandle inHandle) -> FrameGraphImageResourceState*;
	auto _GetResourceState(FrameGraphBufferHandle inHandle) -> FrameGraphBufferResourceState*;

	void _TopologicalSort(std::vector<std::set<NodeBlueprint*>>& outBatches);
	void _GenerateResourceCreationTask(const std::vector<std::set<NodeBlueprint*>>& inBatches);
	void _GenerateSyncTask(const std::vector<std::set<NodeBlueprint*>>& inBatches);
	
	// update frame graph private member
	void _AddInternalBufferToGraph(FrameGraph* inGraph, std::unique_ptr<Buffer> inBufferToOwn) const;
	void _AddExternalBufferToGraph(FrameGraph* inGraph, Buffer* inBuffer) const;
	void _AddInternalImageToGraph(FrameGraph* inGraph, std::unique_ptr<Image> inImageToOwn) const;
	void _AddExternalImageToGraph(FrameGraph* inGraph, Image* inImage) const;
	void _RegisterHandleToResource(FrameGraph* inGraph, FrameGraphBufferHandle inHandle, Buffer* inResource) const;
	void _RegisterHandleToResource(FrameGraph* inGraph, FrameGraphImageHandle inHandle, Image* inResource) const;

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
	void ArrangePasses();
};
