#include "render_graph.h"

#include <algorithm>
#include <unordered_set>

RenderGraph::ImageSubresourceRange::ImageSubresourceRange(uint32_t inMipLevel, uint32_t inArrayLayer)
{
	baseMipLevel = inMipLevel;
	levelCount = 1;
	baseArrayLayer = inArrayLayer;
	layerCount = 1;
}

auto RenderGraph::ImageSubresourceRange::operator==(const ImageSubresourceRange& inOther) const -> bool
{
	return baseMipLevel == inOther.baseMipLevel &&
		levelCount == inOther.levelCount &&
		baseArrayLayer == inOther.baseArrayLayer &&
		layerCount == inOther.layerCount;
}

auto RenderGraph::ImageSubresourceRange::Overlap(const ImageSubresourceRange& inOther) const -> bool
{
	const uint32_t mipEnd = baseMipLevel + levelCount;
	const uint32_t otherMipEnd = inOther.baseMipLevel + inOther.levelCount;
	const uint32_t layerEnd = baseArrayLayer + layerCount;
	const uint32_t otherLayerEnd = inOther.baseArrayLayer + inOther.layerCount;
	return baseMipLevel < otherMipEnd &&
		inOther.baseMipLevel < mipEnd &&
		baseArrayLayer < otherLayerEnd &&
		inOther.baseArrayLayer < layerEnd;
}

auto RenderGraph::ImageSubresourceRange::Intersect(const ImageSubresourceRange& inOther) const -> ImageSubresourceRange
{
	CHECK_TRUE(Overlap(inOther), "Cannot intersect non-overlapping image subresource ranges!");

	ImageSubresourceRange result;
	result.baseMipLevel = std::max(baseMipLevel, inOther.baseMipLevel);
	const uint32_t mipEnd = std::min(
		baseMipLevel + levelCount,
		inOther.baseMipLevel + inOther.levelCount);
	result.levelCount = mipEnd - result.baseMipLevel;
	result.baseArrayLayer = std::max(baseArrayLayer, inOther.baseArrayLayer);
	const uint32_t layerEnd = std::min(
		baseArrayLayer + layerCount,
		inOther.baseArrayLayer + inOther.layerCount);
	result.layerCount = layerEnd - result.baseArrayLayer;
	return result;
}

namespace
{
	auto _MakeEdgeKey(uint32_t inBefore, uint32_t inAfter)->uint64_t
	{
		return (static_cast<uint64_t>(inBefore) << 32u) | static_cast<uint64_t>(inAfter);
	}

	template <typename FunctionType>
	void _ForEachImageSubresource(
		const RenderGraph::ImageSubresourceRange& inRange,
		FunctionType inFunction)
	{
		for (uint32_t layer = inRange.baseArrayLayer; layer < inRange.baseArrayLayer + inRange.layerCount; ++layer)
		{
			for (uint32_t mip = inRange.baseMipLevel; mip < inRange.baseMipLevel + inRange.levelCount; ++mip)
			{
				inFunction(mip, layer);
			}
		}
	}

	auto _GetImageSubresourceIndex(uint32_t inMipLevel, uint32_t inArrayLayer, uint32_t inMipLevelCount)->uint32_t
	{
		return inArrayLayer * inMipLevelCount + inMipLevel;
	}

}

auto RenderGraph::_GetQueueType(PassType inType) -> QueueType
{
	switch (inType)
	{
	case PassType::COMPUTE:
		return QueueType::COMPUTE;
	case PassType::GRAPHICS:
	case PassType::RENDER_PASS:
	case PassType::SUBPASS:
		return QueueType::GRAPHICS;
	default:
		CHECK_TRUE(false, "Unsupported render graph pass type!");
		return QueueType::GRAPHICS;
	}
}

auto RenderGraph::ImageInfo::GetWholeSubresourceRange() const -> ImageSubresourceRange
{
	ImageSubresourceRange range;
	range.baseMipLevel = 0;
	range.levelCount = m_mipLevels;
	range.baseArrayLayer = 0;
	range.layerCount = m_arrayLayers;
	return range;
}

auto RenderGraph::ImageInfo::NormalizeSubresourceRange(const ImageSubresourceRange& inRange) const -> ImageSubresourceRange
{
	const ImageSubresourceRange wholeRange = GetWholeSubresourceRange();

	ImageSubresourceRange result = inRange;
	if (result.levelCount == 0)
	{
		result.levelCount = wholeRange.levelCount - result.baseMipLevel;
	}
	if (result.layerCount == 0)
	{
		result.layerCount = wholeRange.layerCount - result.baseArrayLayer;
	}

	CHECK_TRUE(result.levelCount > 0, "Render graph image subresource range mip count must be greater than 0!");
	CHECK_TRUE(result.layerCount > 0, "Render graph image subresource range layer count must be greater than 0!");
	CHECK_TRUE(
		result.baseMipLevel < wholeRange.levelCount &&
		result.baseMipLevel + result.levelCount <= wholeRange.levelCount,
		"Render graph image subresource mip range is out of bounds!");
	CHECK_TRUE(
		result.baseArrayLayer < wholeRange.layerCount &&
		result.baseArrayLayer + result.layerCount <= wholeRange.layerCount,
		"Render graph image subresource array layer range is out of bounds!");
	return result;
}

auto RenderGraph::BuildResult::IsValid() const -> bool
{
	return valid;
}

auto RenderGraph::BuildResult::GetPassCount() const -> size_t
{
	return passes.size();
}

auto RenderGraph::BuildResult::GetPass(PassIndex inPassIndex) const -> const PassRecord&
{
	CHECK_TRUE(inPassIndex < passes.size(), "Invalid render graph build result pass index!");
	return passes[inPassIndex];
}

auto RenderGraph::BuildResult::GetSubmitBatchCount() const -> size_t
{
	return submitBatches.size();
}

auto RenderGraph::BuildResult::GetSubmitBatch(uint32_t inSubmitIndex) const -> const SubmitBatch&
{
	CHECK_TRUE(inSubmitIndex < submitBatches.size(), "Invalid render graph build result submit batch index!");
	return submitBatches[inSubmitIndex];
}

auto RenderGraph::BuildResult::GetBufferCount() const -> size_t
{
	return buffers.size();
}

auto RenderGraph::BuildResult::GetImageCount() const -> size_t
{
	return images.size();
}

auto RenderGraph::BuildResult::GetBufferInfo(BufferIndex inBufferIndex) const -> const BufferInfo&
{
	CHECK_TRUE(inBufferIndex < buffers.size(), "Invalid render graph build result buffer index!");
	return buffers[inBufferIndex];
}

auto RenderGraph::BuildResult::GetImageInfo(ImageIndex inImageIndex) const -> const ImageInfo&
{
	CHECK_TRUE(inImageIndex < images.size(), "Invalid render graph build result image index!");
	return images[inImageIndex];
}

auto RenderGraph::BuildResult::GetBufferIndex(const std::string& inName) const -> BufferIndex
{
	const auto iter = nameToBuffer.find(inName);
	CHECK_TRUE(iter != nameToBuffer.end(), "Render graph build result buffer name is not registered!");
	return iter->second;
}

auto RenderGraph::BuildResult::GetImageIndex(const std::string& inName) const -> ImageIndex
{
	const auto iter = nameToImage.find(inName);
	CHECK_TRUE(iter != nameToImage.end(), "Render graph build result image name is not registered!");
	return iter->second;
}

auto RenderGraph::BuildResult::GetPassIndex(const std::string& inName) const -> PassIndex
{
	const auto iter = nameToPass.find(inName);
	CHECK_TRUE(iter != nameToPass.end(), "Render graph build result pass name is not registered!");
	return iter->second;
}

auto RenderGraph::BuildResult::GetImageAccessState(
	PassIndex inPassIndex,
	ImageIndex inImageIndex,
	const ImageSubresourceRange& inSubresourceRange) const -> AccessState
{
	CHECK_TRUE(inPassIndex < passes.size(), "Invalid render graph image usage pass index!");

	AccessState state;
	bool found = false;
	const PassRecord& pass = passes[inPassIndex];
	for (const ImageUsage& usage : pass.imageUsages)
	{
		if (usage.imageIndex != inImageIndex)
		{
			continue;
		}
		if (!usage.subresourceRange.Overlap(inSubresourceRange))
		{
			continue;
		}

		if (!found)
		{
			state.layout = usage.layout;
			found = true;
		}
		else
		{
			CHECK_TRUE(state.layout == usage.layout, "Render graph image has multiple layouts in the same pass!");
		}

		state.stage |= usage.stage;
		state.access |= usage.access;
		state.reads |= usage.reads;
		state.writes |= usage.writes;
	}

	CHECK_TRUE(found, "Render graph image usage is missing!");
	CHECK_TRUE(state.stage != 0, "Render graph image usage stage cannot be empty!");
	return state;
}

auto RenderGraph::BuildResult::GetBufferAccessState(PassIndex inPassIndex, BufferIndex inBufferIndex) const -> AccessState
{
	CHECK_TRUE(inPassIndex < passes.size(), "Invalid render graph buffer usage pass index!");

	AccessState state;
	bool found = false;
	const PassRecord& pass = passes[inPassIndex];
	for (const BufferUsage& usage : pass.bufferUsages)
	{
		if (usage.bufferIndex != inBufferIndex)
		{
			continue;
		}

		state.stage |= usage.stage;
		state.access |= usage.access;
		state.reads |= usage.reads;
		state.writes |= usage.writes;
		found = true;
	}

	CHECK_TRUE(found, "Render graph buffer usage is missing!");
	CHECK_TRUE(state.stage != 0, "Render graph buffer usage stage cannot be empty!");
	return state;
}

auto RenderGraph::_NeedsMemoryDependency(HazardType inHazard, VkImageLayout inOldLayout, VkImageLayout inNewLayout) -> bool
{
	if (inOldLayout != inNewLayout)
	{
		return true;
	}

	return inHazard != HazardType::WAR;
}

auto RenderGraph::BufferInfo::IsAliasCompatible(const BufferInfo& inOther) const -> bool
{
	return m_size == inOther.m_size &&
		m_usage == inOther.m_usage &&
		m_memoryProperty == inOther.m_memoryProperty &&
		m_sharingMode == inOther.m_sharingMode &&
		m_optAlignment == inOther.m_optAlignment;
}

