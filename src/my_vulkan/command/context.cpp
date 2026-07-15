#include "context.h"

#include "buffer.h"
#include "image.h"

#include <algorithm>

namespace
{
	auto MakeBufferBarrier(
		VkBuffer inBuffer,
		VkDeviceSize inOffset,
		VkDeviceSize inSize,
		uint32_t inSrcQueueFamily,
		uint32_t inDstQueueFamily,
		VkAccessFlags inSrcAccess,
		VkAccessFlags inDstAccess) -> VkBufferMemoryBarrier
	{
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = inSrcAccess;
		barrier.dstAccessMask = inDstAccess;
		barrier.srcQueueFamilyIndex = inSrcQueueFamily;
		barrier.dstQueueFamilyIndex = inDstQueueFamily;
		barrier.buffer = inBuffer;
		barrier.offset = inOffset;
		barrier.size = inSize;
		return barrier;
	}

	auto MakeImageBarrier(
		VkImage inImage,
		VkImageLayout inOldLayout,
		VkImageLayout inNewLayout,
		VkImageAspectFlags inAspect,
		uint32_t inBaseMipLevel,
		uint32_t inMipLevelCount,
		uint32_t inBaseArrayLayer,
		uint32_t inArrayLayerCount,
		uint32_t inSrcQueueFamily,
		uint32_t inDstQueueFamily,
		VkAccessFlags inSrcAccess,
		VkAccessFlags inDstAccess) -> VkImageMemoryBarrier
	{
		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.srcAccessMask = inSrcAccess;
		barrier.dstAccessMask = inDstAccess;
		barrier.oldLayout = inOldLayout;
		barrier.newLayout = inNewLayout;
		barrier.srcQueueFamilyIndex = inSrcQueueFamily;
		barrier.dstQueueFamilyIndex = inDstQueueFamily;
		barrier.image = inImage;
		barrier.subresourceRange.aspectMask = inAspect;
		barrier.subresourceRange.baseMipLevel = inBaseMipLevel;
		barrier.subresourceRange.levelCount = inMipLevelCount;
		barrier.subresourceRange.baseArrayLayer = inBaseArrayLayer;
		barrier.subresourceRange.layerCount = inArrayLayerCount;
		return barrier;
	}

	auto NormalizeSrcStages(VkPipelineStageFlags inStages) -> VkPipelineStageFlags
	{
		return inStages == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : inStages;
	}

	auto NormalizeDstStages(VkPipelineStageFlags inStages) -> VkPipelineStageFlags
	{
		return inStages == 0 ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : inStages;
	}

	auto Overlaps(VkDeviceSize inLeftBegin, VkDeviceSize inLeftEnd, VkDeviceSize inRightBegin, VkDeviceSize inRightEnd) -> bool
	{
		return inLeftBegin < inRightEnd && inRightBegin < inLeftEnd;
	}

	auto Overlaps(uint32_t inLeftBegin, uint32_t inLeftEnd, uint32_t inRightBegin, uint32_t inRightEnd) -> bool
	{
		return inLeftBegin < inRightEnd && inRightBegin < inLeftEnd;
	}
}

QueueContext::QueueContext(uint32_t inQueueFamilyIndex)
	: m_queueFamilyIndex(inQueueFamilyIndex)
{
	CHECK_TRUE(m_queueFamilyIndex != VK_QUEUE_FAMILY_IGNORED, "Invalid queue context family index!");
}

auto QueueContext::ResolveWholeBufferSize(const Resource& inResource) -> VkDeviceSize
{
	const BufferState& state = std::get<BufferState>(inResource.m_state);
	if (state.size != VK_WHOLE_SIZE)
	{
		return state.size;
	}

	const auto& info = state.buffer->GetBufferInformation();
	CHECK_TRUE(info.size >= state.offset);
	return info.size - state.offset;
}

auto QueueContext::BufferEndOffset(const Resource& inResource) -> VkDeviceSize
{
	const BufferState& state = std::get<BufferState>(inResource.m_state);
	return state.offset + ResolveWholeBufferSize(inResource);
}

