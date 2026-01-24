#include "frame_graph_resource.h"
#include <queue>

FrameGraphBufferResourceState::FrameGraphBufferResourceState(VkDeviceSize size)
	:m_size(size)
{
	m_segmentTree = 
		std::make_unique<BUFFER_SEGMENT_TREE>(
			0,
			size,
			[](VkDeviceSize inStart, VkDeviceSize inEnd, FrameGraphBufferSubResourceState& inoutState)
			{
				inoutState.offset = inStart;
				inoutState.size = inEnd - inStart;
			});
}

void FrameGraphBufferResourceState::SetSubResourceState(
	const FrameGraphBufferSubResourceState& inSubState)
{
	m_segmentTree->SetSegment(
		inSubState.offset,
		inSubState.offset + inSubState.size,
		inSubState);
}

void FrameGraphBufferResourceState::GetSubResourceState(
	VkDeviceSize inOffset, 
	VkDeviceSize inRange, 
	std::vector<FrameGraphBufferSubResourceState>& outSubState) const
{
	m_segmentTree->GetSegment(
		inOffset,
		inOffset + inRange,
		outSubState);
}

FrameGraphImageResourceState::FrameGraphImageResourceState(
	uint32_t mipLevels, 
	uint32_t arrayLayers)
	:m_arrayLayers(arrayLayers), 
	m_mipLevels(mipLevels)
{
	m_splitByMipLevels = (mipLevels <= arrayLayers);
	uint32_t numTrees = m_splitByMipLevels ? mipLevels : arrayLayers;
	auto arrayMajorLamda = [](uint32_t inStart, uint32_t inEnd, FrameGraphImageSubResourceState& inoutState)
	{
		inoutState.range.baseMipLevel = inStart;
		inoutState.range.levelCount = inEnd - inStart;
	};
	auto mipMajorLamda = [](uint32_t inStart, uint32_t inEnd, FrameGraphImageSubResourceState& inoutState)
	{
		inoutState.range.baseArrayLayer = inStart;
		inoutState.range.layerCount = inEnd - inStart;
	};
	for (uint32_t i = 0; i < numTrees; ++i)
	{
		std::unique_ptr<IMAGE_SEGMENT_TREE> tree = std::make_unique<IMAGE_SEGMENT_TREE>(
			0,
			m_splitByMipLevels ? arrayLayers : mipLevels,
			m_splitByMipLevels ? mipMajorLamda : arrayMajorLamda);
		m_segmentTrees.push_back(std::move(tree));
	}
}

void FrameGraphImageResourceState::SetSubResourceState(
	const FrameGraphImageSubResourceState& inSubState)
{
	if (m_splitByMipLevels)
	{
		for (uint32_t i = 0; i < inSubState.range.levelCount; ++i)
		{
			uint32_t mipLevel = inSubState.range.baseMipLevel + i;
			FrameGraphImageSubResourceState subState = inSubState;
			subState.range.baseMipLevel = mipLevel;
			subState.range.levelCount = 1;
			m_segmentTrees[mipLevel]->SetSegment(
				subState.range.baseArrayLayer,
				subState.range.baseArrayLayer + subState.range.layerCount,
				subState);
		}
	}
	else
	{
		for (uint32_t i = 0; i < inSubState.range.layerCount; ++i)
		{
			uint32_t arrayLayer = inSubState.range.baseArrayLayer + i;
			FrameGraphImageSubResourceState subState = inSubState;
			subState.range.baseArrayLayer = arrayLayer;
			subState.range.layerCount = 1;
			m_segmentTrees[arrayLayer]->SetSegment(
				subState.range.baseMipLevel,
				subState.range.baseMipLevel + subState.range.levelCount,
				subState);
		}
	}
}

void FrameGraphImageResourceState::GetSubResourceState(
	const VkImageSubresourceRange& inRange, 
	std::vector<FrameGraphImageSubResourceState>& outSubState) const
{
	if (m_splitByMipLevels)
	{
		for (uint32_t i = 0; i < inRange.levelCount; ++i)
		{
			m_segmentTrees[inRange.baseMipLevel + i]->GetSegment(
				inRange.baseArrayLayer,
				inRange.baseArrayLayer + inRange.layerCount,
				outSubState);
		}
	}
	else
	{
		for (uint32_t i = 0; i < inRange.layerCount; ++i)
		{
			m_segmentTrees[inRange.baseArrayLayer + i]->GetSegment(
				inRange.baseMipLevel,
				inRange.baseMipLevel + inRange.levelCount,
				outSubState);
		}
	}
}

uint32_t FrameGraphImageResourceState::GetMipLevelCount() const
{
    return m_arrayLayers;
}

uint32_t FrameGraphImageResourceState::GetArrayLayerCount() const
{
	return m_mipLevels;
}