auto RenderGraph::ImageInfo::IsAliasCompatible(const ImageInfo& inOther) const -> bool
{
	return m_usage == inOther.m_usage &&
		m_type == inOther.m_type &&
		m_optWidth == inOther.m_optWidth &&
		m_optHeight == inOther.m_optHeight &&
		m_optDepth == inOther.m_optDepth &&
		m_mipLevels == inOther.m_mipLevels &&
		m_arrayLayers == inOther.m_arrayLayers &&
		m_optFormat == inOther.m_optFormat &&
		m_optTiling == inOther.m_optTiling &&
		m_optMemoryProperty == inOther.m_optMemoryProperty &&
		m_optSampleCount == inOther.m_optSampleCount;
}

void RenderGraph::PassInfo::SetNeverCull(bool inNeverCull)
{
	m_neverCull = inNeverCull;
}

void RenderGraph::PassInfo::AddSampledImage(
	const std::string& inName,
	VkPipelineStageFlags2 inReadStage,
	const ImageSubresourceRange& inRange)
{
	CHECK_TRUE(!inName.empty(), "Sampled image name cannot be empty!");
	CHECK_TRUE(inReadStage != 0, "Sampled image read stage cannot be empty!");

	ImageUsage usage;
	usage.image = inName;
	usage.subresourceRange = inRange;
	usage.type = ResourceUsageType::SAMPLED_IMAGE;
	usage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	usage.stage = inReadStage;
	usage.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	usage.reads = true;
	m_imageUsages.push_back(usage);
}

void RenderGraph::PassInfo::AddStorageImage(
	const std::string& inName,
	VkPipelineStageFlags2 inWriteStage,
	const ImageSubresourceRange& inRange)
{
	CHECK_TRUE(!inName.empty(), "Storage image name cannot be empty!");
	CHECK_TRUE(inWriteStage != 0, "Storage image stage cannot be empty!");

	ImageUsage usage;
	usage.image = inName;
	usage.subresourceRange = inRange;
	usage.type = ResourceUsageType::STORAGE_IMAGE;
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
	usage.stage = inWriteStage;
	usage.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
	usage.reads = true;
	usage.writes = true;
	m_bufferUsages.push_back(usage);
}

void RenderGraph::AttachmentPassInfo::AddColorAttachment(
	uint32_t inLocation,
	const std::string& inName,
	const AttachmentInfo& inAttachmentInfo)
{
	CHECK_TRUE(!inName.empty(), "Color attachment image name cannot be empty!");
	for (const ImageUsage& existing : m_imageUsages)
	{
		CHECK_TRUE(
			existing.type != ResourceUsageType::COLOR_ATTACHMENT || existing.attachmentSlot != inLocation,
			"Color attachment location is already used!");
	}

	ImageUsage usage;
	usage.image = inName;
	usage.subresourceRange = inAttachmentInfo.m_subresourceRange;
	usage.type = ResourceUsageType::COLOR_ATTACHMENT;
	usage.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	usage.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	usage.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	usage.reads = inAttachmentInfo.m_loadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
	usage.writes = true;
	if (usage.reads)
	{
		usage.access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
	}
	usage.attachmentSlot = inLocation;
	usage.loadOp = inAttachmentInfo.m_loadOp;
	usage.storeOp = inAttachmentInfo.m_storeOp;
	usage.clearColor = inAttachmentInfo.m_clearColor;
	m_imageUsages.push_back(usage);
}

void RenderGraph::AttachmentPassInfo::SetDepthStencilAttachment(
	const std::string& inName,
	const AttachmentInfo& inAttachmentInfo)
{
	CHECK_TRUE(!inName.empty(), "Depth stencil attachment image name cannot be empty!");
	for (const ImageUsage& existing : m_imageUsages)
	{
		CHECK_TRUE(
			existing.type != ResourceUsageType::DEPTH_STENCIL_ATTACHMENT,
			"A pass can only have one depth stencil attachment!");
	}

	ImageUsage usage;
	usage.image = inName;
	usage.subresourceRange = inAttachmentInfo.m_subresourceRange;
	usage.type = ResourceUsageType::DEPTH_STENCIL_ATTACHMENT;
	usage.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	usage.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	usage.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	usage.reads = inAttachmentInfo.m_loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ||
		inAttachmentInfo.m_stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
	usage.writes = true;
	if (usage.reads)
	{
		usage.access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	}
	usage.loadOp = inAttachmentInfo.m_loadOp;
	usage.storeOp = inAttachmentInfo.m_storeOp;
	usage.stencilLoadOp = inAttachmentInfo.m_stencilLoadOp;
	usage.stencilStoreOp = inAttachmentInfo.m_stencilStoreOp;
	usage.clearDepthStencil = inAttachmentInfo.m_clearDepthStencil;
	m_imageUsages.push_back(usage);
}

void RenderGraph::AttachmentPassInfo::SetResolveAttachment(
	uint32_t inLocation,
	const std::string& inName,
	const AttachmentInfo& inAttachmentInfo)
{
	CHECK_TRUE(!inName.empty(), "Resolve attachment image name cannot be empty!");
	CHECK_TRUE(
		inAttachmentInfo.m_loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
		inAttachmentInfo.m_stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		"Resolve attachments cannot load or clear previous contents!");
	for (const ImageUsage& existing : m_imageUsages)
	{
		CHECK_TRUE(
			existing.type != ResourceUsageType::RESOLVE_ATTACHMENT || existing.attachmentSlot != inLocation,
			"Resolve attachment location is already used!");
	}

	ImageUsage usage;
	usage.image = inName;
	usage.subresourceRange = inAttachmentInfo.m_subresourceRange;
	usage.type = ResourceUsageType::RESOLVE_ATTACHMENT;
	usage.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	usage.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	usage.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	usage.writes = true;
	usage.attachmentSlot = inLocation;
	usage.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	usage.storeOp = inAttachmentInfo.m_storeOp;
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
	m_buildResult = {};
}

