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
	m_submitBatches.clear();
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

void RenderGraph::_ResolveDependency(BuildContext& inContext)
{
	auto addEdge = [&](PassIndex inBefore, PassIndex inAfter)
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
		m_dependencyEdges.push_back(edge);
		inContext.adjacency[inBefore].push_back(inAfter);
	};

	inContext.imageUsageRefs.assign(m_images.size(), {});
	inContext.bufferUsageRefs.assign(m_buffers.size(), {});
	inContext.adjacency.assign(m_passes.size(), {});

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
				if (lastWriter.has_value() && lastWriter->pass != ref.pass)
				{
					addEdge(lastWriter->pass, ref.pass);
				}
				else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
				{
					addEdge(firstWriter->pass, ref.pass);
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
					addEdge(lastWriter->pass, ref.pass);
				}

				for (const BuildContext::ImageUsageRef& reader : pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						addEdge(reader.pass, ref.pass);
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
					addEdge(lastWriter->pass, ref.pass);
				}
				else if (!hasExternalInitialState && firstWriter.has_value() && firstWriter->pass != ref.pass)
				{
					addEdge(firstWriter->pass, ref.pass);
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
					addEdge(lastWriter->pass, ref.pass);
				}

				for (const BuildContext::BufferUsageRef& reader : pendingReaders)
				{
					if (reader.pass != ref.pass)
					{
						addEdge(reader.pass, ref.pass);
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
}

void RenderGraph::_BuildSyncPlans(BuildContext& inContext)
{
	auto emitImagePlan = [&](const BuildContext::ImageUsageRef& inBefore, const BuildContext::ImageUsageRef& inAfter, HazardType inHazard)
	{
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

	auto emitBufferPlan = [&](const BuildContext::BufferUsageRef& inBefore, const BuildContext::BufferUsageRef& inAfter, HazardType inHazard)
	{
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

	for (ImageIndex imageIndex = 0; imageIndex < inContext.imageUsageRefs.size(); ++imageIndex)
	{
		const bool hasExternalInitialState = m_images[imageIndex].m_external;
		const auto& refs = inContext.imageUsageRefs[imageIndex];
		std::optional<BuildContext::ImageUsageRef> lastWriter;
		std::vector<BuildContext::ImageUsageRef> pendingReaders;
		std::optional<BuildContext::ImageUsageRef> firstWriter;

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

				for (const BuildContext::ImageUsageRef& reader : pendingReaders)
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

				for (const BuildContext::BufferUsageRef& reader : pendingReaders)
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
		inContext.queueSyncEdgeSet.insert(_MakeEdgeKey(edge.before, edge.after));
	}
}

void RenderGraph::_BuildScheduleAndBatches(BuildContext& inContext)
{
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

	auto sortUnique = [](auto& inValues)
	{
		std::sort(inValues.begin(), inValues.end());
		inValues.erase(std::unique(inValues.begin(), inValues.end()), inValues.end());
	};

	auto buildRenderPassMergeInfo = [&](PassIndex inPassIndex) -> BuildContext::RenderPassMergeInfo
	{
		const PassRecord& pass = m_passes[inPassIndex];
		BuildContext::RenderPassMergeInfo info;
		if (!isBatchableSubpass(inPassIndex))
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

		sortUnique(info.attachmentTokens);
		sortUnique(info.attachmentImages);
		sortUnique(info.colorAttachmentImages);
		sortUnique(info.depthAttachmentImages);
		sortUnique(info.nonAttachmentImages);
		sortUnique(info.nonAttachmentBuffers);
		sortUnique(info.writtenImages);
		sortUnique(info.writtenNonAttachmentImages);
		sortUnique(info.writtenBuffers);
		return info;
	};

	auto hasIntersection = [](const auto& inLeft, const auto& inRight) -> bool
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

	auto canMergeSubpasses = [&](PassIndex inPrev, PassIndex inNext) -> bool
	{
		if (!isBatchableSubpass(inPrev) || !isBatchableSubpass(inNext))
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

		if (hasIntersection(prev.depthAttachmentImages, next.colorAttachmentImages) ||
			hasIntersection(prev.colorAttachmentImages, next.depthAttachmentImages))
		{
			return false;
		}

		if (hasIntersection(next.nonAttachmentImages, prev.writtenImages))
		{
			return false;
		}

		if (hasIntersection(next.attachmentImages, prev.nonAttachmentImages))
		{
			return false;
		}

		if (hasIntersection(next.attachmentImages, prev.writtenNonAttachmentImages))
		{
			return false;
		}

		if (hasIntersection(next.nonAttachmentBuffers, prev.writtenBuffers))
		{
			return false;
		}

		if (hasIntersection(next.writtenBuffers, prev.nonAttachmentBuffers))
		{
			return false;
		}

		return true;
	};

	auto canMergeIntoRenderPassBatch = [&](const std::vector<PassIndex>& inBatch, PassIndex inCandidate) -> bool
	{
		if (!isBatchableSubpass(inCandidate) || inBatch.empty())
		{
			return false;
		}

		for (PassIndex index : inBatch)
		{
			if (!canMergeSubpasses(index, inCandidate))
			{
				return false;
			}
		}
		return true;
	};

	auto hasAttachmentOverlapWithBatch = [&](const std::vector<PassIndex>& inBatch, PassIndex inCandidate) -> bool
	{
		const BuildContext::RenderPassMergeInfo& candidate = inContext.passRefs[inCandidate].renderPassMergeInfo;
		for (PassIndex index : inBatch)
		{
			if (hasIntersection(inContext.passRefs[index].renderPassMergeInfo.attachmentImages, candidate.attachmentImages))
			{
				return true;
			}
		}
		return false;
	};

	for (const DependencyEdge& edge : m_dependencyEdges)
	{
		++inContext.passRefs[edge.after].indegree;
	}

	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
		BuildContext::PassBuildRef& ref = inContext.passRefs[index];
		ref.renderPassMergeInfo = buildRenderPassMergeInfo(index);
		ref.downstreamDependCount = static_cast<uint32_t>(inContext.adjacency[index].size());

		for (PassIndex next : inContext.adjacency[index])
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

	auto sortGraphicsReady = [&](std::vector<PassIndex>& inReady, const std::vector<PassIndex>& inActiveRenderPassBatch)
	{
		for (PassIndex index : inReady)
		{
			BuildContext::PassBuildRef& ref = inContext.passRefs[index];
			if (!isBatchableSubpass(index) || inActiveRenderPassBatch.empty())
			{
				ref.batchAffinity = 1u;
			}
			else if (canMergeIntoRenderPassBatch(inActiveRenderPassBatch, index))
			{
				ref.batchAffinity = hasAttachmentOverlapWithBatch(inActiveRenderPassBatch, index) ? 0u : 2u;
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

	auto fineSortQueue = [&](const std::vector<PassIndex>& inPasses, QueueType inQueue) -> std::vector<PassIndex>
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
				sortGraphicsReady(ready, activeRenderPassBatch);
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
				if (!activeRenderPassBatch.empty() && canMergeIntoRenderPassBatch(activeRenderPassBatch, index))
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

	std::vector<PassIndex> ready;
	for (PassIndex index = 0; index < m_passes.size(); ++index)
	{
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
			sortReady(currentReady);
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

	CHECK_TRUE(scheduledPassCount == m_passes.size(), "Render graph has a dependency cycle!");

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

		const std::vector<PassIndex> sortedGraphicsPasses = fineSortQueue(graphicsPasses, QueueType::GRAPHICS);
		submitBatch.computePasses = fineSortQueue(computePasses, QueueType::COMPUTE);

		for (PassIndex index : sortedGraphicsPasses)
		{
			if (isBatchableSubpass(index))
			{
				if (!submitBatch.graphicsPasses.empty() &&
					canMergeIntoRenderPassBatch(submitBatch.graphicsPasses.back(), index))
				{
					submitBatch.graphicsPasses.back().push_back(index);
				}
				else
				{
					submitBatch.graphicsPasses.push_back({ index });
				}
			}
			else
			{
				submitBatch.graphicsPasses.push_back({ index });
			}
		}

		m_submitBatches.push_back(std::move(submitBatch));
	}
}

void RenderGraph::Build()
{
	m_dependencyEdges.clear();
	m_submitBatches.clear();
	m_barrierPlans.clear();
	m_queueSyncPlans.clear();

	BuildContext context;
	_ResolveDependency(context);
	_BuildSyncPlans(context);
	_BuildScheduleAndBatches(context);

	uint32_t scheduledPassCount = 0;
	for (const SubmitBatch& submitBatch : m_submitBatches)
	{
		for (const std::vector<PassIndex>& graphicsPasses : submitBatch.graphicsPasses)
		{
			scheduledPassCount += static_cast<uint32_t>(graphicsPasses.size());
		}
		scheduledPassCount += static_cast<uint32_t>(submitBatch.computePasses.size());
	}
	CHECK_TRUE(scheduledPassCount == m_passes.size(), "Render graph has a dependency cycle!");
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

	for (BufferIndex index = 0; index < m_pRenderGraph->m_buffers.size(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_pRenderGraph->m_buffers[index];
		if (graphBuffer.m_external)
		{
			CHECK_TRUE(m_externalBufferInfos[index].has_value(), "External render graph buffer is not set up!");
			Buffer* buffer = m_externalBufferInfos[index]->pBuffer;
			CHECK_TRUE(buffer != nullptr, "External render graph buffer is null!");
			m_buffers[index] = buffer;
			m_nameToBuffer[graphBuffer.m_name] = buffer;
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
		m_nameToBuffer[graphBuffer.m_name] = buffer.get();
		m_internalBuffers.push_back(std::move(buffer));
	}

	for (ImageIndex index = 0; index < m_pRenderGraph->m_images.size(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_pRenderGraph->m_images[index];
		if (graphImage.m_external)
		{
			CHECK_TRUE(m_externalImageInfos[index].has_value(), "External render graph image is not set up!");
			Image* image = m_externalImageInfos[index]->pImage;
			CHECK_TRUE(image != nullptr, "External render graph image is null!");
			m_images[index] = image;
			m_nameToImage[graphImage.m_name] = image;
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
		m_nameToImage[graphImage.m_name] = image.get();
		m_internalImages.push_back(std::move(image));
	}
}

void RenderGraphInstance::_ResolveExternalUsageBoundaries()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");

	m_firstBufferUsagePass.assign(m_pRenderGraph->m_buffers.size(), INVALID_INDEX);
	m_lastBufferUsagePass.assign(m_pRenderGraph->m_buffers.size(), INVALID_INDEX);
	m_firstImageUsagePass.assign(m_pRenderGraph->m_images.size(), INVALID_INDEX);
	m_lastImageUsagePass.assign(m_pRenderGraph->m_images.size(), INVALID_INDEX);

	auto visitPass = [&](PassIndex inPassIndex)
	{
		const RenderGraph::PassRecord& pass = m_pRenderGraph->m_passes[inPassIndex];
		for (const RenderGraph::BufferUsage& usage : pass.bufferUsages)
		{
			const BufferIndex bufferIndex = m_pRenderGraph->_GetBufferIndex(usage.buffer);
			if (m_firstBufferUsagePass[bufferIndex] == INVALID_INDEX)
			{
				m_firstBufferUsagePass[bufferIndex] = inPassIndex;
			}
			m_lastBufferUsagePass[bufferIndex] = inPassIndex;
		}
		for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
		{
			const ImageIndex imageIndex = m_pRenderGraph->_GetImageIndex(usage.image);
			if (m_firstImageUsagePass[imageIndex] == INVALID_INDEX)
			{
				m_firstImageUsagePass[imageIndex] = inPassIndex;
			}
			m_lastImageUsagePass[imageIndex] = inPassIndex;
		}
	};

	for (const RenderGraph::SubmitBatch& submitBatch : m_pRenderGraph->m_submitBatches)
	{
		for (const std::vector<PassIndex>& graphicsBatch : submitBatch.graphicsPasses)
		{
			for (PassIndex passIndex : graphicsBatch)
			{
				visitPass(passIndex);
			}
		}
		for (PassIndex passIndex : submitBatch.computePasses)
		{
			visitPass(passIndex);
		}
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
		m_graphicsBatchToTemporaryRenderPass[submitIndex].assign(submitBatch.graphicsPasses.size(), INVALID_INDEX);

		for (uint32_t batchIndex = 0; batchIndex < submitBatch.graphicsPasses.size(); ++batchIndex)
		{
			const std::vector<PassIndex>& passBatch = submitBatch.graphicsPasses[batchIndex];
			CHECK_TRUE(!passBatch.empty(), "Render graph graphics pass batch cannot be empty!");
			if (passBatch.size() == 1 && m_pRenderGraph->m_passes[passBatch.front()].type != RenderGraph::PassType::SUBPASS)
			{
				continue;
			}

			std::vector<ImageIndex> attachmentImages;
			std::unordered_map<ImageIndex, uint32_t> imageToAttachment;
			std::vector<std::vector<std::pair<uint32_t, VkImageLayout>>> colorReferences;
			std::vector<std::optional<std::pair<uint32_t, VkImageLayout>>> depthReferences;

			colorReferences.resize(passBatch.size());
			depthReferences.resize(passBatch.size());

			auto getAttachmentIndex = [&](ImageIndex inImageIndex)->uint32_t
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
					const uint32_t attachmentIndex = getAttachmentIndex(imageIndex);
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

				for (const RenderGraph::BarrierPlan& plan : m_pRenderGraph->m_barrierPlans)
				{
					if (plan.resourceType != RenderGraph::ResourceType::IMAGE || plan.image != imageIndex)
					{
						continue;
					}

					const bool barrierAfterBatch = std::find(passBatch.begin(), passBatch.end(), plan.after) != passBatch.end();
					const bool barrierBeforeBatch = std::find(passBatch.begin(), passBatch.end(), plan.before) != passBatch.end();
					if (barrierAfterBatch && !barrierBeforeBatch)
					{
						hasIncomingBarrier = true;
						break;
					}
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

				if (subpassIndex > 0)
				{
					description.AddDependencyOnSubpass(
						"subpass_" + std::to_string(subpassIndex - 1),
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
						VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
						VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
				}
				description.CustomizeAvailableState(
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
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

void RenderGraphInstance::_AppendBarriersBeforePasses(
	const std::vector<PassIndex>& inPasses,
	bool inIgnoreInternalDependencies,
	CommandBuffer::PrimaryScope& inPrimaryScope,
	std::vector<std::unique_ptr<Command>>& inoutOwnedCommands)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	if (inPasses.empty())
	{
		return;
	}

	std::unordered_set<PassIndex> passSet(inPasses.begin(), inPasses.end());
	PipelineBarrierCommand::Parameters parameters;

	for (const RenderGraph::BarrierPlan& plan : m_pRenderGraph->m_barrierPlans)
	{
		if (passSet.find(plan.after) == passSet.end())
		{
			continue;
		}
		if (inIgnoreInternalDependencies && passSet.find(plan.before) != passSet.end())
		{
			continue;
		}

		parameters.srcStageMask |= _ToStageFlags(plan.srcStage);
		parameters.dstStageMask |= _ToStageFlags(plan.dstStage);

		if (plan.resourceType == RenderGraph::ResourceType::IMAGE)
		{
			CHECK_TRUE(plan.image < m_images.size() && m_images[plan.image] != nullptr, "Render graph barrier image is not available!");
			const ImageView* view = m_images[plan.image]->View();
			const Image::Information& imageInfo = m_images[plan.image]->GetImageInformation();

			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(plan.srcAccess);
			barrier.dstAccessMask = _ToAccessFlags(plan.dstAccess);
			barrier.oldLayout = plan.oldLayout;
			barrier.newLayout = plan.newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
			const Buffer::Information& bufferInfo = m_buffers[plan.buffer]->GetBufferInformation();

			VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(plan.srcAccess);
			barrier.dstAccessMask = _ToAccessFlags(plan.dstAccess);
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = m_buffers[plan.buffer]->GetVkBuffer();
			barrier.offset = 0;
			barrier.size = bufferInfo.size;
			parameters.bufferBarriers.push_back(barrier);
		}
	}

	if (parameters.imageBarriers.empty() && parameters.bufferBarriers.empty() && parameters.memoryBarriers.empty())
	{
		return;
	}

	auto command = std::make_unique<PipelineBarrierCommand>();
	command->SetParameters(parameters);
	inPrimaryScope.commands.push_back(command.get());
	inoutOwnedCommands.push_back(std::move(command));
}

void RenderGraphInstance::_AppendExternalBarriers(
	const std::vector<PassIndex>& inPasses,
	bool inBeforePasses,
	CommandBuffer::PrimaryScope& inPrimaryScope,
	std::vector<std::unique_ptr<Command>>& inoutOwnedCommands)
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	if (inPasses.empty())
	{
		return;
	}

	std::unordered_set<PassIndex> passSet(inPasses.begin(), inPasses.end());
	PipelineBarrierCommand::Parameters parameters;

	for (BufferIndex bufferIndex = 0; bufferIndex < m_pRenderGraph->m_buffers.size(); ++bufferIndex)
	{
		if (!m_pRenderGraph->m_buffers[bufferIndex].m_external)
		{
			continue;
		}

		const PassIndex boundaryPass = inBeforePasses ? m_firstBufferUsagePass[bufferIndex] : m_lastBufferUsagePass[bufferIndex];
		if (boundaryPass == INVALID_INDEX || passSet.find(boundaryPass) == passSet.end())
		{
			continue;
		}

		CHECK_TRUE(bufferIndex < m_externalBufferInfos.size() && m_externalBufferInfos[bufferIndex].has_value(), "External render graph buffer is not set up!");
		CHECK_TRUE(m_buffers[bufferIndex] != nullptr, "External render graph buffer is not available!");

		const ExternalBufferInfo& externalInfo = m_externalBufferInfos[bufferIndex].value();
		const RenderGraph::PassRecord& pass = m_pRenderGraph->m_passes[boundaryPass];
		VkPipelineStageFlags2 graphStage = 0;
		VkAccessFlags2 graphAccess = 0;
		for (const RenderGraph::BufferUsage& usage : pass.bufferUsages)
		{
			if (m_pRenderGraph->_GetBufferIndex(usage.buffer) == bufferIndex)
			{
				graphStage |= usage.stage;
				graphAccess |= usage.access;
			}
		}
		CHECK_TRUE(graphStage != 0, "External render graph buffer boundary stage is empty!");

		const Buffer::Information& bufferInfo = m_buffers[bufferIndex]->GetBufferInformation();
		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = _ToAccessFlags(inBeforePasses ? externalInfo.enteringAccess : graphAccess);
		barrier.dstAccessMask = _ToAccessFlags(inBeforePasses ? graphAccess : externalInfo.leavingAccess);
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = m_buffers[bufferIndex]->GetVkBuffer();
		barrier.offset = 0;
		barrier.size = bufferInfo.size;
		parameters.srcStageMask |= _ToStageFlags(inBeforePasses ? externalInfo.enteringStage : graphStage);
		parameters.dstStageMask |= _ToStageFlags(inBeforePasses ? graphStage : externalInfo.leavingStage);
		parameters.bufferBarriers.push_back(barrier);
	}

	for (ImageIndex imageIndex = 0; imageIndex < m_pRenderGraph->m_images.size(); ++imageIndex)
	{
		if (!m_pRenderGraph->m_images[imageIndex].m_external)
		{
			continue;
		}

		const PassIndex boundaryPass = inBeforePasses ? m_firstImageUsagePass[imageIndex] : m_lastImageUsagePass[imageIndex];
		if (boundaryPass == INVALID_INDEX || passSet.find(boundaryPass) == passSet.end())
		{
			continue;
		}

		CHECK_TRUE(imageIndex < m_externalImageInfos.size() && m_externalImageInfos[imageIndex].has_value(), "External render graph image is not set up!");
		CHECK_TRUE(m_images[imageIndex] != nullptr, "External render graph image is not available!");

		const ExternalImageInfo& externalInfo = m_externalImageInfos[imageIndex].value();
		const RenderGraph::PassRecord& pass = m_pRenderGraph->m_passes[boundaryPass];
		VkPipelineStageFlags2 graphStage = 0;
		VkAccessFlags2 graphAccess = 0;
		VkImageLayout graphLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
		{
			if (m_pRenderGraph->_GetImageIndex(usage.image) == imageIndex)
			{
				graphStage |= usage.stage;
				graphAccess |= usage.access;
				graphLayout = usage.layout;
			}
		}
		CHECK_TRUE(graphStage != 0, "External render graph image boundary stage is empty!");

		VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.srcAccessMask = _ToAccessFlags(inBeforePasses ? externalInfo.enteringAccess : graphAccess);
		barrier.dstAccessMask = _ToAccessFlags(inBeforePasses ? graphAccess : externalInfo.leavingAccess);
		barrier.oldLayout = inBeforePasses ? externalInfo.enteringLayout : graphLayout;
		barrier.newLayout = inBeforePasses ? graphLayout : externalInfo.leavingLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_images[imageIndex]->GetVkImage();
		barrier.subresourceRange = m_images[imageIndex]->View()->GetImageSubresourceRange();
		parameters.srcStageMask |= _ToStageFlags(inBeforePasses ? externalInfo.enteringStage : graphStage);
		parameters.dstStageMask |= _ToStageFlags(inBeforePasses ? graphStage : externalInfo.leavingStage);
		parameters.imageBarriers.push_back(barrier);
	}

	if (parameters.imageBarriers.empty() && parameters.bufferBarriers.empty() && parameters.memoryBarriers.empty())
	{
		return;
	}

	CHECK_TRUE(parameters.srcStageMask != 0 && parameters.dstStageMask != 0, "External render graph barrier stage cannot be empty!");
	auto command = std::make_unique<PipelineBarrierCommand>();
	command->SetParameters(parameters);
	inPrimaryScope.commands.push_back(command.get());
	inoutOwnedCommands.push_back(std::move(command));
}

void RenderGraphInstance::Compile()
{
	CHECK_TRUE(m_pRenderGraph != nullptr, "No render graph!");
	CHECK_TRUE(m_pRenderGraph->m_built, "Render graph must be built before compiling an instance!");

	for (PassIndex index = 0; index < m_pRenderGraph->m_passes.size(); ++index)
	{
		CHECK_TRUE(m_passInfos[index].m_process != nullptr, "Render graph pass process is not set up!");
	}

	_SetUpPhysicalResources();
	_ResolveExternalUsageBoundaries();
	_CreateTemporaryRenderPasses();
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

	for (uint32_t submitIndex = 0; submitIndex < m_pRenderGraph->m_submitBatches.size(); ++submitIndex)
	{
		const RenderGraph::SubmitBatch& submitBatch = m_pRenderGraph->m_submitBatches[submitIndex];
		CommandBuffer graphicsCommandBuffer;
		CommandBuffer computeCommandBuffer;
		std::vector<std::unique_ptr<Command>> ownedCommands;
		bool hasGraphicsCommands = false;
		bool hasComputeCommands = false;

		for (uint32_t graphicsBatchIndex = 0; graphicsBatchIndex < submitBatch.graphicsPasses.size(); ++graphicsBatchIndex)
		{
			const std::vector<PassIndex>& passBatch = submitBatch.graphicsPasses[graphicsBatchIndex];
			CHECK_TRUE(!passBatch.empty(), "Render graph graphics batch cannot be empty!");

			if (TemporaryRenderPass* temporaryRenderPass = _GetTemporaryRenderPass(submitIndex, graphicsBatchIndex))
			{
				CommandBuffer::PrimaryScope barrierScope;
				_AppendExternalBarriers(passBatch, true, barrierScope, ownedCommands);
				_AppendBarriersBeforePasses(passBatch, true, barrierScope, ownedCommands);
				if (!barrierScope.commands.empty())
				{
					graphicsCommandBuffer.AppendCommands(&barrierScope);
				}

				_AppendRenderPassCommands(passBatch, *temporaryRenderPass, graphicsCommandBuffer);

				CommandBuffer::PrimaryScope epilogueBarrierScope;
				_AppendExternalBarriers(passBatch, false, epilogueBarrierScope, ownedCommands);
				if (!epilogueBarrierScope.commands.empty())
				{
					graphicsCommandBuffer.AppendCommands(&epilogueBarrierScope);
				}
				hasGraphicsCommands = true;
				continue;
			}

			for (PassIndex passIndex : passBatch)
			{
				CommandBuffer::PrimaryScope primaryScope;
				_AppendExternalBarriers({ passIndex }, true, primaryScope, ownedCommands);
				_AppendBarriersBeforePasses({ passIndex }, false, primaryScope, ownedCommands);
				_AppendPassCommands(passIndex, primaryScope);
				_AppendExternalBarriers({ passIndex }, false, primaryScope, ownedCommands);

				if (!primaryScope.commands.empty())
				{
					graphicsCommandBuffer.AppendCommands(&primaryScope);
					hasGraphicsCommands = true;
				}
			}
		}

		if (!submitBatch.computePasses.empty())
		{
			for (PassIndex passIndex : submitBatch.computePasses)
			{
				CommandBuffer::PrimaryScope primaryScope;
				_AppendExternalBarriers({ passIndex }, true, primaryScope, ownedCommands);
				_AppendBarriersBeforePasses({ passIndex }, false, primaryScope, ownedCommands);
				_AppendPassCommands(passIndex, primaryScope);
				_AppendExternalBarriers({ passIndex }, false, primaryScope, ownedCommands);

				if (!primaryScope.commands.empty())
				{
					computeCommandBuffer.AppendCommands(&primaryScope);
					hasComputeCommands = true;
				}
			}
		}

		if (hasGraphicsCommands)
		{
			graphicsQueue->Enqueue(&graphicsCommandBuffer, 1).Submit(CommandQueue::SyncInfo{});
		}
		if (hasGraphicsCommands && hasComputeCommands)
		{
			graphicsQueue->WaitTillDone();
		}
		if (hasComputeCommands)
		{
			computeQueue->Enqueue(&computeCommandBuffer, 1).Submit(CommandQueue::SyncInfo{});
		}
		if (hasGraphicsCommands)
		{
			graphicsQueue->WaitTillDone();
		}
		if (hasComputeCommands)
		{
			computeQueue->WaitTillDone();
		}
	}
}
