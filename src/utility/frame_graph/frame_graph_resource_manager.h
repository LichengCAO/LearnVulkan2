#pragma once

#include "frame_graph_resource.h"
#include "image.h"
#include "buffer.h"

class FrameGraphResourceManager
{
private:
	struct ResourceProducer
	{
		size_t initializerIndex;
		std::vector<uint32_t> referenceCount;
		std::vector<Image*>  referenceBuffers;
		std::vector<Buffer*> referenceImages;
		bool isImage;
		bool isBuffer;
	};
	std::vector<Image::Initializer> m_promisedImageInitializers;
	std::vector<Buffer::Initializer> m_promisedBufferInitializers;
	std::vector<ResourceProducer> m_resourceProducers;

	void _MapHandleToIndices(
		const FrameGraphImageHandle& inHandle, 
		size_t& outProducerIndex, 
		size_t& outResourcePtrIndex);
	void _MapHandleToIndices(
		const FrameGraphBufferHandle& inHandle, 
		size_t& outProducerIndex, 
		size_t& outResourcePtrIndex);

public:
	struct Initializer
	{
		std::function<Buffer*()> bufferCreator;
		std::function<Image*()>  imageCreator;
	};
	void Init(const FrameGraphResourceManager::Initializer* inInitPtr);

	// Preordain functions return a handle that can be used later to map to actual resources
	FrameGraphImageHandle PromiseImageResource(const FrameGraphBufferResourceAllocator* inAllocatorPtr);
	FrameGraphBufferHandle PromiseBufferResource(const FrameGraphImageResourceAllocator* inAllocatorPtr);

	void 

	Image* GetImageResource(const FrameGraphImageHandle& inHandle);
	Buffer* GetBufferResource(const FrameGraphBufferHandle& inHandle);
};
