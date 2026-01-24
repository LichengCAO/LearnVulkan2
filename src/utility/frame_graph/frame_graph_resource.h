#pragma once
#include "common.h"
#include <queue>
#include <functional>
#include <variant>

#ifndef FRAME_GRAPH_RESOURCE_HANDLE
#define FRAME_GRAPH_RESOURCE_HANDLE std::variant<std::monostate, FrameGraphBufferHandle, FrameGraphImageHandle>
#endif // FRAME_GRAPH_RESOURCE_HANDLE
#ifndef FRAME_GRAPH_RESOURCE_STATE
#define FRAME_GRAPH_RESOURCE_STATE std::variant<std::monostate, FrameGraphBufferSubResourceState, FrameGraphImageSubResourceState>
#endif //FRAME_GRAPH_RESOURCE_STATE
#ifndef BUFFER_SEGMENT_TREE
#define BUFFER_SEGMENT_TREE frame_graph_util::SegmentTree<FrameGraphBufferSubResourceState, VkDeviceSize>
#endif // BUFFER_SEGMENT_TREE
#ifndef IMAGE_SEGMENT_TREE
#define IMAGE_SEGMENT_TREE frame_graph_util::SegmentTree<FrameGraphImageSubResourceState, uint32_t>
#endif // IMAGE_SEGMENT_TREE

namespace frame_graph_util
{
	template<class ValueType, class IntervalType>
	class SegmentTree
	{
	private:
		struct Segment
		{
			std::optional<ValueType> value;
			IntervalType left;	// inclusive
			IntervalType right; // exclusive
			Segment* leftChild;
			Segment* rightChild;
		};
		// Segment tree
		std::vector<std::unique_ptr<Segment>> m_segments;
		// Sometimes ValueType has attributes that based on segment range, 
		// this tree may create new ValueType inside, so we need to know how to update it
		std::function<void(IntervalType, IntervalType, ValueType&)> m_updateFunction;

	public:
		SegmentTree(
			IntervalType inStartInclusive,
			IntervalType inEndExclusive,
			std::function<void(IntervalType, IntervalType, ValueType&)> inRangeUpdateFunc)
			: m_updateFunction(inRangeUpdateFunc)
		{
			std::unique_ptr<Segment> rootSegment = std::make_unique<Segment>();
			rootSegment->left = inStartInclusive;
			rootSegment->right = inEndExclusive;
			m_segments.push_back(std::move(rootSegment));
		}

		SegmentTree(const SegmentTree& inTreeToClone)
		{
			std::queue<Segment*> modifyChildQueue;
			static auto funcShallowCopy = [](const Segment* inSrcSegment, Segment* outDstSegment)
			{
				outDstSegment->left = inSrcSegment->left;
				outDstSegment->right = inSrcSegment->right;
				outDstSegment->value = inSrcSegment->value;
				outDstSegment->leftChild = inSrcSegment->leftChild;	// process later
				outDstSegment->rightChild = inSrcSegment->rightChild; // process later
			};
			
			this->m_updateFunction = inTreeToClone.m_updateFunction;
			if (inTreeToClone.m_segments.empty())
			{
				return;
			}
			
			std::unique_ptr<Segment> rootSegment = std::make_unique<Segment>();
			funcShallowCopy(inTreeToClone.m_segments[0].get(), rootSegment.get());
			modifyChildQueue.push(rootSegment.get()); // start from root
			m_segments.push_back(std::move(rootSegment));

			while (!modifyChildQueue.empty())
			{
				size_t n = modifyChildQueue.size();
				for (size_t i = 0; i < n; ++i)
				{
					auto currentParentSegment = modifyChildQueue.front();
					bool hasValidChildToClone = (currentParentSegment->leftChild != nullptr);
					modifyChildQueue.pop();
					if (!hasValidChildToClone)
					{
						continue;
					}
					std::unique_ptr<Segment> newLeftChild = std::make_unique<Segment>();
					std::unique_ptr<Segment> newRightChild = std::make_unique<Segment>();
					funcShallowCopy(currentParentSegment->leftChild, newLeftChild.get());
					funcShallowCopy(currentParentSegment->rightChild, newRightChild.get());
					currentParentSegment->leftChild = newLeftChild.get();
					currentParentSegment->rightChild = newRightChild.get();
					modifyChildQueue.push(newLeftChild.get());
					modifyChildQueue.push(newRightChild.get());
					m_segments.push_back(std::move(newLeftChild));
					m_segments.push_back(std::move(newRightChild));
				}
			}
		}

		void SetSegment(
			IntervalType inStartInclusive,
			IntervalType inEndExclusive,
			const ValueType& inUpdateValue)
		{
			std::queue<Segment*> segmentQueue;
			IntervalType subLeft = inStartInclusive;
			IntervalType subRight = inEndExclusive;

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
						ValueType newState = inUpdateValue;
						m_updateFunction(currentSegment->left, currentSegment->right, newState);
						currentSegment->state = newState;
						continue;
					}

