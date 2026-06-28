#pragma once
#include "common.h"
#include "common_enums.h"

enum class VulkanAccessState : uint8_t
{
	Unknown,
	Present,
	TransferRead,
	TransferWrite,
	VertexBuffer,
	IndexBuffer,
	UniformBuffer,
	ShaderRead,
	ShaderWrite,
	ColorAttachmentRead,
	ColorAttachmentWrite,
	DepthStencilRead,
	DepthStencilWrite,
	IndirectCommand,
	AccelerationStructureRead,
	AccelerationStructureWrite,
	RayTracingShaderRead,
	RayTracingShaderWrite,
};

struct VulkanAccessInfo
{
	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAccessFlags accessFlags = 0;
	VkPipelineStageFlags pipelineStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
};

enum class VulkanBufferUsage : uint32_t
{
	None = 0,
	TransferSrc = 1 << 0,
	TransferDst = 1 << 1,
	Vertex = 1 << 2,
	Index = 1 << 3,
	Uniform = 1 << 4,
	Storage = 1 << 5,
	Indirect = 1 << 6,
	ShaderDeviceAddress = 1 << 7,
	AccelerationStructureStorage = 1 << 8,
	AccelerationStructureBuildInput = 1 << 9,
	ShaderBindingTable = 1 << 10,
};

enum class VulkanTextureUsage : uint32_t
{
	None = 0,
	TransferSrc = 1 << 0,
	TransferDst = 1 << 1,
	Sampled = 1 << 2,
	Storage = 1 << 3,
	ColorAttachment = 1 << 4,
	DepthStencilAttachment = 1 << 5,
	InputAttachment = 1 << 6,
	TransientAttachment = 1 << 7,
};

inline VulkanBufferUsage operator|(VulkanBufferUsage lhs, VulkanBufferUsage rhs)
{
	return static_cast<VulkanBufferUsage>(
		static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline VulkanBufferUsage operator&(VulkanBufferUsage lhs, VulkanBufferUsage rhs)
{
	return static_cast<VulkanBufferUsage>(
		static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline VulkanBufferUsage& operator|=(VulkanBufferUsage& lhs, VulkanBufferUsage rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline bool HasAnyVulkanBufferUsage(VulkanBufferUsage value, VulkanBufferUsage usage)
{
	return static_cast<uint32_t>(value & usage) != 0;
}

inline VulkanTextureUsage operator|(VulkanTextureUsage lhs, VulkanTextureUsage rhs)
{
	return static_cast<VulkanTextureUsage>(
		static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline VulkanTextureUsage operator&(VulkanTextureUsage lhs, VulkanTextureUsage rhs)
{
	return static_cast<VulkanTextureUsage>(
		static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline VulkanTextureUsage& operator|=(VulkanTextureUsage& lhs, VulkanTextureUsage rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline bool HasAnyVulkanTextureUsage(VulkanTextureUsage value, VulkanTextureUsage usage)
{
	return static_cast<uint32_t>(value & usage) != 0;
}

struct VulkanBufferDesc
{
	VkDeviceSize size = 0;
	VulkanBufferUsage usage = VulkanBufferUsage::None;
	VkMemoryPropertyFlags memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	std::optional<VkDeviceSize> alignment;
	std::string debugName;
};

struct VulkanTextureDesc
{
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t depth = 1;
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
	VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkImageType imageType = VK_IMAGE_TYPE_2D;
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VulkanTextureUsage usage = VulkanTextureUsage::None;
	VkMemoryPropertyFlags memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bool isSwapchainTexture = false;
	std::string debugName;
};

struct VulkanTextureViewDesc
{
	VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	uint32_t baseMipLevel = 0;
	uint32_t levelCount = VK_REMAINING_MIP_LEVELS;
	uint32_t baseArrayLayer = 0;
	uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS;
	std::string debugName;
};

struct VulkanShaderResourceViewDesc : VulkanTextureViewDesc
{
};

struct VulkanUnorderedAccessViewDesc : VulkanTextureViewDesc
{
};

struct VulkanRenderTargetViewDesc : VulkanTextureViewDesc
{
	VulkanRenderTargetViewDesc()
	{
		aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
};

struct VulkanDepthStencilViewDesc : VulkanTextureViewDesc
{
	VulkanDepthStencilViewDesc()
	{
		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}
};

struct VulkanSamplerDesc
{
	VkFilter magFilter = VK_FILTER_LINEAR;
	VkFilter minFilter = VK_FILTER_LINEAR;
	VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	float mipLodBias = 0.0f;
	bool anisotropyEnable = false;
	float maxAnisotropy = 1.0f;
	bool compareEnable = false;
	VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;
	float minLod = 0.0f;
	float maxLod = VK_LOD_CLAMP_NONE;
	VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	bool unnormalizedCoordinates = false;
	std::string debugName;
};

struct VulkanViewport
{
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	float minDepth = 0.0f;
	float maxDepth = 1.0f;
};

struct VulkanScissorRect
{
	int32_t offsetX = 0;
	int32_t offsetY = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct VulkanTextureState
{
	VulkanAccessState accessState = VulkanAccessState::Unknown;
	uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
};

struct VulkanBufferState
{
	VulkanAccessState accessState = VulkanAccessState::Unknown;
	uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;
};

struct VulkanTextureTransitionInfo
{
	VkImage image = VK_NULL_HANDLE;
	VulkanAccessState srcState = VulkanAccessState::Unknown;
	VulkanAccessState dstState = VulkanAccessState::Unknown;
	VkImageSubresourceRange subresourceRange{};
	uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;
};

struct VulkanBufferTransitionInfo
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VulkanAccessState srcState = VulkanAccessState::Unknown;
	VulkanAccessState dstState = VulkanAccessState::Unknown;
	VkDeviceSize offset = 0;
	VkDeviceSize size = VK_WHOLE_SIZE;
	uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;
};

struct VulkanClearValue
{
	VkClearValue value{};
};

struct VulkanRenderPassInfo
{
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkRect2D renderArea{};
	std::vector<VkClearValue> clearValues;
	VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
};

VkBufferUsageFlags ToVkBufferUsageFlags(VulkanBufferUsage inUsage);
VkImageUsageFlags ToVkImageUsageFlags(VulkanTextureUsage inUsage);
VulkanAccessInfo GetVulkanAccessInfo(VulkanAccessState inState);
VkViewport ToVkViewport(const VulkanViewport& inViewport);
VkRect2D ToVkRect2D(const VulkanScissorRect& inScissor);
VkSamplerCreateInfo ToVkSamplerCreateInfo(const VulkanSamplerDesc& inDesc);
