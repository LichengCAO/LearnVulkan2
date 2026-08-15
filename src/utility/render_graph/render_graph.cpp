#include "render_graph.h"

#include "buffer.h"
#include "command_buffer.h"
#include "command_queue.h"
#include "device.h"
#include "image.h"

#include <algorithm>
#include <unordered_set>

namespace
{
	auto _MakeEdgeKey(uint32_t inBefore, uint32_t inAfter)->uint64_t
	{
		return (static_cast<uint64_t>(inBefore) << 32u) | static_cast<uint64_t>(inAfter);
	}

	auto _ToStageFlags(VkPipelineStageFlags2 inStage)->VkPipelineStageFlags
	{
		VkPipelineStageFlags result = 0;
		if (inStage & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT) result |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) result |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT) result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT) result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT) result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT) result |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT) result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT) result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) result |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) result |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if (inStage & (
			VK_PIPELINE_STAGE_2_COPY_BIT |
			VK_PIPELINE_STAGE_2_RESOLVE_BIT |
			VK_PIPELINE_STAGE_2_BLIT_BIT |
			VK_PIPELINE_STAGE_2_CLEAR_BIT |
			VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT))
		{
			result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		if (inStage & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT) result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_HOST_BIT) result |= VK_PIPELINE_STAGE_HOST_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) result |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		if (inStage & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) result |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
#ifdef VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
		if (inStage & VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT) result |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
#endif
#ifdef VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
		if (inStage & VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT) result |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
#endif
#ifdef VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
		if (inStage & VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR) result |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
#endif
#ifdef VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
		if (inStage & VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) result |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
