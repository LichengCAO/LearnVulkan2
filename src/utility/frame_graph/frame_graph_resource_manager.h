#pragma once

#include "frame_graph_resource.h"
#include "image.h"
#include "buffer.h"

class FrameGraphBufferResourceAllocator;
class FrameGraphImageResourceAllocator;

class FrameGraphResourceManager
{
private:
	struct ResourceProducer
	{
		size_t createInfoIndex;
		std::vector<uint32_t> referenceCount;
		std::vector<Image*>  referenceImages;
		std::vector<Buffer*> referenceBuffers;
		bool isImage;
		bool isBuffer;
	};
	std::vector<ImageCreateInfo> m_promisedImageCreateInfos;
	std::vector<BufferCreateInfo> m_promisedBufferCreateInfos;
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
	void Create(const FrameGraphResourceManager::Initializer* inInitPtr);

	// Preordain functions return a handle that can be used later to map to actual resources
	FrameGraphImageHandle PromiseImageResource(const FrameGraphImageResourceAllocator* inAllocatorPtr);
	FrameGraphBufferHandle PromiseBufferResource(const FrameGraphBufferResourceAllocator* inAllocatorPtr);

	Image* GetImageResource(const FrameGraphImageHandle& inHandle);
	Buffer* GetBufferResource(const FrameGraphBufferHandle& inHandle);
};
