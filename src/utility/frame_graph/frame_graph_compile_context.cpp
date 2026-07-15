#include "frame_graph_compile_context.h"
#include "device.h"

#define REF_COUNT_SEG_TREE(intervalType) frame_graph_util::SegmentTree<RefCount<intervalType>, intervalType>

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
		const VkImageSubresourceRange& inRange,
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
		barrier.subresourceRange = inRange;
		return barrier;
	}

	FrameGraphQueueType _GetTypeFromQueueFamilyIndex(uint32_t inQueueFamilyIndex)
	{
		auto& device = MyDevice::GetInstance();

		uint32_t graphicsIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
		uint32_t computeIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::COMPUTE);

		if (graphicsIndex == inQueueFamilyIndex)
		{
			return FrameGraphQueueType::GRAPHICS;
		}
		else if (inQueueFamilyIndex == computeIndex)
		{
			return FrameGraphQueueType::COMPUTE;
		}
		CHECK_TRUE(false);

		return FrameGraphQueueType::GRAPHICS;
	}
}


void FrameGraphCompileContext::RequireSubResourceStateBeforePass(
	const FrameGraphImageHandle& inHandle, 
	const FrameGraphImageSubResourceState& inState)
{
	const auto& currentState = _GetResourceState(inHandle);
	std::vector<FrameGraphImageSubResourceState> origStates;
	std::vector<VkImageMemoryBarrier> barriers;
	std::vector<VkSemaphore> waitSemaphores;
	std::vector<VkPipelineStageFlags> waitStages;
	VkImage image = VK_NULL_HANDLE; // _GetImageResourceVulkanImage(inHandle); TODO

	currentState.GetSubResourceState(inState.range, origStates);
	for (size_t i = 0; i < origStates.size(); ++i)
	{
		const auto origState = origStates[i];
		bool involveQueueTransfer = 
			origState.queueFamily != VK_QUEUE_FAMILY_IGNORED && origState.queueFamily != inState.queueFamily;
		std::vector<VkSemaphore> localWaitSemaphores;

		if (involveQueueTransfer)
		{
			_PullPendingSemaphore(
				inHandle, 
				origState.range, 
				localWaitSemaphores);

			std::vector<VkPipelineStageFlags> localStages(localWaitSemaphores.size(), inState.stage);

			waitSemaphores.insert(
				waitSemaphores.end(), 
				std::make_move_iterator(localWaitSemaphores.begin()), 
				std::make_move_iterator(localWaitSemaphores.end()));
			waitStages.insert(
				waitStages.end(),
				std::make_move_iterator(localStages.begin()),
				std::make_move_iterator(localStages.end()));
		}
		VkImageMemoryBarrier barrier = MakeImageBarrier(
			image,
			origState.layout,
			inState.layout,
			origState.range,
			involveQueueTransfer ? origState.queueFamily : VK_QUEUE_FAMILY_IGNORED,
			involveQueueTransfer ? inState.queueFamily : VK_QUEUE_FAMILY_IGNORED,
			origState.access,
			inState.access);

		_AddPrologueBarrier(
			_GetTypeFromQueueFamilyIndex(inState.queueFamily),
			origState.stage, 
			inState.stage, 
			{}, 
			{}, 
			{ barrier });
	}
}