auto RenderGraph::_CreateBuildContext() const -> BuildContext
{
	BuildContext context;
	context.passes = m_passes;
	context.imageInfos = m_images;
	context.bufferInfos = m_buffers;
	context.nameToImage = m_nameToImage;
	context.nameToBuffer = m_nameToBuffer;
	context.extraDependencies = m_extraDependencies;
	context.enableResourceAliasing = m_enableResourceAliasing;
	context.logicalToPhysicalImages.resize(context.imageInfos.size());
	context.logicalToPhysicalBuffers.resize(context.bufferInfos.size());
	for (ImageIndex index = 0; index < context.logicalToPhysicalImages.size(); ++index)
	{
		context.logicalToPhysicalImages[index] = index;
	}
	for (BufferIndex index = 0; index < context.logicalToPhysicalBuffers.size(); ++index)
	{
		context.logicalToPhysicalBuffers[index] = index;
	}

	for (const PassRecord& pass : context.passes)
	{
		for (const ImageUsage& usage : pass.imageUsages)
		{
			const auto imageIter = context.nameToImage.find(usage.image);
			CHECK_TRUE(imageIter != context.nameToImage.end(), "Render graph image name is not registered!");
			VkImageUsageFlags inferredUsage = 0;
			switch (usage.type)
			{
			case ResourceUsageType::SAMPLED_IMAGE:
				inferredUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
				break;
			case ResourceUsageType::STORAGE_IMAGE:
				inferredUsage = VK_IMAGE_USAGE_STORAGE_BIT;
				break;
			case ResourceUsageType::COLOR_ATTACHMENT:
			case ResourceUsageType::RESOLVE_ATTACHMENT:
				inferredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				break;
			case ResourceUsageType::DEPTH_STENCIL_ATTACHMENT:
				inferredUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				break;
			default:
				break;
			}
			context.imageInfos[imageIter->second].m_usage |= inferredUsage;
		}
	}
	return context;
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
	record.neverCull = inPassInfo.m_neverCull;
	record.imageUsages = inPassInfo.m_imageUsages;
	record.bufferUsages = inPassInfo.m_bufferUsages;

	if (record.type == PassType::RENDER_PASS || record.type == PassType::SUBPASS)
	{
		bool hasAttachment = false;
		std::unordered_set<uint32_t> colorSlots;
		std::unordered_set<uint32_t> resolveSlots;
		for (const ImageUsage& usage : record.imageUsages)
		{
			if (usage.type == ResourceUsageType::COLOR_ATTACHMENT)
			{
				hasAttachment = true;
				colorSlots.insert(usage.attachmentSlot);
			}
			else if (usage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT)
			{
				hasAttachment = true;
			}
			else if (usage.type == ResourceUsageType::RESOLVE_ATTACHMENT)
			{
				hasAttachment = true;
				resolveSlots.insert(usage.attachmentSlot);
			}
		}
		CHECK_TRUE(hasAttachment, "A graph-managed render pass must declare at least one attachment!");
		for (uint32_t resolveSlot : resolveSlots)
		{
			CHECK_TRUE(colorSlots.contains(resolveSlot), "Resolve attachment has no color attachment at the same location!");
		}
	}

	for (ImageUsage& usage : record.imageUsages)
	{
		_GetImageIndex(usage.image);
	}
	for (BufferUsage& usage : record.bufferUsages)
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

void RenderGraph::EnableResourceAliasing(bool inEnable)
{
	if (m_enableResourceAliasing == inEnable)
	{
		return;
	}

	m_enableResourceAliasing = inEnable;
	_InvalidateBuild();
}

void RenderGraph::_LinkPasses(BuildContext& inoutContext) const
{
	std::unordered_set<uint64_t> edgeSet;
	inoutContext.images.assign(inoutContext.imageInfos.size(), {});
	inoutContext.buffers.assign(inoutContext.bufferInfos.size(), {});
	for (PassRecord& pass : inoutContext.passes)
	{
		pass.adjacency.clear();
		for (ImageUsage& usage : pass.imageUsages)
		{
			const auto imageIter = inoutContext.nameToImage.find(usage.image);
			CHECK_TRUE(imageIter != inoutContext.nameToImage.end(), "Render graph image name is not registered!");
			const ImageIndex imageIndex = imageIter->second;
			usage.imageIndex = imageIndex;
			CHECK_TRUE(imageIndex < inoutContext.imageInfos.size(), "Render graph image index is invalid!");
			usage.subresourceRange = inoutContext.imageInfos[imageIndex].NormalizeSubresourceRange(usage.subresourceRange);
		}
		for (BufferUsage& usage : pass.bufferUsages)
		{
			const auto bufferIter = inoutContext.nameToBuffer.find(usage.buffer);
			CHECK_TRUE(bufferIter != inoutContext.nameToBuffer.end(), "Render graph buffer name is not registered!");
			usage.bufferIndex = bufferIter->second;
			CHECK_TRUE(usage.bufferIndex < inoutContext.bufferInfos.size(), "Render graph buffer index is invalid!");
		}
	}

	auto funcAddEdge = [&](PassIndex inBefore, PassIndex inAfter)
	{
		CHECK_TRUE(inBefore != inAfter, "Render graph dependency cycle detected through self edge!");
		const uint64_t key = _MakeEdgeKey(inBefore, inAfter);
		if (!edgeSet.insert(key).second)
		{
			return;
		}

		inoutContext.passes[inBefore].adjacency.push_back(inAfter);
	};

	inoutContext.queueSyncPlans.clear();

	for (PassIndex passIndex = 0; passIndex < inoutContext.passes.size(); ++passIndex)
	{
		const PassRecord& pass = inoutContext.passes[passIndex];
		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = usage.imageIndex;
			inoutContext.images[imageIndex].usages.push_back(BuildContext::ImageUsageRef{ passIndex, usage });
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = usage.bufferIndex;
			inoutContext.buffers[bufferIndex].usages.push_back(BuildContext::BufferUsageRef{ passIndex, usage });
		}
	}

	for (ImageIndex imageIndex = 0; imageIndex < inoutContext.images.size(); ++imageIndex)
	{
		const bool hasExternalInitialState = inoutContext.imageInfos[imageIndex].m_external;
		const auto& refs = inoutContext.images[imageIndex].usages;

		const ImageSubresourceRange imageRange = inoutContext.imageInfos[imageIndex].GetWholeSubresourceRange();
		_ForEachImageSubresource(imageRange, [&](uint32_t inMipLevel, uint32_t inArrayLayer)
		{
			const ImageSubresourceRange subresourceRange(inMipLevel, inArrayLayer);
			std::optional<BuildContext::ImageUsageRef> lastWriter;
			std::vector<BuildContext::ImageUsageRef> pendingReaders;
			std::optional<BuildContext::ImageUsageRef> firstWriter;

			for (const BuildContext::ImageUsageRef& ref : refs)
			{
				if (ref.writes && ref.subresourceRange.Overlap(subresourceRange))
				{
					firstWriter = ref;
					break;
				}
			}

			for (const BuildContext::ImageUsageRef& ref : refs)
			{
				if (!ref.subresourceRange.Overlap(subresourceRange))
				{
					continue;
				}

				if (ref.reads)
				{
					if (!lastWriter.has_value() && !hasExternalInitialState)
					{
						if (ref.type != ResourceUsageType::COLOR_ATTACHMENT &&
							ref.type != ResourceUsageType::DEPTH_STENCIL_ATTACHMENT &&
							ref.type != ResourceUsageType::RESOLVE_ATTACHMENT &&
							!ref.writes &&
							!firstWriter.has_value())
						{
							CHECK_TRUE(false, "Internal render graph image cannot be read before it is written!");
						}
						CHECK_TRUE(
							ref.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
							ref.stencilLoadOp != VK_ATTACHMENT_LOAD_OP_LOAD,
							"Internal render graph attachment cannot use LOAD before it is written!");
					}

					if (lastWriter.has_value() && lastWriter->pass != ref.pass)
					{
						funcAddEdge(lastWriter->pass, ref.pass);
					}
					else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
					{
						funcAddEdge(firstWriter->pass, ref.pass);
					}
					else if (hasExternalInitialState)
					{
						pendingReaders.push_back(ref);
					}
				}

				if (ref.writes)
				{
					if (lastWriter.has_value() && !ref.reads && lastWriter->pass != ref.pass)
					{
						funcAddEdge(lastWriter->pass, ref.pass);
					}

					for (const BuildContext::ImageUsageRef& reader : pendingReaders)
					{
						if (reader.pass != ref.pass)
						{
							funcAddEdge(reader.pass, ref.pass);
						}
					}

					pendingReaders.clear();
					lastWriter = ref;
				}
				else if (ref.reads && lastWriter.has_value())
				{
					pendingReaders.push_back(ref);
				}
			}
		});
	}

	for (BufferIndex bufferIndex = 0; bufferIndex < inoutContext.buffers.size(); ++bufferIndex)
	{
		const bool hasExternalInitialState = inoutContext.bufferInfos[bufferIndex].m_external;
		const auto& refs = inoutContext.buffers[bufferIndex].usages;
		std::optional<BuildContext::BufferUsageRef> lastWriter;
		std::vector<BuildContext::BufferUsageRef> pendingReaders;
		std::optional<BuildContext::BufferUsageRef> firstWriter;

		for (const BuildContext::BufferUsageRef& ref : refs)
		{
			if (ref.writes)
			{
				firstWriter = ref;
				break;
			}
		}

		for (const BuildContext::BufferUsageRef& ref : refs)
		{
			if (ref.reads)
			{
				if (lastWriter.has_value() && lastWriter->pass != ref.pass)
				{
					funcAddEdge(lastWriter->pass, ref.pass);
				}
				else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
				{
					funcAddEdge(firstWriter->pass, ref.pass);
				}
				else if (hasExternalInitialState)
				{
					pendingReaders.push_back(ref);
				}
			}

			if (ref.writes)
			{
				if (lastWriter.has_value() && !ref.reads && lastWriter->pass != ref.pass)
				{
					funcAddEdge(lastWriter->pass, ref.pass);
				}

				for (const BuildContext::BufferUsageRef& reader : pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						funcAddEdge(reader.pass, ref.pass);
					}
				}

				pendingReaders.clear();
				lastWriter = ref;
			}
			else if (ref.reads && lastWriter.has_value())
			{
				pendingReaders.push_back(ref);
			}
		}
	}

	for (const DependencyEdge& edge : inoutContext.extraDependencies)
	{
		funcAddEdge(edge.before, edge.after);
	}
}

void RenderGraph::_ResolveDependency(BuildContext& inContext) const
{
	inContext.queueSyncPlans.clear();

	for (PassIndex beforeIndex = 0; beforeIndex < inContext.passes.size(); ++beforeIndex)
	{
		const PassRecord& before = inContext.passes[beforeIndex];
		for (PassIndex afterIndex : before.adjacency)
		{
			const PassRecord& after = inContext.passes[afterIndex];
			if (before.queue == after.queue)
			{
				continue;
			}

			QueueSyncPlan plan;
			plan.before = beforeIndex;
			plan.after = afterIndex;
		plan.srcQueue = before.queue;
		plan.dstQueue = after.queue;
		inContext.queueSyncPlans.push_back(plan);
		}
	}
}

void RenderGraph::_CullPasses(BuildContext& inContext) const
{
	for (PassRecord& pass : inContext.passes)
	{
		pass.active = false;
	}

	std::vector<std::vector<PassIndex>> reverseAdjacency(inContext.passes.size());
	for (PassIndex beforeIndex = 0; beforeIndex < inContext.passes.size(); ++beforeIndex)
	{
		for (PassIndex afterIndex : inContext.passes[beforeIndex].adjacency)
		{
			CHECK_TRUE(afterIndex < inContext.passes.size(), "Invalid render graph dependency edge!");
			reverseAdjacency[afterIndex].push_back(beforeIndex);
		}
	}

	std::vector<PassIndex> stack;
	auto funcMarkActive = [&](PassIndex inPassIndex)
	{
		CHECK_TRUE(inPassIndex < inContext.passes.size(), "Invalid render graph cull root pass!");
		if (inContext.passes[inPassIndex].active)
		{
			return;
		}

		inContext.passes[inPassIndex].active = true;
		stack.push_back(inPassIndex);
	};

	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.buffers.size(); ++bufferIndex)
	{
		if (!inContext.bufferInfos[bufferIndex].m_external)
		{
			continue;
		}

		for (const BuildContext::BufferUsageRef& ref : inContext.buffers[bufferIndex].usages)
		{
			funcMarkActive(ref.pass);
		}
	}

	for (ImageIndex imageIndex = 0; imageIndex < inContext.images.size(); ++imageIndex)
	{
		if (!inContext.imageInfos[imageIndex].m_external)
		{
			continue;
		}

		for (const BuildContext::ImageUsageRef& ref : inContext.images[imageIndex].usages)
		{
			funcMarkActive(ref.pass);
		}
	}

	for (PassIndex passIndex = 0; passIndex < inContext.passes.size(); ++passIndex)
	{
		if (inContext.passes[passIndex].neverCull)
		{
			funcMarkActive(passIndex);
		}
	}

	while (!stack.empty())
	{
		const PassIndex passIndex = stack.back();
		stack.pop_back();

		for (PassIndex dependency : reverseAdjacency[passIndex])
		{
			funcMarkActive(dependency);
		}
	}

	auto funcIsActive = [&](PassIndex inPassIndex)->bool
	{
		return inPassIndex < inContext.passes.size() && inContext.passes[inPassIndex].active;
	};

	for (PassRecord& pass : inContext.passes)
	{
		pass.adjacency.erase(
			std::remove_if(
				pass.adjacency.begin(),
				pass.adjacency.end(),
				[&](PassIndex inNextPass)
				{
					return !funcIsActive(inNextPass);
				}),
			pass.adjacency.end());
	}
	inContext.queueSyncPlans.clear();
	for (BuildContext::ImageRecord& image : inContext.images)
	{
		auto& refs = image.usages;
		refs.erase(
			std::remove_if(
				refs.begin(),
				refs.end(),
				[&](const BuildContext::ImageUsageRef& inRef)
				{
					return !funcIsActive(inRef.pass);
				}),
			refs.end());
	}

	for (BuildContext::BufferRecord& buffer : inContext.buffers)
	{
		auto& refs = buffer.usages;
		refs.erase(
			std::remove_if(
				refs.begin(),
				refs.end(),
				[&](const BuildContext::BufferUsageRef& inRef)
				{
					return !funcIsActive(inRef.pass);
				}),
			refs.end());
	}
}

