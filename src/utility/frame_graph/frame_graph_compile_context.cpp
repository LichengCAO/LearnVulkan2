#include "frame_graph_compile_context.h"
#include "vulkan_struct_util.h"
#include "device.h"

#define REF_COUNT_SEG_TREE(intervalType) frame_graph_util::SegmentTree<RefCount<intervalType>, intervalType>

namespace
{
	FrameGraphQueueType _GetTypeFromQueueFamilyIndex(uint32_t inQueueFamilyIndex)
	{
		auto& device = MyDevice::GetInstance();

		uint32_t graphicsIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
		uint32_t computeIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::COMPUTE);

		if (graphicsIndex == inQueueFamilyIndex)
		{
			return FrameGraphQueueType::GRAPHICS_ONLY;
		}
		else if (inQueueFamilyIndex == computeIndex)
		{
			return FrameGraphQueueType::COMPUTE_ONLY;
		}
		CHECK_TRUE(false);

		return FrameGraphQueueType::GRAPHICS_ONLY;
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
	ImageBarrierBuilder barrierBuilder{};
	VkImage image = VK_NULL_HANDLE; // _GetImageResourceVulkanImage(inHandle); TODO

	currentState.GetSubResourceState(inState.range, origStates);
	for (size_t i = 0; i < origStates.size(); ++i)
	{
		const auto origState = origStates[i];
		bool involveQueueTransfer = 
			origState.queueFamily != VK_QUEUE_FAMILY_IGNORED && origState.queueFamily != inState.queueFamily;
		std::vector<VkSemaphore> localWaitSemaphores;

		barrierBuilder.Reset();
		barrierBuilder.CustomizeArrayLayerRange(origState.range.baseArrayLayer, origState.range.layerCount);
		barrierBuilder.CustomizeMipLevelRange(origState.range.baseMipLevel, origState.range.levelCount);
		if (involveQueueTransfer)
		{
			barrierBuilder.CustomizeQueueFamilyTransfer(origState.queueFamily, inState.queueFamily);
			
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
		VkImageMemoryBarrier barrier = barrierBuilder.Build(
			image, 
			origState.layout, 
			inState.layout, 
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
	BufferBarrierBuilder barrierBuilder{};

	currentState.GetSubResourceState(inState.offset, inState.size, origStates);
	for (size_t i = 0; i < origStates.size(); ++i)
	{
		const auto origState = origStates[i];
		bool involveQueueTransfer =
			origState.queueFamily != VK_QUEUE_FAMILY_IGNORED && origState.queueFamily != inState.queueFamily;
		std::vector<VkSemaphore> localWaitSemaphores;

		barrierBuilder.Reset();
		barrierBuilder.CustomizeOffsetAndSize(origState.offset, origState.size);
		if (involveQueueTransfer)
		{
			barrierBuilder.CustomizeQueueFamilyTransfer(origState.queueFamily, inState.queueFamily);
			_PullPendingSemaphore(inHandle, origState.offset, origState.size, localWaitSemaphores);
			waitSemaphores.insert(waitSemaphores.end(), localWaitSemaphores.begin(), localWaitSemaphores.end());
		}

		VkBufferMemoryBarrier barrier = barrierBuilder.Build(buffer, origState.access, inState.access);

		_AddPrologueBarrier(origState.stage, inState.stage, {}, {barrier}, {});
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
	ImageBarrierBuilder barrierBuilder{};
	VkImage image = VK_NULL_HANDLE; // TODO: _GetImageResourceVulkanImage(inHandle),
	currentState.GetSubResourceState(inNewState.range, currentStates);

	for (size_t i = 0; i < currentStates.size(); ++i)
	{
		const auto& currState = currentStates[i];
		bool involveQueueTransfer =
			currState.queueFamily != VK_QUEUE_FAMILY_IGNORED && currState.queueFamily != inNewState.queueFamily;
		
		if (involveQueueTransfer)
		{
			barrierBuilder.Reset();
			barrierBuilder.CustomizeArrayLayerRange(currState.range.baseArrayLayer, currState.range.layerCount);
			barrierBuilder.CustomizeMipLevelRange(currState.range.baseMipLevel, currState.range.levelCount);
			barrierBuilder.CustomizeQueueFamilyTransfer(currState.queueFamily, inNewState.queueFamily);
			auto imageBarrier = barrierBuilder.Build(
				image, 
				currState.layout, 
				inNewState.layout, 
				currState.access, 
				inNewState.access);
			_AddEpilogueBarrier(
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
	BufferBarrierBuilder barrierBuilder{};
	VkBuffer buffer = VK_NULL_HANDLE; // TODO: _GetImageResourceVulkanImage(inHandle),
	currentState.GetSubResourceState(inNewState.offset, inNewState.size, currentStates);

	for (size_t i = 0; i < currentStates.size(); ++i)
	{
		const auto& currState = currentStates[i];
		bool involveQueueTransfer =
			currState.queueFamily != VK_QUEUE_FAMILY_IGNORED && currState.queueFamily != inNewState.queueFamily;

		if (involveQueueTransfer)
		{
			barrierBuilder.Reset();
			barrierBuilder.CustomizeOffsetAndSize(currState.offset, currState.size);
			barrierBuilder.CustomizeQueueFamilyTransfer(currState.queueFamily, inNewState.queueFamily);
			auto imageBarrier = barrierBuilder.Build(
				buffer,
				currState.access,
				inNewState.access);
			_AddEpilogueBarrier(
				currState.stage,
				inNewState.stage,
				{},
				{ imageBarrier },
				{});
		}
	}
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