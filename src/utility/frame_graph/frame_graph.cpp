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