void RenderGraph::_BuildScheduleAndBatches(BuildContext& inContext, BuildResult& inoutResult) const
{
	struct RenderPassMergeInfo
	{
		std::vector<std::string> attachmentViews;
		std::vector<std::string> colorAttachmentViews;
		std::vector<std::string> depthAttachmentViews;
		std::vector<ImageIndex> attachmentImages;
		std::vector<ImageIndex> nonAttachmentImages;
		std::vector<BufferIndex> nonAttachmentBuffers;
		std::vector<ImageIndex> writtenImages;
		std::vector<ImageIndex> writtenNonAttachmentImages;
		std::vector<BufferIndex> writtenBuffers;
	};

	struct PassBuildRef
	{
		uint32_t indegree = 0;
		uint32_t sortFactor = 100u;
		uint32_t downstreamStageRank = 100u;
		uint32_t downstreamDependCount = 0;
		RenderPassMergeInfo renderPassMergeInfo;
		uint32_t batchAffinity = 1u;
	};

	constexpr uint32_t DEFAULT_STAGE_RANK = 100u;
	auto funcGetStageRank = [](VkPipelineStageFlags2 inStage) -> uint32_t
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

	auto funcGetPassStageRank = [&](PassIndex inPassIndex) -> uint32_t
	{
		const PassRecord& pass = inContext.passes[inPassIndex];
		uint32_t rank = DEFAULT_STAGE_RANK;

		for (const ImageUsage& usage : pass.imageUsages)
		{
			rank = std::min(rank, funcGetStageRank(usage.stage));
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			rank = std::min(rank, funcGetStageRank(usage.stage));
		}

		if (rank == DEFAULT_STAGE_RANK)
		{
			rank = pass.type == PassType::COMPUTE ? funcGetStageRank(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) : funcGetStageRank(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
		}

		return rank;
	};

	auto funcIsBatchableSubpass = [&](PassIndex inPassIndex) -> bool
	{
		const PassRecord& pass = inContext.passes[inPassIndex];
		return pass.type == PassType::SUBPASS;
	};

	auto funcSortUnique = [](auto& inValues)
	{
		std::sort(inValues.begin(), inValues.end());
		inValues.erase(std::unique(inValues.begin(), inValues.end()), inValues.end());
	};

	auto funcBuildRenderPassMergeInfo = [&](PassIndex inPassIndex) -> RenderPassMergeInfo
	{
		const PassRecord& pass = inContext.passes[inPassIndex];
		RenderPassMergeInfo info;
		if (!funcIsBatchableSubpass(inPassIndex))
		{
			return info;
		}

		auto funcMakeViewToken = [](const ImageUsage& inUsage)->std::string
		{
			const ImageSubresourceRange& range = inUsage.subresourceRange;
			return std::to_string(inUsage.imageIndex) + ":" +
				std::to_string(range.baseMipLevel) + ":" + std::to_string(range.levelCount) + ":" +
				std::to_string(range.baseArrayLayer) + ":" + std::to_string(range.layerCount);
		};

		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = usage.imageIndex;
			if (usage.writes)
			{
				info.writtenImages.push_back(imageIndex);
				if (usage.type != ResourceUsageType::COLOR_ATTACHMENT &&
					usage.type != ResourceUsageType::DEPTH_STENCIL_ATTACHMENT &&
					usage.type != ResourceUsageType::RESOLVE_ATTACHMENT)
				{
					info.writtenNonAttachmentImages.push_back(imageIndex);
				}
			}

			if (usage.type == ResourceUsageType::COLOR_ATTACHMENT ||
					usage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT ||
					usage.type == ResourceUsageType::RESOLVE_ATTACHMENT)
			{
				info.attachmentImages.push_back(imageIndex);
				// Attachment identity includes the normalized mip/layer range. Roles are checked
				// pairwise when a candidate is considered for the current managed group.
				const std::string viewToken = funcMakeViewToken(usage);
				info.attachmentViews.push_back(viewToken);

				if (usage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT)
				{
					info.depthAttachmentViews.push_back(viewToken);
				}
				else
				{
					info.colorAttachmentViews.push_back(viewToken);
				}
			}
			else
			{
				info.nonAttachmentImages.push_back(imageIndex);
			}
		}

		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = usage.bufferIndex;
			info.nonAttachmentBuffers.push_back(bufferIndex);
			if (usage.writes)
			{
				info.writtenBuffers.push_back(bufferIndex);
			}
		}

		funcSortUnique(info.attachmentViews);
		funcSortUnique(info.colorAttachmentViews);
		funcSortUnique(info.depthAttachmentViews);
		funcSortUnique(info.attachmentImages);
		funcSortUnique(info.nonAttachmentImages);
		funcSortUnique(info.nonAttachmentBuffers);
		funcSortUnique(info.writtenImages);
		funcSortUnique(info.writtenNonAttachmentImages);
		funcSortUnique(info.writtenBuffers);
		return info;
	};

	auto funcHasIntersection = [](const auto& inLeft, const auto& inRight) -> bool
	{
		auto left = inLeft.begin();
		auto right = inRight.begin();
		while (left != inLeft.end() && right != inRight.end())
		{
			if (*left < *right)
			{
				++left;
			}
			else if (*right < *left)
			{
				++right;
			}
			else
			{
				return true;
			}
		}
		return false;
	};

	std::vector<PassBuildRef> passRefs(inContext.passes.size());
	std::vector<std::vector<PassIndex>> submitPassBatches;

	auto funcCanMergeSubpasses = [&](PassIndex inPrev, PassIndex inNext) -> bool
	{
		if (!funcIsBatchableSubpass(inPrev) || !funcIsBatchableSubpass(inNext))
		{
			return false;
		}

		if (inContext.passes[inPrev].queue != QueueType::GRAPHICS ||
			inContext.passes[inNext].queue != QueueType::GRAPHICS ||
			inContext.passes[inPrev].queue != inContext.passes[inNext].queue)
		{
			return false;
		}

		const RenderPassMergeInfo& prev = passRefs[inPrev].renderPassMergeInfo;
		const RenderPassMergeInfo& next = passRefs[inNext].renderPassMergeInfo;
		auto funcIsAttachmentUsage = [](const ImageUsage& inUsage)->bool
		{
			return inUsage.type == ResourceUsageType::COLOR_ATTACHMENT ||
				inUsage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT ||
				inUsage.type == ResourceUsageType::RESOLVE_ATTACHMENT;
		};
		for (const ImageUsage& prevUsage : inContext.passes[inPrev].imageUsages)
		{
			if (!funcIsAttachmentUsage(prevUsage))
			{
				continue;
			}
			for (const ImageUsage& nextUsage : inContext.passes[inNext].imageUsages)
			{
				if (!funcIsAttachmentUsage(nextUsage) || prevUsage.imageIndex != nextUsage.imageIndex ||
					!prevUsage.subresourceRange.Overlap(nextUsage.subresourceRange))
				{
					continue;
				}
				if (prevUsage.subresourceRange != nextUsage.subresourceRange || prevUsage.type != nextUsage.type)
				{
					return false;
				}
			}
		}

		if (!prev.depthAttachmentViews.empty() &&
			!next.depthAttachmentViews.empty() &&
			prev.depthAttachmentViews != next.depthAttachmentViews)
		{
			return false;
		}

		if (funcHasIntersection(prev.depthAttachmentViews, next.colorAttachmentViews) ||
			funcHasIntersection(prev.colorAttachmentViews, next.depthAttachmentViews))
		{
			return false;
		}

		for (const ImageUsage& nextUsage : inContext.passes[inNext].imageUsages)
		{
			if (nextUsage.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR &&
				nextUsage.stencilLoadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
			{
				continue;
			}
			for (const ImageUsage& prevUsage : inContext.passes[inPrev].imageUsages)
			{
				const bool prevIsAttachment =
					prevUsage.type == ResourceUsageType::COLOR_ATTACHMENT ||
					prevUsage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT ||
					prevUsage.type == ResourceUsageType::RESOLVE_ATTACHMENT;
				if (prevIsAttachment && prevUsage.imageIndex == nextUsage.imageIndex &&
					prevUsage.subresourceRange == nextUsage.subresourceRange)
				{
					return false;
				}
			}
		}

		if (funcHasIntersection(next.nonAttachmentImages, prev.writtenImages))
		{
			return false;
		}

		if (funcHasIntersection(next.writtenNonAttachmentImages, prev.nonAttachmentImages))
		{
			return false;
		}

		if (funcHasIntersection(next.attachmentImages, prev.nonAttachmentImages))
		{
			return false;
		}

		if (funcHasIntersection(next.attachmentImages, prev.writtenNonAttachmentImages))
		{
			return false;
		}

		if (funcHasIntersection(next.nonAttachmentBuffers, prev.writtenBuffers))
		{
			return false;
		}

		if (funcHasIntersection(next.writtenBuffers, prev.nonAttachmentBuffers))
		{
			return false;
		}

		return true;
	};

	auto funcCanMergeIntoRenderPassBatch = [&](const std::vector<PassIndex>& inBatch, PassIndex inCandidate) -> bool
	{
		if (!funcIsBatchableSubpass(inCandidate) || inBatch.empty())
		{
			return false;
		}

		for (PassIndex index : inBatch)
		{
			if (!funcCanMergeSubpasses(index, inCandidate))
			{
				return false;
			}
		}
		return true;
	};

	auto funcHasAttachmentOverlapWithBatch = [&](const std::vector<PassIndex>& inBatch, PassIndex inCandidate) -> bool
	{
		const RenderPassMergeInfo& candidate = passRefs[inCandidate].renderPassMergeInfo;
		for (PassIndex index : inBatch)
		{
			if (funcHasIntersection(passRefs[index].renderPassMergeInfo.attachmentViews, candidate.attachmentViews))
			{
				return true;
			}
		}
		return false;
	};

	for (PassIndex beforeIndex = 0; beforeIndex < inContext.passes.size(); ++beforeIndex)
	{
		for (PassIndex afterIndex : inContext.passes[beforeIndex].adjacency)
		{
			++passRefs[afterIndex].indegree;
		}
	}

	for (PassIndex index = 0; index < inContext.passes.size(); ++index)
	{
		PassBuildRef& ref = passRefs[index];
		ref.renderPassMergeInfo = funcBuildRenderPassMergeInfo(index);
		ref.downstreamDependCount = static_cast<uint32_t>(inContext.passes[index].adjacency.size());

		for (PassIndex next : inContext.passes[index].adjacency)
		{
			ref.downstreamStageRank = std::min(ref.downstreamStageRank, funcGetPassStageRank(next));
		}
		if (ref.downstreamStageRank == DEFAULT_STAGE_RANK)
		{
			ref.downstreamStageRank = funcGetPassStageRank(index);
		}
		ref.sortFactor = ref.downstreamStageRank;
	}

	auto funcSortReady = [&](std::vector<PassIndex>& inReady)
	{
		std::stable_sort(inReady.begin(), inReady.end(), [&](PassIndex inLeft, PassIndex inRight)
		{
			const PassBuildRef& left = passRefs[inLeft];
			const PassBuildRef& right = passRefs[inRight];
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

	auto funcSortGraphicsReady = [&](std::vector<PassIndex>& inReady, const std::vector<PassIndex>& inActiveRenderPassBatch)
	{
		for (PassIndex index : inReady)
		{
			PassBuildRef& ref = passRefs[index];
			if (!funcIsBatchableSubpass(index) || inActiveRenderPassBatch.empty())
			{
				ref.batchAffinity = 1u;
			}
			else if (funcCanMergeIntoRenderPassBatch(inActiveRenderPassBatch, index))
			{
				ref.batchAffinity = funcHasAttachmentOverlapWithBatch(inActiveRenderPassBatch, index) ? 0u : 2u;
			}
			else
			{
				ref.batchAffinity = 3u;
			}
		}

		std::stable_sort(inReady.begin(), inReady.end(), [&](PassIndex inLeft, PassIndex inRight)
		{
			const PassBuildRef& left = passRefs[inLeft];
			const PassBuildRef& right = passRefs[inRight];
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

	auto funcFineSortQueue = [&](const std::vector<PassIndex>& inPasses, QueueType inQueue) -> std::vector<PassIndex>
	{
		std::unordered_set<PassIndex> passSet(inPasses.begin(), inPasses.end());
		std::vector<uint32_t> localIndegrees(inContext.passes.size(), 0);
		for (PassIndex index : inPasses)
		{
			for (PassIndex next : inContext.passes[index].adjacency)
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
		std::vector<PassIndex> activeRenderPassBatch;
		while (!ready.empty())
		{
			if (inQueue == QueueType::GRAPHICS)
			{
				funcSortGraphicsReady(ready, activeRenderPassBatch);
			}
			else
			{
				funcSortReady(ready);
			}

			std::swap(ready.front(), ready.back());
			const PassIndex index = ready.back();
			ready.pop_back();
			sorted.push_back(index);

			if (inQueue == QueueType::GRAPHICS && funcIsBatchableSubpass(index))
			{
				if (!activeRenderPassBatch.empty() && funcCanMergeIntoRenderPassBatch(activeRenderPassBatch, index))
				{
					activeRenderPassBatch.push_back(index);
				}
				else
				{
					activeRenderPassBatch = { index };
				}
			}
			else
			{
				activeRenderPassBatch.clear();
			}

			for (PassIndex next : inContext.passes[index].adjacency)
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

	uint32_t activePassCount = 0;
	std::vector<PassIndex> ready;
	for (PassIndex index = 0; index < inContext.passes.size(); ++index)
	{
		if (index < inContext.passes.size() && inContext.passes[index].active)
		{
			++activePassCount;
		}
		else
		{
			continue;
		}

		if (passRefs[index].indegree == 0)
		{
			ready.push_back(index);
		}
	}

	submitPassBatches.clear();
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
		std::vector<bool> deferToNextSubmit(inContext.passes.size(), false);

		while (!currentReady.empty())
		{
			funcSortReady(currentReady);
			std::swap(currentReady.front(), currentReady.back());
			const PassIndex index = currentReady.back();
			currentReady.pop_back();
			submitPasses.push_back(index);
			++scheduledPassCount;

			for (PassIndex next : inContext.passes[index].adjacency)
			{
				CHECK_TRUE(passRefs[next].indegree > 0, "Invalid render graph dependency indegree!");
				const bool isQueueSyncEdge = inContext.passes[index].queue != inContext.passes[next].queue;
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

	CHECK_TRUE(scheduledPassCount == activePassCount, "Render graph has a dependency cycle!");

	for (const std::vector<PassIndex>& submitPasses : submitPassBatches)
	{
		SubmitBatch submitBatch;
		std::vector<PassIndex> graphicsPasses;
		std::vector<PassIndex> computePasses;
		for (PassIndex index : submitPasses)
		{
			if (inContext.passes[index].queue == QueueType::GRAPHICS)
			{
				graphicsPasses.push_back(index);
			}
			else
			{
				computePasses.push_back(index);
			}
		}

		const std::vector<PassIndex> sortedGraphicsPasses = funcFineSortQueue(graphicsPasses, QueueType::GRAPHICS);
		const std::vector<PassIndex> sortedComputePasses = funcFineSortQueue(computePasses, QueueType::COMPUTE);

		auto funcCreateGroup = [&](QueueType inQueue, std::vector<PassIndex> inPasses) -> SubmitBatch::PassGroupPlan
		{
			CHECK_TRUE(!inPasses.empty(), "Render graph pass group cannot be empty!");

			SubmitBatch::PassGroupPlan group;
			group.queue = inQueue;
			group.managedRenderPass = inQueue == QueueType::GRAPHICS &&
				(inContext.passes[inPasses.front()].type == PassType::RENDER_PASS ||
				 inContext.passes[inPasses.front()].type == PassType::SUBPASS);
			group.passes = std::move(inPasses);
			return group;
		};

		for (PassIndex index : sortedGraphicsPasses)
		{
			if (funcIsBatchableSubpass(index))
			{
				if (!submitBatch.graphicsGroups.empty() &&
					funcCanMergeIntoRenderPassBatch(submitBatch.graphicsGroups.back().passes, index))
				{
					submitBatch.graphicsGroups.back().passes.push_back(index);
					submitBatch.graphicsGroups.back().managedRenderPass = true;
				}
				else
				{
					submitBatch.graphicsGroups.push_back(funcCreateGroup(QueueType::GRAPHICS, { index }));
				}
			}
			else
			{
				submitBatch.graphicsGroups.push_back(funcCreateGroup(QueueType::GRAPHICS, { index }));
			}
		}

		for (PassIndex index : sortedComputePasses)
		{
			submitBatch.computeGroups.push_back(funcCreateGroup(QueueType::COMPUTE, { index }));
		}

		inoutResult.submitBatches.push_back(std::move(submitBatch));
	}
}

void RenderGraph::_BuildResourceAliases(BuildContext& inoutContext, const BuildResult& inResult) const
{
	struct ResourceLifetime
	{
		uint32_t firstGroup = INVALID_INDEX;
		uint32_t lastGroup = 0;
		QueueType queue = QueueType::GRAPHICS;
		bool active = false;
		bool hasQueue = false;
		bool multiQueue = false;
	};

	inoutContext.bufferAliasRoots.resize(inoutContext.bufferInfos.size());
	for (BufferIndex index = 0; index < inoutContext.bufferAliasRoots.size(); ++index)
	{
		inoutContext.bufferAliasRoots[index] = index;
	}
	inoutContext.imageAliasRoots.resize(inoutContext.imageInfos.size());
	for (ImageIndex index = 0; index < inoutContext.imageAliasRoots.size(); ++index)
	{
		inoutContext.imageAliasRoots[index] = index;
	}

	if (!inoutContext.enableResourceAliasing)
	{
		return;
	}

	std::vector<uint32_t> passToGroup(inoutContext.passes.size(), INVALID_INDEX);
	uint32_t groupIndex = 0;
	for (const SubmitBatch& submitBatch : inResult.submitBatches)
	{
		submitBatch.ForEachGroup([&](const SubmitBatch::PassGroupPlan& group)
		{
			for (PassIndex passIndex : group.passes)
			{
				CHECK_TRUE(passIndex < passToGroup.size(), "Invalid render graph scheduled pass index!");
				CHECK_TRUE(passToGroup[passIndex] == INVALID_INDEX, "Render graph pass is scheduled more than once!");
				passToGroup[passIndex] = groupIndex;
			}
			++groupIndex;
		});
	}

	auto funcApplyPass = [&](ResourceLifetime& inoutLifetime, PassIndex inPassIndex)
	{
		CHECK_TRUE(inPassIndex < passToGroup.size(), "Invalid render graph resource usage pass index!");
		const uint32_t useGroup = passToGroup[inPassIndex];
		CHECK_TRUE(useGroup != INVALID_INDEX, "Render graph active resource usage pass is not scheduled!");

		inoutLifetime.active = true;
		inoutLifetime.firstGroup = std::min(inoutLifetime.firstGroup, useGroup);
		inoutLifetime.lastGroup = std::max(inoutLifetime.lastGroup, useGroup);

		const QueueType queue = inoutContext.passes[inPassIndex].queue;
		if (!inoutLifetime.hasQueue)
		{
			inoutLifetime.queue = queue;
			inoutLifetime.hasQueue = true;
		}
		else if (inoutLifetime.queue != queue)
		{
			inoutLifetime.multiQueue = true;
		}
	};

	std::vector<ResourceLifetime> bufferLifetimes(inoutContext.bufferInfos.size());
	for (BufferIndex bufferIndex = 0; bufferIndex < inoutContext.buffers.size(); ++bufferIndex)
	{
		for (const BuildContext::BufferUsageRef& ref : inoutContext.buffers[bufferIndex].usages)
		{
			funcApplyPass(bufferLifetimes[bufferIndex], ref.pass);
		}
	}

	std::vector<ResourceLifetime> imageLifetimes(inoutContext.imageInfos.size());
	for (ImageIndex imageIndex = 0; imageIndex < inoutContext.images.size(); ++imageIndex)
	{
		for (const BuildContext::ImageUsageRef& ref : inoutContext.images[imageIndex].usages)
		{
			funcApplyPass(imageLifetimes[imageIndex], ref.pass);
		}
	}

	auto funcBuildAliases = [&](auto& inoutRoots, const auto& inInfos, const std::vector<ResourceLifetime>& inLifetimes, auto inDescriptorsMatch)
	{
		using Index = typename std::decay_t<decltype(inoutRoots)>::value_type;

		struct Candidate
		{
			Index index = INVALID_INDEX;
			uint32_t firstGroup = INVALID_INDEX;
			uint32_t lastGroup = INVALID_INDEX;
		};

		struct ActiveRoot
		{
			Index root = INVALID_INDEX;
			uint32_t lastGroup = INVALID_INDEX;
		};

		std::vector<Candidate> candidates;
		candidates.reserve(inInfos.size());
		for (Index index = 0; index < inInfos.size(); ++index)
		{
			const ResourceLifetime& lifetime = inLifetimes[index];
			if (inInfos[index].m_external || !lifetime.active || lifetime.multiQueue)
			{
				continue;
			}

			Candidate candidate;
			candidate.index = index;
			candidate.firstGroup = lifetime.firstGroup;
			candidate.lastGroup = lifetime.lastGroup;
			candidates.push_back(candidate);
		}

		std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& inLeft, const Candidate& inRight)
		{
			if (inLeft.firstGroup != inRight.firstGroup)
			{
				return inLeft.firstGroup < inRight.firstGroup;
			}
			return inLeft.index < inRight.index;
		});

		std::vector<ActiveRoot> activeRoots;
		std::vector<Index> freeRoots;
		for (const Candidate& candidate : candidates)
		{
			for (auto iter = activeRoots.begin(); iter != activeRoots.end();)
			{
				if (iter->lastGroup < candidate.firstGroup)
				{
					freeRoots.push_back(iter->root);
					iter = activeRoots.erase(iter);
				}
				else
				{
					++iter;
				}
			}

			const ResourceLifetime& candidateLifetime = inLifetimes[candidate.index];
			auto aliasIter = std::find_if(freeRoots.begin(), freeRoots.end(), [&](Index inRoot)
			{
				const ResourceLifetime& rootLifetime = inLifetimes[inRoot];
				return rootLifetime.hasQueue &&
					rootLifetime.queue == candidateLifetime.queue &&
					inDescriptorsMatch(inRoot, candidate.index);
			});

			Index root = candidate.index;
			if (aliasIter != freeRoots.end())
			{
				root = *aliasIter;
				freeRoots.erase(aliasIter);
				inoutRoots[candidate.index] = root;
			}

			ActiveRoot activeRoot;
			activeRoot.root = root;
			activeRoot.lastGroup = candidate.lastGroup;
			activeRoots.push_back(activeRoot);
		}
	};

	funcBuildAliases(
		inoutContext.bufferAliasRoots,
		inoutContext.bufferInfos,
		bufferLifetimes,
		[&](BufferIndex inLeft, BufferIndex inRight)
		{
			return inoutContext.bufferInfos[inLeft].IsAliasCompatible(inoutContext.bufferInfos[inRight]);
		});
	funcBuildAliases(
		inoutContext.imageAliasRoots,
		inoutContext.imageInfos,
		imageLifetimes,
		[&](ImageIndex inLeft, ImageIndex inRight)
		{
			return inoutContext.imageInfos[inLeft].IsAliasCompatible(inoutContext.imageInfos[inRight]);
		});
}

void RenderGraph::_MaterializeResourceAliases(BuildContext& inoutContext) const
{
	CHECK_TRUE(!inoutContext.aliasesMaterialized, "Render graph aliases are already materialized!");
	CHECK_TRUE(
		inoutContext.bufferAliasRoots.size() == inoutContext.bufferInfos.size(),
		"Render graph buffer alias mapping size is invalid!");
	CHECK_TRUE(
		inoutContext.imageAliasRoots.size() == inoutContext.imageInfos.size(),
		"Render graph image alias mapping size is invalid!");
	for (BufferIndex index = 0; index < inoutContext.bufferAliasRoots.size(); ++index)
	{
		const BufferIndex root = inoutContext.bufferAliasRoots[index];
		CHECK_TRUE(root < inoutContext.bufferAliasRoots.size(), "Render graph buffer alias root is invalid!");
		CHECK_TRUE(inoutContext.bufferAliasRoots[root] == root, "Render graph buffer alias mapping is not canonical!");
	}
	for (ImageIndex index = 0; index < inoutContext.imageAliasRoots.size(); ++index)
	{
		const ImageIndex root = inoutContext.imageAliasRoots[index];
		CHECK_TRUE(root < inoutContext.imageAliasRoots.size(), "Render graph image alias root is invalid!");
		CHECK_TRUE(inoutContext.imageAliasRoots[root] == root, "Render graph image alias mapping is not canonical!");
	}

	inoutContext.logicalToPhysicalBuffers.assign(inoutContext.bufferInfos.size(), INVALID_INDEX);
	inoutContext.logicalToPhysicalImages.assign(inoutContext.imageInfos.size(), INVALID_INDEX);

	std::vector<BufferInfo> physicalBuffers;
	std::vector<ImageInfo> physicalImages;
	std::vector<BufferIndex> rootToPhysicalBuffer(inoutContext.bufferInfos.size(), INVALID_INDEX);
	std::vector<ImageIndex> rootToPhysicalImage(inoutContext.imageInfos.size(), INVALID_INDEX);

	for (BufferIndex logicalIndex = 0; logicalIndex < inoutContext.bufferInfos.size(); ++logicalIndex)
	{
		const BufferIndex root = inoutContext.bufferAliasRoots[logicalIndex];
		CHECK_TRUE(root < inoutContext.bufferInfos.size(), "Render graph buffer alias root is invalid!");
		if (rootToPhysicalBuffer[root] == INVALID_INDEX)
		{
			rootToPhysicalBuffer[root] = static_cast<BufferIndex>(physicalBuffers.size());
			physicalBuffers.push_back(inoutContext.bufferInfos[root]);
		}
		inoutContext.logicalToPhysicalBuffers[logicalIndex] = rootToPhysicalBuffer[root];
	}

	for (ImageIndex logicalIndex = 0; logicalIndex < inoutContext.imageInfos.size(); ++logicalIndex)
	{
		const ImageIndex root = inoutContext.imageAliasRoots[logicalIndex];
		CHECK_TRUE(root < inoutContext.imageInfos.size(), "Render graph image alias root is invalid!");
		if (rootToPhysicalImage[root] == INVALID_INDEX)
		{
			rootToPhysicalImage[root] = static_cast<ImageIndex>(physicalImages.size());
			physicalImages.push_back(inoutContext.imageInfos[root]);
		}
		inoutContext.logicalToPhysicalImages[logicalIndex] = rootToPhysicalImage[root];
	}

	for (auto& [name, index] : inoutContext.nameToBuffer)
	{
		CHECK_TRUE(index < inoutContext.logicalToPhysicalBuffers.size(), "Render graph named buffer index is invalid!");
		index = inoutContext.logicalToPhysicalBuffers[index];
	}
	for (auto& [name, index] : inoutContext.nameToImage)
	{
		CHECK_TRUE(index < inoutContext.logicalToPhysicalImages.size(), "Render graph named image index is invalid!");
		index = inoutContext.logicalToPhysicalImages[index];
	}

	for (PassRecord& pass : inoutContext.passes)
	{
		for (ImageUsage& usage : pass.imageUsages)
		{
			CHECK_TRUE(
				usage.imageIndex < inoutContext.logicalToPhysicalImages.size(),
				"Render graph image usage index is invalid before alias materialization!");
			usage.imageIndex = inoutContext.logicalToPhysicalImages[usage.imageIndex];
		}
		for (BufferUsage& usage : pass.bufferUsages)
		{
			CHECK_TRUE(
				usage.bufferIndex < inoutContext.logicalToPhysicalBuffers.size(),
				"Render graph buffer usage index is invalid before alias materialization!");
			usage.bufferIndex = inoutContext.logicalToPhysicalBuffers[usage.bufferIndex];
		}
	}

	inoutContext.bufferInfos = std::move(physicalBuffers);
	inoutContext.imageInfos = std::move(physicalImages);
	inoutContext.buffers.assign(inoutContext.bufferInfos.size(), {});
	inoutContext.images.assign(inoutContext.imageInfos.size(), {});

	for (PassIndex passIndex = 0; passIndex < inoutContext.passes.size(); ++passIndex)
	{
		const PassRecord& pass = inoutContext.passes[passIndex];
		if (!pass.active)
		{
			continue;
		}

		for (const ImageUsage& usage : pass.imageUsages)
		{
			CHECK_TRUE(usage.imageIndex < inoutContext.images.size(), "Materialized render graph image index is invalid!");
			inoutContext.images[usage.imageIndex].usages.emplace_back(passIndex, usage);
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			CHECK_TRUE(usage.bufferIndex < inoutContext.buffers.size(), "Materialized render graph buffer index is invalid!");
			inoutContext.buffers[usage.bufferIndex].usages.emplace_back(passIndex, usage);
		}
	}

	inoutContext.aliasesMaterialized = true;
}

void RenderGraph::_BuildScheduledResourceBarriers(BuildContext& inContext, BuildResult& inoutResult) const
{
	CHECK_TRUE(inContext.aliasesMaterialized, "Render graph aliases must be materialized before building barriers!");

	struct GroupRef
	{
		SubmitBatch::PassGroupPlan* group = nullptr;
		uint32_t passOrder = INVALID_INDEX;
	};

	struct ImageState
	{
		std::optional<BuildContext::ImageUsageRef> lastWriter;
		std::vector<BuildContext::ImageUsageRef> pendingReaders;
	};

	struct ImageLogicalInfo
	{
		std::optional<BuildContext::ImageUsageRef> firstWriter;
		PassIndex firstUse = INVALID_INDEX;
		PassIndex lastUse = INVALID_INDEX;
	};

	struct BufferState
	{
		std::optional<BuildContext::BufferUsageRef> lastWriter;
		std::vector<BuildContext::BufferUsageRef> pendingReaders;
	};

	struct BufferLogicalInfo
	{
		std::optional<BuildContext::BufferUsageRef> firstWriter;
		PassIndex firstUse = INVALID_INDEX;
		PassIndex lastUse = INVALID_INDEX;
	};

	std::vector<GroupRef> passToGroup(inContext.passes.size());
	uint32_t passOrder = 0;

	for (SubmitBatch& submitBatch : inoutResult.submitBatches)
	{
		submitBatch.ForEachGroup([&](SubmitBatch::PassGroupPlan& group)
		{
			group.prologueBarriers.clear();
			group.epilogueBarriers.clear();
			group.queueReleaseBarriers.clear();
			group.subpassDependencies.clear();
			group.queueSignalPlans.clear();
			group.queueWaitPlans.clear();
			for (PassIndex passIndex : group.passes)
			{
				CHECK_TRUE(passIndex < passToGroup.size(), "Invalid render graph pass index in group!");
				CHECK_TRUE(passToGroup[passIndex].group == nullptr, "Render graph pass is scheduled more than once!");
				passToGroup[passIndex].group = &group;
				passToGroup[passIndex].passOrder = passOrder++;
			}
		});
	}

	for (PassIndex passIndex = 0; passIndex < passToGroup.size(); ++passIndex)
	{
		if (passIndex >= inContext.passes.size() || !inContext.passes[passIndex].active)
		{
			continue;
		}

		CHECK_TRUE(passToGroup[passIndex].group != nullptr, "Render graph pass is not scheduled!");
	}

	auto funcEmitBarrier = [&](const BarrierPlan& inPlan)
	{
		CHECK_TRUE(inPlan.after < passToGroup.size(), "Render graph barrier target pass is invalid!");
		SubmitBatch::PassGroupPlan* targetGroup = passToGroup[inPlan.after].group;
		CHECK_TRUE(targetGroup != nullptr, "Render graph barrier target pass is not scheduled!");

		const bool isSubpassDependency =
			targetGroup->managedRenderPass &&
			inPlan.before < passToGroup.size() &&
			passToGroup[inPlan.before].group == targetGroup;
		if (isSubpassDependency)
		{
			targetGroup->subpassDependencies.push_back(inPlan);
			return;
		}

		const bool needsQueueSync =
			inPlan.before < inContext.passes.size() &&
			inPlan.after < inContext.passes.size() &&
			inContext.passes[inPlan.before].queue != inContext.passes[inPlan.after].queue;
		if (needsQueueSync)
		{
			CHECK_TRUE(inPlan.before < passToGroup.size(), "Render graph queue release barrier source pass is invalid!");
			SubmitBatch::PassGroupPlan* sourceGroup = passToGroup[inPlan.before].group;
			CHECK_TRUE(sourceGroup != nullptr, "Render graph queue release barrier source pass is not scheduled!");
			sourceGroup->queueReleaseBarriers.push_back(inPlan);
		}

		targetGroup->prologueBarriers.push_back(inPlan);
	};

	auto funcEmitInitialImageBarrier = [&](ImageIndex inImageIndex, const BuildContext::ImageUsageRef& inFirstUse)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::IMAGE;
		plan.image = inImageIndex;
		plan.subresourceRange = inFirstUse.subresourceRange;
		plan.after = inFirstUse.pass;
		funcEmitBarrier(plan);
	};

	auto funcEmitImageBarrier = [&](ImageIndex inImageIndex, const BuildContext::ImageUsageRef& inBefore, const BuildContext::ImageUsageRef& inAfter)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::IMAGE;
		plan.image = inImageIndex;
		plan.subresourceRange = inBefore.subresourceRange.Intersect(inAfter.subresourceRange);
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		funcEmitBarrier(plan);
	};

	auto funcEmitBufferBarrier = [&](BufferIndex inBufferIndex, const BuildContext::BufferUsageRef& inBefore, const BuildContext::BufferUsageRef& inAfter)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::BUFFER;
		plan.buffer = inBufferIndex;
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		funcEmitBarrier(plan);
	};

	auto funcIsAttachmentUsage = [](ResourceUsageType inType)->bool
	{
		return inType == ResourceUsageType::COLOR_ATTACHMENT ||
			inType == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT ||
			inType == ResourceUsageType::RESOLVE_ATTACHMENT;
	};

	std::vector<std::vector<ImageLogicalInfo>> imageLogicalInfos(inContext.imageInfos.size());
	std::vector<BufferLogicalInfo> bufferLogicalInfos(inContext.bufferInfos.size());

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageInfos.size(); ++imageIndex)
	{
		const ImageSubresourceRange imageRange = inContext.imageInfos[imageIndex].GetWholeSubresourceRange();
		const uint32_t subresourceCount = imageRange.levelCount * imageRange.layerCount;
		imageLogicalInfos[imageIndex].resize(subresourceCount);

		_ForEachImageSubresource(imageRange, [&](uint32_t inMipLevel, uint32_t inArrayLayer)
		{
			const uint32_t subresourceIndex = _GetImageSubresourceIndex(inMipLevel, inArrayLayer, imageRange.levelCount);
			ImageLogicalInfo& info = imageLogicalInfos[imageIndex][subresourceIndex];
			const ImageSubresourceRange subresourceRange(inMipLevel, inArrayLayer);
			uint32_t firstOrder = INVALID_INDEX;
			uint32_t lastOrder = 0;
			uint32_t firstWriterOrder = INVALID_INDEX;

			for (const BuildContext::ImageUsageRef& ref : inContext.images[imageIndex].usages)
			{
				if (!ref.subresourceRange.Overlap(subresourceRange))
				{
					continue;
				}

				const uint32_t order = passToGroup[ref.pass].passOrder;
				CHECK_TRUE(order != INVALID_INDEX, "Render graph image usage pass is not scheduled!");
				if (order < firstOrder)
				{
					info.firstUse = ref.pass;
					firstOrder = order;
				}
				if (order >= lastOrder)
				{
					info.lastUse = ref.pass;
					lastOrder = order;
				}
				if (ref.writes && order < firstWriterOrder)
				{
					info.firstWriter = ref;
					info.firstWriter->subresourceRange = subresourceRange;
					firstWriterOrder = order;
				}
			}
		});
	}

	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.buffers.size(); ++bufferIndex)
	{
		BufferLogicalInfo& info = bufferLogicalInfos[bufferIndex];
		uint32_t firstOrder = INVALID_INDEX;
		uint32_t lastOrder = 0;
		uint32_t firstWriterOrder = INVALID_INDEX;
		for (const BuildContext::BufferUsageRef& ref : inContext.buffers[bufferIndex].usages)
		{
			const uint32_t order = passToGroup[ref.pass].passOrder;
			CHECK_TRUE(order != INVALID_INDEX, "Render graph buffer usage pass is not scheduled!");
			if (order < firstOrder)
			{
				info.firstUse = ref.pass;
				firstOrder = order;
			}
			if (order >= lastOrder)
			{
				info.lastUse = ref.pass;
				lastOrder = order;
			}
			if (ref.writes && order < firstWriterOrder)
			{
				info.firstWriter = ref;
				firstWriterOrder = order;
			}
		}
	}

	std::vector<std::vector<ImageState>> imageStates(inContext.imageInfos.size());
	std::vector<BufferState> bufferStates(inContext.bufferInfos.size());

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageInfos.size(); ++imageIndex)
	{
		const ImageSubresourceRange imageRange = inContext.imageInfos[imageIndex].GetWholeSubresourceRange();
		imageStates[imageIndex].resize(imageRange.levelCount * imageRange.layerCount);
	}

	auto funcVisitPass = [&](PassIndex inPassIndex)
	{
		const PassRecord& pass = inContext.passes[inPassIndex];

		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = usage.imageIndex;
			CHECK_TRUE(imageIndex < imageStates.size(), "Invalid render graph image index!");
			const bool hasExternalInitialState = inContext.imageInfos[imageIndex].m_external;
			const ImageSubresourceRange imageRange = inContext.imageInfos[imageIndex].GetWholeSubresourceRange();

			_ForEachImageSubresource(usage.subresourceRange, [&](uint32_t inMipLevel, uint32_t inArrayLayer)
			{
				const uint32_t subresourceIndex = _GetImageSubresourceIndex(inMipLevel, inArrayLayer, imageRange.levelCount);
				CHECK_TRUE(subresourceIndex < imageStates[imageIndex].size(), "Render graph image subresource state is missing!");
				CHECK_TRUE(subresourceIndex < imageLogicalInfos[imageIndex].size(), "Render graph image subresource logical info is missing!");

				ImageState& state = imageStates[imageIndex][subresourceIndex];
				const ImageLogicalInfo& info = imageLogicalInfos[imageIndex][subresourceIndex];
				ImageUsage cellUsage = usage;
				cellUsage.subresourceRange = ImageSubresourceRange(inMipLevel, inArrayLayer);
				const BuildContext::ImageUsageRef ref{ inPassIndex, cellUsage };

				if (ref.pass == info.firstUse && hasExternalInitialState)
				{
					BarrierPlan plan;
					plan.resourceType = ResourceType::IMAGE;
					plan.image = imageIndex;
					plan.subresourceRange = ref.subresourceRange;
					plan.after = ref.pass;
					plan.external = true;
					passToGroup[ref.pass].group->prologueBarriers.push_back(plan);
				}

				if (!state.lastWriter.has_value() &&
					!hasExternalInitialState &&
					ref.writes &&
					!funcIsAttachmentUsage(ref.type))
				{
					funcEmitInitialImageBarrier(imageIndex, ref);
				}

				if (ref.reads)
				{
					if (state.lastWriter.has_value() && state.lastWriter->pass != ref.pass)
					{
						funcEmitImageBarrier(imageIndex, state.lastWriter.value(), ref);
					}
					else if (!hasExternalInitialState && info.firstWriter.has_value() && info.firstWriter->pass != ref.pass)
					{
						funcEmitImageBarrier(imageIndex, info.firstWriter.value(), ref);
					}
					else if (hasExternalInitialState)
					{
						state.pendingReaders.push_back(ref);
					}
				}

				if (ref.writes)
				{
					if (state.lastWriter.has_value() && !ref.reads && state.lastWriter->pass != ref.pass)
					{
						funcEmitImageBarrier(imageIndex, state.lastWriter.value(), ref);
					}

					for (const BuildContext::ImageUsageRef& reader : state.pendingReaders)
					{
						if (reader.pass != ref.pass)
						{
							funcEmitImageBarrier(imageIndex, reader, ref);
						}
					}

					state.pendingReaders.clear();
					state.lastWriter = ref;
				}
				else if (ref.reads && state.lastWriter.has_value())
				{
					state.pendingReaders.push_back(ref);
				}

				if (ref.pass == info.lastUse && inContext.imageInfos[imageIndex].m_external)
				{
					BarrierPlan plan;
					plan.resourceType = ResourceType::IMAGE;
					plan.image = imageIndex;
					plan.subresourceRange = ref.subresourceRange;
					plan.before = ref.pass;
					plan.external = true;
					passToGroup[ref.pass].group->epilogueBarriers.push_back(plan);
				}
			});
		}

		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = usage.bufferIndex;
			CHECK_TRUE(bufferIndex < bufferStates.size(), "Invalid render graph buffer index!");
			BufferState& state = bufferStates[bufferIndex];
			const BufferLogicalInfo& info = bufferLogicalInfos[bufferIndex];
			const BuildContext::BufferUsageRef ref{ inPassIndex, usage };
			const bool hasExternalInitialState = inContext.bufferInfos[bufferIndex].m_external;

			if (ref.pass == info.firstUse && hasExternalInitialState)
			{
				BarrierPlan plan;
				plan.resourceType = ResourceType::BUFFER;
				plan.buffer = bufferIndex;
				plan.after = ref.pass;
				plan.external = true;
				passToGroup[ref.pass].group->prologueBarriers.push_back(plan);
			}

			if (ref.reads)
			{
				if (state.lastWriter.has_value() && state.lastWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(bufferIndex, state.lastWriter.value(), ref);
				}
				else if (!hasExternalInitialState && info.firstWriter.has_value() && info.firstWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(bufferIndex, info.firstWriter.value(), ref);
				}
				else if (hasExternalInitialState)
				{
					state.pendingReaders.push_back(ref);
				}
			}

			if (ref.writes)
			{
				if (state.lastWriter.has_value() && !ref.reads && state.lastWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(bufferIndex, state.lastWriter.value(), ref);
				}

				for (const BuildContext::BufferUsageRef& reader : state.pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						funcEmitBufferBarrier(bufferIndex, reader, ref);
					}
				}

				state.pendingReaders.clear();
				state.lastWriter = ref;
			}
			else if (ref.reads && state.lastWriter.has_value())
			{
				state.pendingReaders.push_back(ref);
			}

			if (ref.pass == info.lastUse && inContext.bufferInfos[bufferIndex].m_external)
			{
				BarrierPlan plan;
				plan.resourceType = ResourceType::BUFFER;
				plan.buffer = bufferIndex;
				plan.before = ref.pass;
				plan.external = true;
				passToGroup[ref.pass].group->epilogueBarriers.push_back(plan);
			}
		}
	};

	for (SubmitBatch& submitBatch : inoutResult.submitBatches)
	{
		submitBatch.ForEachGroup([&](SubmitBatch::PassGroupPlan& group)
		{
			for (PassIndex passIndex : group.passes)
			{
				funcVisitPass(passIndex);
			}
		});
	}

	for (const QueueSyncPlan& plan : inContext.queueSyncPlans)
	{
		CHECK_TRUE(plan.before < passToGroup.size(), "Render graph queue sync source pass is invalid!");
		CHECK_TRUE(plan.after < passToGroup.size(), "Render graph queue sync target pass is invalid!");
		SubmitBatch::PassGroupPlan* sourceGroup = passToGroup[plan.before].group;
		SubmitBatch::PassGroupPlan* targetGroup = passToGroup[plan.after].group;
		CHECK_TRUE(sourceGroup != nullptr, "Render graph queue sync source pass is not scheduled!");
		CHECK_TRUE(targetGroup != nullptr, "Render graph queue sync target pass is not scheduled!");
		CHECK_TRUE(sourceGroup != targetGroup, "Render graph queue sync cannot target the same pass group!");

		sourceGroup->queueSignalPlans.push_back(plan);
		targetGroup->queueWaitPlans.push_back(plan);
	}
}

