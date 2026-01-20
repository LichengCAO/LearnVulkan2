#include "frame_graph.h"
#include "frame_graph_node.h"
#include <queue>

namespace
{
	uint32_t _GetThreadByQueueType(FrameGraphQueueType inQueueType)
	{
		return 0;
	}
}

void FrameGraph::_TopologicalSortFrameGraphNodes(std::vector<std::set<size_t>>& outOrderedNodeIndex)
{
	size_t remainCount = 0;
	std::unordered_map<const FrameGraphNode*, size_t> nodeInDegree;
	std::unordered_map<const FrameGraphNode*, size_t> nodeIndex;
	std::queue<const FrameGraphNode*> zeroInNodes;

	for (const auto& uptrNode : m_nodes)
	{
		std::set<FrameGraphNode*> preNodes;
		uptrNode->GetPreGraphNodes(preNodes);
		nodeInDegree.insert({ uptrNode.get(), preNodes.size() });
		nodeIndex.insert({ uptrNode.get(), remainCount++ });
		if (preNodes.size() == 0)
		{
			zeroInNodes.push(uptrNode.get());
		}
	}

	while (!zeroInNodes.empty())
	{
		size_t n = zeroInNodes.size();
		outOrderedNodeIndex.push_back({});
		auto& currentLayerNodes = outOrderedNodeIndex.back();

		for (size_t i = 0; i < n; ++i)
		{
			const FrameGraphNode* pNode = zeroInNodes.front();
			std::set<FrameGraphNode*> postNodes;

			zeroInNodes.pop();
			
			// node with zero in degree should be in front
			currentLayerNodes.insert(nodeIndex.at(pNode));
			pNode->GetPostGraphNodes(postNodes);

			// update post node in degree, if it reaches 0, add it to queue
			for (auto pPostNode : postNodes)
			{
				auto& currentNodeInDegree = nodeInDegree[pPostNode];
				CHECK_TRUE(currentNodeInDegree > 0);
				--currentNodeInDegree;
				if (currentNodeInDegree == 0)
				{
					zeroInNodes.push(pPostNode);
				}
			}
		}
		remainCount -= n;
	}
	CHECK_TRUE(remainCount == 0);
}

void FrameGraph::_GenerateFrameGraphNodeBatchPrologue(const std::set<FrameGraphNode*>& inNodeBatch)
{
	FrameGraphCompileContext& context = m_currentContext;

	for (auto pNode : inNodeBatch)
	{
		pNode->RequireInputResourceState();
	}

	// from context, get semaphore and stage to wait
	// add memory barrier in current command buffers
}

void FrameGraph::_GenerateFrameGraphNodeBatchExecutionTasks(const std::set<size_t>& inNodeBatch)
{
	std::set<FrameGraphNode*> nodePtrs;
	MySinglThreadTask* prologueTask;
	MySinglThreadTask* epilogueTask;

	for (auto nodePtr : nodePtrs)
	{
		std::unique_ptr<MySinglThreadTask> newTask = std::make_unique<MySinglThreadTask>(
			[=]()
			{
				nodePtr->Execute();
			},
			_GetThreadByQueueType(nodePtr->GetQueueType()));

		if (!nodePtr->UseExternalCommandPool())
		{
			epilogueTask->DependOn(newTask.get());
		}
		newTask->DependOn(prologueTask);
		//if ()

		m_hostExecution.push_back(std::move(newTask));
	}
}

void FrameGraph::_GenerateFrameGraphNodeBatchEpilogue(const std::set<size_t>& inNodeBatch)
{
	std::vector<VkSemaphore> toSubmit;
	std::vector<VkSemaphore> toWait;
	bool needSubmitCmd = false;
	MySinglThreadTask* epilogueTask = nullptr;

	if (needSubmitCmd)
	{
		// create new cmd
		std::unique_ptr<MySinglThreadTask> task = std::make_unique<MySinglThreadTask>(
			[]()
			{
				// add barrier
				// submit queue
			}
			);
		epilogueTask = task.get();
		m_hostExecution.push_back(std::move(task));
	}
	else
	{
		std::unique_ptr<MySinglThreadTask> task = std::make_unique<MySinglThreadTask>(
			[]()
			{
				// add barrier
			}
			);
		epilogueTask = task.get();
		m_hostExecution.push_back(std::move(task));
	}
}

void FrameGraph::Compile()
{
	std::vector<std::set<FrameGraphNode*>> nodeBatches;

	_TopologicalSortFrameGraphNodes(nodeBatches);

	// first we will do device object creation for each internal device resource
	for (const auto& nodeBatch : nodeBatches)
	{
		for (auto nodePtr : nodeBatch)
		{
			// Here, we check resource handle generate inside, if we already assign device object,
			// we're good. If not, we check if there is a no longer referenced device object,
			// if there is one, we assign this object to the handle, and attach a prologue barrier to the node;
			// if there is not, we create a new device object for the handle
			nodePtr->RequireResource();
		}

		for (auto nodePtr : nodeBatch)
		{
			// Here, we decrease reference count for input, and increase reference count for output
			nodePtr->UpdateResourceReferenceCount();
		}
	}

	// at this point, all resource handle should point to an existed device object
	for (const auto& nodeBatch : nodeBatches)
	{
		// create new free command buffer if we don't have one

		for (auto nodePtr : nodeBatch)
		{
			// immediate barrier to graphics cmd and compute cmd, 
			// add wait semaphore info for next graphics queue submission and next compute queue submission
			nodePtr->RequireInputResourceState();
		}

		// build a single thread task T1 to record barrier

		// update resource state after barriers complete

		for (auto nodePtr : nodeBatch)
		{
			// update resource state after passes complete
			nodePtr->MergeOutputResourceState();
		}

		// build single thread tasks, those that record graphics cmd and compute cmd
		// should depends on single thread task T1 (this also implies tasks that use external
		// command pool do not need to wait task T1 done)

		// add <nodePtr, task> to a map in case there is a execution dependency

		// add dependency for tasks that has extra execution dependency

		for (auto nodePtr : nodeBatch)
		{
			// immediate barrier to graphics cmd and compute cmd,
			// record semaphore for each queue ownership transfer
			nodePtr->PresageResourceStateTransfer();
		}
		
		// build a single thread task T2 to record barrier,
		// if there is queue ownership transfer, submit queue with stored wait semaphore info
		// and semaphore to signal

		// add dependency for task T2, it depends on tasks that record graphics cmd and compute cmd
		// in this batch
	}

	// we collect tasks from first node batch as initial task
}
