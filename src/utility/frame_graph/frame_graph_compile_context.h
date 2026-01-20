#pragma once
#include "frame_graph_resource.h"

#define REF_COUNT_SEG_TREE(intervalType) frame_graph_util::SegmentTree<RefCount<intervalType>, intervalType>

// Context that helps us to decide whether to add barrier between execution of frame graph nodes
class FrameGraphCompileContext
{
private:
	template<class IntervalType>
	struct RefCount
	{
		IntervalType left;
		IntervalType right;
		uint32_t count;
	};

	struct ImageRefCountTree
	{
		std::vector<std::unique_ptr<REF_COUNT_SEG_TREE(uint32_t)>> tree;
		bool splitByMipLevel = false;
		uint32_t mipLevelCount = 0;
		uint32_t arrayLayerCount = 0;
	};

	struct LocalSyncInfo
	{
		FrameGraphQueueType queueType;
		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;
		std::vector<VkMemoryBarrier> memoryBarriers;
		std::vector<VkBufferMemoryBarrier> bufferBarriers;
		std::vector<VkImageMemoryBarrier> imageBarriers;
	};

	struct GlobalSyncInfo
	{
		VkSemaphore semaphore;
		VkPipelineStageFlags stage;
	};

	std::vector<FrameGraphImageResourceState> m_imageResourceStates;
	std::vector<FrameGraphImageResourceState> m_imageResourceRequireStates;
	std::vector<FrameGraphBufferResourceState> m_bufferResourceStates;
	std::vector<FrameGraphBufferResourceState> m_bufferResourceRequireStates;
	std::unordered_map<uint32_t, size_t> m_imageResourceHandleToIndex;
	std::unordered_map<uint32_t, size_t> m_bufferResourceHandleToIndex;
	std::vector<std::unique_ptr<REF_COUNT_SEG_TREE(VkDeviceSize)>> m_bufferRefCounts;
	std::vector<std::unique_ptr<ImageRefCountTree>> m_subImageRefCounts;
	std::vector<LocalSyncInfo> m_prologueLocalSync;
	std::vector<LocalSyncInfo> m_epilogueLocalSync;
	std::vector<GlobalSyncInfo> m_waitSemaphoresInNextGraphicsSubmit;
	std::vector<GlobalSyncInfo> m_waitSemaphoresInNextComputeSubmit;
	
private:
	auto _GetResourceState(const FrameGraphImageResourceHandle& inHandle) -> FrameGraphImageResourceState&;

	auto _GetResourceState(const FrameGraphBufferResourceHandle& inHandle) -> FrameGraphBufferResourceState&;

	// When there is a queue ownership transfer, we need to create a semaphore
	// and submit it in the release operation queue, and wait it in the acquire operation queue
	void _PushPendingSemaphore(
		FrameGraphQueueType inQueueToSignal,
		const FrameGraphImageResourceHandle& inHandle,
		const VkImageSubresourceRange& inRange,
		VkSemaphore inSemaphoreToWait);

	// When we are about to record command buffer that uses the resource,
	// we need to pull all semaphores that are pending for this resource
	void _PullPendingSemaphore(
		const FrameGraphImageResourceHandle& inHandle,
		const VkImageSubresourceRange& inRange,
		std::vector<VkSemaphore>& outSemaphoresToWait);
	void _PullPendingSemaphore(
		const FrameGraphBufferResourceHandle& inHandle,
		VkDeviceSize inOffset,
		VkDeviceSize inSize,
		std::vector<VkSemaphore>& outSemaphoresToWait);

	void _AddPrologueBarrier(
		FrameGraphQueueType inQueueToSync,
		VkPipelineStageFlags inSrcStage,
		VkPipelineStageFlags inDstStage,
		const std::vector<VkMemoryBarrier>& inBarriers,
		const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
		const std::vector<VkImageMemoryBarrier>& inImageBarriers);

	void _AddEpilogueBarrier(
		FrameGraphQueueType inQueueToSync,
		VkPipelineStageFlags inSrcStage,
		VkPipelineStageFlags inDstStage,
		const std::vector<VkMemoryBarrier>& inBarriers,
		const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
		const std::vector<VkImageMemoryBarrier>& inImageBarriers);

public:
	// Resource layout, queue ownership may not meet requirement,
	// so we need add barrier, wait semaphore based on these requirements
	void RequireSubResourceStateBeforePass(
		const FrameGraphImageResourceHandle& inHandle,
		const FrameGraphImageSubResourceState& inState);
	void RequireSubResourceStateBeforePass(
		const FrameGraphBufferResourceHandle& inHandle,
		const FrameGraphBufferSubResourceState& inState);

	// Report semaphore to wait in current submission, and barriers to apply before pass
	void ApplyPrologueSynchronization();

	// After pass, resource state changes, we store it for next pass
	void MergeSubResourceStateAfterPass(
		const FrameGraphImageResourceHandle& inHandle,
		const FrameGraphImageSubResourceState& inState);
	void MergeSubResourceStateAfterPass(
		const FrameGraphBufferResourceHandle& inHandle,
		const FrameGraphBufferSubResourceState& inState);

	// According to spec, queue ownership transform requires both release operation and acquire operation,
	// see: https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-queue-transfers
	// this requires memory barriers be on both queue.
	// Therefore, we'd better to check every incoming resource state
	// transform ahead and build necessary release operation memory barrier
	// and semaphore in current batch
	void PresageSubResourceStateNextPass(
		const FrameGraphImageResourceHandle& inHandle,
		const FrameGraphImageSubResourceState& inNewState);
	void PresageSubResourceStateNextPass(
		const FrameGraphBufferResourceHandle& inHandle,
		const FrameGraphBufferSubResourceState& inNewState);

	// Report semaphore to signal in current submission, 
	// and barriers to apply after pass (release queue ownership)
	void ApplyEpilogueSynchronization();

	void AddSubResourceReference(
		const FrameGraphImageResourceHandle& inHandle,
		const VkImageSubresourceRange& inRange);
	void AddSubResourceReference(
		const FrameGraphBufferResourceHandle& inHandle,
		VkDeviceSize inOffset,
		VkDeviceSize inSize);

	void ReleaseSubResourceReference(
		const FrameGraphImageResourceHandle& inHandle,
		const VkImageSubresourceRange& inRange);
	void ReleaseSubResourceReference(
		const FrameGraphBufferResourceHandle& inHandle,
		VkDeviceSize inOffset,
		VkDeviceSize inSize);
};

#undef REF_COUNT_SEG_TREE