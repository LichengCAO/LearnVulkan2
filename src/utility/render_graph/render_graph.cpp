#include "render_graph.h"

#include <algorithm>
#include <unordered_set>

namespace
{
	auto _MakeEdgeKey(uint32_t inBefore, uint32_t inAfter)->uint64_t
	{
		return (static_cast<uint64_t>(inBefore) << 32u) | static_cast<uint64_t>(inAfter);
	}
}

auto RenderGraph::_GetQueueType(PassType inType) -> QueueType
{
	switch (inType)
	{
	case PassType::COMPUTE:
		return QueueType::COMPUTE;
	case PassType::GRAPHICS:
	case PassType::SUBPASS:
		return QueueType::GRAPHICS;
	default:
		CHECK_TRUE(false, "Unsupported render graph pass type!");
		return QueueType::GRAPHICS;
	}
}

auto RenderGraph::_NeedsMemoryDependency(HazardType inHazard, VkImageLayout inOldLayout, VkImageLayout inNewLayout) -> bool
{
	if (inOldLayout != inNewLayout)
	{
		return true;
	}

	return inHazard != HazardType::WAR;
}

void RenderGraph::BufferInfo::AddUsage(VkBufferUsageFlags inUsage)
{
	CHECK_TRUE(inUsage != 0, "Render graph buffer usage cannot be empty!");
	m_usage |= inUsage;
}

void RenderGraph::BufferInfo::SetAsExternal()
{
	m_external = true;
}

void RenderGraph::ImageInfo::AddUsage(VkImageUsageFlags inUsage)
{
	CHECK_TRUE(inUsage != 0, "Render graph image usage cannot be empty!");
	m_usage |= inUsage;
}

void RenderGraph::ImageInfo::SetAsExternal()
{
	m_external = true;
}

void RenderGraph::PassInfo::AddSampledImage(const std::string& inName, VkPipelineStageFlags2 inReadStage)
{
	CHECK_TRUE(!inName.empty(), "Sampled image name cannot be empty!");
	CHECK_TRUE(inReadStage != 0, "Sampled image read stage cannot be empty!");

	ImageUsage usage;
	usage.image = inName;
	usage.type = ImageUsageType::SAMPLED;
	usage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	usage.stage = inReadStage;
	usage.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	usage.reads = true;
	m_imageUsages.push_back(usage);
}

void RenderGraph::PassInfo::AddStorageImage(const std::string& inName, VkPipelineStageFlags2 inWriteStage)
{
	CHECK_TRUE(!inName.empty(), "Storage image name cannot be empty!");
	CHECK_TRUE(inWriteStage != 0, "Storage image stage cannot be empty!");

	ImageUsage usage;
	usage.image = inName;
	usage.type = ImageUsageType::STORAGE;
	usage.layout = VK_IMAGE_LAYOUT_GENERAL;
	usage.stage = inWriteStage;
	usage.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	usage.reads = true;
	usage.writes = true;
	m_imageUsages.push_back(usage);
}

void RenderGraph::PassInfo::AddDescriptorUniformBuffer(const std::string& inName, VkPipelineStageFlags2 inReadStage)
{
	CHECK_TRUE(!inName.empty(), "Uniform buffer name cannot be empty!");
	CHECK_TRUE(inReadStage != 0, "Uniform buffer read stage cannot be empty!");

	BufferUsage usage;
	usage.buffer = inName;
	usage.type = BufferUsageType::UNIFORM;
	usage.stage = inReadStage;
	usage.access = VK_ACCESS_2_UNIFORM_READ_BIT;
	usage.reads = true;
	m_bufferUsages.push_back(usage);
}

void RenderGraph::PassInfo::AddDescriptorStorageBuffer(const std::string& inName, VkPipelineStageFlags2 inWriteStage)
{
	CHECK_TRUE(!inName.empty(), "Storage buffer name cannot be empty!");
	CHECK_TRUE(inWriteStage != 0, "Storage buffer stage cannot be empty!");

	BufferUsage usage;
	usage.buffer = inName;
	usage.type = BufferUsageType::STORAGE;
	usage.stage = inWriteStage;
	usage.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	usage.reads = true;
	usage.writes = true;
	m_bufferUsages.push_back(usage);
}