#endif
		return result;
	}

	auto _ToAccessFlags(VkAccessFlags2 inAccess)->VkAccessFlags
	{
		VkAccessFlags result = 0;
		if (inAccess & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT) result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		if (inAccess & VK_ACCESS_2_INDEX_READ_BIT) result |= VK_ACCESS_INDEX_READ_BIT;
		if (inAccess & VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT) result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		if (inAccess & VK_ACCESS_2_UNIFORM_READ_BIT) result |= VK_ACCESS_UNIFORM_READ_BIT;
		if (inAccess & (
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
			VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
			VK_ACCESS_2_SHADER_READ_BIT))
		{
			result |= VK_ACCESS_SHADER_READ_BIT;
		}
		if (inAccess & (
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
			VK_ACCESS_2_SHADER_WRITE_BIT))
		{
			result |= VK_ACCESS_SHADER_WRITE_BIT;
		}
		if (inAccess & VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT) result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		if (inAccess & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		if (inAccess & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (inAccess & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		if (inAccess & VK_ACCESS_2_TRANSFER_READ_BIT) result |= VK_ACCESS_TRANSFER_READ_BIT;
		if (inAccess & VK_ACCESS_2_TRANSFER_WRITE_BIT) result |= VK_ACCESS_TRANSFER_WRITE_BIT;
		if (inAccess & VK_ACCESS_2_HOST_READ_BIT) result |= VK_ACCESS_HOST_READ_BIT;
		if (inAccess & VK_ACCESS_2_HOST_WRITE_BIT) result |= VK_ACCESS_HOST_WRITE_BIT;
		if (inAccess & VK_ACCESS_2_MEMORY_READ_BIT) result |= VK_ACCESS_MEMORY_READ_BIT;
		if (inAccess & VK_ACCESS_2_MEMORY_WRITE_BIT) result |= VK_ACCESS_MEMORY_WRITE_BIT;
#ifdef VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
		if (inAccess & VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR) result |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
#endif
#ifdef VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
		if (inAccess & VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR) result |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
#endif
		return result;
	}

	auto _IsDepthStencilFormat(VkFormat inFormat)->bool
	{
		switch (inFormat)
		{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_S8_UINT:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return true;
		default:
			return false;
		}
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

auto RenderGraph::_GetImageUsageState(PassIndex inPassIndex, ImageIndex inImageIndex) const -> ImageUsageState
{
	CHECK_TRUE(inPassIndex < m_passes.size(), "Invalid render graph image usage pass index!");

	ImageUsageState state;
	bool found = false;
	const PassRecord& pass = m_passes[inPassIndex];
	for (const ImageUsage& usage : pass.imageUsages)
	{
		if (_GetImageIndex(usage.image) != inImageIndex)
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
		state.reads = state.reads || usage.reads;
		state.writes = state.writes || usage.writes;
	}

	CHECK_TRUE(found, "Render graph image usage is missing!");
	CHECK_TRUE(state.stage != 0, "Render graph image usage stage cannot be empty!");
	return state;
}

auto RenderGraph::_GetBufferUsageState(PassIndex inPassIndex, BufferIndex inBufferIndex) const -> BufferUsageState
{
	CHECK_TRUE(inPassIndex < m_passes.size(), "Invalid render graph buffer usage pass index!");

	BufferUsageState state;
	bool found = false;
	const PassRecord& pass = m_passes[inPassIndex];
	for (const BufferUsage& usage : pass.bufferUsages)
	{
		if (_GetBufferIndex(usage.buffer) != inBufferIndex)
		{
			continue;
		}

		state.stage |= usage.stage;
		state.access |= usage.access;
		state.reads = state.reads || usage.reads;
		state.writes = state.writes || usage.writes;
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

void RenderGraph::BufferInfo::SetSize(VkDeviceSize inSize)
{
	CHECK_TRUE(inSize > 0, "Render graph buffer size must be greater than 0!");
	m_size = inSize;
}

void RenderGraph::BufferInfo::AddUsage(VkBufferUsageFlags inUsage)
{
	CHECK_TRUE(inUsage != 0, "Render graph buffer usage cannot be empty!");
	m_usage |= inUsage;
}

void RenderGraph::BufferInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperty)
{
	CHECK_TRUE(inMemoryProperty != 0, "Render graph buffer memory property cannot be empty!");
	m_memoryProperty = inMemoryProperty;
}

void RenderGraph::BufferInfo::CustomizeSharingMode(VkSharingMode inSharingMode)
{
	m_sharingMode = inSharingMode;
}

void RenderGraph::BufferInfo::CustomizeAlignment(VkDeviceSize inAlignment)
{
	CHECK_TRUE(inAlignment > 0, "Render graph buffer alignment must be greater than 0!");
	m_optAlignment = inAlignment;
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

void RenderGraph::ImageInfo::CustomizeSize1D(uint32_t inWidth)
{
	CHECK_TRUE(inWidth > 0, "Render graph image width must be greater than 0!");
	m_type = VK_IMAGE_TYPE_1D;
	m_optWidth = inWidth;
	m_optHeight.reset();
	m_optDepth.reset();
}

void RenderGraph::ImageInfo::CustomizeSize2D(uint32_t inWidth, uint32_t inHeight)
{
	CHECK_TRUE(inWidth > 0 && inHeight > 0, "Render graph image size must be greater than 0!");
	m_type = VK_IMAGE_TYPE_2D;
	m_optWidth = inWidth;
	m_optHeight = inHeight;
	m_optDepth.reset();
}

void RenderGraph::ImageInfo::CustomizeSize3D(uint32_t inWidth, uint32_t inHeight, uint32_t inDepth)
{
	CHECK_TRUE(inWidth > 0 && inHeight > 0 && inDepth > 0, "Render graph image size must be greater than 0!");
	m_type = VK_IMAGE_TYPE_3D;
	m_optWidth = inWidth;
	m_optHeight = inHeight;
	m_optDepth = inDepth;
}

void RenderGraph::ImageInfo::CustomizeMipLevels(uint32_t inMipLevelCount)
{
	CHECK_TRUE(inMipLevelCount > 0, "Render graph image mip level count must be greater than 0!");
	m_optMipLevels = inMipLevelCount;
}

void RenderGraph::ImageInfo::CustomizeArrayLayers(uint32_t inArrayLayerCount)
{
	CHECK_TRUE(inArrayLayerCount > 0, "Render graph image array layer count must be greater than 0!");
	m_optArrayLayers = inArrayLayerCount;
}

void RenderGraph::ImageInfo::CustomizeFormat(VkFormat inFormat)
{
	CHECK_TRUE(inFormat != VK_FORMAT_UNDEFINED, "Render graph image format cannot be undefined!");
	m_optFormat = inFormat;
}

void RenderGraph::ImageInfo::CustomizeImageTiling(VkImageTiling inTiling)
{
	m_optTiling = inTiling;
}

void RenderGraph::ImageInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperty)
{
	CHECK_TRUE(inMemoryProperty != 0, "Render graph image memory property cannot be empty!");
	m_optMemoryProperty = inMemoryProperty;
}

void RenderGraph::ImageInfo::CustomizeSampleCount(VkSampleCountFlagBits inSampleCount)
{
	m_optSampleCount = inSampleCount;
}

void RenderGraph::ImageInfo::SetAsExternal()
{
	m_external = true;
}

void RenderGraph::PassInfo::SetNeverCull(bool inNeverCull)
{
	m_neverCull = inNeverCull;
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
	m_submitBatches.clear();
	m_activePasses.clear();
	m_bufferAliasRoots.clear();
	m_imageAliasRoots.clear();
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
	record.neverCull = inPassInfo.m_neverCull;
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

void RenderGraph::EnableResourceAliasing(bool inEnable)
{
	if (m_enableResourceAliasing == inEnable)
	{
		return;
	}

	m_enableResourceAliasing = inEnable;
	_InvalidateBuild();
}

void RenderGraph::_LinkPasses(BuildContext& inContext)
{
	auto funcAddEdge = [&](PassIndex inBefore, PassIndex inAfter)
	{
		CHECK_TRUE(inBefore != inAfter, "Render graph dependency cycle detected through self edge!");
		const uint64_t key = _MakeEdgeKey(inBefore, inAfter);
		if (!inContext.edgeSet.insert(key).second)
		{
			return;
		}

		DependencyEdge edge;
		edge.before = inBefore;
		edge.after = inAfter;
		inContext.dependencyEdges.push_back(edge);
		inContext.adjacency[inBefore].push_back(inAfter);
	};

	inContext.imageUsageRefs.assign(m_images.size(), {});
	inContext.bufferUsageRefs.assign(m_buffers.size(), {});
	inContext.adjacency.assign(m_passes.size(), {});
	inContext.queueSyncPlans.clear();
	inContext.queueSyncEdgeSet.clear();

	for (PassIndex passIndex = 0; passIndex < m_passes.size(); ++passIndex)
	{
		const PassRecord& pass = m_passes[passIndex];
		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = _GetImageIndex(usage.image);
			inContext.imageUsageRefs[imageIndex].push_back(BuildContext::ImageUsageRef{ passIndex, imageIndex, usage });
		}
		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = _GetBufferIndex(usage.buffer);
			inContext.bufferUsageRefs[bufferIndex].push_back(BuildContext::BufferUsageRef{ passIndex, bufferIndex, usage });
		}
	}

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageUsageRefs.size(); ++imageIndex)
	{
		const bool hasExternalInitialState = m_images[imageIndex].m_external;
		const auto& refs = inContext.imageUsageRefs[imageIndex];
		std::optional<BuildContext::ImageUsageRef> lastWriter;
		std::vector<BuildContext::ImageUsageRef> pendingReaders;
		std::optional<BuildContext::ImageUsageRef> firstWriter;
		const auto funcIsAttachmentUsage = [](const ImageUsage& inUsage)->bool
		{
			return inUsage.type == ImageUsageType::COLOR_ATTACHMENT ||
				inUsage.type == ImageUsageType::DEPTH_ATTACHMENT;
		};

		for (const BuildContext::ImageUsageRef& ref : refs)
		{
			if (ref.usage.writes)
			{
				firstWriter = ref;
				break;
			}
		}

		for (const BuildContext::ImageUsageRef& ref : refs)
		{
			if (ref.usage.reads)
			{
				if (!lastWriter.has_value() && !hasExternalInitialState)
				{
					if (!funcIsAttachmentUsage(ref.usage) && !ref.usage.writes && !firstWriter.has_value())
					{
						CHECK_TRUE(false, "Internal render graph image cannot be read before it is written!");
					}
					CHECK_TRUE(
						ref.usage.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD,
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

			if (ref.usage.writes)
			{
				if (lastWriter.has_value() && !ref.usage.reads && lastWriter->pass != ref.pass)
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
			else if (ref.usage.reads && lastWriter.has_value())
			{
				pendingReaders.push_back(ref);
			}
		}
	}

	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.bufferUsageRefs.size(); ++bufferIndex)
	{
		const bool hasExternalInitialState = m_buffers[bufferIndex].m_external;
		const auto& refs = inContext.bufferUsageRefs[bufferIndex];
		std::optional<BuildContext::BufferUsageRef> lastWriter;
		std::vector<BuildContext::BufferUsageRef> pendingReaders;
		std::optional<BuildContext::BufferUsageRef> firstWriter;

		for (const BuildContext::BufferUsageRef& ref : refs)
		{
			if (ref.usage.writes)
			{
				firstWriter = ref;
				break;
			}
		}

		for (const BuildContext::BufferUsageRef& ref : refs)
		{
			if (ref.usage.reads)
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

			if (ref.usage.writes)
			{
				if (lastWriter.has_value() && !ref.usage.reads && lastWriter->pass != ref.pass)
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
			else if (ref.usage.reads && lastWriter.has_value())
			{
				pendingReaders.push_back(ref);
			}
		}
	}

	for (const DependencyEdge& edge : m_extraDependencies)
	{
		funcAddEdge(edge.before, edge.after);
	}
}

void RenderGraph::_ResolveDependency(BuildContext& inContext)
{
	inContext.queueSyncPlans.clear();
	inContext.queueSyncEdgeSet.clear();

	for (const DependencyEdge& edge : inContext.dependencyEdges)
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
		inContext.queueSyncPlans.push_back(plan);
		inContext.queueSyncEdgeSet.insert(_MakeEdgeKey(edge.before, edge.after));
	}
}

void RenderGraph::_CullPasses(BuildContext& inContext)
{
	inContext.activePasses.assign(m_passes.size(), false);

	std::vector<std::vector<PassIndex>> reverseAdjacency(m_passes.size());
	for (const DependencyEdge& edge : inContext.dependencyEdges)
	{
		CHECK_TRUE(edge.before < m_passes.size() && edge.after < m_passes.size(), "Invalid render graph dependency edge!");
		reverseAdjacency[edge.after].push_back(edge.before);
	}

	std::vector<PassIndex> stack;
	auto funcMarkActive = [&](PassIndex inPassIndex)
	{
		CHECK_TRUE(inPassIndex < inContext.activePasses.size(), "Invalid render graph cull root pass!");
		if (inContext.activePasses[inPassIndex])
		{
			return;
		}

		inContext.activePasses[inPassIndex] = true;
		stack.push_back(inPassIndex);
	};

	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.bufferUsageRefs.size(); ++bufferIndex)
	{
		if (!m_buffers[bufferIndex].m_external)
		{
			continue;
		}

		for (const BuildContext::BufferUsageRef& ref : inContext.bufferUsageRefs[bufferIndex])
		{
			funcMarkActive(ref.pass);
		}
	}

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageUsageRefs.size(); ++imageIndex)
	{
		if (!m_images[imageIndex].m_external)
		{
			continue;
		}

		for (const BuildContext::ImageUsageRef& ref : inContext.imageUsageRefs[imageIndex])
		{
			funcMarkActive(ref.pass);
		}
	}

	for (PassIndex passIndex = 0; passIndex < m_passes.size(); ++passIndex)
	{
		if (m_passes[passIndex].neverCull)
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
		return inPassIndex < inContext.activePasses.size() && inContext.activePasses[inPassIndex];
	};

	inContext.dependencyEdges.erase(
		std::remove_if(
			inContext.dependencyEdges.begin(),
			inContext.dependencyEdges.end(),
			[&](const DependencyEdge& inEdge)
			{
				return !funcIsActive(inEdge.before) || !funcIsActive(inEdge.after);
			}),
		inContext.dependencyEdges.end());

	inContext.edgeSet.clear();
	inContext.adjacency.assign(m_passes.size(), {});
	inContext.queueSyncPlans.clear();
	inContext.queueSyncEdgeSet.clear();
	for (const DependencyEdge& edge : inContext.dependencyEdges)
	{
		const uint64_t key = _MakeEdgeKey(edge.before, edge.after);
		inContext.edgeSet.insert(key);
		inContext.adjacency[edge.before].push_back(edge.after);
	}

	for (auto& refs : inContext.imageUsageRefs)
	{
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

	for (auto& refs : inContext.bufferUsageRefs)
	{
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

void RenderGraph::_BuildScheduleAndBatches(BuildContext& inContext)
{
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
		const PassRecord& pass = m_passes[inPassIndex];
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
		const PassRecord& pass = m_passes[inPassIndex];
		return pass.type == PassType::SUBPASS && !pass.useDedicatedRenderPass;
	};

	auto funcSortUnique = [](auto& inValues)
	{
		std::sort(inValues.begin(), inValues.end());
		inValues.erase(std::unique(inValues.begin(), inValues.end()), inValues.end());
	};

	auto funcBuildRenderPassMergeInfo = [&](PassIndex inPassIndex) -> BuildContext::RenderPassMergeInfo
	{
		const PassRecord& pass = m_passes[inPassIndex];
		BuildContext::RenderPassMergeInfo info;
		if (!funcIsBatchableSubpass(inPassIndex))
		{
			return info;
		}

		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = _GetImageIndex(usage.image);
			if (usage.writes)
			{
				info.writtenImages.push_back(imageIndex);
				if (usage.type != ImageUsageType::COLOR_ATTACHMENT &&
					usage.type != ImageUsageType::DEPTH_ATTACHMENT)
				{
					info.writtenNonAttachmentImages.push_back(imageIndex);
				}
			}

			if (usage.type == ImageUsageType::COLOR_ATTACHMENT ||
				usage.type == ImageUsageType::DEPTH_ATTACHMENT)
			{
				// Automatic render pass batching only needs the attachment role and physical image.
				// Load/store ops and layouts are resolved when the merged render pass is emitted:
				// the first/last subpass owns load/store, and subpass dependencies handle layouts.
				// Attachment order is also not part of compatibility; each subpass references the
				// merged render pass attachments through its own attachment references.
				std::string token;
				token += std::to_string(static_cast<uint32_t>(usage.type));
				token += ':';
				token += std::to_string(imageIndex);
				info.attachmentTokens.push_back(std::move(token));
				info.attachmentImages.push_back(imageIndex);

				if (usage.type == ImageUsageType::DEPTH_ATTACHMENT)
				{
					info.depthAttachmentImages.push_back(imageIndex);
				}
				else
				{
					info.colorAttachmentImages.push_back(imageIndex);
				}
			}
			else
			{
				info.nonAttachmentImages.push_back(imageIndex);
			}
		}

		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = _GetBufferIndex(usage.buffer);
			info.nonAttachmentBuffers.push_back(bufferIndex);
			if (usage.writes)
			{
				info.writtenBuffers.push_back(bufferIndex);
			}
		}

		funcSortUnique(info.attachmentTokens);
		funcSortUnique(info.attachmentImages);
		funcSortUnique(info.colorAttachmentImages);
		funcSortUnique(info.depthAttachmentImages);
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

	inContext.passRefs.assign(m_passes.size(), {});

	auto funcCanMergeSubpasses = [&](PassIndex inPrev, PassIndex inNext) -> bool
	{
		if (!funcIsBatchableSubpass(inPrev) || !funcIsBatchableSubpass(inNext))
		{
			return false;
		}

		if (m_passes[inPrev].queue != QueueType::GRAPHICS ||
			m_passes[inNext].queue != QueueType::GRAPHICS ||
			m_passes[inPrev].queue != m_passes[inNext].queue)
		{
			return false;
		}

		if (inContext.queueSyncEdgeSet.find(_MakeEdgeKey(inPrev, inNext)) != inContext.queueSyncEdgeSet.end() ||
			inContext.queueSyncEdgeSet.find(_MakeEdgeKey(inNext, inPrev)) != inContext.queueSyncEdgeSet.end())
		{
			return false;
		}

		const BuildContext::RenderPassMergeInfo& prev = inContext.passRefs[inPrev].renderPassMergeInfo;
		const BuildContext::RenderPassMergeInfo& next = inContext.passRefs[inNext].renderPassMergeInfo;

		if (!prev.depthAttachmentImages.empty() &&
			!next.depthAttachmentImages.empty() &&
			prev.depthAttachmentImages != next.depthAttachmentImages)
		{
			return false;
		}

		if (funcHasIntersection(prev.depthAttachmentImages, next.colorAttachmentImages) ||
			funcHasIntersection(prev.colorAttachmentImages, next.depthAttachmentImages))
		{
			return false;
		}

		if (funcHasIntersection(next.nonAttachmentImages, prev.writtenImages))
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
		const BuildContext::RenderPassMergeInfo& candidate = inContext.passRefs[inCandidate].renderPassMergeInfo;
		for (PassIndex index : inBatch)
		{
			if (funcHasIntersection(inContext.passRefs[index].renderPassMergeInfo.attachmentImages, candidate.attachmentImages))
			{
				return true;
			}
		}
		return false;
	};

	for (const DependencyEdge& edge : inContext.dependencyEdges)
	{
		++inContext.passRefs[edge.after].indegree;
	}

	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
		BuildContext::PassBuildRef& ref = inContext.passRefs[index];
		ref.renderPassMergeInfo = funcBuildRenderPassMergeInfo(index);
		ref.downstreamDependCount = static_cast<uint32_t>(inContext.adjacency[index].size());

		for (PassIndex next : inContext.adjacency[index])
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
			const BuildContext::PassBuildRef& left = inContext.passRefs[inLeft];
			const BuildContext::PassBuildRef& right = inContext.passRefs[inRight];
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
			BuildContext::PassBuildRef& ref = inContext.passRefs[index];
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
			const BuildContext::PassBuildRef& left = inContext.passRefs[inLeft];
			const BuildContext::PassBuildRef& right = inContext.passRefs[inRight];
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
		std::vector<uint32_t> localIndegrees(m_passes.size(), 0);
		for (PassIndex index : inPasses)
		{
			for (PassIndex next : inContext.adjacency[index])
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

			const PassIndex index = ready.front();
			ready.erase(ready.begin());
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

			for (PassIndex next : inContext.adjacency[index])
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
	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
		if (index < inContext.activePasses.size() && inContext.activePasses[index])
		{
			++activePassCount;
		}
		else
		{
			continue;
		}

		if (inContext.passRefs[index].indegree == 0)
		{
			ready.push_back(index);
		}
	}

	inContext.submitPassBatches.clear();
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
			funcSortReady(currentReady);
			const PassIndex index = currentReady.front();
			currentReady.erase(currentReady.begin());
			submitPasses.push_back(index);
			++scheduledPassCount;

			for (PassIndex next : inContext.adjacency[index])
			{
				CHECK_TRUE(inContext.passRefs[next].indegree > 0, "Invalid render graph dependency indegree!");
				const bool isQueueSyncEdge = inContext.queueSyncEdgeSet.find(_MakeEdgeKey(index, next)) != inContext.queueSyncEdgeSet.end();
				// Defer only consumers of cross-queue producers that were scheduled in this window.
				// Cross-queue producers themselves should not be held back just because they will
				// signal another queue later; starting them early preserves async overlap.
				deferToNextSubmit[next] = deferToNextSubmit[next] || isQueueSyncEdge;
				--inContext.passRefs[next].indegree;
				if (inContext.passRefs[next].indegree == 0)
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
		inContext.submitPassBatches.push_back(std::move(submitPasses));
		ready = std::move(nextSubmitReady);
	}

	CHECK_TRUE(scheduledPassCount == activePassCount, "Render graph has a dependency cycle!");

	for (const std::vector<PassIndex>& submitPasses : inContext.submitPassBatches)
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

		const std::vector<PassIndex> sortedGraphicsPasses = funcFineSortQueue(graphicsPasses, QueueType::GRAPHICS);
		const std::vector<PassIndex> sortedComputePasses = funcFineSortQueue(computePasses, QueueType::COMPUTE);

		auto funcCreateGroup = [&](QueueType inQueue, std::vector<PassIndex> inPasses) -> SubmitBatch::PassGroupPlan
		{
			CHECK_TRUE(!inPasses.empty(), "Render graph pass group cannot be empty!");

			SubmitBatch::PassGroupPlan group;
			group.queue = inQueue;
			group.managedRenderPass = inQueue == QueueType::GRAPHICS &&
				(inPasses.size() > 1 || m_passes[inPasses.front()].type == PassType::SUBPASS);
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

		m_submitBatches.push_back(std::move(submitBatch));
	}
}

void RenderGraph::_BuildResourceAliases(BuildContext& inContext)
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

	m_bufferAliasRoots.resize(m_buffers.size());
	for (BufferIndex index = 0; index < m_bufferAliasRoots.size(); ++index)
	{
		m_bufferAliasRoots[index] = index;
	}
	m_imageAliasRoots.resize(m_images.size());
	for (ImageIndex index = 0; index < m_imageAliasRoots.size(); ++index)
	{
		m_imageAliasRoots[index] = index;
	}

	if (!m_enableResourceAliasing)
	{
		return;
	}

	std::vector<uint32_t> passToGroup(m_passes.size(), INVALID_INDEX);
	uint32_t groupIndex = 0;
	for (const SubmitBatch& submitBatch : m_submitBatches)
	{
		auto funcRecordGroups = [&](const std::vector<SubmitBatch::PassGroupPlan>& inGroups)
		{
			for (const SubmitBatch::PassGroupPlan& group : inGroups)
			{
				for (PassIndex passIndex : group.passes)
				{
					CHECK_TRUE(passIndex < passToGroup.size(), "Invalid render graph scheduled pass index!");
					CHECK_TRUE(passToGroup[passIndex] == INVALID_INDEX, "Render graph pass is scheduled more than once!");
					passToGroup[passIndex] = groupIndex;
				}
				++groupIndex;
			}
		};

		funcRecordGroups(submitBatch.graphicsGroups);
		funcRecordGroups(submitBatch.computeGroups);
	}

	auto funcApplyPass = [&](ResourceLifetime& inoutLifetime, PassIndex inPassIndex)
	{
		CHECK_TRUE(inPassIndex < passToGroup.size(), "Invalid render graph resource usage pass index!");
		const uint32_t useGroup = passToGroup[inPassIndex];
		CHECK_TRUE(useGroup != INVALID_INDEX, "Render graph active resource usage pass is not scheduled!");

		inoutLifetime.active = true;
		inoutLifetime.firstGroup = std::min(inoutLifetime.firstGroup, useGroup);
		inoutLifetime.lastGroup = std::max(inoutLifetime.lastGroup, useGroup);

		const QueueType queue = m_passes[inPassIndex].queue;
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

	std::vector<ResourceLifetime> bufferLifetimes(m_buffers.size());
	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.bufferUsageRefs.size(); ++bufferIndex)
	{
		for (const BuildContext::BufferUsageRef& ref : inContext.bufferUsageRefs[bufferIndex])
		{
			funcApplyPass(bufferLifetimes[bufferIndex], ref.pass);
		}
	}

	std::vector<ResourceLifetime> imageLifetimes(m_images.size());
	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageUsageRefs.size(); ++imageIndex)
	{
		for (const BuildContext::ImageUsageRef& ref : inContext.imageUsageRefs[imageIndex])
		{
			funcApplyPass(imageLifetimes[imageIndex], ref.pass);
		}
	}

	auto funcBufferDescriptorsMatch = [&](BufferIndex inLeft, BufferIndex inRight)->bool
	{
		const BufferInfo& left = m_buffers[inLeft];
		const BufferInfo& right = m_buffers[inRight];
		return left.m_size == right.m_size &&
			left.m_usage == right.m_usage &&
			left.m_memoryProperty == right.m_memoryProperty &&
			left.m_sharingMode == right.m_sharingMode &&
			left.m_optAlignment == right.m_optAlignment;
	};

	auto funcImageDescriptorsMatch = [&](ImageIndex inLeft, ImageIndex inRight)->bool
	{
		const ImageInfo& left = m_images[inLeft];
		const ImageInfo& right = m_images[inRight];
		return left.m_usage == right.m_usage &&
			left.m_type == right.m_type &&
			left.m_optWidth == right.m_optWidth &&
			left.m_optHeight == right.m_optHeight &&
			left.m_optDepth == right.m_optDepth &&
			left.m_optMipLevels == right.m_optMipLevels &&
			left.m_optArrayLayers == right.m_optArrayLayers &&
			left.m_optFormat == right.m_optFormat &&
			left.m_optTiling == right.m_optTiling &&
			left.m_optMemoryProperty == right.m_optMemoryProperty &&
			left.m_optSampleCount == right.m_optSampleCount;
	};

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

	funcBuildAliases(m_bufferAliasRoots, m_buffers, bufferLifetimes, funcBufferDescriptorsMatch);
	funcBuildAliases(m_imageAliasRoots, m_images, imageLifetimes, funcImageDescriptorsMatch);
}

void RenderGraph::_BuildScheduledResourceBarriers(BuildContext& inContext)
{
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

	std::vector<GroupRef> passToGroup(m_passes.size());
	uint32_t passOrder = 0;

	for (SubmitBatch& submitBatch : m_submitBatches)
	{
		for (SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
		{
			group.prologueBarriers.clear();
			group.epilogueBarriers.clear();
			group.queueReleaseBarriers.clear();
			group.subpassDependencies.clear();
			group.queueSignalPlans.clear();
			group.queueWaitPlans.clear();
			for (PassIndex passIndex : group.passes)
			{
				CHECK_TRUE(passIndex < passToGroup.size(), "Invalid render graph pass index in graphics group!");
				CHECK_TRUE(passToGroup[passIndex].group == nullptr, "Render graph pass is scheduled more than once!");
				passToGroup[passIndex].group = &group;
				passToGroup[passIndex].passOrder = passOrder++;
			}
		}
		for (SubmitBatch::PassGroupPlan& group : submitBatch.computeGroups)
		{
			group.prologueBarriers.clear();
			group.epilogueBarriers.clear();
			group.queueReleaseBarriers.clear();
			group.subpassDependencies.clear();
			group.queueSignalPlans.clear();
			group.queueWaitPlans.clear();
			for (PassIndex passIndex : group.passes)
			{
				CHECK_TRUE(passIndex < passToGroup.size(), "Invalid render graph pass index in compute group!");
				CHECK_TRUE(passToGroup[passIndex].group == nullptr, "Render graph pass is scheduled more than once!");
				passToGroup[passIndex].group = &group;
				passToGroup[passIndex].passOrder = passOrder++;
			}
		}
	}

	for (PassIndex passIndex = 0; passIndex < passToGroup.size(); ++passIndex)
	{
		if (passIndex >= inContext.activePasses.size() || !inContext.activePasses[passIndex])
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
			inPlan.before < m_passes.size() &&
			inPlan.after < m_passes.size() &&
			m_passes[inPlan.before].queue != m_passes[inPlan.after].queue;
		if (needsQueueSync)
		{
			CHECK_TRUE(inPlan.before < passToGroup.size(), "Render graph queue release barrier source pass is invalid!");
			SubmitBatch::PassGroupPlan* sourceGroup = passToGroup[inPlan.before].group;
			CHECK_TRUE(sourceGroup != nullptr, "Render graph queue release barrier source pass is not scheduled!");
			sourceGroup->queueReleaseBarriers.push_back(inPlan);
		}

		targetGroup->prologueBarriers.push_back(inPlan);
	};

	auto funcEmitInitialImageBarrier = [&](const BuildContext::ImageUsageRef& inFirstUse)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::IMAGE;
		plan.image = inFirstUse.image;
		plan.after = inFirstUse.pass;
		funcEmitBarrier(plan);
	};

	auto funcEmitImageBarrier = [&](const BuildContext::ImageUsageRef& inBefore, const BuildContext::ImageUsageRef& inAfter)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::IMAGE;
		plan.image = inAfter.image;
		plan.sourceImage = inBefore.image;
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		funcEmitBarrier(plan);
	};

	auto funcEmitBufferBarrier = [&](const BuildContext::BufferUsageRef& inBefore, const BuildContext::BufferUsageRef& inAfter)
	{
		BarrierPlan plan;
		plan.resourceType = ResourceType::BUFFER;
		plan.buffer = inAfter.buffer;
		plan.sourceBuffer = inBefore.buffer;
		plan.before = inBefore.pass;
		plan.after = inAfter.pass;
		funcEmitBarrier(plan);
	};

	auto funcIsAttachmentUsage = [](const ImageUsage& inUsage)->bool
	{
		return inUsage.type == ImageUsageType::COLOR_ATTACHMENT ||
			inUsage.type == ImageUsageType::DEPTH_ATTACHMENT;
	};

	std::vector<ImageLogicalInfo> imageLogicalInfos(m_images.size());
	std::vector<BufferLogicalInfo> bufferLogicalInfos(m_buffers.size());

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageUsageRefs.size(); ++imageIndex)
	{
		ImageLogicalInfo& info = imageLogicalInfos[imageIndex];
		uint32_t firstOrder = INVALID_INDEX;
		uint32_t lastOrder = 0;
		uint32_t firstWriterOrder = INVALID_INDEX;
		for (const BuildContext::ImageUsageRef& ref : inContext.imageUsageRefs[imageIndex])
		{
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
			if (ref.usage.writes && order < firstWriterOrder)
			{
				info.firstWriter = ref;
				firstWriterOrder = order;
			}
		}
	}

	for (BufferIndex bufferIndex = 0; bufferIndex < inContext.bufferUsageRefs.size(); ++bufferIndex)
	{
		BufferLogicalInfo& info = bufferLogicalInfos[bufferIndex];
		uint32_t firstOrder = INVALID_INDEX;
		uint32_t lastOrder = 0;
		uint32_t firstWriterOrder = INVALID_INDEX;
		for (const BuildContext::BufferUsageRef& ref : inContext.bufferUsageRefs[bufferIndex])
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
			if (ref.usage.writes && order < firstWriterOrder)
			{
				info.firstWriter = ref;
				firstWriterOrder = order;
			}
		}
	}

	std::vector<ImageState> imageStates(m_images.size());
	std::vector<BufferState> bufferStates(m_buffers.size());

	auto funcVisitPass = [&](PassIndex inPassIndex)
	{
		const PassRecord& pass = m_passes[inPassIndex];

		for (const ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = _GetImageIndex(usage.image);
			CHECK_TRUE(imageIndex < m_imageAliasRoots.size(), "Render graph image alias root is missing!");
			const ImageIndex aliasIndex = m_imageAliasRoots[imageIndex];
			CHECK_TRUE(aliasIndex < imageStates.size(), "Invalid render graph image alias root!");
			CHECK_TRUE(imageIndex < imageStates.size(), "Invalid render graph image index!");
			ImageState& state = imageStates[aliasIndex];
			const ImageLogicalInfo& info = imageLogicalInfos[imageIndex];
			const BuildContext::ImageUsageRef ref{ inPassIndex, imageIndex, usage };
			const bool hasExternalInitialState = m_images[imageIndex].m_external;

			if (ref.pass == info.firstUse && hasExternalInitialState)
			{
				BarrierPlan plan;
				plan.resourceType = ResourceType::IMAGE;
				plan.image = imageIndex;
				plan.after = ref.pass;
				plan.external = true;
				passToGroup[ref.pass].group->prologueBarriers.push_back(plan);
			}

			if (!state.lastWriter.has_value() &&
				!hasExternalInitialState &&
				ref.usage.writes &&
				!funcIsAttachmentUsage(ref.usage))
			{
				funcEmitInitialImageBarrier(ref);
			}

			if (ref.usage.reads)
			{
				if (state.lastWriter.has_value() && state.lastWriter->pass != ref.pass)
				{
					funcEmitImageBarrier(state.lastWriter.value(), ref);
				}
				else if (!hasExternalInitialState && info.firstWriter.has_value() && info.firstWriter->pass != ref.pass)
				{
					funcEmitImageBarrier(info.firstWriter.value(), ref);
				}
				else if (hasExternalInitialState)
				{
					state.pendingReaders.push_back(ref);
				}
			}

			if (ref.usage.writes)
			{
				if (state.lastWriter.has_value() && !ref.usage.reads && state.lastWriter->pass != ref.pass)
				{
					funcEmitImageBarrier(state.lastWriter.value(), ref);
				}

				for (const BuildContext::ImageUsageRef& reader : state.pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						funcEmitImageBarrier(reader, ref);
					}
				}

				state.pendingReaders.clear();
				state.lastWriter = ref;
			}
			else if (ref.usage.reads && state.lastWriter.has_value())
			{
				state.pendingReaders.push_back(ref);
			}

			if (ref.pass == info.lastUse && m_images[imageIndex].m_external)
			{
				BarrierPlan plan;
				plan.resourceType = ResourceType::IMAGE;
				plan.image = imageIndex;
				plan.before = ref.pass;
				plan.external = true;
				passToGroup[ref.pass].group->epilogueBarriers.push_back(plan);
			}
		}

		for (const BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = _GetBufferIndex(usage.buffer);
			CHECK_TRUE(bufferIndex < m_bufferAliasRoots.size(), "Render graph buffer alias root is missing!");
			const BufferIndex aliasIndex = m_bufferAliasRoots[bufferIndex];
			CHECK_TRUE(aliasIndex < bufferStates.size(), "Invalid render graph buffer alias root!");
			CHECK_TRUE(bufferIndex < bufferStates.size(), "Invalid render graph buffer index!");
			BufferState& state = bufferStates[aliasIndex];
			const BufferLogicalInfo& info = bufferLogicalInfos[bufferIndex];
			const BuildContext::BufferUsageRef ref{ inPassIndex, bufferIndex, usage };
			const bool hasExternalInitialState = m_buffers[bufferIndex].m_external;

			if (ref.pass == info.firstUse && hasExternalInitialState)
			{
				BarrierPlan plan;
				plan.resourceType = ResourceType::BUFFER;
				plan.buffer = bufferIndex;
				plan.after = ref.pass;
				plan.external = true;
				passToGroup[ref.pass].group->prologueBarriers.push_back(plan);
			}

			if (ref.usage.reads)
			{
				if (state.lastWriter.has_value() && state.lastWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(state.lastWriter.value(), ref);
				}
				else if (!hasExternalInitialState && info.firstWriter.has_value() && info.firstWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(info.firstWriter.value(), ref);
				}
				else if (hasExternalInitialState)
				{
					state.pendingReaders.push_back(ref);
				}
			}

			if (ref.usage.writes)
			{
				if (state.lastWriter.has_value() && !ref.usage.reads && state.lastWriter->pass != ref.pass)
				{
					funcEmitBufferBarrier(state.lastWriter.value(), ref);
				}

				for (const BuildContext::BufferUsageRef& reader : state.pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						funcEmitBufferBarrier(reader, ref);
					}
				}

				state.pendingReaders.clear();
				state.lastWriter = ref;
			}
			else if (ref.usage.reads && state.lastWriter.has_value())
			{
				state.pendingReaders.push_back(ref);
			}

			if (ref.pass == info.lastUse && m_buffers[bufferIndex].m_external)
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

	for (SubmitBatch& submitBatch : m_submitBatches)
	{
		for (SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
		{
			for (PassIndex passIndex : group.passes)
			{
				funcVisitPass(passIndex);
			}
		}
		for (SubmitBatch::PassGroupPlan& group : submitBatch.computeGroups)
		{
			for (PassIndex passIndex : group.passes)
			{
				funcVisitPass(passIndex);
			}
		}
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

void RenderGraph::Build()
{
	m_submitBatches.clear();

	BuildContext context;
	_LinkPasses(context);
	_CullPasses(context);
	_ResolveDependency(context);
	_BuildScheduleAndBatches(context);
	_BuildResourceAliases(context);
	_BuildScheduledResourceBarriers(context);

	uint32_t scheduledPassCount = 0;
	for (const SubmitBatch& submitBatch : m_submitBatches)
	{
		for (const SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
		{
			scheduledPassCount += static_cast<uint32_t>(group.passes.size());
		}
		for (const SubmitBatch::PassGroupPlan& group : submitBatch.computeGroups)
		{
			scheduledPassCount += static_cast<uint32_t>(group.passes.size());
		}
	}

	uint32_t activePassCount = 0;
	for (bool active : context.activePasses)
	{
		if (active)
		{
			++activePassCount;
		}
	}
	CHECK_TRUE(scheduledPassCount == activePassCount, "Render graph has a dependency cycle!");
	m_activePasses = context.activePasses;
	m_built = true;
}

auto RenderGraphInstance::ExecutionContext::ResolveBuffer(const std::string& inName) -> Buffer*
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	return m_pInstance->_GetBuffer(inName);
}

auto RenderGraphInstance::ExecutionContext::ResolveImage(const std::string& inName) -> Image*
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	return m_pInstance->_GetImage(inName);
}

void RenderGraphInstance::ExecutionContext::FillSubpassCommands(
	const std::string& inTarget,
	std::vector<const Command*> inCommands)
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	CHECK_TRUE(m_pRenderPassScope != nullptr, "FillSubpassCommands can only be used inside a render pass scope!");

	const PassIndex passIndex = m_pInstance->m_pRenderGraph->_GetPassIndex(inTarget);
	const auto iter = m_passToSubpass.find(passIndex);
	CHECK_TRUE(iter != m_passToSubpass.end(), "Target pass is not in current render pass scope!");
	CHECK_TRUE(iter->second < m_pRenderPassScope->subpassScopes.size(), "Invalid target subpass index!");

	auto& commands = m_pRenderPassScope->subpassScopes[iter->second].commands;
	commands.insert(commands.end(), inCommands.begin(), inCommands.end());
}

void RenderGraphInstance::ExecutionContext::RecordCommandBuffer(
	const std::string& inTarget,
	std::function<void(CommandBuffer*)> inProcess)
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	CHECK_TRUE(inProcess != nullptr, "Render graph pass command recording process is empty!");

	const PassIndex passIndex = m_pInstance->m_pRenderGraph->_GetPassIndex(inTarget);
	const RenderGraph::PassRecord& pass = m_pInstance->m_pRenderGraph->m_passes[passIndex];

	if (pass.type == RenderGraph::PassType::SUBPASS)
	{
		m_pInstance->_RecordSubpassCommandBuffer(passIndex, std::move(inProcess), *this);
	}
	else
	{
		CHECK_TRUE(m_pCommandBuffer != nullptr, "Command recording needs a command buffer!");
		inProcess(m_pCommandBuffer);
	}
}