auto QueueContext::IsSameResource(const Resource& inLeft, const Resource& inRight) -> bool
{
	if (inLeft.m_type != inRight.m_type)
	{
		return false;
	}

	if (std::holds_alternative<BufferState>(inLeft.m_state))
	{
		const BufferState& leftState = std::get<BufferState>(inLeft.m_state);
		const BufferState& rightState = std::get<BufferState>(inRight.m_state);
		if (leftState.buffer != rightState.buffer)
		{
			return false;
		}

		return Overlaps(
			leftState.offset,
			BufferEndOffset(inLeft),
			rightState.offset,
			BufferEndOffset(inRight));
	}

	if (std::holds_alternative<ImageState>(inLeft.m_state))
	{
		const ImageState& leftState = std::get<ImageState>(inLeft.m_state);
		const ImageState& rightState = std::get<ImageState>(inRight.m_state);
		if (leftState.image != rightState.image)
		{
			return false;
		}

		return Overlaps(
			leftState.baseMipLevel,
			leftState.baseMipLevel + leftState.mipLevelCount,
			rightState.baseMipLevel,
			rightState.baseMipLevel + rightState.mipLevelCount) &&
			Overlaps(
				leftState.baseArrayLayer,
				leftState.baseArrayLayer + leftState.arrayLayerCount,
				rightState.baseArrayLayer,
				rightState.baseArrayLayer + rightState.arrayLayerCount);
	}

	return true;
}

auto QueueContext::FindResourceIndex(
	const std::vector<Resource>& inResources,
	const Resource& inTarget) -> size_t
{
	for (size_t i = 0; i < inResources.size(); ++i)
	{
		if (IsSameResource(inResources[i], inTarget))
		{
			return i;
		}
	}

	return inResources.size();
}

void QueueContext::UpsertResource(const Resource& inResource)
{
	size_t index = FindResourceIndex(m_resources, inResource);
	if (index == m_resources.size())
	{
		m_resources.push_back(inResource);
		return;
	}

	m_resources[index] = inResource;
}

void QueueContext::AppendBarrier(
	BarrierBatch& inoutBatch,
	const Resource& inSource,
	const Resource& inDestination,
	uint32_t inSourceQueueFamily,
	uint32_t inDestinationQueueFamily,
	VkAccessFlags inSourceAccess,
	VkAccessFlags inDestinationAccess,
	VkPipelineStageFlags inSourceStage,
	VkPipelineStageFlags inDestinationStage)
{
	inoutBatch.srcStages |= NormalizeSrcStages(inSourceStage);
	inoutBatch.dstStages |= NormalizeDstStages(inDestinationStage);

	if (std::holds_alternative<BufferState>(inSource.m_state))
	{
		const BufferState& dstState = std::get<BufferState>(inDestination.m_state);
		const uint32_t srcQueueFamily = inSourceQueueFamily != inDestinationQueueFamily ? inSourceQueueFamily : VK_QUEUE_FAMILY_IGNORED;
		const uint32_t dstQueueFamily = inSourceQueueFamily != inDestinationQueueFamily ? inDestinationQueueFamily : VK_QUEUE_FAMILY_IGNORED;

		inoutBatch.bufferBarriers.push_back(
			MakeBufferBarrier(
				dstState.buffer->GetVkBuffer(),
				dstState.offset,
				ResolveWholeBufferSize(inDestination),
				srcQueueFamily,
				dstQueueFamily,
				inSourceAccess,
				inDestinationAccess));
		return;
	}

	if (std::holds_alternative<ImageState>(inSource.m_state))
	{
		const ImageState& srcState = std::get<ImageState>(inSource.m_state);
		const ImageState& dstState = std::get<ImageState>(inDestination.m_state);
		const uint32_t srcQueueFamily = inSourceQueueFamily != inDestinationQueueFamily ? inSourceQueueFamily : VK_QUEUE_FAMILY_IGNORED;
		const uint32_t dstQueueFamily = inSourceQueueFamily != inDestinationQueueFamily ? inDestinationQueueFamily : VK_QUEUE_FAMILY_IGNORED;

		inoutBatch.imageBarriers.push_back(
			MakeImageBarrier(
				dstState.image->GetVkImage(),
				srcState.layout,
				dstState.layout,
				dstState.aspect,
				dstState.baseMipLevel,
				dstState.mipLevelCount,
				dstState.baseArrayLayer,
				dstState.arrayLayerCount,
				srcQueueFamily,
				dstQueueFamily,
				inSourceAccess,
				inDestinationAccess));
	}
}

void QueueContext::Resource::SetBufferMemory(
	const Buffer* inBuffer,
	VkDeviceSize inSize,
	VkDeviceSize inOffset)
{
	CHECK_TRUE(inBuffer != nullptr);
	CHECK_TRUE(inSize == VK_WHOLE_SIZE || inSize > 0);
	m_type = ResourceType::BUFFER;
	m_state = BufferState{
		.buffer = inBuffer,
		.size = inSize,
		.offset = inOffset,
	};
}

void QueueContext::Resource::SetBufferMemoryConcise(const Buffer* inBuffer)
{
	CHECK_TRUE(inBuffer != nullptr);
	SetBufferMemory(inBuffer, inBuffer->GetBufferInformation().size, 0);
}