					bool needSplit = currentSegment->leftChild == nullptr;
					bool hasPrevState = currentSegment->state.has_value();
					if (!needSplit)
					{
						if (hasPrevState)
						{
							ValueType prevStateLeft = currentSegment->state.value();
							ValueType prevStateRight = currentSegment->state.value();
							
							// invalidate current state
							currentSegment->state.reset();

							// propagate previous state to children
							m_updateFunction(currentSegment->leftChild->left, currentSegment->leftChild->right, prevStateLeft);
							m_updateFunction(currentSegment->rightChild->left, currentSegment->rightChild->right, prevStateRight);
							currentSegment->leftChild->state = prevStateLeft;
							currentSegment->rightChild->state = prevStateRight;
						}
						
						segmentQueue.push(currentSegment->leftChild);
						segmentQueue.push(currentSegment->rightChild);
						continue;
					}

					// need split, try to find split point
					IntervalType splitPoint{};
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
					if (hasPrevState)
					{
						ValueType prevStateLeft = currentSegment->state.value();
						ValueType prevStateRight = currentSegment->state.value();

						// invalidate current state
						currentSegment->state.reset();

						// propagate previous state to children
						m_updateFunction(currentSegment->leftChild->left, currentSegment->leftChild->right, prevStateLeft);
						m_updateFunction(currentSegment->rightChild->left, currentSegment->rightChild->right, prevStateRight);
						currentSegment->leftChild->state = prevStateLeft;
						currentSegment->rightChild->state = prevStateRight;
					}

					segmentQueue.push(currentSegment->leftChild);
					segmentQueue.push(currentSegment->rightChild);
				}
			}
		}
		
		void GetSegment(
			IntervalType inStartInclusive,
			IntervalType inEndExclusive,
			std::vector<ValueType>& outSubState) const
		{
			std::queue<Segment*> segmentQueue;
			IntervalType subLeft = inStartInclusive;
			IntervalType subRight = inEndExclusive;

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
						ValueType subState = currentSegment->state.value();
						IntervalType leftBound = std::max(subLeft, currentSegment->left);
						IntervalType rightBound = std::min(subRight, currentSegment->right);

						CHECK_TRUE(leftBound < rightBound);
						m_updateFunction(leftBound, rightBound, subState);
						outSubState.push_back(subState);
					}
				}
			}
		}

		void GetRange(IntervalType& outStartInclusive, IntervalType& outEndExclusive)
		{
			outStartInclusive = m_segments[0]->left;
			outEndExclusive = m_segments[0]->right;
		}
	};
}

// Resource handle represent a device object (i.e. Image, Buffer) in frame graph
// different handle may represent the same device object due to memory aliasing
struct FrameGraphBufferHandle
{
	uint32_t handle = ~0u;
	bool operator==(const FrameGraphBufferHandle& other) const 
	{
		return this->handle == other.handle;
	}
};
struct FrameGraphImageHandle
{
	uint32_t handle = ~0u;
	bool operator==(const FrameGraphImageHandle& other) const 
	{
		return this->handle == other.handle;
	}
};
struct FrameGraphNodeHandle
{
	uint32_t handle = ~0u;
	bool operator==(const FrameGraphNodeHandle& other) const 
	{
		return this->handle == other.handle;
	}
};
namespace std {
	template<>
	struct hash<FrameGraphBufferHandle> 
	{
		size_t operator()(const FrameGraphBufferHandle& key) const 
		{
			return hash<uint32_t>()(key.handle);
		}
	};
	template<>
	struct hash<FrameGraphImageHandle>
	{
		size_t operator()(const FrameGraphImageHandle& key) const
		{
			return hash<uint32_t>()(key.handle);
		}
	};
	template<>
	struct hash<FrameGraphNodeHandle>
	{
		size_t operator()(const FrameGraphNodeHandle& key) const
		{
			return hash<uint32_t>()(key.handle);
		}
	};
}

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
struct FrameGraphBufferSubResourceState
{
	VkDeviceSize offset;
	VkDeviceSize size;
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
	FrameGraphImageResourceState(const FrameGraphImageResourceState& inOther);
	void SetSubResourceState(const FrameGraphImageSubResourceState& inSubState);
	void GetSubResourceState(
		const VkImageSubresourceRange& inRange,
		std::vector<FrameGraphImageSubResourceState>& outSubState) const;
	uint32_t GetMipLevelCount() const;
	uint32_t GetArrayLayerCount() const;
};
class FrameGraphBufferResourceState
{
private:
	VkDeviceSize m_size = 0;
	std::unique_ptr<BUFFER_SEGMENT_TREE> m_segmentTree;

public:
	FrameGraphBufferResourceState(VkDeviceSize size);
	FrameGraphBufferResourceState(const FrameGraphBufferResourceState& inOther);
	void SetSubResourceState(const FrameGraphBufferSubResourceState& inSubState);
	void GetSubResourceState(
		VkDeviceSize inOffset, 
		VkDeviceSize inRange, 
		std::vector<FrameGraphBufferSubResourceState>& outSubState) const;
	VkDeviceSize GetSize() const { return m_size; };
};

enum class FrameGraphQueueType
{
	GRAPHICS_ONLY,  // so, it can be delegated to graphics recording thread
	COMPUTE_ONLY,	// so, it can be delegated to compute recording thread
	ALL				// it can only be run on main thread
};