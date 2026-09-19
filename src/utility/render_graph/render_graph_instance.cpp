#include "render_graph_instance.h"

#include "buffer.h"
#include "command_buffer.h"
#include "command_queue.h"
#include "device.h"
#include "graphics_shader_program.h"
#include "image.h"

#include <algorithm>
#include <unordered_set>

namespace
{
    auto _MakeEdgeKey(uint32_t inBefore, uint32_t inAfter)->uint64_t
    {
        return (static_cast<uint64_t>(inBefore) << 32u) | static_cast<uint64_t>(inAfter);
    }

    auto _MakeImageViewInfo(const RenderGraph::ImageSubresourceRange& inRange)->ImageViewInfo
    {
        ImageViewInfo viewInfo;
        viewInfo.CustomizeMipLevels(inRange.baseMipLevel, inRange.levelCount);
        viewInfo.CustomizeArrayLayers(inRange.baseArrayLayer, inRange.layerCount);
        return viewInfo;
    }

    auto _MipExtent(uint32_t inExtent, uint32_t inMipLevel)->uint32_t
    {
        return std::max(1u, inExtent >> inMipLevel);
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

void RenderGraphInstance::ExecutionContext::ConfigureGraphicsPipelineState(
	GraphicsPipelineStateInfo& inoutStateInfo) const
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	CHECK_TRUE(m_currentPass < m_pInstance->m_buildResult.GetPassCount(), "No current render graph pass!");
	const RenderGraph::PassType passType = m_pInstance->m_buildResult.GetPass(m_currentPass).type;
	CHECK_TRUE(
		passType == RenderGraph::PassType::RENDER_PASS || passType == RenderGraph::PassType::SUBPASS,
		"Graphics pipeline state can only be configured by a graph-managed render pass!");
	CHECK_TRUE(m_pRenderPass != nullptr, "Managed render pass is not available!");
	CHECK_TRUE(m_currentSubpass != INVALID_INDEX, "Managed render pass subpass index is not available!");
	inoutStateInfo.SetRenderPassSubpass(m_pRenderPass, m_currentSubpass);
}

void RenderGraphInstance::ExecutionContext::RecordCommands(
	std::function<void(CommandBuffer*)> inProcess)
{
	CHECK_TRUE(m_pInstance != nullptr, "Render graph execution context is not initialized!");
	CHECK_TRUE(inProcess != nullptr, "Render graph pass command recording process is empty!");
	CHECK_TRUE(m_currentPass < m_pInstance->m_buildResult.GetPassCount(), "No current render graph pass!");
	const RenderGraph::PassType passType = m_pInstance->m_buildResult.GetPass(m_currentPass).type;
	if (passType == RenderGraph::PassType::RENDER_PASS || passType == RenderGraph::PassType::SUBPASS)
	{
		m_pInstance->_RecordSubpassCommandBuffer(m_currentPass, std::move(inProcess), *this);
		return;
	}

	CHECK_TRUE(m_pCommandBuffer != nullptr, "Command recording needs a command buffer!");
	inProcess(m_pCommandBuffer);
}

void RenderGraphInstance::PassInfo::SetProcess(PassProcess inProcess)
{
	CHECK_TRUE(inProcess != nullptr, "Render graph pass process cannot be empty!");
	m_process = std::move(inProcess);
}

void RenderGraphInstance::PassInfo::CustomizeColorClearValue(
	uint32_t inLocation,
	const VkClearColorValue& inClearValue)
{
	m_colorClearValueOverrides[inLocation] = inClearValue;
}

void RenderGraphInstance::PassInfo::CustomizeDepthStencilClearValue(
	const VkClearDepthStencilValue& inClearValue)
{
	m_depthStencilClearValueOverride = inClearValue;
}

void RenderGraphInstance::ExecuteInfo::SetUpPass(
	const std::string& inName,
	const PassInfo& inPassInfo)
{
	CHECK_TRUE(!inName.empty(), "Render graph pass name cannot be empty!");
	const auto iter = std::find_if(m_passes.begin(), m_passes.end(),
		[&](const PassBinding& inBinding)
		{
			return inBinding.name == inName;
		});
	CHECK_TRUE(iter == m_passes.end(), "Render graph pass is already set up!");
	m_passes.push_back({ inName, inPassInfo });
}

void RenderGraphInstance::ExecuteInfo::SetUpExternalBuffer(
	const std::string& inName,
	const ExternalBufferInfo& inBufferInfo)
{
	CHECK_TRUE(!inName.empty(), "External buffer name cannot be empty!");
	CHECK_TRUE(inBufferInfo.pBuffer != nullptr, "External render graph buffer is null!");
	const auto iter = std::find_if(m_externalBuffers.begin(), m_externalBuffers.end(),
		[&](const ExternalBufferBinding& inBinding)
		{
			return inBinding.name == inName;
		});
	CHECK_TRUE(iter == m_externalBuffers.end(), "External render graph buffer is already set up!");
	m_externalBuffers.push_back({ inName, inBufferInfo });
}

void RenderGraphInstance::ExecuteInfo::SetUpExternalImage(
	const std::string& inName,
	const ExternalImageInfo& inImageInfo)
{
	CHECK_TRUE(!inName.empty(), "External image name cannot be empty!");
	CHECK_TRUE(inImageInfo.pImage != nullptr, "External render graph image is null!");
	const auto iter = std::find_if(m_externalImages.begin(), m_externalImages.end(),
		[&](const ExternalImageBinding& inBinding)
		{
			return inBinding.name == inName;
		});
	CHECK_TRUE(iter == m_externalImages.end(), "External render graph image is already set up!");
	m_externalImages.push_back({ inName, inImageInfo });
}

void RenderGraphInstance::ExecuteInfo::SetGraphicsCompletionFence(HostFence& inCompletionFence)
{
	m_graphicsCompletionFence = &inCompletionFence;
}

void RenderGraphInstance::ExecuteInfo::SetComputeCompletionFence(HostFence& inCompletionFence)
{
	m_computeCompletionFence = &inCompletionFence;
}

RenderGraphInstance::RenderGraphInstance(const RenderGraph& inRenderGraph)
	: m_buildResult(inRenderGraph.GetBuildResult())
{
	CHECK_TRUE(m_buildResult.IsValid(), "Render graph must be built before creating an instance!");

	m_buffers.resize(m_buildResult.GetBufferCount(), nullptr);
	m_images.resize(m_buildResult.GetImageCount(), nullptr);
	m_externalBufferInfos.resize(m_buildResult.GetBufferCount());
	m_externalImageInfos.resize(m_buildResult.GetImageCount());
	m_passInfos.resize(m_buildResult.GetPassCount());
	_SetUpInternalResources();
}

RenderGraphInstance::~RenderGraphInstance()
{
	_DestroyManagedRenderPasses();
	_DestroyInternalResources();
}

void RenderGraphInstance::_DestroyManagedRenderPasses()
{
	for (ManagedRenderPass& renderPass : m_managedRenderPasses)
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

	m_managedRenderPasses.clear();
	m_graphicsBatchToManagedRenderPass.clear();
}

void RenderGraphInstance::_DestroyInternalResources()
{
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

void RenderGraphInstance::_ApplyExternalResources(const ExecuteInfo& inExecuteInfo)
{
	std::fill(m_externalBufferInfos.begin(), m_externalBufferInfos.end(), std::nullopt);
	std::fill(m_externalImageInfos.begin(), m_externalImageInfos.end(), std::nullopt);

	for (const ExecuteInfo::ExternalBufferBinding& binding : inExecuteInfo.m_externalBuffers)
	{
		const BufferIndex index = m_buildResult.GetBufferIndex(binding.name);
		CHECK_TRUE(m_buildResult.GetBufferInfo(index).m_external, "Render graph buffer is not external!");
		CHECK_TRUE(binding.info.pBuffer != nullptr, "External render graph buffer is null!");
		CHECK_TRUE(!m_externalBufferInfos[index].has_value(), "External render graph buffer is already set up!");
		m_externalBufferInfos[index] = binding.info;
	}

	for (const ExecuteInfo::ExternalImageBinding& binding : inExecuteInfo.m_externalImages)
	{
		const ImageIndex index = m_buildResult.GetImageIndex(binding.name);
		CHECK_TRUE(m_buildResult.GetImageInfo(index).m_external, "Render graph image is not external!");
		CHECK_TRUE(binding.info.pImage != nullptr, "External render graph image is null!");
		CHECK_TRUE(!m_externalImageInfos[index].has_value(), "External render graph image is already set up!");
		m_externalImageInfos[index] = binding.info;
	}
}

void RenderGraphInstance::_ApplyPasses(const ExecuteInfo& inExecuteInfo)
{
	m_passInfos.assign(m_buildResult.GetPassCount(), {});
	std::vector<bool> configuredPasses(m_buildResult.GetPassCount(), false);

	for (const ExecuteInfo::PassBinding& binding : inExecuteInfo.m_passes)
	{
		const PassIndex index = m_buildResult.GetPassIndex(binding.name);
		CHECK_TRUE(index < configuredPasses.size(), "Invalid render graph pass index!");
		CHECK_TRUE(!configuredPasses[index], "Render graph pass is already set up!");
		CHECK_TRUE(binding.info.m_process != nullptr, "Render graph pass process cannot be empty!");

		const RenderGraph::PassRecord& pass = m_buildResult.GetPass(index);
		CHECK_TRUE(
			pass.type == RenderGraph::PassType::RENDER_PASS || pass.type == RenderGraph::PassType::SUBPASS ||
			(binding.info.m_colorClearValueOverrides.empty() && !binding.info.m_depthStencilClearValueOverride.has_value()),
			"Clear values can only be overridden for graph-managed render passes!");
		for (const auto& clearOverride : binding.info.m_colorClearValueOverrides)
		{
			const uint32_t location = clearOverride.first;
			const auto usageIter = std::find_if(pass.imageUsages.begin(), pass.imageUsages.end(),
				[&](const RenderGraph::ImageUsage& inUsage)
				{
					return inUsage.type == RenderGraph::ResourceUsageType::COLOR_ATTACHMENT &&
						inUsage.attachmentSlot == location;
				});
			CHECK_TRUE(usageIter != pass.imageUsages.end(), "Color attachment location does not exist in the pass!");
			CHECK_TRUE(usageIter->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR, "Only a color attachment with CLEAR loadOp can override its clear value!");
		}
		if (binding.info.m_depthStencilClearValueOverride.has_value())
		{
			const auto usageIter = std::find_if(pass.imageUsages.begin(), pass.imageUsages.end(),
				[](const RenderGraph::ImageUsage& inUsage)
				{
					return inUsage.type == RenderGraph::ResourceUsageType::DEPTH_STENCIL_ATTACHMENT;
				});
			CHECK_TRUE(usageIter != pass.imageUsages.end(), "Depth stencil attachment does not exist in the pass!");
			CHECK_TRUE(
				usageIter->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || usageIter->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
				"Only a depth stencil attachment with CLEAR loadOp can override its clear value!");
		}

		m_passInfos[index] = binding.info;
		configuredPasses[index] = true;
	}

	for (PassIndex index = 0; index < m_buildResult.GetPassCount(); ++index)
	{
		if (!m_buildResult.GetPass(index).active)
		{
			continue;
		}
		CHECK_TRUE(configuredPasses[index], "Active render graph pass is not set up for this execution!");
	}
}

auto RenderGraphInstance::_GetBuffer(const std::string& inName) const -> Buffer*
{
	const BufferIndex index = m_buildResult.GetBufferIndex(inName);
	CHECK_TRUE(index < m_buffers.size() && m_buffers[index] != nullptr, "Render graph buffer is not available!");
	return m_buffers[index];
}

auto RenderGraphInstance::_GetImage(const std::string& inName) const -> Image*
{
	const ImageIndex index = m_buildResult.GetImageIndex(inName);
	CHECK_TRUE(index < m_images.size() && m_images[index] != nullptr, "Render graph image is not available!");
	return m_images[index];
}

void RenderGraphInstance::_SetUpInternalResources()
{
	_DestroyManagedRenderPasses();
	_DestroyInternalResources();

	m_buffers.assign(m_buildResult.GetBufferCount(), nullptr);
	m_images.assign(m_buildResult.GetImageCount(), nullptr);

	for (BufferIndex index = 0; index < m_buildResult.GetBufferCount(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_buildResult.GetBufferInfo(index);
		if (graphBuffer.m_external)
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

	for (BufferIndex index = 0; index < m_buildResult.GetBufferCount(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_buildResult.GetBufferInfo(index);
		CHECK_TRUE(graphBuffer.m_external || m_buffers[index] != nullptr,
			"Internal render graph buffer is not available!");
	}

	for (ImageIndex index = 0; index < m_buildResult.GetImageCount(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_buildResult.GetImageInfo(index);
		if (graphImage.m_external)
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
		createInfo.CustomizeMipLevels(graphImage.m_mipLevels);
		createInfo.CustomizeArrayLayers(graphImage.m_arrayLayers);
		if (graphImage.m_optFormat.has_value()) createInfo.CustomizeFormat(graphImage.m_optFormat.value());
		if (graphImage.m_optTiling.has_value()) createInfo.CustomizeImageTiling(graphImage.m_optTiling.value());
		if (graphImage.m_optMemoryProperty.has_value()) createInfo.CustomizeMemoryProperty(graphImage.m_optMemoryProperty.value());
		if (graphImage.m_optSampleCount.has_value()) createInfo.CustomizeSampleCount(graphImage.m_optSampleCount.value());

		auto image = std::make_unique<Image>();
		image->Create(&createInfo);
		m_images[index] = image.get();
		m_internalImages.push_back(std::move(image));
	}

	for (ImageIndex index = 0; index < m_buildResult.GetImageCount(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_buildResult.GetImageInfo(index);
		CHECK_TRUE(graphImage.m_external || m_images[index] != nullptr,
			"Internal render graph image is not available!");
	}
}

void RenderGraphInstance::_BindExternalResources()
{
	for (BufferIndex index = 0; index < m_buildResult.GetBufferCount(); ++index)
	{
		const RenderGraph::BufferInfo& graphBuffer = m_buildResult.GetBufferInfo(index);
		if (graphBuffer.m_external)
		{
			CHECK_TRUE(m_externalBufferInfos[index].has_value(), "External render graph buffer is not set up!");
			Buffer* buffer = m_externalBufferInfos[index]->pBuffer;
			CHECK_TRUE(buffer != nullptr, "External render graph buffer is null!");
			m_buffers[index] = buffer;
		}
		CHECK_TRUE(m_buffers[index] != nullptr, "Render graph buffer is not available!");
	}

	for (ImageIndex index = 0; index < m_buildResult.GetImageCount(); ++index)
	{
		const RenderGraph::ImageInfo& graphImage = m_buildResult.GetImageInfo(index);
		if (graphImage.m_external)
		{
			CHECK_TRUE(m_externalImageInfos[index].has_value(), "External render graph image is not set up!");
			Image* image = m_externalImageInfos[index]->pImage;
			CHECK_TRUE(image != nullptr, "External render graph image is null!");
			m_images[index] = image;
		}
		CHECK_TRUE(m_images[index] != nullptr, "Render graph image is not available!");
		const Image::Information& imageInfo = m_images[index]->GetImageInformation();
		CHECK_TRUE((imageInfo.usage & graphImage.m_usage) == graphImage.m_usage, "Render graph image is missing required usages!");
		CHECK_TRUE(imageInfo.imageType == graphImage.m_type, "Render graph image type does not match its declaration!");
		CHECK_TRUE(imageInfo.mipLevels >= graphImage.m_mipLevels, "Render graph image has too few mip levels!");
		CHECK_TRUE(imageInfo.arrayLayers >= graphImage.m_arrayLayers, "Render graph image has too few array layers!");
		if (graphImage.m_optWidth.has_value()) CHECK_TRUE(imageInfo.width == graphImage.m_optWidth.value(), "Render graph image width does not match its declaration!");
		if (graphImage.m_optHeight.has_value()) CHECK_TRUE(imageInfo.height == graphImage.m_optHeight.value(), "Render graph image height does not match its declaration!");
		if (graphImage.m_optDepth.has_value()) CHECK_TRUE(imageInfo.depth == graphImage.m_optDepth.value(), "Render graph image depth does not match its declaration!");
		if (graphImage.m_optFormat.has_value()) CHECK_TRUE(imageInfo.format == graphImage.m_optFormat.value(), "Render graph image format does not match its declaration!");
		if (graphImage.m_optSampleCount.has_value()) CHECK_TRUE(imageInfo.samples == graphImage.m_optSampleCount.value(), "Render graph image sample count does not match its declaration!");
	}
}

void RenderGraphInstance::_CreateManagedRenderPasses()
{
	m_graphicsBatchToManagedRenderPass.clear();
	m_graphicsBatchToManagedRenderPass.resize(m_buildResult.GetSubmitBatchCount());

	for (uint32_t submitIndex = 0; submitIndex < m_buildResult.GetSubmitBatchCount(); ++submitIndex)
	{
		const RenderGraph::SubmitBatch& submitBatch = m_buildResult.GetSubmitBatch(submitIndex);
		m_graphicsBatchToManagedRenderPass[submitIndex].assign(submitBatch.graphicsGroups.size(), INVALID_INDEX);

		for (uint32_t batchIndex = 0; batchIndex < submitBatch.graphicsGroups.size(); ++batchIndex)
		{
			const RenderGraph::SubmitBatch::PassGroupPlan& group = submitBatch.graphicsGroups[batchIndex];
			const std::vector<PassIndex>& passBatch = group.passes;
			CHECK_TRUE(!passBatch.empty(), "Render graph graphics pass batch cannot be empty!");
			if (!group.managedRenderPass)
			{
				continue;
			}
			CHECK_TRUE(group.renderPassPlan.has_value(), "Managed render pass group has no compiled plan!");
			const RenderGraph::SubmitBatch::ManagedRenderPassPlan& managedPlan = group.renderPassPlan.value();
			CHECK_TRUE(managedPlan.subpasses.size() == passBatch.size(), "Managed render pass subpass count is inconsistent!");
			for (const RenderGraph::SubmitBatch::ManagedAttachmentPlan& attachment : managedPlan.attachments)
			{
				CHECK_TRUE(attachment.image < m_images.size() && m_images[attachment.image] != nullptr, "Managed attachment image is unavailable!");
				const Image::Information& imageInfo = m_images[attachment.image]->GetImageInformation();
				CHECK_TRUE(imageInfo.imageType == VK_IMAGE_TYPE_2D, "Managed framebuffer attachments must be 2D images!");
				CHECK_TRUE(attachment.subresourceRange.levelCount == 1, "Managed framebuffer attachments must select exactly one mip level!");
				const VkFormat format = imageInfo.format;
				if (attachment.role == RenderGraph::ResourceUsageType::DEPTH_STENCIL_ATTACHMENT)
				{
					CHECK_TRUE(_IsDepthStencilFormat(format), "Depth stencil attachment must use a depth stencil format!");
				}
				else
				{
					CHECK_TRUE(!_IsDepthStencilFormat(format), "Color and resolve attachments cannot use a depth stencil format!");
				}
			}
			for (const RenderGraph::SubmitBatch::ManagedSubpassPlan& subpass : managedPlan.subpasses)
			{
				std::optional<VkSampleCountFlagBits> rasterSamples;
				for (uint32_t location = 0; location < subpass.colorAttachments.size(); ++location)
				{
					const uint32_t colorIndex = subpass.colorAttachments[location];
					if (colorIndex == INVALID_INDEX)
					{
						continue;
					}
					CHECK_TRUE(colorIndex < managedPlan.attachments.size(), "Invalid color attachment plan index!");
					const auto colorSamples = m_images[managedPlan.attachments[colorIndex].image]->GetImageInformation().samples;
					CHECK_TRUE(!rasterSamples.has_value() || rasterSamples.value() == colorSamples, "Subpass color attachments must use the same sample count!");
					rasterSamples = colorSamples;
					if (location < subpass.resolveAttachments.size() && subpass.resolveAttachments[location] != INVALID_INDEX)
					{
						const uint32_t resolveIndex = subpass.resolveAttachments[location];
						CHECK_TRUE(resolveIndex < managedPlan.attachments.size(), "Invalid resolve attachment plan index!");
						const Image::Information& colorInfo = m_images[managedPlan.attachments[colorIndex].image]->GetImageInformation();
						const Image::Information& resolveInfo = m_images[managedPlan.attachments[resolveIndex].image]->GetImageInformation();
						const auto resolveSamples = resolveInfo.samples;
						CHECK_TRUE(colorSamples != VK_SAMPLE_COUNT_1_BIT, "Resolve source color attachment must be multisampled!");
						CHECK_TRUE(resolveSamples == VK_SAMPLE_COUNT_1_BIT, "Resolve attachment must be single-sampled!");
						CHECK_TRUE(colorInfo.format == resolveInfo.format, "Resolve source and destination attachments must have the same format!");
					}
				}
				if (subpass.depthStencilAttachment.has_value())
				{
					const uint32_t depthIndex = subpass.depthStencilAttachment.value();
					CHECK_TRUE(depthIndex < managedPlan.attachments.size(), "Invalid depth stencil attachment plan index!");
					const auto depthSamples = m_images[managedPlan.attachments[depthIndex].image]->GetImageInformation().samples;
					CHECK_TRUE(!rasterSamples.has_value() || rasterSamples.value() == depthSamples, "Depth stencil and color attachments must use the same sample count!");
				}
			}

			std::unordered_map<PassIndex, uint32_t> passToSubpass;
			passToSubpass.reserve(passBatch.size());
			for (uint32_t subpassIndex = 0; subpassIndex < passBatch.size(); ++subpassIndex)
			{
				passToSubpass.emplace(passBatch[subpassIndex], subpassIndex);
			}

			std::vector<VkPipelineStageFlags2> subpassAvailableStages(passBatch.size(), 0);
			std::vector<VkAccessFlags2> subpassAvailableAccesses(passBatch.size(), 0);
			std::vector<std::vector<RenderGraph::BarrierPlan>> dependenciesBySubpass(passBatch.size());

			for (const RenderGraph::BarrierPlan& dependency : managedPlan.dependencies)
			{
				const auto beforeIter = passToSubpass.find(dependency.before);
				const auto afterIter = passToSubpass.find(dependency.after);
				CHECK_TRUE(
					beforeIter != passToSubpass.end() && afterIter != passToSubpass.end(),
					"Render graph subpass dependency references a pass outside the managed render pass group!");

				if (dependency.resourceType == RenderGraph::ResourceType::IMAGE)
				{
					const RenderGraph::AccessState state =
						m_buildResult.GetImageAccessState(dependency.before, dependency.image, dependency.subresourceRange);
					subpassAvailableStages[beforeIter->second] |= state.stage;
					subpassAvailableAccesses[beforeIter->second] |= state.access;
				}
				else
				{
					const RenderGraph::AccessState state = m_buildResult.GetBufferAccessState(dependency.before, dependency.buffer);
					subpassAvailableStages[beforeIter->second] |= state.stage;
					subpassAvailableAccesses[beforeIter->second] |= state.access;
				}
				dependenciesBySubpass[afterIter->second].push_back(dependency);
			}

			RenderPassCreateInfo renderPassInfo;
			std::vector<std::string> attachmentNames;
			attachmentNames.reserve(managedPlan.attachments.size());
			for (uint32_t attachmentIndex = 0; attachmentIndex < managedPlan.attachments.size(); ++attachmentIndex)
			{
				const RenderGraph::SubmitBatch::ManagedAttachmentPlan& attachmentPlan = managedPlan.attachments[attachmentIndex];
				Image* image = m_images[attachmentPlan.image];
				CHECK_TRUE(image != nullptr, "Render graph attachment image is not available!");
				const Image::Information& imageInfo = image->GetImageInformation();

				AttachmentDescription description;
				description.CustomizeFormat(imageInfo.format, attachmentPlan.clearValue);
				description.CustomizeSampleCount(imageInfo.samples);
				description.CustomizeLoadOperation(attachmentPlan.loadOp);
				description.CustomizeStoreOperation(attachmentPlan.storeOp);
				description.CustomizeStencilStoreLoadOperation(
					attachmentPlan.stencilLoadOp,
					attachmentPlan.stencilStoreOp);
				description.CustomizeInitialLayout(attachmentPlan.initialLayout);
				description.CustomizeFinalLayout(attachmentPlan.finalLayout);

				attachmentNames.push_back("attachment_" + std::to_string(attachmentIndex));
				renderPassInfo.AddAttachment(attachmentNames.back(), description);
			}

			for (size_t subpassIndex = 0; subpassIndex < passBatch.size(); ++subpassIndex)
			{
				CHECK_TRUE(subpassIndex < managedPlan.subpasses.size(), "Managed subpass plan is missing!");
				const RenderGraph::SubmitBatch::ManagedSubpassPlan& subpassPlan = managedPlan.subpasses[subpassIndex];
				CHECK_TRUE(subpassPlan.pass == passBatch[subpassIndex], "Managed subpass plan pass order is inconsistent!");
				SubpassDescription description;
				for (uint32_t colorLocation = 0; colorLocation < subpassPlan.colorAttachments.size(); ++colorLocation)
				{
					const uint32_t attachmentIndex = subpassPlan.colorAttachments[colorLocation];
					if (attachmentIndex == INVALID_INDEX)
					{
						continue;
					}
					CHECK_TRUE(attachmentIndex < managedPlan.attachments.size(), "Invalid color attachment index in managed subpass plan!");
					const auto& attachmentPlan = managedPlan.attachments[attachmentIndex];
					CHECK_TRUE(attachmentPlan.role == RenderGraph::ResourceUsageType::COLOR_ATTACHMENT, "Managed subpass color attachment has the wrong role!");
					if (colorLocation < subpassPlan.resolveAttachments.size() &&
						subpassPlan.resolveAttachments[colorLocation] != INVALID_INDEX)
					{
						const uint32_t resolveAttachmentIndex = subpassPlan.resolveAttachments[colorLocation];
						CHECK_TRUE(resolveAttachmentIndex < managedPlan.attachments.size(), "Invalid resolve attachment index in managed subpass plan!");
						const auto& resolvePlan = managedPlan.attachments[resolveAttachmentIndex];
						CHECK_TRUE(resolvePlan.role == RenderGraph::ResourceUsageType::RESOLVE_ATTACHMENT, "Managed subpass resolve attachment has the wrong role!");
						description.AddResolvedAttachment(
							colorLocation,
							attachmentNames[attachmentIndex],
							attachmentNames[resolveAttachmentIndex],
							attachmentPlan.finalLayout,
							resolvePlan.finalLayout);
					}
					else
					{
						description.AddColorAttachment(colorLocation, attachmentNames[attachmentIndex], attachmentPlan.finalLayout);
					}
				}
				if (subpassPlan.depthStencilAttachment.has_value())
				{
					const uint32_t attachmentIndex = subpassPlan.depthStencilAttachment.value();
					CHECK_TRUE(attachmentIndex < managedPlan.attachments.size(), "Invalid depth stencil attachment index in managed subpass plan!");
					const auto& attachmentPlan = managedPlan.attachments[attachmentIndex];
					CHECK_TRUE(attachmentPlan.role == RenderGraph::ResourceUsageType::DEPTH_STENCIL_ATTACHMENT, "Managed subpass depth stencil attachment has the wrong role!");
					description.AddDepthStencilAttachment(attachmentNames[attachmentIndex], attachmentPlan.finalLayout);
				}
				for (uint32_t preserveAttachment : subpassPlan.preserveAttachments)
				{
					CHECK_TRUE(preserveAttachment < attachmentNames.size(), "Invalid preserve attachment index!");
					description.AddPreserveAttachment(attachmentNames[preserveAttachment]);
				}
				description.AllowLocalPipelineBarrier();

				for (const RenderGraph::BarrierPlan& dependency : dependenciesBySubpass[subpassIndex])
				{
					VkPipelineStageFlags2 dstStage = 0;
					VkAccessFlags2 dstAccess = 0;
					if (dependency.resourceType == RenderGraph::ResourceType::IMAGE)
					{
						const RenderGraph::AccessState state =
							m_buildResult.GetImageAccessState(dependency.after, dependency.image, dependency.subresourceRange);
						dstStage = state.stage;
						dstAccess = state.access;
					}
					else
					{
						const RenderGraph::AccessState state = m_buildResult.GetBufferAccessState(dependency.after, dependency.buffer);
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

			ManagedRenderPass managedRenderPass;
			managedRenderPass.attachmentPlans = managedPlan.attachments;
			managedRenderPass.renderPass = std::make_unique<RenderPass>();
			managedRenderPass.renderPass->Create(&renderPassInfo);

			CHECK_TRUE(!managedPlan.attachments.empty(), "Managed render pass has no attachments!");
			const RenderGraph::SubmitBatch::ManagedAttachmentPlan& firstAttachmentPlan = managedPlan.attachments.front();
			Image* firstAttachment = m_images[firstAttachmentPlan.image];
			const VkExtent3D extent = firstAttachment->GetImageSize();
			const RenderGraph::ImageSubresourceRange firstRange = firstAttachmentPlan.subresourceRange;
			managedRenderPass.renderArea.offset = { 0, 0 };
			managedRenderPass.renderArea.extent = {
				_MipExtent(extent.width, firstRange.baseMipLevel),
				_MipExtent(extent.height, firstRange.baseMipLevel)
			};

			for (const RenderGraph::SubmitBatch::ManagedAttachmentPlan& attachment : managedPlan.attachments)
			{
				const VkExtent3D attachmentExtent = m_images[attachment.image]->GetImageSize();
				CHECK_TRUE(
					_MipExtent(attachmentExtent.width, attachment.subresourceRange.baseMipLevel) == managedRenderPass.renderArea.extent.width &&
					_MipExtent(attachmentExtent.height, attachment.subresourceRange.baseMipLevel) == managedRenderPass.renderArea.extent.height,
					"Render graph framebuffer attachments must have identical 2D size!");
			}

			FramebufferCreateInfo framebufferInfo;
			framebufferInfo.SetRenderPass(managedRenderPass.renderPass.get());
			for (uint32_t attachmentIndex = 0; attachmentIndex < managedPlan.attachments.size(); ++attachmentIndex)
			{
				const RenderGraph::SubmitBatch::ManagedAttachmentPlan& attachment = managedPlan.attachments[attachmentIndex];
				framebufferInfo.SetImageView(
					attachmentNames[attachmentIndex],
					m_images[attachment.image]->View(_MakeImageViewInfo(attachment.subresourceRange)));
			}
			managedRenderPass.framebuffer = std::make_unique<Framebuffer>();
			managedRenderPass.framebuffer->Create(&framebufferInfo);
			managedRenderPass.defaultClearValues = managedRenderPass.renderPass->GetClearValues();

			const uint32_t managedIndex = static_cast<uint32_t>(m_managedRenderPasses.size());
			m_graphicsBatchToManagedRenderPass[submitIndex][batchIndex] = managedIndex;
			m_managedRenderPasses.push_back(std::move(managedRenderPass));
		}
	}
}

auto RenderGraphInstance::_GetManagedRenderPass(
	uint32_t inSubmitIndex,
	uint32_t inGraphicsBatchIndex) -> ManagedRenderPass*
{
	CHECK_TRUE(inSubmitIndex < m_graphicsBatchToManagedRenderPass.size(), "Invalid render graph submit index!");
	CHECK_TRUE(
		inGraphicsBatchIndex < m_graphicsBatchToManagedRenderPass[inSubmitIndex].size(),
		"Invalid render graph graphics batch index!");

	const uint32_t managedIndex = m_graphicsBatchToManagedRenderPass[inSubmitIndex][inGraphicsBatchIndex];
	if (managedIndex == INVALID_INDEX)
	{
		return nullptr;
	}

	CHECK_TRUE(managedIndex < m_managedRenderPasses.size(), "Invalid managed render pass index!");
	return &m_managedRenderPasses[managedIndex];
}

void RenderGraphInstance::_BuildCompiledGraphPlan()
{
	m_compiledPlan.submitBatches.clear();
	m_compiledPlan.queueSyncEdges.clear();
	m_compiledPlan.submitBatches.resize(m_buildResult.GetSubmitBatchCount());

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
				waitStage |= _ToStageFlags(
					m_buildResult.GetImageAccessState(barrier.after, barrier.image, barrier.subresourceRange).stage);
			}
			else
			{
				waitStage |= _ToStageFlags(m_buildResult.GetBufferAccessState(barrier.after, barrier.buffer).stage);
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

	auto funcAppendQueueWaits = [&](const RenderGraph::SubmitBatch::PassGroupPlan& inGroup, uint32_t inSubmitIndex, std::vector<uint32_t>& inoutWaits)
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

			inoutWaits.push_back(edgeIndex);
		}
	};

	for (uint32_t submitIndex = 0; submitIndex < m_buildResult.GetSubmitBatchCount(); ++submitIndex)
	{
		const RenderGraph::SubmitBatch& submitBatch = m_buildResult.GetSubmitBatch(submitIndex);

		CompiledSubmitBatch& compiledSubmit = m_compiledPlan.submitBatches[submitIndex];
		compiledSubmit.graphicsGroups.resize(submitBatch.graphicsGroups.size());
		compiledSubmit.computeGroups.resize(submitBatch.computeGroups.size());

		for (uint32_t groupIndex = 0; groupIndex < submitBatch.graphicsGroups.size(); ++groupIndex)
		{
			const RenderGraph::SubmitBatch::PassGroupPlan& group = submitBatch.graphicsGroups[groupIndex];
			CompiledPassGroup& compiledGroup = compiledSubmit.graphicsGroups[groupIndex];
			compiledGroup.passes = group.passes;
			if (_GetManagedRenderPass(submitIndex, groupIndex) != nullptr)
			{
				compiledGroup.managedRenderPass = m_graphicsBatchToManagedRenderPass[submitIndex][groupIndex];
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
	CommandBuffer& inCommandBuffer)
{
	CHECK_TRUE(inPassIndex < m_passInfos.size(), "Invalid render graph pass index!");
	CHECK_TRUE(m_passInfos[inPassIndex].m_process != nullptr, "Render graph pass process is not set up!");

	ExecutionContext context;
	context.m_pInstance = this;
	context.m_currentPass = inPassIndex;

	CommandBuffer tmpCommandBuffer;
	context.m_pCommandBuffer = &tmpCommandBuffer;
	m_passInfos[inPassIndex].m_process(context);
	inCommandBuffer._Append(std::move(tmpCommandBuffer));
}

void RenderGraphInstance::_AppendRenderPassCommands(
	const std::vector<PassIndex>& inPasses,
	const ManagedRenderPass& inRenderPass,
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

	BeginRenderPassCommand::Parameters beginParameters;
	beginParameters.renderPass = inRenderPass.renderPass->GetVkRenderPass();
	beginParameters.framebuffer = inRenderPass.framebuffer->GetVkFramebuffer();
	beginParameters.renderArea = inRenderPass.renderArea;
	beginParameters.clearValues = inRenderPass.defaultClearValues;
	beginParameters.contents = VK_SUBPASS_CONTENTS_INLINE;

	ExecutionContext context;
	context.m_pInstance = this;
	context.m_pCommandBuffer = &inCommandBuffer;
	context.m_pRenderPass = inRenderPass.renderPass.get();
	for (size_t subpassIndex = 0; subpassIndex < inPasses.size(); ++subpassIndex)
	{
		context.m_passToSubpass[inPasses[subpassIndex]] = subpassIndex;
	}

	auto funcFindAttachment = [&](const RenderGraph::ImageUsage& inUsage)->uint32_t
	{
		for (uint32_t attachmentIndex = 0; attachmentIndex < inRenderPass.attachmentPlans.size(); ++attachmentIndex)
		{
			const RenderGraph::SubmitBatch::ManagedAttachmentPlan& attachment = inRenderPass.attachmentPlans[attachmentIndex];
			if (attachment.image == inUsage.imageIndex && attachment.subresourceRange == inUsage.subresourceRange &&
				attachment.role == inUsage.type)
			{
				return attachmentIndex;
			}
		}
		CHECK_TRUE(false, "Managed render pass attachment is missing from its instance plan!");
		return INVALID_INDEX;
	};

	for (PassIndex passIndex : inPasses)
	{
		CHECK_TRUE(passIndex < m_passInfos.size(), "Invalid render graph pass index!");
		const PassInfo& passInfo = m_passInfos[passIndex];
		CHECK_TRUE(passInfo.m_process != nullptr, "Render graph pass process is not set up!");
		const RenderGraph::PassRecord& pass = m_buildResult.GetPass(passIndex);
		for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
		{
			if (usage.type == RenderGraph::ResourceUsageType::COLOR_ATTACHMENT && usage.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
			{
				const auto overrideIter = passInfo.m_colorClearValueOverrides.find(usage.attachmentSlot);
				if (overrideIter != passInfo.m_colorClearValueOverrides.end())
				{
					const uint32_t attachmentIndex = funcFindAttachment(usage);
					CHECK_TRUE(attachmentIndex < beginParameters.clearValues.size(), "Invalid managed color attachment clear value index!");
					beginParameters.clearValues[attachmentIndex].color = overrideIter->second;
				}
			}
			else if (usage.type == RenderGraph::ResourceUsageType::DEPTH_STENCIL_ATTACHMENT &&
				(usage.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR || usage.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR))
			{
				if (passInfo.m_depthStencilClearValueOverride.has_value())
				{
					const uint32_t attachmentIndex = funcFindAttachment(usage);
					CHECK_TRUE(attachmentIndex < beginParameters.clearValues.size(), "Invalid managed depth stencil attachment clear value index!");
					beginParameters.clearValues[attachmentIndex].depthStencil = passInfo.m_depthStencilClearValueOverride.value();
				}
			}
		}
	}

	inCommandBuffer.BeginRenderPass(beginParameters, static_cast<uint32_t>(inPasses.size()));

	for (uint32_t subpassIndex = 0; subpassIndex < inPasses.size(); ++subpassIndex)
	{
		const PassIndex passIndex = inPasses[subpassIndex];
		const PassInfo& passInfo = m_passInfos[passIndex];
		context.m_currentPass = passIndex;
		context.m_currentSubpass = subpassIndex;
		passInfo.m_process(context);
		if (subpassIndex + 1 < inPasses.size())
		{
			inCommandBuffer.NextSubpass();
		}
	}

	inCommandBuffer.EndRenderPass();
}

void RenderGraphInstance::_RecordSubpassCommandBuffer(
	PassIndex inPassIndex,
	std::function<void(CommandBuffer*)> inProcess,
	ExecutionContext& inContext)
{
	CHECK_TRUE(inProcess != nullptr, "Render graph subpass command recording process is empty!");
	CHECK_TRUE(inContext.m_pCommandBuffer != nullptr, "Subpass command recording needs a command buffer!");

	const auto iter = inContext.m_passToSubpass.find(inPassIndex);
	CHECK_TRUE(iter != inContext.m_passToSubpass.end(), "Target pass is not in current render pass scope!");
	CHECK_TRUE(iter->second == inContext.m_currentSubpass, "Target pass is not the current subpass!");

	inProcess(inContext.m_pCommandBuffer);
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

		const RenderGraph::QueueType srcQueue = m_buildResult.GetPass(inPlan.before).queue;
		const RenderGraph::QueueType dstQueue = m_buildResult.GetPass(inPlan.after).queue;
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
				const RenderGraph::AccessState graphState = m_buildResult.GetBufferAccessState(boundaryPass, bufferIndex);

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
			const RenderGraph::AccessState graphState =
				m_buildResult.GetImageAccessState(boundaryPass, imageIndex, plan.subresourceRange);

			VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.srcAccessMask = _ToAccessFlags(entering ? externalInfo.enteringAccess : graphState.access);
			barrier.dstAccessMask = _ToAccessFlags(entering ? graphState.access : externalInfo.leavingAccess);
			barrier.oldLayout = entering ? externalInfo.enteringLayout : graphState.layout;
			barrier.newLayout = entering ? graphState.layout : externalInfo.leavingLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_images[imageIndex]->GetVkImage();
			const RenderGraph::ImageSubresourceRange barrierRange =
				m_buildResult.GetImageInfo(imageIndex).NormalizeSubresourceRange(plan.subresourceRange);
			const ImageView* view = m_images[imageIndex]->View(_MakeImageViewInfo(barrierRange));
			barrier.subresourceRange = view->GetImageSubresourceRange();
			parameters.srcStageMask |= _ToStageFlags(entering ? externalInfo.enteringStage : graphState.stage);
			parameters.dstStageMask |= _ToStageFlags(entering ? graphState.stage : externalInfo.leavingStage);
			parameters.imageBarriers.push_back(barrier);
			continue;
		}

		if (plan.resourceType == RenderGraph::ResourceType::IMAGE)
		{
			CHECK_TRUE(plan.image < m_images.size() && m_images[plan.image] != nullptr, "Render graph barrier image is not available!");
			const RenderGraph::ImageSubresourceRange barrierRange =
				m_buildResult.GetImageInfo(plan.image).NormalizeSubresourceRange(plan.subresourceRange);
			const ImageView* view = m_images[plan.image]->View(_MakeImageViewInfo(barrierRange));
			const Image::Information& imageInfo = m_images[plan.image]->GetImageInformation();

			RenderGraph::AccessState srcState;
			RenderGraph::AccessState dstState;
			if (plan.before == INVALID_INDEX)
			{
				CHECK_TRUE(plan.after != INVALID_INDEX, "Render graph initial image barrier target pass is invalid!");
				dstState = m_buildResult.GetImageAccessState(plan.after, plan.image, plan.subresourceRange);
				srcState.layout = VK_IMAGE_LAYOUT_UNDEFINED;
				srcState.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			}
			else
			{
				CHECK_TRUE(plan.after != INVALID_INDEX, "Render graph image barrier target pass is invalid!");
				srcState = m_buildResult.GetImageAccessState(plan.before, plan.image, plan.subresourceRange);
				dstState = m_buildResult.GetImageAccessState(plan.after, plan.image, plan.subresourceRange);
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
			const RenderGraph::AccessState srcState = m_buildResult.GetBufferAccessState(plan.before, plan.buffer);
			const RenderGraph::AccessState dstState = m_buildResult.GetBufferAccessState(plan.after, plan.buffer);

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

auto RenderGraphInstance::_BuildExternalDependencyBindings() const -> std::vector<ExternalDependencyBinding>
{
	std::vector<ExternalDependencyBinding> bindings;
	std::unordered_map<QueueDependency*, size_t> dependencyToBinding;

	auto funcGetFirstSubmit = [](const RenderGraph::SubmitBoundary& inBoundary, RenderGraph::QueueType inQueue)->uint32_t
	{
		return inQueue == RenderGraph::QueueType::GRAPHICS
			? inBoundary.firstGraphicsSubmit
			: inBoundary.firstComputeSubmit;
	};
	auto funcGetLastSubmit = [](const RenderGraph::SubmitBoundary& inBoundary, RenderGraph::QueueType inQueue)->uint32_t
	{
		return inQueue == RenderGraph::QueueType::GRAPHICS
			? inBoundary.lastGraphicsSubmit
			: inBoundary.lastComputeSubmit;
	};
	auto funcGetOtherFirstSubmit = [](const RenderGraph::SubmitBoundary& inBoundary, RenderGraph::QueueType inQueue)->uint32_t
	{
		return inQueue == RenderGraph::QueueType::GRAPHICS
			? inBoundary.firstComputeSubmit
			: inBoundary.firstGraphicsSubmit;
	};
	auto funcBindDependency = [&bindings, &dependencyToBinding](
		QueueDependency* inDependency,
		RenderGraph::QueueType inQueue,
		uint32_t inSubmit,
		bool inWait)
	{
		if (inDependency == nullptr)
		{
			return;
		}
		CHECK_TRUE(inSubmit != INVALID_INDEX,
			"External resource dependency requires an active access on its declared queue!");

		auto [iter, inserted] = dependencyToBinding.emplace(inDependency, bindings.size());
		if (inserted)
		{
			bindings.push_back({ inDependency, inQueue });
		}
		ExternalDependencyBinding& binding = bindings[iter->second];
		CHECK_TRUE(binding.queue == inQueue,
			"One queue dependency cannot be shared by external resources on different queues!");
		if (inWait)
		{
			binding.waitSubmit = std::min(binding.waitSubmit, inSubmit);
		}
		else
		{
			binding.signalSubmit = binding.signalSubmit == INVALID_INDEX
				? inSubmit
				: std::max(binding.signalSubmit, inSubmit);
		}
	};
	auto funcBindExternalResource = [&funcGetFirstSubmit, &funcGetLastSubmit, &funcGetOtherFirstSubmit, &funcBindDependency](
		RenderGraph::QueueType inQueue,
		const RenderGraph::SubmitBoundary& inBoundary,
		QueueDependency* inAcquireDependency,
		QueueDependency* inReleaseDependency)
	{
		CHECK_TRUE(funcGetOtherFirstSubmit(inBoundary, inQueue) == INVALID_INDEX,
			"External resource was scheduled on a queue other than its declared queue!");
		funcBindDependency(inAcquireDependency, inQueue, funcGetFirstSubmit(inBoundary, inQueue), true);
		funcBindDependency(inReleaseDependency, inQueue, funcGetLastSubmit(inBoundary, inQueue), false);
	};

	for (BufferIndex index = 0; index < m_buildResult.GetBufferCount(); ++index)
	{
		const RenderGraph::BufferInfo& graphInfo = m_buildResult.GetBufferInfo(index);
		if (!graphInfo.m_external)
		{
			continue;
		}
		CHECK_TRUE(m_externalBufferInfos[index].has_value(), "External render graph buffer is not set up!");
		const ExternalBufferInfo& externalInfo = m_externalBufferInfos[index].value();
		funcBindExternalResource(
			graphInfo.m_externalAccessQueue,
			m_buildResult.GetBufferSubmitBoundary(index),
			externalInfo.pAcquireDependency,
			externalInfo.pReleaseDependency);
	}
	for (ImageIndex index = 0; index < m_buildResult.GetImageCount(); ++index)
	{
		const RenderGraph::ImageInfo& graphInfo = m_buildResult.GetImageInfo(index);
		if (!graphInfo.m_external)
		{
			continue;
		}
		CHECK_TRUE(m_externalImageInfos[index].has_value(), "External render graph image is not set up!");
		const ExternalImageInfo& externalInfo = m_externalImageInfos[index].value();
		funcBindExternalResource(
			graphInfo.m_externalAccessQueue,
			m_buildResult.GetImageSubmitBoundary(index),
			externalInfo.pAcquireDependency,
			externalInfo.pReleaseDependency);
	}

	return bindings;
}

void RenderGraphInstance::Execute(const ExecuteInfo& inExecuteInfo)
{
	const RenderGraph::SubmitBoundary& graphBoundary = m_buildResult.GetGraphSubmitBoundary();
	const bool usesGraphicsQueue = graphBoundary.lastGraphicsSubmit != INVALID_INDEX;
	const bool usesComputeQueue = graphBoundary.lastComputeSubmit != INVALID_INDEX;
	CHECK_TRUE(!usesGraphicsQueue || inExecuteInfo.m_graphicsCompletionFence != nullptr,
		"A graphics completion fence is required for this render graph execution!");
	CHECK_TRUE(!usesComputeQueue || inExecuteInfo.m_computeCompletionFence != nullptr,
		"A compute completion fence is required for this render graph execution!");
	CHECK_TRUE(
		!usesGraphicsQueue || !usesComputeQueue ||
		inExecuteInfo.m_graphicsCompletionFence != inExecuteInfo.m_computeCompletionFence,
		"Graphics and compute completion fences must be different objects!");

	_ApplyPasses(inExecuteInfo);
	_DestroyManagedRenderPasses();
	m_compiledPlan = {};
	_ApplyExternalResources(inExecuteInfo);
	_BindExternalResources();
	_CreateManagedRenderPasses();
	_BuildCompiledGraphPlan();

	auto& device = MyDevice::GetInstance();
	GraphicsQueue* graphicsQueue = device.GetGraphicsCommandQueue();
	ComputeQueue* computeQueue = device.GetComputeCommandQueue();
	CHECK_TRUE(graphicsQueue != nullptr, "Graphics command queue is not available!");
	CHECK_TRUE(computeQueue != nullptr, "Compute command queue is not available!");

	std::vector<std::vector<QueueDependency*>> externalWaits(m_compiledPlan.submitBatches.size() * 2);
	std::vector<std::vector<QueueDependency*>> externalSignals(m_compiledPlan.submitBatches.size() * 2);
	const std::vector<ExternalDependencyBinding> externalDependencyBindings =
		_BuildExternalDependencyBindings();

	for (const ExternalDependencyBinding& binding : externalDependencyBindings)
	{
		CHECK_TRUE(binding.dependency != nullptr, "External resource queue dependency is null!");
		const bool hasWait = binding.waitSubmit != INVALID_INDEX;
		const bool hasSignal = binding.signalSubmit != INVALID_INDEX;
		CHECK_TRUE(!hasWait || binding.dependency->_HasSemaphore(),
			"External acquire dependency has no pending signal!");
		CHECK_TRUE(hasWait || !hasSignal || !binding.dependency->_HasSemaphore(),
			"External release dependency already has a pending signal!");
		if (hasWait && hasSignal)
		{
			CHECK_TRUE(binding.waitSubmit <= binding.signalSubmit,
				"External resource dependency signal precedes its wait!");
		}

		const size_t queueOffset = binding.queue == RenderGraph::QueueType::GRAPHICS ? 0 : 1;
		if (hasWait)
		{
			externalWaits[static_cast<size_t>(binding.waitSubmit) * 2 + queueOffset].push_back(binding.dependency);
		}
		if (hasSignal)
		{
			externalSignals[static_cast<size_t>(binding.signalSubmit) * 2 + queueOffset].push_back(binding.dependency);
		}
	}
	std::vector<std::unique_ptr<QueueDependency>> queueSyncDependencies;
	try
	{
		queueSyncDependencies.reserve(m_compiledPlan.queueSyncEdges.size());
		for (uint32_t edgeIndex = 0; edgeIndex < m_compiledPlan.queueSyncEdges.size(); ++edgeIndex)
		{
			queueSyncDependencies.push_back(std::make_unique<QueueDependency>());
		}

	auto funcAppendCompiledCommands = [](const std::vector<std::unique_ptr<Command>>& inCommands, CommandBuffer& inCommandBuffer) -> bool
	{
		if (inCommands.empty())
		{
			return false;
		}

		std::vector<const Command*> commands;
		commands.reserve(inCommands.size());
		for (const std::unique_ptr<Command>& command : inCommands)
		{
			CHECK_TRUE(command != nullptr, "Compiled render graph command cannot be null!");
			commands.push_back(command.get());
		}
		inCommandBuffer.AddCommands(commands.data(), commands.size());
		return true;
	};

	auto funcFillSyncInfo = [&](const std::vector<uint32_t>& inWaits, const std::vector<uint32_t>& inSignals)->CommandQueue::SubmitInfo
	{
		CommandQueue::SubmitInfo syncInfo;
		for (uint32_t syncEdge : inWaits)
		{
			CHECK_TRUE(syncEdge < queueSyncDependencies.size(), "Invalid render graph queue wait sync edge!");
			CHECK_TRUE(queueSyncDependencies[syncEdge] != nullptr, "Render graph wait dependency is missing!");
			const VkPipelineStageFlags waitStage = m_compiledPlan.queueSyncEdges[syncEdge].waitStage == 0
				? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
				: m_compiledPlan.queueSyncEdges[syncEdge].waitStage;
			syncInfo.AddWaitQueueDependency(*queueSyncDependencies[syncEdge], waitStage);
		}

		for (uint32_t syncEdge : inSignals)
		{
			CHECK_TRUE(syncEdge < queueSyncDependencies.size(), "Invalid render graph queue signal sync edge!");
			CHECK_TRUE(queueSyncDependencies[syncEdge] != nullptr, "Render graph signal dependency is missing!");
			syncInfo.AddSignalQueueDependency(*queueSyncDependencies[syncEdge]);
		}

		return syncInfo;
	};

	auto funcAppendExternalSync = [&](CommandQueue::SubmitInfo& inoutSyncInfo, uint32_t inSubmitIndex, RenderGraph::QueueType inQueue)
	{
		const size_t index = static_cast<size_t>(inSubmitIndex) * 2 + (inQueue == RenderGraph::QueueType::GRAPHICS ? 0 : 1);
		for (QueueDependency* dependency : externalWaits[index])
		{
			CHECK_TRUE(dependency != nullptr, "External queue wait dependency is null!");
			inoutSyncInfo.AddWaitQueueDependency(*dependency, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		}
		for (QueueDependency* dependency : externalSignals[index])
		{
			CHECK_TRUE(dependency != nullptr, "External queue signal dependency is null!");
			inoutSyncInfo.AddSignalQueueDependency(*dependency);
		}
	};

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

			if (group.managedRenderPass != INVALID_INDEX)
			{
				CHECK_TRUE(group.managedRenderPass < m_managedRenderPasses.size(), "Invalid compiled managed render pass index!");
				ManagedRenderPass* managedRenderPass = &m_managedRenderPasses[group.managedRenderPass];
				_AppendRenderPassCommands(passBatch, *managedRenderPass, graphicsCommandBuffer);
				hasGraphicsCommands = true;
			}
			else
			{
				for (PassIndex passIndex : passBatch)
				{
					_AppendPassCommands(passIndex, graphicsCommandBuffer);
					hasGraphicsCommands = true;
				}
			}

			hasGraphicsCommands = funcAppendCompiledCommands(group.epilogueCommands, graphicsCommandBuffer) || hasGraphicsCommands;
			hasGraphicsCommands = funcAppendCompiledCommands(group.queueReleaseCommands, graphicsCommandBuffer) || hasGraphicsCommands;
		}

		for (const CompiledPassGroup& group : submitBatch.computeGroups)
		{
			CHECK_TRUE(!group.passes.empty(), "Render graph compute group cannot be empty!");
			CHECK_TRUE(group.managedRenderPass == INVALID_INDEX, "Compute pass group cannot use a managed render pass!");

			hasComputeCommands = funcAppendCompiledCommands(group.prologueCommands, computeCommandBuffer) || hasComputeCommands;

			for (PassIndex passIndex : group.passes)
			{
				_AppendPassCommands(passIndex, computeCommandBuffer);
				hasComputeCommands = true;
			}

			hasComputeCommands = funcAppendCompiledCommands(group.epilogueCommands, computeCommandBuffer) || hasComputeCommands;
			hasComputeCommands = funcAppendCompiledCommands(group.queueReleaseCommands, computeCommandBuffer) || hasComputeCommands;
		}

		if (hasGraphicsCommands)
		{
			CommandQueue::SubmitInfo syncInfo = funcFillSyncInfo(submitBatch.graphicsWaitSyncs, submitBatch.graphicsSignalSyncs);
			funcAppendExternalSync(syncInfo, submitIndex, RenderGraph::QueueType::GRAPHICS);
			if (submitIndex == graphBoundary.lastGraphicsSubmit)
			{
				syncInfo.SetFence(*inExecuteInfo.m_graphicsCompletionFence);
			}
			graphicsQueue->Enqueue(&graphicsCommandBuffer, 1).Submit(std::move(syncInfo));
		}
		if (hasComputeCommands)
		{
			CommandQueue::SubmitInfo syncInfo = funcFillSyncInfo(submitBatch.computeWaitSyncs, submitBatch.computeSignalSyncs);
			funcAppendExternalSync(syncInfo, submitIndex, RenderGraph::QueueType::COMPUTE);
			if (submitIndex == graphBoundary.lastComputeSubmit)
			{
				syncInfo.SetFence(*inExecuteInfo.m_computeCompletionFence);
			}
			computeQueue->Enqueue(&computeCommandBuffer, 1).Submit(std::move(syncInfo));
		}
	}
		for (const std::unique_ptr<QueueDependency>& dependency : queueSyncDependencies)
		{
			CHECK_TRUE(dependency != nullptr && !dependency->_HasSemaphore(),
				"Render graph execution left a pending queue dependency!");
		}
	}
	catch (...)
	{
		std::vector<QueueDependency*> dependenciesToDiscard;
		dependenciesToDiscard.reserve(queueSyncDependencies.size() + externalDependencyBindings.size());
		for (const std::unique_ptr<QueueDependency>& dependency : queueSyncDependencies)
		{
			dependenciesToDiscard.push_back(dependency.get());
		}
		for (const ExternalDependencyBinding& binding : externalDependencyBindings)
		{
			dependenciesToDiscard.push_back(binding.dependency);
		}
		CommandQueueManager* manager = device.GetCommandQueueManager();
		CHECK_TRUE(manager != nullptr, "Command queue manager is not available!");
		manager->_WaitIdleAndDiscardDependencies(dependenciesToDiscard.data(), dependenciesToDiscard.size());
		throw;
	}
}
