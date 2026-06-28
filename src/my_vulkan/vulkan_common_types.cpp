#include "vulkan_common_types.h"

VkBufferUsageFlags ToVkBufferUsageFlags(VulkanBufferUsage inUsage)
{
	VkBufferUsageFlags result = 0;
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::TransferSrc))
	{
		result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::TransferDst))
	{
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::Vertex))
	{
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::Index))
	{
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::Uniform))
	{
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::Storage))
	{
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::Indirect))
	{
		result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::ShaderDeviceAddress))
	{
		result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::AccelerationStructureStorage))
	{
		result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::AccelerationStructureBuildInput))
	{
		result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	}
	if (HasAnyVulkanBufferUsage(inUsage, VulkanBufferUsage::ShaderBindingTable))
	{
		result |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
	}
	return result;
}

VkImageUsageFlags ToVkImageUsageFlags(VulkanTextureUsage inUsage)
{
	VkImageUsageFlags result = 0;
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::TransferSrc))
	{
		result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::TransferDst))
	{
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::Sampled))
	{
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::Storage))
	{
		result |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::ColorAttachment))
	{
		result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::DepthStencilAttachment))
	{
		result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::InputAttachment))
	{
		result |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}
	if (HasAnyVulkanTextureUsage(inUsage, VulkanTextureUsage::TransientAttachment))
	{
		result |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	}
	return result;
}

VulkanAccessInfo GetVulkanAccessInfo(VulkanAccessState inState)
{
	switch (inState)
	{
	case VulkanAccessState::Unknown:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			0,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		};
	case VulkanAccessState::Present:
		return {
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			0,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
		};
	case VulkanAccessState::TransferRead:
		return {
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT
		};
	case VulkanAccessState::TransferWrite:
		return {
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT
		};
	case VulkanAccessState::VertexBuffer:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
		};
	case VulkanAccessState::IndexBuffer:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_INDEX_READ_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
		};
	case VulkanAccessState::UniformBuffer:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_UNIFORM_READ_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
		};
	case VulkanAccessState::ShaderRead:
		return {
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
		};
	case VulkanAccessState::ShaderWrite:
		return {
			VK_IMAGE_LAYOUT_GENERAL,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
		};
	case VulkanAccessState::ColorAttachmentRead:
		return {
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
	case VulkanAccessState::ColorAttachmentWrite:
		return {
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
	case VulkanAccessState::DepthStencilRead:
		return {
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
		};
	case VulkanAccessState::DepthStencilWrite:
		return {
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
		};
	case VulkanAccessState::IndirectCommand:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
		};
	case VulkanAccessState::AccelerationStructureRead:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
		};
	case VulkanAccessState::AccelerationStructureWrite:
		return {
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
		};
	case VulkanAccessState::RayTracingShaderRead:
		return {
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
		};
	case VulkanAccessState::RayTracingShaderWrite:
		return {
			VK_IMAGE_LAYOUT_GENERAL,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
		};
	default:
		CHECK_TRUE(false, "Unsupported Vulkan access state.");
		return {};
	}
}

VkViewport ToVkViewport(const VulkanViewport& inViewport)
{
	VkViewport result{};
	result.x = inViewport.x;
	result.y = inViewport.y;
	result.width = inViewport.width;
	result.height = inViewport.height;
	result.minDepth = inViewport.minDepth;
	result.maxDepth = inViewport.maxDepth;
	return result;
}

VkRect2D ToVkRect2D(const VulkanScissorRect& inScissor)
{
	VkRect2D result{};
	result.offset = { inScissor.offsetX, inScissor.offsetY };
	result.extent = { inScissor.width, inScissor.height };
	return result;
}

VkSamplerCreateInfo ToVkSamplerCreateInfo(const VulkanSamplerDesc& inDesc)
{
	VkSamplerCreateInfo result{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	result.magFilter = inDesc.magFilter;
	result.minFilter = inDesc.minFilter;
	result.mipmapMode = inDesc.mipmapMode;
	result.addressModeU = inDesc.addressModeU;
	result.addressModeV = inDesc.addressModeV;
	result.addressModeW = inDesc.addressModeW;
	result.mipLodBias = inDesc.mipLodBias;
	result.anisotropyEnable = inDesc.anisotropyEnable;
	result.maxAnisotropy = inDesc.maxAnisotropy;
	result.compareEnable = inDesc.compareEnable;
	result.compareOp = inDesc.compareOp;
	result.minLod = inDesc.minLod;
	result.maxLod = inDesc.maxLod;
	result.borderColor = inDesc.borderColor;
	result.unnormalizedCoordinates = inDesc.unnormalizedCoordinates;
	return result;
}