void RenderGraphInstance::PassInfo::SetProcess(
	std::function<void(RenderGraphInstance::ExecutionContext&)> inProcess)
{
	CHECK_TRUE(inProcess != nullptr, "Render graph pass process cannot be empty!");
	m_process = std::move(inProcess);
}

RenderGraphInstance::RenderGraphInstance(const RenderGraph& inRenderGraph)
	: m_pRenderGraph(&inRenderGraph)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	CHECK_TRUE(m_pRenderGraph->m_built, "Render graph must be built before creating an instance!");

	m_buffers.resize(m_pRenderGraph->m_buffers.size(), nullptr);
	m_images.resize(m_pRenderGraph->m_images.size(), nullptr);
	m_externalBufferInfos.resize(m_pRenderGraph->m_buffers.size());
	m_externalImageInfos.resize(m_pRenderGraph->m_images.size());
	m_passInfos.resize(m_pRenderGraph->m_passes.size());
}

RenderGraphInstance::~RenderGraphInstance()
{
	_DestroyTemporaryRenderPasses();
	_DestroyInternalResources();

	auto& device = MyDevice::GetInstance();
	for (VkSemaphore& semaphore : m_executeSemaphores)
	{
		if (semaphore != VK_NULL_HANDLE)
		{
			device.DestroyVkSemaphore(semaphore);
		}
	}
	for (VkSemaphore& semaphore : m_freeSemaphores)
	{
		if (semaphore != VK_NULL_HANDLE)
		{
			device.DestroyVkSemaphore(semaphore);
		}
	}
	m_executeSemaphores.clear();
	m_freeSemaphores.clear();
}

