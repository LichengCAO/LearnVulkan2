#pragma once
#include "common.h"
#include <queue>
#include <functional>

#define BUFFER_SEGMENT_TREE frame_graph_util::SegmentTree<FrameGraphBufferSubResourceState, VkDeviceSize>
#define IMAGE_SEGMENT_TREE frame_graph_util::SegmentTree<FrameGraphImageSubResourceState, uint32_t>

namespace frame_graph_util
{
	template<typename T, typename BoundaryType>
	class SegmentTree
	{
	private:
		struct Segment
		{
			std::optional<T> value;
			BoundaryType left;	// inclusive
			BoundaryType right; // exclusive
			Segment* leftChild;
			Segment* rightChild;
		};
		// Segment tree
		std::vector<std::unique_ptr<Segment>> m_segments;
		// Sometimes T has attributes that based on segment range, 
		// this tree may create new T inside, so we need to know how to update it
		std::function<void(BoundaryType, BoundaryType, T&)> m_updateFunction;

	public:
		SegmentTree(
			BoundaryType inSize,
			std::function<void(BoundaryType, BoundaryType, T&)> inUpdateFunction)
			: m_updateFunction(inUpdateFunction)
		{
			std::unique_ptr<Segment> rootSegment = std::make_unique<Segment>();
			rootSegment->left = 0;
			rootSegment->right = inSize;
			m_segments.push_back(std::move(rootSegment));
		}

		void SetSegment(
			BoundaryType inStartInclusive,
			BoundaryType inEndExclusive, 
			const T& inSubState)
		{
			std::queue<Segment*> segmentQueue;
			BoundaryType subLeft = inStartInclusive;
			BoundaryType subRight = inEndExclusive;

			segmentQueue.push(m_segments[0].get()); // start from root
			while (!segmentQueue.empty())
			{
				size_t n = segmentQueue.size();
				for (size_t i = 0; i < n; ++i)
				{
					auto currentSegment = segmentQueue.front();
					segmentQueue.pop();

					bool overlap = !(subRight <= currentSegment->left || subLeft >= currentSegment->right);
					// no overlap, skip
					if (!overlap)
					{
						continue;
					}

					bool fullyOverlap = (subLeft <= currentSegment->left && subRight >= currentSegment->right);
					// fully overlapped, set state and continue
					if (fullyOverlap)
					{
						T newState = inSubState;
						m_updateFunction(currentSegment->left, currentSegment->right, newState);
						currentSegment->state = newState;
						continue;
					}

					currentSegment->state.reset(); // invalidate current state
					bool needSplit = currentSegment->leftChild == nullptr;
					if (!needSplit)
					{
						segmentQueue.push(currentSegment->leftChild);
						segmentQueue.push(currentSegment->rightChild);
						continue;
					}

					// need split, try to find split point
					BoundaryType splitPoint{};
					if (subLeft > currentSegment->left && subLeft < currentSegment->right)
					{
						splitPoint = subLeft;
					}
					else if (subRight > currentSegment->left && subRight < currentSegment->right)
					{
						splitPoint = subRight;
					}
					else
					{
						// should not reach here
						CHECK_TRUE(false);
					}
					std::unique_ptr<Segment> leftChild = std::make_unique<Segment>();
					std::unique_ptr<Segment> rightChild = std::make_unique<Segment>();

					currentSegment->leftChild = leftChild.get();
					currentSegment->rightChild = rightChild.get();
					leftChild->left = currentSegment->left;
					leftChild->right = splitPoint;
					rightChild->left = splitPoint;
					rightChild->right = currentSegment->right;
					m_segments.push_back(std::move(leftChild));
					m_segments.push_back(std::move(rightChild));
					segmentQueue.push(currentSegment->leftChild);
					segmentQueue.push(currentSegment->rightChild);
				}
			}
		}
		
		void GetSegments(
			BoundaryType inStartInclusive,
			BoundaryType inEndExclusive,
			std::vector<T>& outSubState) const
		{
			std::queue<Segment*> segmentQueue;
			BoundaryType subLeft = inStartInclusive;
			BoundaryType subRight = inEndExclusive;

			segmentQueue.push(m_segments[0].get()); // start from root
			while (!segmentQueue.empty())
			{
				size_t n = segmentQueue.size();
				for (size_t i = 0; i < n; ++i)
				{
					auto currentSegment = segmentQueue.front();
					segmentQueue.pop();

					bool overlap = !(subRight <= currentSegment->left || subLeft >= currentSegment->right);
					// no overlap, skip
					if (!overlap)
					{
						continue;
					}

					bool needCheckChildren = !currentSegment->state.has_value();
					if (needCheckChildren)
					{
						if (currentSegment->leftChild != nullptr)
						{
							segmentQueue.push(currentSegment->leftChild);
							segmentQueue.push(currentSegment->rightChild);
							continue;
						}
					}
					else
					{
						T subState = currentSegment->state.value();
						BoundaryType leftBound = std::max(subLeft, currentSegment->left);
						BoundaryType rightBound = std::min(subRight, currentSegment->right);

						CHECK_TRUE(leftBound < rightBound);
						m_updateFunction(leftBound, rightBound, subState);
						outSubState.push_back(subState);
					}
				}
			}
		}
	};
}

// Resource handle represent a device object (i.e. Image, Buffer) in frame graph
// different handle may represent the same device object due to memory aliasing
struct FrameGraphBufferResourceHandle
{
	uint32_t handle;
};

struct FrameGraphImageResourceHandle
{
	uint32_t handle;
};

// Resource state tracks the state of a device object in frame graph,
// it holds multiple sub-resource states which are modified by
// different passes through image views or buffer views.
// I manage them with segment tree for efficient range update and query.
struct FrameGraphImageSubResourceState
{
	VkImageSubresourceRange range;
	VkImageLayout layout;
	uint32_t queueFamily;
	VkAccessFlags access;
	VkPipelineStageFlags stage; // last time resource is used
};

class FrameGraphImageResourceState
{
private:
	std::vector<std::unique_ptr<IMAGE_SEGMENT_TREE>> m_segmentTrees;
	uint32_t m_mipLevels = 0;
	uint32_t m_arrayLayers = 0;
	bool m_splitByMipLevels; // each miplevel has its own segment tree

public:
	FrameGraphImageResourceState(uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
	void SetSubResourceState(const FrameGraphImageSubResourceState& inSubState);
	void GetSubResourceState(
		const VkImageSubresourceRange& inRange,
		std::vector<FrameGraphImageSubResourceState>& outSubState) const;
};

struct FrameGraphBufferSubResourceState
{
	VkDeviceSize offset;
	VkDeviceSize range;
	uint32_t queueFamily;
	VkAccessFlags access;
	VkPipelineStageFlags stage; // last time resource is used
};

class FrameGraphBufferResourceState
{
private:
	VkDeviceSize m_size = 0;
	std::unique_ptr<BUFFER_SEGMENT_TREE> m_segmentTree;

public:
	FrameGraphBufferResourceState(VkDeviceSize size);
	void SetSubResourceState(const FrameGraphBufferSubResourceState& inSubState);
	void GetSubResourceState(
		VkDeviceSize inOffset, 
		VkDeviceSize inRange, 
		std::vector<FrameGraphBufferSubResourceState>& outSubState) const;
};

enum class FrameGraphQueueType
{
	GRAPHICS_ONLY,  // so, it can be delegated to graphics recording thread
	COMPUTE_ONLY,	// so, it can be delegated to compute recording thread
	ALL				// it can only be run on main thread
};

#undef BUFFER_SEGMENT_TREE
#undef IMAGE_SEGMENT_TREE