void FrameGraphCompileContext::RequireSubResourceStateBeforePass(
	const FrameGraphBufferHandle& inHandle, 
	const FrameGraphBufferSubResourceState& inState)
{
	const auto& currentState = _GetResourceState(inHandle);
	std::vector<FrameGraphBufferSubResourceState> origStates;
	std::vector<VkBufferMemoryBarrier> barriers;
	std::vector<VkSemaphore> waitSemaphores;
	VkBuffer buffer = VK_NULL_HANDLE; // _GetImageResourceVulkanImage(inHandle); TODO

	currentState.GetSubResourceState(inState.offset, inState.size, origStates);
	for (size_t i = 0; i < origStates.size(); ++i)
	{
		const auto origState = origStates[i];
		bool involveQueueTransfer =
			origState.queueFamily != VK_QUEUE_FAMILY_IGNORED && origState.queueFamily != inState.queueFamily;
		std::vector<VkSemaphore> localWaitSemaphores;

		if (involveQueueTransfer)
		{
			_PullPendingSemaphore(inHandle, origState.offset, origState.size, localWaitSemaphores);
			waitSemaphores.insert(waitSemaphores.end(), localWaitSemaphores.begin(), localWaitSemaphores.end());
		}

		VkBufferMemoryBarrier barrier = MakeBufferBarrier(
			buffer,
			origState.offset,
			origState.size,
			involveQueueTransfer ? origState.queueFamily : VK_QUEUE_FAMILY_IGNORED,
			involveQueueTransfer ? inState.queueFamily : VK_QUEUE_FAMILY_IGNORED,
			origState.access,
			inState.access);

		_AddPrologueBarrier(_GetTypeFromQueueFamilyIndex(inState.queueFamily), origState.stage, inState.stage, {}, {barrier}, {});
	}
}

void FrameGraphCompileContext::MergeSubResourceStateAfterPass(
	const FrameGraphImageHandle& inHandle, 
	const FrameGraphImageSubResourceState& inState)
{
	auto& currentState = _GetResourceState(inHandle);
	currentState.SetSubResourceState(inState);
}

void FrameGraphCompileContext::MergeSubResourceStateAfterPass(
	const FrameGraphBufferHandle& inHandle, 
	const FrameGraphBufferSubResourceState& inState)
{
	auto& currentState = _GetResourceState(inHandle);
	currentState.SetSubResourceState(inState);
}

void FrameGraphCompileContext::PresageSubResourceStateNextPass(
	const FrameGraphImageHandle& inHandle, 
	const FrameGraphImageSubResourceState& inNewState)
{
	const auto& currentState = _GetResourceState(inHandle);
	std::vector<FrameGraphImageSubResourceState> currentStates;
	VkImage image = VK_NULL_HANDLE; // TODO: _GetImageResourceVulkanImage(inHandle),
	currentState.GetSubResourceState(inNewState.range, currentStates);

	for (size_t i = 0; i < currentStates.size(); ++i)
	{
		const auto& currState = currentStates[i];
		bool involveQueueTransfer =
			currState.queueFamily != VK_QUEUE_FAMILY_IGNORED && currState.queueFamily != inNewState.queueFamily;
		
		if (involveQueueTransfer)
		{
			auto imageBarrier = MakeImageBarrier(
				image,
				currState.layout,
				inNewState.layout,
				currState.range,
				currState.queueFamily,
				inNewState.queueFamily,
				currState.access,
				inNewState.access);
			_AddEpilogueBarrier(
				_GetTypeFromQueueFamilyIndex(currState.queueFamily),
				currState.stage, 
				inNewState.stage,
				{}, 
				{},
				{ imageBarrier });
		}
	}
}

void FrameGraphCompileContext::PresageSubResourceStateNextPass(
	const FrameGraphBufferHandle& inHandle, 
	const FrameGraphBufferSubResourceState& inNewState)
{
	const auto& currentState = _GetResourceState(inHandle);
	std::vector<FrameGraphBufferSubResourceState> currentStates;
	VkBuffer buffer = VK_NULL_HANDLE; // TODO: _GetImageResourceVulkanImage(inHandle),
	currentState.GetSubResourceState(inNewState.offset, inNewState.size, currentStates);

	for (size_t i = 0; i < currentStates.size(); ++i)
	{
		const auto& currState = currentStates[i];
		bool involveQueueTransfer =
			currState.queueFamily != VK_QUEUE_FAMILY_IGNORED && currState.queueFamily != inNewState.queueFamily;

		if (involveQueueTransfer)
		{
			auto bufferBarrier = MakeBufferBarrier(
				buffer,
				currState.offset,
				currState.size,
				currState.queueFamily,
				inNewState.queueFamily,
				currState.access,
				inNewState.access);
			_AddEpilogueBarrier(
				_GetTypeFromQueueFamilyIndex(currState.queueFamily),
				currState.stage,
				inNewState.stage,
				{},
				{ bufferBarrier },
				{});
		}
	}
}