void RenderGraph::SubpassInfo::UseDedicateRenderPass()
{
	m_useDedicatedRenderPass = true;
}

void RenderGraph::SubpassInfo::AddColorAttachment(
	const std::string& inName,
	VkAttachmentLoadOp inLoadOp,
	VkPipelineStageFlags2 inLoadStage,
	VkAttachmentStoreOp inStoreOp,
	VkPipelineStageFlags2 inStoreStage)
{
	CHECK_TRUE(!inName.empty(), "Color attachment image name cannot be empty!");
	CHECK_TRUE(inStoreStage != 0, "Color attachment store stage cannot be empty!");
	if (inLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
	{
		CHECK_TRUE(inLoadStage != 0, "Color attachment load stage cannot be empty when load op is LOAD!");
	}

	ImageUsage usage;
	usage.image = inName;
	usage.type = ImageUsageType::COLOR_ATTACHMENT;
	usage.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	usage.stage = inLoadStage | inStoreStage;
	usage.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	usage.reads = inLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
	usage.writes = true;
	if (usage.reads)
	{
		usage.access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
	}
	usage.loadOp = inLoadOp;
	usage.storeOp = inStoreOp;
	m_imageUsages.push_back(usage);
}

void RenderGraph::SubpassInfo::AddDepthAttachment(
	const std::string& inName,
	VkAttachmentLoadOp inLoadOp,
	VkPipelineStageFlags2 inLoadStage,
	VkAttachmentStoreOp inStoreOp,
	VkPipelineStageFlags2 inStoreStage)
{
	CHECK_TRUE(!inName.empty(), "Depth attachment image name cannot be empty!");
	CHECK_TRUE(inStoreStage != 0, "Depth attachment store stage cannot be empty!");
	if (inLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
	{
		CHECK_TRUE(inLoadStage != 0, "Depth attachment load stage cannot be empty when load op is LOAD!");
	}

	ImageUsage usage;
	usage.image = inName;
	usage.type = ImageUsageType::DEPTH_ATTACHMENT;
	usage.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	usage.stage = inLoadStage | inStoreStage;
	usage.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	usage.reads = inLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
	usage.writes = true;
	if (usage.reads)
	{
		usage.access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	}
	usage.loadOp = inLoadOp;
	usage.storeOp = inStoreOp;
	m_imageUsages.push_back(usage);
}

auto RenderGraph::_GetBufferIndex(const std::string& inName) const -> BufferIndex
{
	CHECK_TRUE(!inName.empty(), "Render graph buffer name cannot be empty!");
	const auto iter = m_nameToBuffer.find(inName);
	CHECK_TRUE(iter != m_nameToBuffer.end(), "Render graph buffer name is not registered!");
	return iter->second;
}

auto RenderGraph::_GetImageIndex(const std::string& inName) const -> ImageIndex
{
	CHECK_TRUE(!inName.empty(), "Render graph image name cannot be empty!");
	const auto iter = m_nameToImage.find(inName);
	CHECK_TRUE(iter != m_nameToImage.end(), "Render graph image name is not registered!");
	return iter->second;
}

auto RenderGraph::_GetPassIndex(const std::string& inName) const -> PassIndex
{
	CHECK_TRUE(!inName.empty(), "Render graph pass name cannot be empty!");
	const auto iter = m_nameToPass.find(inName);
	CHECK_TRUE(iter != m_nameToPass.end(), "Render graph pass name is not registered!");
	return iter->second;
}

void RenderGraph::_InvalidateBuild()
{
	m_built = false;
	m_dependencyEdges.clear();
	m_sortedPasses.clear();
	m_submitBatches.clear();
	m_passesInExecutionOrder.clear();
	m_passBatches.clear();
	m_barrierPlans.clear();
	m_queueSyncPlans.clear();
}

void RenderGraph::AddBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo)
{
	CHECK_TRUE(!inName.empty(), "Render graph buffer name cannot be empty!");
	CHECK_TRUE(m_nameToBuffer.find(inName) == m_nameToBuffer.end(), "Render graph buffer name already exists!");
	CHECK_TRUE(inBufferInfo.m_external || inBufferInfo.m_usage != 0, "Internal render graph buffer must have usages!");

	const BufferIndex index = static_cast<BufferIndex>(m_buffers.size());
	BufferInfo info = inBufferInfo;
	info.m_name = inName;
	m_buffers.push_back(info);
	m_nameToBuffer.emplace(inName, index);
	_InvalidateBuild();
}

void RenderGraph::AddImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo)
{
	CHECK_TRUE(!inName.empty(), "Render graph image name cannot be empty!");
	CHECK_TRUE(m_nameToImage.find(inName) == m_nameToImage.end(), "Render graph image name already exists!");
	CHECK_TRUE(inImageInfo.m_external || inImageInfo.m_usage != 0, "Internal render graph image must have usages!");

	const ImageIndex index = static_cast<ImageIndex>(m_images.size());
	ImageInfo info = inImageInfo;
	info.m_name = inName;
	m_images.push_back(info);
	m_nameToImage.emplace(inName, index);
	_InvalidateBuild();
}