void RenderGraph::_BuildManagedRenderPassPlans(
	const BuildContext& inContext,
	BuildResult& inoutResult) const
{
	struct AttachmentKey
	{
		ImageIndex image = INVALID_INDEX;
		ImageSubresourceRange range;
	};

	auto funcIsAttachment = [](ResourceUsageType inType)->bool
	{
		return inType == ResourceUsageType::COLOR_ATTACHMENT ||
			inType == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT ||
			inType == ResourceUsageType::RESOLVE_ATTACHMENT;
	};

	for (SubmitBatch& submitBatch : inoutResult.submitBatches)
	{
		for (SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
		{
			group.renderPassPlan.reset();
			if (!group.managedRenderPass)
			{
				continue;
			}
			CHECK_TRUE(!group.passes.empty(), "Managed render pass group cannot be empty!");
			SubmitBatch::ManagedRenderPassPlan plan;
			std::vector<AttachmentKey> attachmentKeys;

			auto funcFindOrAddAttachment = [&](const ImageUsage& inUsage)->uint32_t
			{
				for (uint32_t index = 0; index < attachmentKeys.size(); ++index)
				{
					if (attachmentKeys[index].image == inUsage.imageIndex &&
						attachmentKeys[index].range == inUsage.subresourceRange)
					{
						CHECK_TRUE(
							plan.attachments[index].role == inUsage.type,
							"A managed render pass image view cannot change attachment roles!");
						plan.attachments[index].storeOp = inUsage.storeOp;
						plan.attachments[index].stencilStoreOp = inUsage.stencilStoreOp;
						plan.attachments[index].finalLayout = inUsage.layout;
						return index;
					}
				}

				SubmitBatch::ManagedAttachmentPlan attachment;
				attachment.image = inUsage.imageIndex;
				attachment.subresourceRange = inUsage.subresourceRange;
				attachment.role = inUsage.type;
				attachment.loadOp = inUsage.loadOp;
				attachment.storeOp = inUsage.storeOp;
				attachment.stencilLoadOp = inUsage.stencilLoadOp;
				attachment.stencilStoreOp = inUsage.stencilStoreOp;
				attachment.finalLayout = inUsage.layout;
				if (inUsage.type == ResourceUsageType::DEPTH_STENCIL_ATTACHMENT)
				{
					attachment.clearValue.depthStencil = inUsage.clearDepthStencil;
				}
				else
				{
					attachment.clearValue.color = inUsage.clearColor;
				}

				bool hasIncomingBarrier = false;
				for (const BarrierPlan& barrier : group.prologueBarriers)
				{
					if (barrier.resourceType == ResourceType::IMAGE &&
						barrier.image == inUsage.imageIndex &&
						barrier.subresourceRange.Overlap(inUsage.subresourceRange))
					{
						hasIncomingBarrier = true;
						break;
					}
				}
				attachment.initialLayout = (hasIncomingBarrier ||
					inUsage.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ||
					inUsage.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					? inUsage.layout
					: VK_IMAGE_LAYOUT_UNDEFINED;

				const uint32_t index = static_cast<uint32_t>(plan.attachments.size());
				attachmentKeys.push_back({ inUsage.imageIndex, inUsage.subresourceRange });
				plan.attachments.push_back(attachment);
				return index;
			};

			for (PassIndex passIndex : group.passes)
			{
				CHECK_TRUE(passIndex < inContext.passes.size(), "Managed render pass references an invalid pass!");
				const PassRecord& pass = inContext.passes[passIndex];
				CHECK_TRUE(
					pass.type == PassType::RENDER_PASS || pass.type == PassType::SUBPASS,
					"Only RenderPassInfo and SubpassInfo can use a managed render pass!");

				SubmitBatch::ManagedSubpassPlan subpass;
				subpass.pass = passIndex;
				for (const ImageUsage& usage : pass.imageUsages)
				{
					if (!funcIsAttachment(usage.type))
					{
						continue;
					}
					const uint32_t attachmentIndex = funcFindOrAddAttachment(usage);
					if (usage.type == ResourceUsageType::COLOR_ATTACHMENT)
					{
						if (subpass.colorAttachments.size() <= usage.attachmentSlot)
						{
							subpass.colorAttachments.resize(static_cast<size_t>(usage.attachmentSlot) + 1, INVALID_INDEX);
						}
						subpass.colorAttachments[usage.attachmentSlot] = attachmentIndex;
					}
					else if (usage.type == ResourceUsageType::RESOLVE_ATTACHMENT)
					{
						if (subpass.resolveAttachments.size() <= usage.attachmentSlot)
						{
							subpass.resolveAttachments.resize(static_cast<size_t>(usage.attachmentSlot) + 1, INVALID_INDEX);
						}
						subpass.resolveAttachments[usage.attachmentSlot] = attachmentIndex;
					}
					else
					{
						CHECK_TRUE(!subpass.depthStencilAttachment.has_value(), "A subpass can only have one depth stencil attachment!");
						subpass.depthStencilAttachment = attachmentIndex;
					}
				}

				if (!subpass.resolveAttachments.empty())
				{
					subpass.resolveAttachments.resize(subpass.colorAttachments.size(), INVALID_INDEX);
				}
				plan.subpasses.push_back(std::move(subpass));
			}

			CHECK_TRUE(!plan.attachments.empty(), "Managed render pass must have at least one attachment!");
			plan.dependencies = group.subpassDependencies;
			for (uint32_t attachmentIndex = 0; attachmentIndex < plan.attachments.size(); ++attachmentIndex)
			{
				uint32_t firstUse = INVALID_INDEX;
				uint32_t lastUse = INVALID_INDEX;
				auto funcUsesAttachment = [&](const SubmitBatch::ManagedSubpassPlan& inSubpass)->bool
				{
					if (inSubpass.depthStencilAttachment == attachmentIndex)
					{
						return true;
					}
					return std::find(inSubpass.colorAttachments.begin(), inSubpass.colorAttachments.end(), attachmentIndex) != inSubpass.colorAttachments.end() ||
						std::find(inSubpass.resolveAttachments.begin(), inSubpass.resolveAttachments.end(), attachmentIndex) != inSubpass.resolveAttachments.end();
				};

				for (uint32_t subpassIndex = 0; subpassIndex < plan.subpasses.size(); ++subpassIndex)
				{
					if (funcUsesAttachment(plan.subpasses[subpassIndex]))
					{
						firstUse = std::min(firstUse, subpassIndex);
						lastUse = subpassIndex;
					}
				}
				CHECK_TRUE(firstUse != INVALID_INDEX, "Managed render pass attachment is never referenced!");
				for (uint32_t subpassIndex = firstUse + 1; subpassIndex < lastUse; ++subpassIndex)
				{
					if (!funcUsesAttachment(plan.subpasses[subpassIndex]))
					{
						plan.subpasses[subpassIndex].preserveAttachments.push_back(attachmentIndex);
					}
				}
			}

			group.renderPassPlan = std::move(plan);
		}
	}
}

const RenderGraph::BuildResult& RenderGraph::Build()
{
	if (m_built)
	{
		return m_buildResult;
	}

	BuildContext context = _CreateBuildContext();
	BuildResult buildResult;
	_LinkPasses(context);
	_CullPasses(context);
	_ResolveDependency(context);
	_BuildScheduleAndBatches(context, buildResult);
	_BuildResourceAliases(context, buildResult);
	_MaterializeResourceAliases(context);
	_BuildScheduledResourceBarriers(context, buildResult);
	_BuildManagedRenderPassPlans(context, buildResult);
	buildResult.passes = std::move(context.passes);
	buildResult.buffers = std::move(context.bufferInfos);
	buildResult.images = std::move(context.imageInfos);
	buildResult.nameToBuffer = std::move(context.nameToBuffer);
	buildResult.nameToImage = std::move(context.nameToImage);
	buildResult.nameToPass = m_nameToPass;

	uint32_t scheduledPassCount = 0;
	for (const SubmitBatch& submitBatch : buildResult.submitBatches)
	{
		submitBatch.ForEachGroup([&](const SubmitBatch::PassGroupPlan& group)
		{
			scheduledPassCount += static_cast<uint32_t>(group.passes.size());
		});
	}

	uint32_t activePassCount = 0;
	for (const PassRecord& pass : buildResult.passes)
	{
		if (pass.active)
		{
			++activePassCount;
		}
	}
	CHECK_TRUE(scheduledPassCount == activePassCount, "Render graph has a dependency cycle!");
	buildResult.valid = true;
	m_buildResult = std::move(buildResult);
	m_built = true;
	return m_buildResult;
}

const RenderGraph::BuildResult& RenderGraph::GetBuildResult() const
{
	CHECK_TRUE(m_built && m_buildResult.IsValid(), "Render graph must be built before reading its build result!");
	return m_buildResult;
}