auto FrameGraphCompileContext::_GetResourceState(const FrameGraphImageHandle& inHandle) -> FrameGraphImageResourceState&
{
	auto iter = m_imageResourceHandleToIndex.find(inHandle.handle);
	CHECK_TRUE(iter != m_imageResourceHandleToIndex.end(), "FrameGraphCompileContext image state was not registered.");
	return m_imageResourceStates.at(iter->second);
}

auto FrameGraphCompileContext::_GetResourceState(const FrameGraphBufferHandle& inHandle) -> FrameGraphBufferResourceState&
{
	auto iter = m_bufferResourceHandleToIndex.find(inHandle.handle);
	CHECK_TRUE(iter != m_bufferResourceHandleToIndex.end(), "FrameGraphCompileContext buffer state was not registered.");
	return m_bufferResourceStates.at(iter->second);
}

void FrameGraphCompileContext::_PushPendingSemaphore(
	FrameGraphQueueType inQueueToSignal,
	const FrameGraphImageHandle& inHandle,
	const VkImageSubresourceRange& inRange,
	VkSemaphore inSemaphoreToWait)
{
	(void)inQueueToSignal;
	(void)inHandle;
	(void)inRange;
	(void)inSemaphoreToWait;
}

void FrameGraphCompileContext::_PullPendingSemaphore(
	const FrameGraphImageHandle& inHandle,
	const VkImageSubresourceRange& inRange,
	std::vector<VkSemaphore>& outSemaphoresToWait)
{
	(void)inHandle;
	(void)inRange;
	outSemaphoresToWait.clear();
}

void FrameGraphCompileContext::_PullPendingSemaphore(
	const FrameGraphBufferHandle& inHandle,
	VkDeviceSize inOffset,
	VkDeviceSize inSize,
	std::vector<VkSemaphore>& outSemaphoresToWait)
{
	(void)inHandle;
	(void)inOffset;
	(void)inSize;
	outSemaphoresToWait.clear();
}

void FrameGraphCompileContext::_AddPrologueBarrier(
	FrameGraphQueueType inQueueToSync,
	VkPipelineStageFlags inSrcStage,
	VkPipelineStageFlags inDstStage,
	const std::vector<VkMemoryBarrier>& inBarriers,
	const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
	const std::vector<VkImageMemoryBarrier>& inImageBarriers)
{
	m_prologueLocalSync.push_back(LocalSyncInfo{
		inQueueToSync,
		inSrcStage,
		inDstStage,
		inBarriers,
		inBufferBarriers,
		inImageBarriers
	});
}

void FrameGraphCompileContext::_AddEpilogueBarrier(
	FrameGraphQueueType inQueueToSync,
	VkPipelineStageFlags inSrcStage,
	VkPipelineStageFlags inDstStage,
	const std::vector<VkMemoryBarrier>& inBarriers,
	const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
	const std::vector<VkImageMemoryBarrier>& inImageBarriers)
{
	m_epilogueLocalSync.push_back(LocalSyncInfo{
		inQueueToSync,
		inSrcStage,
		inDstStage,
		inBarriers,
		inBufferBarriers,
		inImageBarriers
	});
}

void FrameGraphCompileContext::ApplyPrologueSynchronization()
{
}

void FrameGraphCompileContext::ApplyEpilogueSynchronization()
{
}