void RenderGraph::AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo)
{
	CHECK_TRUE(!inName.empty(), "Render graph pass name cannot be empty!");
	CHECK_TRUE(m_nameToPass.find(inName) == m_nameToPass.end(), "Render graph pass name already exists!");

	PassRecord record;
	record.name = inName;
	record.type = inPassInfo.GetType();
	record.queue = _GetQueueType(record.type);
	record.imageUsages = inPassInfo.m_imageUsages;
	record.bufferUsages = inPassInfo.m_bufferUsages;

	if (record.type == PassType::SUBPASS)
	{
		const auto* subpassInfo = dynamic_cast<const SubpassInfo*>(&inPassInfo);
		CHECK_TRUE(subpassInfo != nullptr, "Subpass pass info type mismatch!");
		record.useDedicatedRenderPass = subpassInfo->m_useDedicatedRenderPass;
	}

	for (const ImageUsage& usage : record.imageUsages)
	{
		_GetImageIndex(usage.image);
	}
	for (const BufferUsage& usage : record.bufferUsages)
	{
		_GetBufferIndex(usage.buffer);
	}

	const PassIndex index = static_cast<PassIndex>(m_passes.size());
	m_passes.push_back(record);
	m_nameToPass.emplace(inName, index);
	_InvalidateBuild();
}

void RenderGraph::AddExtraPassDependency(const std::string& inHappensSooner, const std::string& inHappensLater)
{
	const PassIndex sooner = _GetPassIndex(inHappensSooner);
	const PassIndex later = _GetPassIndex(inHappensLater);
	CHECK_TRUE(sooner != later, "A render graph pass cannot depend on itself!");

	DependencyEdge edge;
	edge.before = sooner;
	edge.after = later;
	m_extraDependencies.push_back(edge);
	_InvalidateBuild();
}