void QueueContext::Resource::SetImageMemory(
	const Image* inImage,
	VkImageLayout inLayout,
	uint32_t inBaseMipLevel,
	uint32_t inMipLevelCount,
	uint32_t inBaseArrayLayer,
	uint32_t inArrayLayerCount)
{
	CHECK_TRUE(inImage != nullptr);
	CHECK_TRUE(inMipLevelCount > 0);
	CHECK_TRUE(inArrayLayerCount > 0);
	m_type = ResourceType::IMAGE;
	m_state = ImageState{
		.image = inImage,
		.layout = inLayout,
		.aspect = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = inBaseMipLevel,
		.mipLevelCount = inMipLevelCount,
		.baseArrayLayer = inBaseArrayLayer,
		.arrayLayerCount = inArrayLayerCount,
	};
}

void QueueContext::Resource::SetImageMemoryConcise(const Image* inImage, VkImageLayout inLayout)
{
	CHECK_TRUE(inImage != nullptr);
	const auto& info = inImage->GetImageInformation();
	SetImageMemory(inImage, inLayout, 0, info.mipLevels, 0, info.arrayLayers);
}

void QueueContext::PushResources(const ResourceReleased* inResources, size_t inCount)
{
	CHECK_TRUE(inCount == 0 || inResources != nullptr);

	for (size_t i = 0; i < inCount; ++i)
	{
		Resource resource = inResources[i];
		resource.PresetQueueFamily(m_queueFamilyIndex);
		UpsertResource(resource);
	}
}

auto QueueContext::PullResources(
	const ResourceAcquired* inResources,
	size_t inCount) -> BarrierBatch
{
	CHECK_TRUE(inCount == 0 || inResources != nullptr);

	BarrierBatch batch;
	for (size_t i = 0; i < inCount; ++i)
	{
		Resource resource = inResources[i];
		resource.PresetQueueFamily(m_queueFamilyIndex);

		size_t stateIndex = FindResourceIndex(m_resources, resource);
		CHECK_TRUE(stateIndex < m_resources.size(), "QueueContext::PullResources requires a previously pushed resource state.");

		AppendBarrier(
			batch,
			m_resources[stateIndex],
			resource,
			m_resources[stateIndex].m_queueFamily,
			resource.m_queueFamily,
			m_resources[stateIndex].m_access,
			resource.m_access,
			m_resources[stateIndex].m_stage,
			resource.m_stage);

		m_resources[stateIndex] = resource;
	}

	return batch;
}

auto QueueContext::GrabResourcesFromOther(
	QueueContext* inSourceContext,
	const ResourceAcquired* inResources,
	size_t inCount) -> QueueTransferBarrierBatches
{
	CHECK_TRUE(inSourceContext != nullptr);
	CHECK_TRUE(inCount == 0 || inResources != nullptr);

	QueueTransferBarrierBatches batches{};
	if (inSourceContext == this || inSourceContext->m_queueFamilyIndex == m_queueFamilyIndex)
	{
		batches.acquire = PullResources(inResources, inCount);
		return batches;
	}

	std::vector<size_t> sourceIndicesToErase;

	for (size_t i = 0; i < inCount; ++i)
	{
		Resource destinationResource = inResources[i];
		destinationResource.PresetQueueFamily(m_queueFamilyIndex);

		size_t sourceIndex = FindResourceIndex(inSourceContext->m_resources, destinationResource);
		CHECK_TRUE(sourceIndex < inSourceContext->m_resources.size(), "QueueContext::GrabResourcesFromOther requires the source context to own the resource.");

		const Resource& sourceResource = inSourceContext->m_resources[sourceIndex];

		AppendBarrier(
			batches.release,
			sourceResource,
			destinationResource,
			sourceResource.m_queueFamily,
			destinationResource.m_queueFamily,
			sourceResource.m_access,
			0,
			sourceResource.m_stage,
			0);

		AppendBarrier(
			batches.acquire,
			sourceResource,
			destinationResource,
			sourceResource.m_queueFamily,
			destinationResource.m_queueFamily,
			0,
			destinationResource.m_access,
			0,
			destinationResource.m_stage);

		UpsertResource(destinationResource);
		sourceIndicesToErase.push_back(sourceIndex);
	}

	std::sort(sourceIndicesToErase.begin(), sourceIndicesToErase.end(), std::greater<size_t>());
	for (size_t index : sourceIndicesToErase)
	{
		inSourceContext->m_resources.erase(inSourceContext->m_resources.begin() + static_cast<std::ptrdiff_t>(index));
	}

	return batches;
}
