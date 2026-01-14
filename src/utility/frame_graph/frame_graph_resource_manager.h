#pragma once

#include "frame_graph_resource.h"
class FrameGraph;

class FrameGraphBufferResourceAllocator
{
public:
	FrameGraphBufferResourceAllocator& SetSize(VkDeviceSize inSize);
	// If this enabled, we do not apply memory aliasing and create
	// a new device object during compilation instead
	FrameGraphBufferResourceAllocator& CustomizeAsDedicated();
	FrameGraphBufferResourceAllocator& CustomizeLoadOperationAsClear(uint32_t inClearValue);
	FrameGraphBufferResourceHandle Allocate(FrameGraph* inFrameGraph);
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
	FrameGraphImageResourceHandle Allocate(FrameGraph* inFrameGraph);
};

class FrameGraphResourceManager
{
private:
	struct LocalImageResourceManager
	{
		Image* GetImage();
		ImageView* GetImageView();
	};

	struct LocalBufferResourceManager
	{
		Buffer* GetBuffer();
	};


private:
	void _MapHandleToResource(FrameGraphImageResourceHandle& inHandle);
	void _MapHandleToResource(FrameGraphBufferResourceHandle& inHandle);

public:
	// Preordain functions return a handle that can be used later to map to actual resources
	FrameGraphImageResourceHandle PreordainImageResource();
	FrameGraphBufferResourceHandle PreordainBufferResource();


};