void RenderGraph::Build()
{
	m_dependencyEdges.clear();
	m_sortedPasses.clear();
	m_submitBatches.clear();
	m_passesInExecutionOrder.clear();
	m_passBatches.clear();
	m_barrierPlans.clear();
	m_queueSyncPlans.clear();

	std::unordered_set<uint64_t> edgeSet;
	auto addEdge = [&](PassIndex inBefore, PassIndex inAfter)
	{
		CHECK_TRUE(inBefore != inAfter, "Render graph dependency cycle detected through self edge!");
		const uint64_t key = _MakeEdgeKey(inBefore, inAfter);
		if (!edgeSet.insert(key).second)
		{
			return;
		}

		DependencyEdge edge;
		edge.before = inBefore;
		edge.after = inAfter;
		m_dependencyEdges.push_back(edge);
	};

	struct ImageUsageRef
	{
		PassIndex pass = INVALID_INDEX;
		ImageIndex image = INVALID_INDEX;
		ImageUsage usage;
	};
	struct BufferUsageRef
	{
		PassIndex pass = INVALID_INDEX;
		BufferIndex buffer = INVALID_INDEX;
		BufferUsage usage;
	};

	std::vector<std::vector<ImageUsageRef>> imageUsageRefs(m_images.size());
	std::vector<std::vector<BufferUsageRef>> bufferUsageRefs(m_buffers.size());

	for (PassIndex passIndex = 0; passIndex < m_passes.size(); ++passIndex)
	{
		const PassRecord& pass = m_passes[passIndex];
		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = _GetImageIndex(usage.image);
			imageUsageRefs[imageIndex].push_back(ImageUsageRef{ passIndex, imageIndex, usage });
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = _GetBufferIndex(usage.buffer);
			bufferUsageRefs[bufferIndex].push_back(BufferUsageRef{ passIndex, bufferIndex, usage });
		}
	}

	auto emitImagePlan = [&](const ImageUsageRef& inBefore, const ImageUsageRef& inAfter, HazardType inHazard)
	{
		addEdge(inBefore.pass, inAfter.pass);

		BarrierPlan plan;
		plan.resourceType = ResourceType::IMAGE;
		plan.image = inAfter.image;
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		plan.hazard = inHazard;
		plan.oldLayout = inBefore.usage.layout;
		plan.newLayout = inAfter.usage.layout;
		plan.srcStage = inBefore.usage.stage;
		plan.dstStage = inAfter.usage.stage;
		plan.executionOnly = !_NeedsMemoryDependency(plan.hazard, plan.oldLayout, plan.newLayout);
		plan.srcAccess = plan.executionOnly ? 0 : inBefore.usage.access;
		plan.dstAccess = plan.executionOnly ? 0 : inAfter.usage.access;
		plan.needsQueueSync = m_passes[inBefore.pass].queue != m_passes[inAfter.pass].queue;
		m_barrierPlans.push_back(plan);
	};

	auto emitBufferPlan = [&](const BufferUsageRef& inBefore, const BufferUsageRef& inAfter, HazardType inHazard)
	{
		addEdge(inBefore.pass, inAfter.pass);

		BarrierPlan plan;
		plan.resourceType = ResourceType::BUFFER;
		plan.buffer = inAfter.buffer;
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		plan.hazard = inHazard;
		plan.srcStage = inBefore.usage.stage;
		plan.dstStage = inAfter.usage.stage;
		plan.executionOnly = inHazard == HazardType::WAR;
		plan.srcAccess = plan.executionOnly ? 0 : inBefore.usage.access;
		plan.dstAccess = plan.executionOnly ? 0 : inAfter.usage.access;
		plan.needsQueueSync = m_passes[inBefore.pass].queue != m_passes[inAfter.pass].queue;
		m_barrierPlans.push_back(plan);
	};

	for (ImageIndex imageIndex = 0; imageIndex < imageUsageRefs.size(); ++imageIndex)
	{
		const bool hasExternalInitialState = m_images[imageIndex].m_external;
		const auto& refs = imageUsageRefs[imageIndex];
		std::optional<ImageUsageRef> lastWriter;
		std::vector<ImageUsageRef> pendingReaders;
		std::optional<ImageUsageRef> firstWriter;

		for (const ImageUsageRef& ref : refs)
		{
			if (ref.usage.writes)
			{
				firstWriter = ref;
				break;
			}
		}

		for (const ImageUsageRef& ref : refs)
		{
			if (ref.usage.reads)
			{
				if (lastWriter.has_value() && lastWriter->pass != ref.pass)
				{
					emitImagePlan(lastWriter.value(), ref, HazardType::RAW);
				}
				else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
				{
					emitImagePlan(firstWriter.value(), ref, HazardType::RAW);
				}
				else if (hasExternalInitialState)
				{
					pendingReaders.push_back(ref);
				}
			}

			if (ref.usage.writes)
			{
				if (lastWriter.has_value() && !ref.usage.reads && lastWriter->pass != ref.pass)
				{
					emitImagePlan(lastWriter.value(), ref, HazardType::WAW);
				}

				for (const ImageUsageRef& reader : pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						emitImagePlan(reader, ref, HazardType::WAR);
					}
				}

				pendingReaders.clear();
				lastWriter = ref;
			}
			else if (ref.usage.reads && lastWriter.has_value())
			{
				pendingReaders.push_back(ref);
			}
		}
	}

	for (BufferIndex bufferIndex = 0; bufferIndex < bufferUsageRefs.size(); ++bufferIndex)
	{
		const bool hasExternalInitialState = m_buffers[bufferIndex].m_external;
		const auto& refs = bufferUsageRefs[bufferIndex];
		std::optional<BufferUsageRef> lastWriter;
		std::vector<BufferUsageRef> pendingReaders;
		std::optional<BufferUsageRef> firstWriter;

		for (const BufferUsageRef& ref : refs)
		{
			if (ref.usage.writes)
			{
				firstWriter = ref;
				break;
			}
		}

		for (const BufferUsageRef& ref : refs)
		{
			if (ref.usage.reads)
			{
				if (lastWriter.has_value() && lastWriter->pass != ref.pass)
				{
					emitBufferPlan(lastWriter.value(), ref, HazardType::RAW);
				}
				else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
				{
					emitBufferPlan(firstWriter.value(), ref, HazardType::RAW);
				}
				else if (hasExternalInitialState)
				{
					pendingReaders.push_back(ref);
				}
			}

			if (ref.usage.writes)
			{
				if (lastWriter.has_value() && !ref.usage.reads && lastWriter->pass != ref.pass)
				{
					emitBufferPlan(lastWriter.value(), ref, HazardType::WAW);
				}

				for (const BufferUsageRef& reader : pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						emitBufferPlan(reader, ref, HazardType::WAR);
					}
				}

				pendingReaders.clear();
				lastWriter = ref;
			}
			else if (ref.usage.reads && lastWriter.has_value())
			{
				pendingReaders.push_back(ref);
			}
		}
	}

	for (const DependencyEdge& edge : m_extraDependencies)
	{
		addEdge(edge.before, edge.after);
	}

	for (const DependencyEdge& edge : m_dependencyEdges)
	{
		const PassRecord& before = m_passes[edge.before];
		const PassRecord& after = m_passes[edge.after];
		if (before.queue == after.queue)
		{
			continue;
		}

		QueueSyncPlan plan;
		plan.before = edge.before;
		plan.after = edge.after;
		plan.srcQueue = before.queue;
		plan.dstQueue = after.queue;
		m_queueSyncPlans.push_back(plan);
	}

	constexpr uint32_t DEFAULT_STAGE_RANK = 100u;
	auto getStageRank = [](VkPipelineStageFlags2 inStage) -> uint32_t
	{
		if (inStage == 0)
		{
			return DEFAULT_STAGE_RANK;
		}

		if (inStage & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT) return 0u;
		if (inStage & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) return 1u;
		if (inStage & VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT) return 2u;
		if (inStage & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT) return 3u;
		if (inStage & VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT) return 4u;
		if (inStage & VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT) return 5u;
		if (inStage & VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT) return 6u;
#ifdef VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
		if (inStage & VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT) return 7u;
#endif
#ifdef VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
		if (inStage & VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT) return 8u;
#endif
		if (inStage & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT) return 9u;
		if (inStage & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) return 10u;
		if (inStage & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) return 11u;
		if (inStage & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) return 12u;
		if (inStage & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) return 13u;
		if (inStage & VK_PIPELINE_STAGE_2_COPY_BIT) return 14u;
		if (inStage & VK_PIPELINE_STAGE_2_RESOLVE_BIT) return 15u;
		if (inStage & VK_PIPELINE_STAGE_2_BLIT_BIT) return 16u;
		if (inStage & VK_PIPELINE_STAGE_2_CLEAR_BIT) return 17u;
		if (inStage & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT) return 18u;
		if (inStage & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT) return 19u;
		if (inStage & VK_PIPELINE_STAGE_2_HOST_BIT) return 20u;
		if (inStage & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) return 3u;
		if (inStage & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) return 0u;
		return DEFAULT_STAGE_RANK;
	};

	auto getPassStageRank = [&](PassIndex inPassIndex) -> uint32_t
	{
		const PassRecord& pass = m_passes[inPassIndex];
		uint32_t rank = DEFAULT_STAGE_RANK;

		for (const ImageUsage& usage : pass.imageUsages)
		{
			rank = std::min(rank, getStageRank(usage.stage));
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			rank = std::min(rank, getStageRank(usage.stage));
		}

		if (rank == DEFAULT_STAGE_RANK)
		{
			rank = pass.type == PassType::COMPUTE ? getStageRank(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) : getStageRank(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
		}

		return rank;
	};

	auto isBatchableSubpass = [&](PassIndex inPassIndex) -> bool
	{
		const PassRecord& pass = m_passes[inPassIndex];
		return pass.type == PassType::SUBPASS && !pass.useDedicatedRenderPass;
	};

	std::vector<std::vector<PassIndex>> adjacency(m_passes.size());
	std::unordered_set<uint64_t> queueSyncEdgeSet;
	for (const DependencyEdge& edge : m_dependencyEdges)
	{
		adjacency[edge.before].push_back(edge.after);
		if (m_passes[edge.before].queue != m_passes[edge.after].queue)
		{
			queueSyncEdgeSet.insert(_MakeEdgeKey(edge.before, edge.after));
		}
	}

	auto getRenderPassSignature = [&](PassIndex inPassIndex) -> std::string
	{
		const PassRecord& pass = m_passes[inPassIndex];
		if (!isBatchableSubpass(inPassIndex))
		{
			return {};
		}

		std::string signature;
		for (const ImageUsage& usage : pass.imageUsages)
		{
			if (usage.type != ImageUsageType::COLOR_ATTACHMENT &&
				usage.type != ImageUsageType::DEPTH_ATTACHMENT)
			{
				continue;
			}

			const ImageIndex imageIndex = _GetImageIndex(usage.image);
			signature += std::to_string(static_cast<uint32_t>(usage.type));
			signature += ':';
			signature += std::to_string(imageIndex);
			signature += ':';
			signature += std::to_string(static_cast<uint32_t>(usage.layout));
			signature += ':';
			signature += std::to_string(static_cast<uint32_t>(usage.loadOp));
			signature += ':';
			signature += std::to_string(static_cast<uint32_t>(usage.storeOp));
			signature += ';';
		}
		return signature;
	};

	struct PassRef
	{
		uint32_t indegree = 0;
		uint32_t sortFactor = 100u;
		uint32_t downstreamStageRank = 100u;
		uint32_t downstreamDependCount = 0;
		std::string renderPassSignature;
		uint32_t batchAffinity = 1u;
	};

	std::vector<PassRef> passRefs(m_passes.size());
	for (const DependencyEdge& edge : m_dependencyEdges)
	{
		++passRefs[edge.after].indegree;
	}

	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
		PassRef& ref = passRefs[index];
		ref.renderPassSignature = getRenderPassSignature(index);
		ref.downstreamDependCount = static_cast<uint32_t>(adjacency[index].size());

		for (PassIndex next : adjacency[index])
		{
			ref.downstreamStageRank = std::min(ref.downstreamStageRank, getPassStageRank(next));
		}
		if (ref.downstreamStageRank == DEFAULT_STAGE_RANK)
		{
			ref.downstreamStageRank = getPassStageRank(index);
		}
		ref.sortFactor = ref.downstreamStageRank;
	}

	auto sortReady = [&](std::vector<PassIndex>& inReady)
	{
		std::stable_sort(inReady.begin(), inReady.end(), [&](PassIndex inLeft, PassIndex inRight)
		{
			const PassRef& left = passRefs[inLeft];
			const PassRef& right = passRefs[inRight];
			if (left.sortFactor != right.sortFactor)
			{
				return left.sortFactor < right.sortFactor;
			}
			if (left.downstreamDependCount != right.downstreamDependCount)
			{
				return left.downstreamDependCount > right.downstreamDependCount;
			}
			return inLeft < inRight;
		});
	};

	auto sortGraphicsReady = [&](std::vector<PassIndex>& inReady, const std::optional<std::string>& inActiveRenderPassSignature)
	{
		for (PassIndex index : inReady)
		{
			PassRef& ref = passRefs[index];
			if (isBatchableSubpass(index))
			{
				ref.batchAffinity = inActiveRenderPassSignature.has_value() && ref.renderPassSignature == inActiveRenderPassSignature.value() ? 0u : 2u;
			}
			else
			{
				ref.batchAffinity = 1u;
			}
		}

		std::stable_sort(inReady.begin(), inReady.end(), [&](PassIndex inLeft, PassIndex inRight)
		{
			const PassRef& left = passRefs[inLeft];
			const PassRef& right = passRefs[inRight];
			if (left.batchAffinity != right.batchAffinity)
			{
				return left.batchAffinity < right.batchAffinity;
			}
			if (left.sortFactor != right.sortFactor)
			{
				return left.sortFactor < right.sortFactor;
			}
			if (left.downstreamDependCount != right.downstreamDependCount)
			{
				return left.downstreamDependCount > right.downstreamDependCount;
			}
			return inLeft < inRight;
		});
	};

	auto fineSortQueue = [&](const std::vector<PassIndex>& inPasses, QueueType inQueue) -> std::vector<PassIndex>
	{
		std::unordered_set<PassIndex> passSet(inPasses.begin(), inPasses.end());
		std::vector<uint32_t> localIndegrees(m_passes.size(), 0);
		for (PassIndex index : inPasses)
		{
			for (PassIndex next : adjacency[index])
			{
				if (passSet.find(next) != passSet.end())
				{
					++localIndegrees[next];
				}
			}
		}

		std::vector<PassIndex> ready;
		for (PassIndex index : inPasses)
		{
			if (localIndegrees[index] == 0)
			{
				ready.push_back(index);
			}
		}

		std::vector<PassIndex> sorted;
		std::optional<std::string> activeRenderPassSignature;
		while (!ready.empty())
		{
			if (inQueue == QueueType::GRAPHICS)
			{
				sortGraphicsReady(ready, activeRenderPassSignature);
			}
			else
			{
				sortReady(ready);
			}

			const PassIndex index = ready.front();
			ready.erase(ready.begin());
			sorted.push_back(index);

			if (inQueue == QueueType::GRAPHICS && isBatchableSubpass(index))
			{
				activeRenderPassSignature = passRefs[index].renderPassSignature;
			}
			else
			{
				activeRenderPassSignature.reset();
			}

			for (PassIndex next : adjacency[index])
			{
				if (passSet.find(next) == passSet.end())
				{
					continue;
				}
				CHECK_TRUE(localIndegrees[next] > 0, "Invalid render graph local dependency indegree!");
				--localIndegrees[next];
				if (localIndegrees[next] == 0)
				{
					ready.push_back(next);
				}
			}
		}

		CHECK_TRUE(sorted.size() == inPasses.size(), "Render graph submit batch has a local dependency cycle!");
		return sorted;
	};

	std::vector<PassIndex> ready;
	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
		if (passRefs[index].indegree == 0)
		{
			ready.push_back(index);
		}
	}

	std::vector<std::vector<PassIndex>> submitPassBatches;
	uint32_t scheduledPassCount = 0;
	while (!ready.empty())
	{
		// Build one coarse submit window at a time. Passes that are ready and do not need to wait
		// on a cross-queue producer from this same window stay in the current window. If a pass is
		// unlocked by such a producer, it becomes ready for the next window instead, where a queue
		// semaphore boundary can be inserted between the producer and consumer.
		std::vector<PassIndex> currentReady = std::move(ready);
		std::vector<PassIndex> nextSubmitReady;
		std::vector<PassIndex> submitPasses;
		std::vector<bool> deferToNextSubmit(m_passes.size(), false);

		while (!currentReady.empty())
		{
			sortReady(currentReady);
			const PassIndex index = currentReady.front();
			currentReady.erase(currentReady.begin());
			submitPasses.push_back(index);
			++scheduledPassCount;

			for (PassIndex next : adjacency[index])
			{
				CHECK_TRUE(passRefs[next].indegree > 0, "Invalid render graph dependency indegree!");
				const bool isQueueSyncEdge = queueSyncEdgeSet.find(_MakeEdgeKey(index, next)) != queueSyncEdgeSet.end();
				// Defer only consumers of cross-queue producers that were scheduled in this window.
				// Cross-queue producers themselves should not be held back just because they will
				// signal another queue later; starting them early preserves async overlap.
				deferToNextSubmit[next] = deferToNextSubmit[next] || isQueueSyncEdge;
				--passRefs[next].indegree;
				if (passRefs[next].indegree == 0)
				{
					if (deferToNextSubmit[next])
					{
						nextSubmitReady.push_back(next);
					}
					else
					{
						currentReady.push_back(next);
					}
				}
			}
		}

		CHECK_TRUE(!submitPasses.empty(), "Render graph submit batch cannot be empty!");
		submitPassBatches.push_back(std::move(submitPasses));
		ready = std::move(nextSubmitReady);
	}

	CHECK_TRUE(scheduledPassCount == m_passes.size(), "Render graph has a dependency cycle!");

	for (const std::vector<PassIndex>& submitPasses : submitPassBatches)
	{
		SubmitBatch submitBatch;
		std::vector<PassIndex> graphicsPasses;
		std::vector<PassIndex> computePasses;
		for (PassIndex index : submitPasses)
		{
			if (m_passes[index].queue == QueueType::GRAPHICS)
			{
				graphicsPasses.push_back(index);
			}
			else
			{
				computePasses.push_back(index);
			}
		}

		submitBatch.graphicsPasses = fineSortQueue(graphicsPasses, QueueType::GRAPHICS);
		submitBatch.computePasses = fineSortQueue(computePasses, QueueType::COMPUTE);

		std::optional<std::string> activeRenderPassSignature;
		for (PassIndex index : submitBatch.graphicsPasses)
		{
			if (isBatchableSubpass(index))
			{
				const std::string& signature = passRefs[index].renderPassSignature;
				if (!submitBatch.graphicsRenderPassBatches.empty() &&
					activeRenderPassSignature.has_value() &&
					signature == activeRenderPassSignature.value())
				{
					submitBatch.graphicsRenderPassBatches.back().push_back(index);
					m_passBatches.back().push_back(m_passes[index].name);
				}
				else
				{
					submitBatch.graphicsRenderPassBatches.push_back({ index });
					m_passBatches.push_back({ m_passes[index].name });
					activeRenderPassSignature = signature;
				}
			}
			else
			{
				submitBatch.graphicsRenderPassBatches.push_back({ index });
				m_passBatches.push_back({ m_passes[index].name });
				activeRenderPassSignature.reset();
			}

			m_sortedPasses.push_back(index);
			m_passesInExecutionOrder.push_back(m_passes[index].name);
		}

		for (PassIndex index : submitBatch.computePasses)
		{
			m_passBatches.push_back({ m_passes[index].name });
			m_sortedPasses.push_back(index);
			m_passesInExecutionOrder.push_back(m_passes[index].name);
		}

		m_submitBatches.push_back(std::move(submitBatch));
	}

	CHECK_TRUE(m_sortedPasses.size() == m_passes.size(), "Render graph has a dependency cycle!");
	m_built = true;
}
