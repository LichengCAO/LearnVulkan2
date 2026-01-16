#include "frame_graph_compile_context.h"
#include "vulkan_struct_util.h"

void FrameGraphCompileContext::RequireSubResourceStateBeforePass(
	const FrameGraphImageResourceHandle& inHandle, 
	const FrameGraphImageSubResourceState& inState)
{
	const auto& currentState = _GetResourceState(inHandle);
	std::vector<FrameGraphImageSubResourceState> origStates;
	std::vector<VkImageMemoryBarrier> barriers;
	std::vector<VkSemaphore> waitSemaphores;
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
			_PullPendingSemaphore(inHandle, origState.range, localWaitSemaphores);
			waitSemaphores.insert(waitSemaphores.end(), localWaitSemaphores.begin(), localWaitSemaphores.end());
		}
		VkImageMemoryBarrier barrier = barrierBuilder.Build(
			image, 
			origState.layout, 
			inState.layout, 
			origState.access, 
			inState.access);

		_AddPrologueBarrier(origState.stage, inState.stage, {}, {}, { barrier });
	}
}

void FrameGraphCompileContext::RequireSubResourceStateBeforePass(
	const FrameGraphBufferResourceHandle& inHandle, 
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
	const FrameGraphImageResourceHandle& inHandle, 
	const FrameGraphImageSubResourceState& inState)
{
	auto& currentState = _GetResourceState(inHandle);
	currentState.SetSubResourceState(inState);
}

void FrameGraphCompileContext::MergeSubResourceStateAfterPass(
	const FrameGraphBufferResourceHandle& inHandle, 
	const FrameGraphBufferSubResourceState& inState)
{
	auto& currentState = _GetResourceState(inHandle);
	currentState.SetSubResourceState(inState);
}

void FrameGraphCompileContext::PresageSubResourceStateNextPass(
	const FrameGraphImageResourceHandle& inHandle, 
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
	const FrameGraphBufferResourceHandle& inHandle, 
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
	const FrameGraphImageResourceHandle& inHandle, 
	const VkImageSubresourceRange& inRange)
{
}

void FrameGraphCompileContext::AddSubResourceReference(
	const FrameGraphBufferResourceHandle& inHandle, 
	VkDeviceSize inOffset, 
	VkDeviceSize inSize)
{
}

void FrameGraphCompileContext::ReleaseSubResourceReference(
	const FrameGraphImageResourceHandle& inHandle, 
	const VkImageSubresourceRange& inRange)
{
}

void FrameGraphCompileContext::ReleaseSubResourceReference(
	const FrameGraphBufferResourceHandle& inHandle, 
	VkDeviceSize inOffset, 
	VkDeviceSize inSize)
{
}