void FrameGraphCompileContext::AddSubResourceReference(
	const FrameGraphImageHandle& inHandle, 
	const VkImageSubresourceRange& inRange)
{
	ImageRefCountTree* refCountTree = nullptr; // TODO
	uint32_t mainIntervalStart = refCountTree->splitByMipLevel ? inRange.baseMipLevel : inRange.baseArrayLayer;
	uint32_t mainIntervalEnd = refCountTree->splitByMipLevel ? 
		(inRange.baseMipLevel + inRange.levelCount) : 
		(inRange.baseArrayLayer + inRange.layerCount);
	uint32_t subIntervalStart = refCountTree->splitByMipLevel ? inRange.baseArrayLayer : inRange.baseMipLevel;
	uint32_t subIntervalEnd = refCountTree->splitByMipLevel ? 
		(inRange.baseArrayLayer + inRange.layerCount) : 
		(inRange.baseMipLevel + inRange.levelCount);

	for (uint32_t i = mainIntervalStart; i < mainIntervalEnd; ++i)
	{
		std::vector<RefCount<uint32_t>> currentCounts;
		auto subTree = refCountTree->tree[i].get();

		subTree->GetSegment(subIntervalStart, subIntervalEnd, currentCounts);
		for (auto& currentCount : currentCounts)
		{
			currentCount.count += 1;
			subTree->SetSegment(currentCount.left, currentCount.right, currentCount);
		}
	}
}

void FrameGraphCompileContext::AddSubResourceReference(
	const FrameGraphBufferHandle& inHandle, 
	VkDeviceSize inOffset, 
	VkDeviceSize inSize)
{
	REF_COUNT_SEG_TREE(VkDeviceSize)* refCountTree = nullptr; // TODO
	std::vector<RefCount<VkDeviceSize>> currentCounts;

	refCountTree->GetSegment(inOffset, inOffset + inSize, currentCounts);
	for (auto& currentCount : currentCounts)
	{
		currentCount.count += 1;
		refCountTree->SetSegment(currentCount.left, currentCount.right, currentCount);
	}
}

void FrameGraphCompileContext::ReleaseSubResourceReference(
	const FrameGraphImageHandle& inHandle, 
	const VkImageSubresourceRange& inRange)
{
	ImageRefCountTree* refCountTree = nullptr; // TODO
	uint32_t mainIntervalStart = refCountTree->splitByMipLevel ? inRange.baseMipLevel : inRange.baseArrayLayer;
	uint32_t mainIntervalEnd = refCountTree->splitByMipLevel ?
		(inRange.baseMipLevel + inRange.levelCount) :
		(inRange.baseArrayLayer + inRange.layerCount);
	uint32_t subIntervalStart = refCountTree->splitByMipLevel ? inRange.baseArrayLayer : inRange.baseMipLevel;
	uint32_t subIntervalEnd = refCountTree->splitByMipLevel ?
		(inRange.baseArrayLayer + inRange.layerCount) :
		(inRange.baseMipLevel + inRange.levelCount);

	for (uint32_t i = mainIntervalStart; i < mainIntervalEnd; ++i)
	{
		std::vector<RefCount<uint32_t>> currentCounts;
		auto subTree = refCountTree->tree[i].get();

		subTree->GetSegment(subIntervalStart, subIntervalEnd, currentCounts);
		for (auto& currentCount : currentCounts)
		{
			if (currentCount.count > 0)
			{
				currentCount.count -= 1;
			}
			subTree->SetSegment(currentCount.left, currentCount.right, currentCount);
		}
	}
}

void FrameGraphCompileContext::ReleaseSubResourceReference(
	const FrameGraphBufferHandle& inHandle, 
	VkDeviceSize inOffset, 
	VkDeviceSize inSize)
{
	REF_COUNT_SEG_TREE(VkDeviceSize)* refCountTree = nullptr; // TODO
	std::vector<RefCount<VkDeviceSize>> currentCounts;

	refCountTree->GetSegment(inOffset, inOffset + inSize, currentCounts);
	for (auto& currentCount : currentCounts)
	{
		if (currentCount.count > 0)
		{
			currentCount.count -= 1;
		}
		refCountTree->SetSegment(currentCount.left, currentCount.right, currentCount);
	}
}

#undef REF_COUNT_SEG_TREE
