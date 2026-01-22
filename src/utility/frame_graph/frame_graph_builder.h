#pragma once
#include "frame_graph_resource.h"
#include "frame_graph_node.h"

class Image;
class Buffer;

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

class FrameGraphBuilder
{
private:
	std::vector<Image*> m_externalImages;
	std::vector<Buffer*> m_externalBuffers;

	auto _MapHandleToResource(const FrameGraphImageResourceHandle& inHandle) -> Image*;
	auto _MapHandleToResource(const FrameGraphBufferResourceHandle& inHandle) -> Buffer*;
	auto _CreateNewImageResourceHandle() -> FrameGraphImageResourceHandle;
	auto _CreateNewBufferResourceHandle() -> FrameGraphBufferResourceHandle;
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
	auto RegisterExternalResource(const ExternalImageResourceRegisterInfo& inRegisterInfo) -> FrameGraphImageResourceHandle;
	auto RegisterExternalResource(const ExternalBufferResourceRegisterInfo& inRegisterInfo) -> FrameGraphBufferResourceHandle;
	auto PromiseInternalResource(const FrameGraphImageResourceAllocator& inAllocator) -> FrameGraphImageResourceHandle;
	auto PromiseInternalResource(const FrameGraphBufferResourceAllocator& inAllocator) -> FrameGraphBufferResourceHandle;
	void AddFrameGraphNode(const FrameGraphNodeInitializer* inNodeInitializerPtr);

};

