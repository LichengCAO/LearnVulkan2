#include "frame_graph.h"
#include "frame_graph_node.h"
#include <queue>

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