void RenderGraphInstance::_DestroyTemporaryRenderPasses()
{
	for (TemporaryRenderPass& renderPass : m_temporaryRenderPasses)
	{
		if (renderPass.framebuffer != nullptr)
		{
			renderPass.framebuffer->Destroy();
			renderPass.framebuffer.reset();
		}
		if (renderPass.renderPass != nullptr)
		{
			renderPass.renderPass->Destroy();
			renderPass.renderPass.reset();
		}
	}

	m_temporaryRenderPasses.clear();
	m_graphicsBatchToTemporaryRenderPass.clear();
}

void RenderGraphInstance::_DestroyInternalResources()
{
	m_nameToBuffer.clear();
	m_nameToImage.clear();
	std::fill(m_buffers.begin(), m_buffers.end(), nullptr);
	std::fill(m_images.begin(), m_images.end(), nullptr);

	for (auto& image : m_internalImages)
	{
		if (image != nullptr)
		{
			image->Destroy();
		}
	}
	m_internalImages.clear();

	for (auto& buffer : m_internalBuffers)
	{
		if (buffer != nullptr)
		{
			buffer->Destroy();
		}
	}
	m_internalBuffers.clear();
}

void RenderGraphInstance::SetUpExternalBuffer(
	const std::string& inName,
	const RenderGraphInstance::ExternalBufferInfo& inBufferInfo)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	const BufferIndex index = m_pRenderGraph->_GetBufferIndex(inName);
	CHECK_TRUE(m_pRenderGraph->m_buffers[index].m_external, "Render graph buffer is not external!");
	CHECK_TRUE(inBufferInfo.pBuffer != nullptr, "External render graph buffer is null!");

	m_externalBufferInfos[index] = inBufferInfo;
	m_buffers[index] = inBufferInfo.pBuffer;
	m_nameToBuffer[inName] = inBufferInfo.pBuffer;
	m_compiled = false;
}

void RenderGraphInstance::SetUpExternalImage(
	const std::string& inName,
	const RenderGraphInstance::ExternalImageInfo& inImageInfo)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	const ImageIndex index = m_pRenderGraph->_GetImageIndex(inName);
	CHECK_TRUE(m_pRenderGraph->m_images[index].m_external, "Render graph image is not external!");
	CHECK_TRUE(inImageInfo.pImage != nullptr, "External render graph image is null!");

	m_externalImageInfos[index] = inImageInfo;
	m_images[index] = inImageInfo.pImage;
	m_nameToImage[inName] = inImageInfo.pImage;
	m_compiled = false;
}

void RenderGraphInstance::SetUpPass(
	const std::string& inName,
	const RenderGraphInstance::PassInfo& inPassInfo)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	const PassIndex index = m_pRenderGraph->_GetPassIndex(inName);
	CHECK_TRUE(inPassInfo.m_process != nullptr, "Render graph pass process cannot be empty!");

	m_passInfos[index] = inPassInfo;
	m_compiled = false;
}

auto RenderGraphInstance::_GetBuffer(const std::string& inName) const -> Buffer*
{
	const auto iter = m_nameToBuffer.find(inName);
	CHECK_TRUE(iter != m_nameToBuffer.end() && iter->second != nullptr, "Render graph buffer is not available!");
	return iter->second;
}

auto RenderGraphInstance::_GetImage(const std::string& inName) const -> Image*
{
	const auto iter = m_nameToImage.find(inName);
	CHECK_TRUE(iter != m_nameToImage.end() && iter->second != nullptr, "Render graph image is not available!");
	return iter->second;
}

void RenderGraphInstance::_SetUpPhysicalResources()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");

	_DestroyTemporaryRenderPasses();
	_DestroyInternalResources();

	m_buffers.assign(m_pRenderGraph->m_buffers.size(), nullptr);
	m_images.assign(m_pRenderGraph->m_images.size(), nullptr);

	CHECK_TRUE(m_pRenderGraph->m_bufferAliasRoots.size() == m_pRenderGraph->m_buffers.size(), "Render graph buffer alias roots are not built!");
	for (BufferIndex index = 0; index < m_pRenderGraph->m_buffers.size(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_pRenderGraph->m_buffers[index];
		if (graphBuffer.m_external)
		{
			CHECK_TRUE(m_externalBufferInfos[index].has_value(), "External render graph buffer is not set up!");
			Buffer* buffer = m_externalBufferInfos[index]->pBuffer;
			CHECK_TRUE(buffer != nullptr, "External render graph buffer is null!");
			m_buffers[index] = buffer;
			continue;
		}

		if (m_pRenderGraph->m_bufferAliasRoots[index] != index)
		{
			continue;
		}

		CHECK_TRUE(graphBuffer.m_size > 0, "Internal render graph buffer size must be set!");
		CHECK_TRUE(graphBuffer.m_usage != 0, "Internal render graph buffer usage must be set!");

		BufferCreateInfo createInfo;
		createInfo.SetBufferSize(graphBuffer.m_size)
			.SetBufferUsage(graphBuffer.m_usage)
			.CustomizeMemoryProperty(graphBuffer.m_memoryProperty)
			.CustomizeSharingMode(graphBuffer.m_sharingMode);
		if (graphBuffer.m_optAlignment.has_value())
		{
			createInfo.CustomizeAlignment(graphBuffer.m_optAlignment.value());
		}

		auto buffer = std::make_unique<Buffer>();
		buffer->Create(&createInfo);
		m_buffers[index] = buffer.get();
		m_internalBuffers.push_back(std::move(buffer));
	}

	for (BufferIndex index = 0; index < m_pRenderGraph->m_buffers.size(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_pRenderGraph->m_buffers[index];
		if (!graphBuffer.m_external)
		{
			const BufferIndex root = m_pRenderGraph->m_bufferAliasRoots[index];
			CHECK_TRUE(root < m_buffers.size() && m_buffers[root] != nullptr, "Render graph buffer alias root is not available!");
			m_buffers[index] = m_buffers[root];
		}

		CHECK_TRUE(m_buffers[index] != nullptr, "Render graph buffer is not available!");
		m_nameToBuffer[graphBuffer.m_name] = m_buffers[index];
	}

	CHECK_TRUE(m_pRenderGraph->m_imageAliasRoots.size() == m_pRenderGraph->m_images.size(), "Render graph image alias roots are not built!");
	for (ImageIndex index = 0; index < m_pRenderGraph->m_images.size(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_pRenderGraph->m_images[index];
		if (graphImage.m_external)
		{
			CHECK_TRUE(m_externalImageInfos[index].has_value(), "External render graph image is not set up!");
			Image* image = m_externalImageInfos[index]->pImage;
			CHECK_TRUE(image != nullptr, "External render graph image is null!");
			m_images[index] = image;
			continue;
		}

		if (m_pRenderGraph->m_imageAliasRoots[index] != index)
		{
			continue;
		}

		CHECK_TRUE(graphImage.m_usage != 0, "Internal render graph image usage must be set!");

		ImageCreateInfo createInfo;
		createInfo.SetUsage(graphImage.m_usage);
		switch (graphImage.m_type)
		{
		case VK_IMAGE_TYPE_1D:
			CHECK_TRUE(graphImage.m_optWidth.has_value(), "1D render graph image width must be set!");
			createInfo.CustomizeSize1D(graphImage.m_optWidth.value());
			break;
		case VK_IMAGE_TYPE_2D:
			if (graphImage.m_optWidth.has_value() || graphImage.m_optHeight.has_value())
			{
				CHECK_TRUE(graphImage.m_optWidth.has_value() && graphImage.m_optHeight.has_value(), "2D render graph image size is incomplete!");
				createInfo.CustomizeSize2D(graphImage.m_optWidth.value(), graphImage.m_optHeight.value());
			}
			break;
		case VK_IMAGE_TYPE_3D:
			CHECK_TRUE(
				graphImage.m_optWidth.has_value() &&
				graphImage.m_optHeight.has_value() &&
				graphImage.m_optDepth.has_value(),
				"3D render graph image size must be set!");
			createInfo.CustomizeSize3D(graphImage.m_optWidth.value(), graphImage.m_optHeight.value(), graphImage.m_optDepth.value());
			break;
		default:
			CHECK_TRUE(false, "Unsupported render graph image type!");
			break;
		}
		if (graphImage.m_optMipLevels.has_value()) createInfo.CustomizeMipLevels(graphImage.m_optMipLevels.value());
		if (graphImage.m_optArrayLayers.has_value()) createInfo.CustomizeArrayLayers(graphImage.m_optArrayLayers.value());
		if (graphImage.m_optFormat.has_value()) createInfo.CustomizeFormat(graphImage.m_optFormat.value());
		if (graphImage.m_optTiling.has_value()) createInfo.CustomizeImageTiling(graphImage.m_optTiling.value());
		if (graphImage.m_optMemoryProperty.has_value()) createInfo.CustomizeMemoryProperty(graphImage.m_optMemoryProperty.value());
		if (graphImage.m_optSampleCount.has_value()) createInfo.CustomizeSampleCount(graphImage.m_optSampleCount.value());

		auto image = std::make_unique<Image>();
		image->Create(&createInfo);
		m_images[index] = image.get();
		m_internalImages.push_back(std::move(image));
	}

	for (ImageIndex index = 0; index < m_pRenderGraph->m_images.size(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_pRenderGraph->m_images[index];
		if (!graphImage.m_external)
		{
			const ImageIndex root = m_pRenderGraph->m_imageAliasRoots[index];
			CHECK_TRUE(root < m_images.size() && m_images[root] != nullptr, "Render graph image alias root is not available!");
			m_images[index] = m_images[root];
		}

		CHECK_TRUE(m_images[index] != nullptr, "Render graph image is not available!");
		m_nameToImage[graphImage.m_name] = m_images[index];
	}
}

void RenderGraphInstance::_CreateTemporaryRenderPasses()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");

	m_graphicsBatchToTemporaryRenderPass.clear();
	m_graphicsBatchToTemporaryRenderPass.resize(m_pRenderGraph->m_submitBatches.size());

	for (uint32_t submitIndex = 0; submitIndex < m_pRenderGraph->m_submitBatches.size(); ++submitIndex)
	{
		const RenderGraph::SubmitBatch& submitBatch = m_pRenderGraph->m_submitBatches[submitIndex];
		m_graphicsBatchToTemporaryRenderPass[submitIndex].assign(submitBatch.graphicsGroups.size(), INVALID_INDEX);

		for (uint32_t batchIndex = 0; batchIndex < submitBatch.graphicsGroups.size(); ++batchIndex)
		{
			const RenderGraph::SubmitBatch::PassGroupPlan& group = submitBatch.graphicsGroups[batchIndex];
			const std::vector<PassIndex>& passBatch = group.passes;
			CHECK_TRUE(!passBatch.empty(), "Render graph graphics pass batch cannot be empty!");
			if (!group.managedRenderPass)
			{
				continue;
			}

			std::vector<ImageIndex> attachmentImages;
			std::unordered_map<ImageIndex, uint32_t> imageToAttachment;
			std::vector<std::vector<std::pair<uint32_t, VkImageLayout>>> colorReferences;
			std::vector<std::optional<std::pair<uint32_t, VkImageLayout>>> depthReferences;

			colorReferences.resize(passBatch.size());
			depthReferences.resize(passBatch.size());

			std::unordered_map<PassIndex, uint32_t> passToSubpass;
			passToSubpass.reserve(passBatch.size());
			for (uint32_t subpassIndex = 0; subpassIndex < passBatch.size(); ++subpassIndex)
			{
				passToSubpass.emplace(passBatch[subpassIndex], subpassIndex);
			}

			std::vector<VkPipelineStageFlags2> subpassAvailableStages(passBatch.size(), 0);
			std::vector<VkAccessFlags2> subpassAvailableAccesses(passBatch.size(), 0);
			std::vector<std::vector<RenderGraph::BarrierPlan>> dependenciesBySubpass(passBatch.size());

			for (const RenderGraph::BarrierPlan& dependency : group.subpassDependencies)
			{
				const auto beforeIter = passToSubpass.find(dependency.before);
				const auto afterIter = passToSubpass.find(dependency.after);
				CHECK_TRUE(
					beforeIter != passToSubpass.end() && afterIter != passToSubpass.end(),
					"Render graph subpass dependency references a pass outside the managed render pass group!");

				if (dependency.resourceType == RenderGraph::ResourceType::IMAGE)
				{
					const ImageIndex sourceImage = dependency.sourceImage == INVALID_INDEX ? dependency.image : dependency.sourceImage;
					const RenderGraph::ImageUsageState state = m_pRenderGraph->_GetImageUsageState(dependency.before, sourceImage);
					subpassAvailableStages[beforeIter->second] |= state.stage;
					subpassAvailableAccesses[beforeIter->second] |= state.access;
				}
				else
				{
					const BufferIndex sourceBuffer = dependency.sourceBuffer == INVALID_INDEX ? dependency.buffer : dependency.sourceBuffer;
					const RenderGraph::BufferUsageState state = m_pRenderGraph->_GetBufferUsageState(dependency.before, sourceBuffer);
					subpassAvailableStages[beforeIter->second] |= state.stage;
					subpassAvailableAccesses[beforeIter->second] |= state.access;
				}
				dependenciesBySubpass[afterIter->second].push_back(dependency);
			}

			auto funcGetAttachmentIndex = [&](ImageIndex inImageIndex)->uint32_t
			{
				const auto iter = imageToAttachment.find(inImageIndex);
				if (iter != imageToAttachment.end())
				{
					return iter->second;
				}

				CHECK_TRUE(inImageIndex < m_images.size() && m_images[inImageIndex] != nullptr, "Render graph attachment image is not available!");
				const uint32_t attachmentIndex = static_cast<uint32_t>(attachmentImages.size());
				attachmentImages.push_back(inImageIndex);
				imageToAttachment.emplace(inImageIndex, attachmentIndex);
				return attachmentIndex;
			};

			for (size_t subpassIndex = 0; subpassIndex < passBatch.size(); ++subpassIndex)
			{
				const RenderGraph::PassRecord& pass = m_pRenderGraph->m_passes[passBatch[subpassIndex]];
				CHECK_TRUE(pass.type == RenderGraph::PassType::SUBPASS, "Only subpasses can be batched into an internal render pass!");

				for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
				{
					if (usage.type != RenderGraph::ImageUsageType::COLOR_ATTACHMENT &&
						usage.type != RenderGraph::ImageUsageType::DEPTH_ATTACHMENT)
					{
						continue;
					}

					const ImageIndex imageIndex = m_pRenderGraph->_GetImageIndex(usage.image);
					const uint32_t attachmentIndex = funcGetAttachmentIndex(imageIndex);
					if (usage.type == RenderGraph::ImageUsageType::DEPTH_ATTACHMENT)
					{
						CHECK_TRUE(
							!depthReferences[subpassIndex].has_value() ||
							depthReferences[subpassIndex]->first == attachmentIndex,
							"Render graph subpass can only have one depth attachment!");
						depthReferences[subpassIndex] = std::pair{ attachmentIndex, usage.layout };
					}
					else
					{
						colorReferences[subpassIndex].push_back(std::pair{ attachmentIndex, usage.layout });
					}
				}
			}

			RenderPassCreateInfo renderPassInfo;
			std::vector<std::string> attachmentNames;
			attachmentNames.reserve(attachmentImages.size());
			for (uint32_t attachmentIndex = 0; attachmentIndex < attachmentImages.size(); ++attachmentIndex)
			{
				const ImageIndex imageIndex = attachmentImages[attachmentIndex];
				Image* image = m_images[imageIndex];
				CHECK_TRUE(image != nullptr, "Render graph attachment image is not available!");
				const Image::Information& imageInfo = image->GetImageInformation();

				VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				bool foundFirstUse = false;
				bool hasIncomingBarrier = false;

				for (const RenderGraph::BarrierPlan& plan : group.prologueBarriers)
				{
					if (plan.resourceType != RenderGraph::ResourceType::IMAGE || plan.image != imageIndex)
					{
						continue;
					}

					hasIncomingBarrier = true;
					break;
				}
				for (PassIndex passIndex : passBatch)
				{
					const RenderGraph::PassRecord& pass = m_pRenderGraph->m_passes[passIndex];
					for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
					{
						if ((usage.type != RenderGraph::ImageUsageType::COLOR_ATTACHMENT &&
							usage.type != RenderGraph::ImageUsageType::DEPTH_ATTACHMENT) ||
							m_pRenderGraph->_GetImageIndex(usage.image) != imageIndex)
						{
							continue;
						}

						if (!foundFirstUse)
						{
							loadOp = usage.loadOp;
							initialLayout = hasIncomingBarrier || usage.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD
								? usage.layout
								: VK_IMAGE_LAYOUT_UNDEFINED;
							foundFirstUse = true;
						}
						storeOp = usage.storeOp;
						finalLayout = usage.layout;
					}
				}

				CHECK_TRUE(foundFirstUse, "Render graph attachment has no usage!");

				AttachmentDescription description;
				description.CustomizeFormat(
					imageInfo.format,
					_IsDepthStencilFormat(imageInfo.format)
						? std::variant<std::pair<float, uint32_t>, glm::vec4>{ std::pair{ 0.0f, 0u } }
						: std::variant<std::pair<float, uint32_t>, glm::vec4>{ glm::vec4{ 0.0f, 0.0f, 0.0f, 0.0f } });
				description.CustomizeSampleCount(imageInfo.samples);
				description.CustomizeLoadOperation(loadOp);
				description.CustomizeStoreOperation(storeOp);
				description.CustomizeStencilStoreLoadOperation(
					_IsDepthStencilFormat(imageInfo.format) ? loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					_IsDepthStencilFormat(imageInfo.format) ? storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE);
				description.CustomizeInitialLayout(initialLayout);
				description.CustomizeFinalLayout(finalLayout);

				attachmentNames.push_back("attachment_" + std::to_string(attachmentIndex));
				renderPassInfo.AddAttachment(attachmentNames.back(), description);
			}

			for (size_t subpassIndex = 0; subpassIndex < passBatch.size(); ++subpassIndex)
			{
				SubpassDescription description;
				for (uint32_t colorIndex = 0; colorIndex < colorReferences[subpassIndex].size(); ++colorIndex)
				{
					const auto& [attachmentIndex, layout] = colorReferences[subpassIndex][colorIndex];
					description.AddColorAttachment(colorIndex, attachmentNames[attachmentIndex], layout);
				}
				if (depthReferences[subpassIndex].has_value())
				{
					const auto& [attachmentIndex, layout] = depthReferences[subpassIndex].value();
					description.AddDepthStencilAttachment(attachmentNames[attachmentIndex], layout);
				}
				description.AllowLocalPipelineBarrier();

				for (const RenderGraph::BarrierPlan& dependency : dependenciesBySubpass[subpassIndex])
				{
					VkPipelineStageFlags2 dstStage = 0;
					VkAccessFlags2 dstAccess = 0;
					if (dependency.resourceType == RenderGraph::ResourceType::IMAGE)
					{
						const RenderGraph::ImageUsageState state = m_pRenderGraph->_GetImageUsageState(dependency.after, dependency.image);
						dstStage = state.stage;
						dstAccess = state.access;
					}
					else
					{
						const RenderGraph::BufferUsageState state = m_pRenderGraph->_GetBufferUsageState(dependency.after, dependency.buffer);
						dstStage = state.stage;
						dstAccess = state.access;
					}
					description.AddDependencyOnSubpass(
						"subpass_" + std::to_string(passToSubpass.at(dependency.before)),
						_ToStageFlags(dstStage),
						_ToAccessFlags(dstAccess));
				}

				const VkPipelineStageFlags2 availableStage = subpassAvailableStages[subpassIndex] != 0
					? subpassAvailableStages[subpassIndex]
					: VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
						VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
				const VkAccessFlags2 availableAccess = subpassAvailableAccesses[subpassIndex] != 0
					? subpassAvailableAccesses[subpassIndex]
					: VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				description.CustomizeAvailableState(
					_ToStageFlags(availableStage),
					_ToAccessFlags(availableAccess));
				renderPassInfo.AddSubpass("subpass_" + std::to_string(subpassIndex), description);
			}

			TemporaryRenderPass temporaryRenderPass;
			temporaryRenderPass.passes = passBatch;
			temporaryRenderPass.renderPass = std::make_unique<RenderPass>();
			temporaryRenderPass.renderPass->Create(&renderPassInfo);

			Image* firstAttachment = m_images[attachmentImages.front()];
			const VkExtent3D extent = firstAttachment->GetImageSize();
			temporaryRenderPass.renderArea.offset = { 0, 0 };
			temporaryRenderPass.renderArea.extent = { extent.width, extent.height };

			for (ImageIndex imageIndex : attachmentImages)
			{
				const VkExtent3D attachmentExtent = m_images[imageIndex]->GetImageSize();
				CHECK_TRUE(
					attachmentExtent.width == extent.width && attachmentExtent.height == extent.height,
					"Render graph framebuffer attachments must have identical 2D size!");
			}

			FramebufferCreateInfo framebufferInfo;
			framebufferInfo.SetRenderPass(temporaryRenderPass.renderPass.get());
			for (uint32_t attachmentIndex = 0; attachmentIndex < attachmentImages.size(); ++attachmentIndex)
			{
				framebufferInfo.SetImageView(
					attachmentNames[attachmentIndex],
					m_images[attachmentImages[attachmentIndex]]->View());
			}
			temporaryRenderPass.framebuffer = std::make_unique<Framebuffer>();
			temporaryRenderPass.framebuffer->Create(&framebufferInfo);

			const uint32_t temporaryIndex = static_cast<uint32_t>(m_temporaryRenderPasses.size());
			m_graphicsBatchToTemporaryRenderPass[submitIndex][batchIndex] = temporaryIndex;
			m_temporaryRenderPasses.push_back(std::move(temporaryRenderPass));
		}
	}
}

auto RenderGraphInstance::_GetTemporaryRenderPass(
	uint32_t inSubmitIndex,
	uint32_t inGraphicsBatchIndex) -> TemporaryRenderPass*
{
	CHECK_TRUE(inSubmitIndex < m_graphicsBatchToTemporaryRenderPass.size(), "Invalid render graph submit index!");
	CHECK_TRUE(
		inGraphicsBatchIndex < m_graphicsBatchToTemporaryRenderPass[inSubmitIndex].size(),
		"Invalid render graph graphics batch index!");

	const uint32_t temporaryIndex = m_graphicsBatchToTemporaryRenderPass[inSubmitIndex][inGraphicsBatchIndex];
	if (temporaryIndex == INVALID_INDEX)
	{
		return nullptr;
	}

	CHECK_TRUE(temporaryIndex < m_temporaryRenderPasses.size(), "Invalid temporary render pass index!");
	return &m_temporaryRenderPasses[temporaryIndex];
}

void RenderGraphInstance::_BuildCompiledGraphPlan()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");

	m_compiledPlan.submitBatches.clear();
	m_compiledPlan.queueSyncEdges.clear();
	m_compiledPlan.submitBatches.resize(m_pRenderGraph->m_submitBatches.size());

	std::unordered_map<uint64_t, uint32_t> queueSyncEdgeToIndex;

	auto funcAppendIfValid = [](std::vector<std::unique_ptr<Command>>& inoutCommands, std::unique_ptr<Command> inCommand)
	{
		if (inCommand != nullptr)
		{
			inoutCommands.push_back(std::move(inCommand));
		}
	};

	auto funcGetQueueSyncEdgeIndex = [&](const RenderGraph::QueueSyncPlan& inPlan)->uint32_t
	{
		const uint64_t key = _MakeEdgeKey(inPlan.before, inPlan.after);
		const auto iter = queueSyncEdgeToIndex.find(key);
		if (iter != queueSyncEdgeToIndex.end())
		{
			return iter->second;
		}

		CompiledQueueSyncEdge edge;
		edge.srcQueue = inPlan.srcQueue;
		edge.dstQueue = inPlan.dstQueue;
		const uint32_t edgeIndex = static_cast<uint32_t>(m_compiledPlan.queueSyncEdges.size());
		m_compiledPlan.queueSyncEdges.push_back(edge);
		queueSyncEdgeToIndex.emplace(key, edgeIndex);
		return edgeIndex;
	};

	auto funcGetQueueWaitStage = [&](const RenderGraph::QueueSyncPlan& inPlan, const std::vector<RenderGraph::BarrierPlan>& inPrologueBarriers)->VkPipelineStageFlags
	{
		VkPipelineStageFlags waitStage = 0;
		for (const RenderGraph::BarrierPlan& barrier : inPrologueBarriers)
		{
			if (barrier.before != inPlan.before || barrier.after != inPlan.after)
			{
				continue;
			}

			if (barrier.resourceType == RenderGraph::ResourceType::IMAGE)
			{
				waitStage |= _ToStageFlags(m_pRenderGraph->_GetImageUsageState(barrier.after, barrier.image).stage);
			}
			else
			{
				waitStage |= _ToStageFlags(m_pRenderGraph->_GetBufferUsageState(barrier.after, barrier.buffer).stage);
			}
		}

		return waitStage == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : waitStage;
	};

	auto funcAppendQueueSignals = [&](const RenderGraph::SubmitBatch::PassGroupPlan& inGroup, uint32_t inSubmitIndex, std::vector<uint32_t>& inoutSignals)
	{
		for (const RenderGraph::QueueSyncPlan& plan : inGroup.queueSignalPlans)
		{
			const uint32_t edgeIndex = funcGetQueueSyncEdgeIndex(plan);
			CompiledQueueSyncEdge& edge = m_compiledPlan.queueSyncEdges[edgeIndex];
			CHECK_TRUE(edge.srcSubmit == INVALID_INDEX || edge.srcSubmit == inSubmitIndex, "Render graph queue sync source submit is inconsistent!");
			edge.srcSubmit = inSubmitIndex;
			edge.srcQueue = plan.srcQueue;
			edge.dstQueue = plan.dstQueue;
			inoutSignals.push_back(edgeIndex);
		}
	};

	auto funcAppendQueueWaits = [&](const RenderGraph::SubmitBatch::PassGroupPlan& inGroup, uint32_t inSubmitIndex, std::vector<CompiledQueueWait>& inoutWaits)
	{
		for (const RenderGraph::QueueSyncPlan& plan : inGroup.queueWaitPlans)
		{
			const uint32_t edgeIndex = funcGetQueueSyncEdgeIndex(plan);
			const VkPipelineStageFlags waitStage = funcGetQueueWaitStage(plan, inGroup.prologueBarriers);
			CompiledQueueSyncEdge& edge = m_compiledPlan.queueSyncEdges[edgeIndex];
			CHECK_TRUE(edge.dstSubmit == INVALID_INDEX || edge.dstSubmit == inSubmitIndex, "Render graph queue sync destination submit is inconsistent!");
			edge.dstSubmit = inSubmitIndex;
			edge.srcQueue = plan.srcQueue;
			edge.dstQueue = plan.dstQueue;
			edge.waitStage |= waitStage;

			CompiledQueueWait wait;
			wait.syncEdge = edgeIndex;
			wait.waitStage = waitStage;
			inoutWaits.push_back(wait);
		}
	};

	for (uint32_t submitIndex = 0; submitIndex < m_pRenderGraph->m_submitBatches.size(); ++submitIndex)
	{
		const RenderGraph::SubmitBatch& submitBatch = m_pRenderGraph->m_submitBatches[submitIndex];

		CompiledSubmitBatch& compiledSubmit = m_compiledPlan.submitBatches[submitIndex];
		compiledSubmit.graphicsGroups.resize(submitBatch.graphicsGroups.size());
		compiledSubmit.computeGroups.resize(submitBatch.computeGroups.size());

		for (uint32_t groupIndex = 0; groupIndex < submitBatch.graphicsGroups.size(); ++groupIndex)
		{
			const RenderGraph::SubmitBatch::PassGroupPlan& group = submitBatch.graphicsGroups[groupIndex];
			CompiledPassGroup& compiledGroup = compiledSubmit.graphicsGroups[groupIndex];
			compiledGroup.queue = group.queue;
			compiledGroup.passes = group.passes;
			if (_GetTemporaryRenderPass(submitIndex, groupIndex) != nullptr)
			{
				compiledGroup.temporaryRenderPass = m_graphicsBatchToTemporaryRenderPass[submitIndex][groupIndex];
			}

			funcAppendIfValid(compiledGroup.prologueCommands, _CreateBarrierCommand(group.prologueBarriers, BarrierCommandMode::QUEUE_ACQUIRE));
			funcAppendIfValid(compiledGroup.epilogueCommands, _CreateBarrierCommand(group.epilogueBarriers));
			funcAppendIfValid(compiledGroup.queueReleaseCommands, _CreateBarrierCommand(group.queueReleaseBarriers, BarrierCommandMode::QUEUE_RELEASE));
			funcAppendQueueSignals(group, submitIndex, compiledSubmit.graphicsSignalSyncs);
			funcAppendQueueWaits(group, submitIndex, compiledSubmit.graphicsWaitSyncs);
		}

		for (uint32_t groupIndex = 0; groupIndex < submitBatch.computeGroups.size(); ++groupIndex)
		{
			const RenderGraph::SubmitBatch::PassGroupPlan& group = submitBatch.computeGroups[groupIndex];
			CompiledPassGroup& compiledGroup = compiledSubmit.computeGroups[groupIndex];
			compiledGroup.queue = group.queue;
			compiledGroup.passes = group.passes;

			funcAppendIfValid(compiledGroup.prologueCommands, _CreateBarrierCommand(group.prologueBarriers, BarrierCommandMode::QUEUE_ACQUIRE));
			funcAppendIfValid(compiledGroup.epilogueCommands, _CreateBarrierCommand(group.epilogueBarriers));
			funcAppendIfValid(compiledGroup.queueReleaseCommands, _CreateBarrierCommand(group.queueReleaseBarriers, BarrierCommandMode::QUEUE_RELEASE));
			funcAppendQueueSignals(group, submitIndex, compiledSubmit.computeSignalSyncs);
			funcAppendQueueWaits(group, submitIndex, compiledSubmit.computeWaitSyncs);
		}
	}

	for (const CompiledQueueSyncEdge& edge : m_compiledPlan.queueSyncEdges)
	{
		CHECK_TRUE(edge.srcSubmit != INVALID_INDEX, "Render graph compiled queue sync is missing a source submit!");
		CHECK_TRUE(edge.dstSubmit != INVALID_INDEX, "Render graph compiled queue sync is missing a destination submit!");
		CHECK_TRUE(edge.srcSubmit < edge.dstSubmit, "Render graph queue sync destination must be submitted after its source!");
		CHECK_TRUE(edge.srcQueue != edge.dstQueue, "Render graph compiled queue sync must cross queues!");
	}
}

void RenderGraphInstance::_AppendPassCommands(
	PassIndex inPassIndex,
	CommandBuffer::PrimaryScope& inPrimaryScope)
{
	CHECK_TRUE(inPassIndex < m_passInfos.size(), "Invalid render graph pass index!");
	CHECK_TRUE(m_passInfos[inPassIndex].m_process != nullptr, "Render graph pass process is not set up!");

	ExecutionContext context;
	context.m_pInstance = this;
	context.m_pPrimaryScope = &inPrimaryScope;

	CommandBuffer tmpCommandBuffer;
	context.m_pCommandBuffer = &tmpCommandBuffer;
	m_passInfos[inPassIndex].m_process(context);

	for (CommandBuffer::Scope& scope : tmpCommandBuffer.m_scopes)
	{
		CHECK_TRUE(std::holds_alternative<CommandBuffer::PrimaryScope>(scope), "Non-subpass render graph pass can only append primary command scopes!");
		auto& srcCommands = std::get<CommandBuffer::PrimaryScope>(scope).commands;
		inPrimaryScope.commands.insert(inPrimaryScope.commands.end(), srcCommands.begin(), srcCommands.end());
	}
}

void RenderGraphInstance::_AppendRenderPassCommands(
	const std::vector<PassIndex>& inPasses,
	const TemporaryRenderPass& inRenderPass,
	CommandBuffer& inCommandBuffer)
{
	CHECK_TRUE(!inPasses.empty(), "Render graph render pass batch cannot be empty!");
	CHECK_TRUE(
		inRenderPass.renderPass != nullptr &&
		inRenderPass.renderPass->GetVkRenderPass() != VK_NULL_HANDLE,
		"Invalid render graph temporary render pass!");
	CHECK_TRUE(
		inRenderPass.framebuffer != nullptr &&
		inRenderPass.framebuffer->GetVkFramebuffer() != VK_NULL_HANDLE,
		"Invalid render graph temporary framebuffer!");

	CommandBuffer::RenderPassScope renderPassScope;
	renderPassScope.renderPass = inRenderPass.renderPass->GetVkRenderPass();
	renderPassScope.framebuffer = inRenderPass.framebuffer->GetVkFramebuffer();
	renderPassScope.renderArea = inRenderPass.renderArea;
	renderPassScope.clearValues = inRenderPass.renderPass->GetClearValues();
	renderPassScope.contents = VK_SUBPASS_CONTENTS_INLINE;
	renderPassScope.subpassScopes.resize(inPasses.size());

	ExecutionContext context;
	context.m_pInstance = this;
	context.m_pRenderPassScope = &renderPassScope;
	for (size_t subpassIndex = 0; subpassIndex < inPasses.size(); ++subpassIndex)
	{
		context.m_passToSubpass[inPasses[subpassIndex]] = subpassIndex;
	}

	for (PassIndex passIndex : inPasses)
	{
		CHECK_TRUE(passIndex < m_passInfos.size(), "Invalid render graph pass index!");
		CHECK_TRUE(m_passInfos[passIndex].m_process != nullptr, "Render graph pass process is not set up!");
		m_passInfos[passIndex].m_process(context);
	}

	inCommandBuffer.AppendRenderPass(&renderPassScope);
}

void RenderGraphInstance::_RecordSubpassCommandBuffer(
	PassIndex inPassIndex,
	std::function<void(CommandBuffer*)> inProcess,
	ExecutionContext& inContext)
{
	CHECK_TRUE(inProcess != nullptr, "Render graph subpass command recording process is empty!");
	CHECK_TRUE(inContext.m_pRenderPassScope != nullptr, "Subpass command recording needs a render pass scope!");

	const auto iter = inContext.m_passToSubpass.find(inPassIndex);
	CHECK_TRUE(iter != inContext.m_passToSubpass.end(), "Target pass is not in current render pass scope!");
	CHECK_TRUE(iter->second < inContext.m_pRenderPassScope->subpassScopes.size(), "Invalid target subpass index!");

	CommandBuffer tmpCommandBuffer;
	inProcess(&tmpCommandBuffer);

	for (CommandBuffer::Scope& scope : tmpCommandBuffer.m_scopes)
	{
		CHECK_TRUE(std::holds_alternative<CommandBuffer::PrimaryScope>(scope), "Subpass can only append primary command scopes!");
		auto& srcCommands = std::get<CommandBuffer::PrimaryScope>(scope).commands;
		auto& dstCommands = inContext.m_pRenderPassScope->subpassScopes[iter->second].commands;
		dstCommands.insert(dstCommands.end(), srcCommands.begin(), srcCommands.end());
	}
}

auto RenderGraphInstance::_AcquireSemaphore() -> VkSemaphore
{
	if (!m_freeSemaphores.empty())
	{
		VkSemaphore semaphore = m_freeSemaphores.back();
		m_freeSemaphores.pop_back();
		CHECK_TRUE(semaphore != VK_NULL_HANDLE, "Render graph semaphore pool returned an invalid semaphore!");
		m_executeSemaphores.push_back(semaphore);
		return semaphore;
	}

	VkSemaphore semaphore = MyDevice::GetInstance().CreateVkSemaphore();
	m_executeSemaphores.push_back(semaphore);
	return semaphore;
}

void RenderGraphInstance::_RecycleExecuteSemaphores()
{
	m_freeSemaphores.insert(m_freeSemaphores.end(), m_executeSemaphores.begin(), m_executeSemaphores.end());
	m_executeSemaphores.clear();
}

auto RenderGraphInstance::_CreateBarrierCommand(
	const std::vector<RenderGraph::BarrierPlan>& inBarrierPlans,
	BarrierCommandMode inMode) -> std::unique_ptr<Command>
{
	if (inBarrierPlans.empty())
	{
		return nullptr;
	}

	auto& device = MyDevice::GetInstance();
	const GraphicsQueue* graphicsQueue = device.GetGraphicsCommandQueue();
	const ComputeQueue* computeQueue = device.GetComputeCommandQueue();

	auto funcGetQueueFamily = [&](RenderGraph::QueueType inQueue)->uint32_t
	{
		if (inQueue == RenderGraph::QueueType::GRAPHICS)
		{
			CHECK_TRUE(graphicsQueue != nullptr, "Graphics command queue is not available!");
			return graphicsQueue->GetQueueFamilyIndex();
		}

		CHECK_TRUE(computeQueue != nullptr, "Compute command queue is not available!");
		return computeQueue->GetQueueFamilyIndex();
	};

	auto funcUsesQueueOwnershipTransfer = [&](const RenderGraph::BarrierPlan& inPlan, uint32_t& outSrcFamily, uint32_t& outDstFamily)->bool
	{
		outSrcFamily = VK_QUEUE_FAMILY_IGNORED;
		outDstFamily = VK_QUEUE_FAMILY_IGNORED;
		if (inMode == BarrierCommandMode::NORMAL || inPlan.external || inPlan.before == INVALID_INDEX || inPlan.after == INVALID_INDEX)
		{
			return false;
		}

		const RenderGraph::QueueType srcQueue = m_pRenderGraph->m_passes[inPlan.before].queue;
		const RenderGraph::QueueType dstQueue = m_pRenderGraph->m_passes[inPlan.after].queue;
		if (srcQueue == dstQueue)
		{
			return false;
		}

		const uint32_t srcFamily = funcGetQueueFamily(srcQueue);
		const uint32_t dstFamily = funcGetQueueFamily(dstQueue);
		if (srcFamily == dstFamily)
		{
			return false;
		}

		outSrcFamily = srcFamily;
		outDstFamily = dstFamily;
		return true;
	};

	PipelineBarrierCommand::Parameters parameters;

	for (const RenderGraph::BarrierPlan& plan : inBarrierPlans)
	{
		if (plan.external)
		{
			const bool entering = plan.before == INVALID_INDEX && plan.after != INVALID_INDEX;
			const bool leaving = plan.before != INVALID_INDEX && plan.after == INVALID_INDEX;
			CHECK_TRUE(entering || leaving, "External render graph barrier must have exactly one boundary pass!");
			const PassIndex boundaryPass = entering ? plan.after : plan.before;

			if (plan.resourceType == RenderGraph::ResourceType::BUFFER)
			{
				const BufferIndex bufferIndex = plan.buffer;
				CHECK_TRUE(bufferIndex < m_externalBufferInfos.size() && m_externalBufferInfos[bufferIndex].has_value(), "External render graph buffer is not set up!");
				CHECK_TRUE(bufferIndex < m_buffers.size() && m_buffers[bufferIndex] != nullptr, "External render graph buffer is not available!");

				const ExternalBufferInfo& externalInfo = m_externalBufferInfos[bufferIndex].value();
				const RenderGraph::BufferUsageState graphState = m_pRenderGraph->_GetBufferUsageState(boundaryPass, bufferIndex);

				const Buffer::Information& bufferInfo = m_buffers[bufferIndex]->GetBufferInformation();
				VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
				barrier.srcAccessMask = _ToAccessFlags(entering ? externalInfo.enteringAccess : graphState.access);
				barrier.dstAccessMask = _ToAccessFlags(entering ? graphState.access : externalInfo.leavingAccess);
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.buffer = m_buffers[bufferIndex]->GetVkBuffer();
				barrier.offset = 0;
				barrier.size = bufferInfo.size;
				parameters.srcStageMask |= _ToStageFlags(entering ? externalInfo.enteringStage : graphState.stage);
				parameters.dstStageMask |= _ToStageFlags(entering ? graphState.stage : externalInfo.leavingStage);
				parameters.bufferBarriers.push_back(barrier);
				continue;
			}

			const ImageIndex imageIndex = plan.image;
			CHECK_TRUE(imageIndex < m_externalImageInfos.size() && m_externalImageInfos[imageIndex].has_value(), "External render graph image is not set up!");
			CHECK_TRUE(imageIndex < m_images.size() && m_images[imageIndex] != nullptr, "External render graph image is not available!");

			const ExternalImageInfo& externalInfo = m_externalImageInfos[imageIndex].value();
			const RenderGraph::ImageUsageState graphState = m_pRenderGraph->_GetImageUsageState(boundaryPass, imageIndex);

			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(entering ? externalInfo.enteringAccess : graphState.access);
			barrier.dstAccessMask = _ToAccessFlags(entering ? graphState.access : externalInfo.leavingAccess);
			barrier.oldLayout = entering ? externalInfo.enteringLayout : graphState.layout;
			barrier.newLayout = entering ? graphState.layout : externalInfo.leavingLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_images[imageIndex]->GetVkImage();
			barrier.subresourceRange = m_images[imageIndex]->View()->GetImageSubresourceRange();
			parameters.srcStageMask |= _ToStageFlags(entering ? externalInfo.enteringStage : graphState.stage);
			parameters.dstStageMask |= _ToStageFlags(entering ? graphState.stage : externalInfo.leavingStage);
			parameters.imageBarriers.push_back(barrier);
			continue;
		}

		if (plan.resourceType == RenderGraph::ResourceType::IMAGE)
		{
			CHECK_TRUE(plan.image < m_images.size() && m_images[plan.image] != nullptr, "Render graph barrier image is not available!");
			const ImageView* view = m_images[plan.image]->View();
			const Image::Information& imageInfo = m_images[plan.image]->GetImageInformation();

			RenderGraph::ImageUsageState srcState;
			RenderGraph::ImageUsageState dstState;
			if (plan.before == INVALID_INDEX)
			{
				CHECK_TRUE(plan.after != INVALID_INDEX, "Render graph initial image barrier target pass is invalid!");
				dstState = m_pRenderGraph->_GetImageUsageState(plan.after, plan.image);
				srcState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
				srcState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			}
			else
			{
				CHECK_TRUE(plan.after != INVALID_INDEX, "Render graph image barrier target pass is invalid!");
				const ImageIndex sourceImage = plan.sourceImage == INVALID_INDEX ? plan.image : plan.sourceImage;
				srcState = m_pRenderGraph->_GetImageUsageState(plan.before, sourceImage);
				dstState = m_pRenderGraph->_GetImageUsageState(plan.after, plan.image);
			}

			RenderGraph::HazardType hazard = RenderGraph::HazardType::WAR;
			if (plan.before == INVALID_INDEX || srcState.writes)
			{
				hazard = dstState.writes ? RenderGraph::HazardType::WAW : RenderGraph::HazardType::RAW;
			}
			uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			const bool ownershipTransfer = funcUsesQueueOwnershipTransfer(plan, srcQueueFamilyIndex, dstQueueFamilyIndex);
			if (inMode == BarrierCommandMode::QUEUE_RELEASE && !ownershipTransfer)
			{
				continue;
			}

			const bool executionOnly = !RenderGraph::_NeedsMemoryDependency(hazard, srcState.layout, dstState.layout);
			VkAccessFlags2 srcAccess = executionOnly ? 0 : srcState.access;
			VkAccessFlags2 dstAccess = executionOnly ? 0 : dstState.access;
			if (ownershipTransfer && inMode == BarrierCommandMode::QUEUE_RELEASE)
			{
				dstAccess = 0;
			}
			else if (ownershipTransfer && inMode == BarrierCommandMode::QUEUE_ACQUIRE)
			{
				srcAccess = 0;
			}

			parameters.srcStageMask |= _ToStageFlags(srcState.stage);
			parameters.dstStageMask |= _ToStageFlags(dstState.stage);

			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(srcAccess);
			barrier.dstAccessMask = _ToAccessFlags(dstAccess);
			barrier.oldLayout = srcState.layout;
			barrier.newLayout = dstState.layout;
			barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
			barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
			barrier.image = m_images[plan.image]->GetVkImage();
			barrier.subresourceRange = view->GetImageSubresourceRange();
			if (imageInfo.imageType == VK_IMAGE_TYPE_3D)
			{
				barrier.subresourceRange.layerCount = 1;
			}
			parameters.imageBarriers.push_back(barrier);
		}
		else
		{
			CHECK_TRUE(plan.buffer < m_buffers.size() && m_buffers[plan.buffer] != nullptr, "Render graph barrier buffer is not available!");
			CHECK_TRUE(plan.before != INVALID_INDEX && plan.after != INVALID_INDEX, "Render graph buffer barrier pass edge is invalid!");
			const Buffer::Information& bufferInfo = m_buffers[plan.buffer]->GetBufferInformation();
			const BufferIndex sourceBuffer = plan.sourceBuffer == INVALID_INDEX ? plan.buffer : plan.sourceBuffer;
			const RenderGraph::BufferUsageState srcState = m_pRenderGraph->_GetBufferUsageState(plan.before, sourceBuffer);
			const RenderGraph::BufferUsageState dstState = m_pRenderGraph->_GetBufferUsageState(plan.after, plan.buffer);

			const RenderGraph::HazardType hazard = srcState.writes ? (dstState.writes ? RenderGraph::HazardType::WAW : RenderGraph::HazardType::RAW) : RenderGraph::HazardType::WAR;
			uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			const bool ownershipTransfer = funcUsesQueueOwnershipTransfer(plan, srcQueueFamilyIndex, dstQueueFamilyIndex);
			if (inMode == BarrierCommandMode::QUEUE_RELEASE && !ownershipTransfer)
			{
				continue;
			}

			const bool executionOnly = hazard == RenderGraph::HazardType::WAR;
			VkAccessFlags2 srcAccess = executionOnly ? 0 : srcState.access;
			VkAccessFlags2 dstAccess = executionOnly ? 0 : dstState.access;
			if (ownershipTransfer && inMode == BarrierCommandMode::QUEUE_RELEASE)
			{
				dstAccess = 0;
			}
			else if (ownershipTransfer && inMode == BarrierCommandMode::QUEUE_ACQUIRE)
			{
				srcAccess = 0;
			}
			parameters.srcStageMask |= _ToStageFlags(srcState.stage);
			parameters.dstStageMask |= _ToStageFlags(dstState.stage);

			VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(srcAccess);
			barrier.dstAccessMask = _ToAccessFlags(dstAccess);
			barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
			barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
			barrier.buffer = m_buffers[plan.buffer]->GetVkBuffer();
			barrier.offset = 0;
			barrier.size = bufferInfo.size;
			parameters.bufferBarriers.push_back(barrier);
		}
	}

	if (parameters.imageBarriers.empty() && parameters.bufferBarriers.empty() && parameters.memoryBarriers.empty())
	{
		return nullptr;
	}

	auto command = std::make_unique<PipelineBarrierCommand>();
	command->SetParameters(parameters);
	return command;
}

void RenderGraphInstance::Compile()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	CHECK_TRUE(m_pRenderGraph->m_built, "Render graph must be built before compiling an instance!");

	for (PassIndex index = 0; index < m_pRenderGraph->m_passes.size(); ++index)
	{
		if (index >= m_pRenderGraph->m_activePasses.size() || !m_pRenderGraph->m_activePasses[index])
		{
			continue;
		}

		CHECK_TRUE(m_passInfos[index].m_process != nullptr, "Render graph pass process is not set up!");
	}

	_SetUpPhysicalResources();
	_CreateTemporaryRenderPasses();
	_BuildCompiledGraphPlan();
	m_compiled = true;
}

void RenderGraphInstance::Execute()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	CHECK_TRUE(m_compiled, "Render graph instance must be compiled before execution!");

	auto& device = MyDevice::GetInstance();
	GraphicsQueue* graphicsQueue = device.GetGraphicsCommandQueue();
	ComputeQueue* computeQueue = device.GetComputeCommandQueue();
	CHECK_TRUE(graphicsQueue != nullptr, "Graphics command queue is not available!");
	CHECK_TRUE(computeQueue != nullptr, "Compute command queue is not available!");

	std::vector<VkSemaphore> queueSyncSemaphores(m_compiledPlan.queueSyncEdges.size(), VK_NULL_HANDLE);
	for (uint32_t edgeIndex = 0; edgeIndex < m_compiledPlan.queueSyncEdges.size(); ++edgeIndex)
	{
		queueSyncSemaphores[edgeIndex] = _AcquireSemaphore();
	}

	auto funcAppendCompiledCommands = [](const std::vector<std::unique_ptr<Command>>& inCommands, CommandBuffer& inCommandBuffer) -> bool
	{
		if (inCommands.empty())
		{
			return false;
		}

		CommandBuffer::PrimaryScope scope;
		scope.commands.reserve(inCommands.size());
		for (const std::unique_ptr<Command>& command : inCommands)
		{
			CHECK_TRUE(command != nullptr, "Compiled render graph command cannot be null!");
			scope.commands.push_back(command.get());
		}
		inCommandBuffer.AppendCommands(&scope);
		return true;
	};

	auto funcFillSyncInfo = [&](const std::vector<CompiledQueueWait>& inWaits, const std::vector<uint32_t>& inSignals)->CommandQueue::SyncInfo
	{
		CommandQueue::SyncInfo syncInfo;
		for (const CompiledQueueWait& wait : inWaits)
		{
			CHECK_TRUE(wait.syncEdge < queueSyncSemaphores.size(), "Invalid render graph queue wait sync edge!");
			const VkSemaphore semaphore = queueSyncSemaphores[wait.syncEdge];
			CHECK_TRUE(semaphore != VK_NULL_HANDLE, "Render graph wait semaphore is missing!");
			const VkPipelineStageFlags waitStage = wait.waitStage == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : wait.waitStage;
			syncInfo.AddWaitSemaphore(semaphore, waitStage);
		}

		for (uint32_t syncEdge : inSignals)
		{
			CHECK_TRUE(syncEdge < queueSyncSemaphores.size(), "Invalid render graph queue signal sync edge!");
			const VkSemaphore semaphore = queueSyncSemaphores[syncEdge];
			CHECK_TRUE(semaphore != VK_NULL_HANDLE, "Render graph signal semaphore is missing!");
			syncInfo.AddSemaphoreToSignal(semaphore);
		}

		return syncInfo;
	};

	bool submittedGraphicsCommands = false;
	bool submittedComputeCommands = false;

	for (uint32_t submitIndex = 0; submitIndex < m_compiledPlan.submitBatches.size(); ++submitIndex)
	{
		const CompiledSubmitBatch& submitBatch = m_compiledPlan.submitBatches[submitIndex];
		CommandBuffer graphicsCommandBuffer;
		CommandBuffer computeCommandBuffer;
		bool hasGraphicsCommands = false;
		bool hasComputeCommands = false;

		for (const CompiledPassGroup& group : submitBatch.graphicsGroups)
		{
			const std::vector<PassIndex>& passBatch = group.passes;
			CHECK_TRUE(!passBatch.empty(), "Render graph graphics batch cannot be empty!");

			hasGraphicsCommands = funcAppendCompiledCommands(group.prologueCommands, graphicsCommandBuffer) || hasGraphicsCommands;

			if (group.temporaryRenderPass != INVALID_INDEX)
			{
				CHECK_TRUE(group.temporaryRenderPass < m_temporaryRenderPasses.size(), "Invalid compiled temporary render pass index!");
				TemporaryRenderPass* temporaryRenderPass = &m_temporaryRenderPasses[group.temporaryRenderPass];
				_AppendRenderPassCommands(passBatch, *temporaryRenderPass, graphicsCommandBuffer);
				hasGraphicsCommands = true;
			}
			else
			{
				for (PassIndex passIndex : passBatch)
				{
					CommandBuffer::PrimaryScope passScope;
					_AppendPassCommands(passIndex, passScope);
					if (!passScope.commands.empty())
					{
						graphicsCommandBuffer.AppendCommands(&passScope);
					}
					hasGraphicsCommands = true;
				}
			}

			hasGraphicsCommands = funcAppendCompiledCommands(group.epilogueCommands, graphicsCommandBuffer) || hasGraphicsCommands;
			hasGraphicsCommands = funcAppendCompiledCommands(group.queueReleaseCommands, graphicsCommandBuffer) || hasGraphicsCommands;
		}

		for (const CompiledPassGroup& group : submitBatch.computeGroups)
		{
			CHECK_TRUE(!group.passes.empty(), "Render graph compute group cannot be empty!");
			CHECK_TRUE(group.temporaryRenderPass == INVALID_INDEX, "Compute pass group cannot use a managed render pass!");

			hasComputeCommands = funcAppendCompiledCommands(group.prologueCommands, computeCommandBuffer) || hasComputeCommands;

			for (PassIndex passIndex : group.passes)
			{
				CommandBuffer::PrimaryScope passScope;
				_AppendPassCommands(passIndex, passScope);
				if (!passScope.commands.empty())
				{
					computeCommandBuffer.AppendCommands(&passScope);
				}
				hasComputeCommands = true;
			}

			hasComputeCommands = funcAppendCompiledCommands(group.epilogueCommands, computeCommandBuffer) || hasComputeCommands;
			hasComputeCommands = funcAppendCompiledCommands(group.queueReleaseCommands, computeCommandBuffer) || hasComputeCommands;
		}

		if (hasGraphicsCommands)
		{
			CommandQueue::SyncInfo syncInfo = funcFillSyncInfo(submitBatch.graphicsWaitSyncs, submitBatch.graphicsSignalSyncs);
			graphicsQueue->Enqueue(&graphicsCommandBuffer, 1).Submit(std::move(syncInfo));
			submittedGraphicsCommands = true;
		}
		if (hasComputeCommands)
		{
			CommandQueue::SyncInfo syncInfo = funcFillSyncInfo(submitBatch.computeWaitSyncs, submitBatch.computeSignalSyncs);
			computeQueue->Enqueue(&computeCommandBuffer, 1).Submit(std::move(syncInfo));
			submittedComputeCommands = true;
		}
	}

	if (submittedGraphicsCommands)
	{
		graphicsQueue->WaitTillDone();
	}
	if (submittedComputeCommands)
	{
		computeQueue->WaitTillDone();
	}
	_RecycleExecuteSemaphores();
}
