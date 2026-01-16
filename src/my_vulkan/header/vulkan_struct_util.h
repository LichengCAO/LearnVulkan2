#pragma once
#include "common.h"

class ImageBarrierBuilder
{
private:
	const void* pNext = nullptr;
	uint32_t                   m_srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	uint32_t                   m_dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	VkImageSubresourceRange    m_subresourceRange{};

public:
	ImageBarrierBuilder();

	ImageBarrierBuilder& Reset();

	// optional, set mip level range of the barrier
	ImageBarrierBuilder& CustomizeMipLevelRange(uint32_t baseMipLevel, uint32_t levelCount = 1);

	// optional, set array layer range of the barrier
	ImageBarrierBuilder& CustomizeArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount = 1);

	// optional, set aspect of the image barrier, default: VK_IMAGE_ASPECT_COLOR_BIT
	ImageBarrierBuilder& CustomizeImageAspect(VkImageAspectFlags aspectMask);

	// optional, useful when trying to upload image in a command buffer from queue family other than the graphics queue family
	ImageBarrierBuilder& CustomizeQueueFamilyTransfer(uint32_t inQueueFamilyTransferFrom, uint32_t inQueueFamilyTransferTo);

	// _srcAccessMask: when the data is updated (available)
	// _dstAccessMask: when the data is visible
	// see: https://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/
	VkImageMemoryBarrier Build(
		VkImage _image,
		VkImageLayout _oldLayout,
		VkImageLayout _newLayout,
		VkAccessFlags _srcAccessMask,
		VkAccessFlags _dstAccessMask) const;
};

class BufferBarrierBuilder
{
private:
	const void* pNext = nullptr;
	uint32_t                   m_srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	uint32_t                   m_dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	VkDeviceSize               m_offset = 0;
	VkDeviceSize               m_size = VK_WHOLE_SIZE;

public:
	BufferBarrierBuilder& Reset();

	// optional, set offset and size of the buffer barrier
	BufferBarrierBuilder& CustomizeOffsetAndSize(VkDeviceSize inOffset, VkDeviceSize inSize);

	// optional, useful when trying to upload buffer in a command buffer from queue family other than the graphics queue family
	BufferBarrierBuilder& CustomizeQueueFamilyTransfer(uint32_t inQueueFamilyTransferFrom, uint32_t inQueueFamilyTransferTo);

	VkBufferMemoryBarrier Build(
		VkBuffer inBuffer,
		VkAccessFlags inSrcAccessMask,
		VkAccessFlags inDstAccessMask) const;
};

class ImageBlitBuilder
{
private:
	VkImageSubresourceLayers m_srcSubresourceLayers{};
	VkImageSubresourceLayers m_dstSubresourceLayers{};

public:
	ImageBlitBuilder();
	ImageBlitBuilder& Reset();
	ImageBlitBuilder& CustomizeSrcAspect(VkImageAspectFlags aspectMask);
	ImageBlitBuilder& CustomizeDstAspect(VkImageAspectFlags aspectMask);
	ImageBlitBuilder& CustomizeSrcArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount = 1);
	ImageBlitBuilder& CustomizeDstArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount = 1);

	VkImageBlit NewBlit(
		const VkOffset3D& srcOffsetUL,
		const VkOffset3D& srcOffsetLR,
		uint32_t srcMipLevel,
		const VkOffset3D& dstOffsetUL,
		const VkOffset3D& dstOffsetLR,
		uint32_t dstMipLevel) const;

	VkImageBlit NewBlit(
		const VkExtent2D& inSrcImageSize,
		uint32_t srcMipLevel,
		const VkExtent2D& inDstImageSize,
		uint32_t dstMipLevel) const;

	VkImageBlit NewBlit(
		const VkOffset2D& inSrcOffsetUpperLeftXY,
		const VkOffset2D& inSrcOffsetLowerRightXY,
		uint32_t inSrcMipLevel,
		const VkOffset2D& inDstOffsetUpperLeftXY,
		const VkOffset2D& inDstOffsetLowerRightXY,
		uint32_t inDstMipLevel) const;
};

// Graphics pipeline can have multiple vertex bindings,
// i.e. layout(binding = 0, location = 0); layout(binding = 1, location = 0)... ==> wrong, see: https://gist.github.com/SaschaWillems/428d15ed4b5d71ead462bc63adffa93a
// This class helps to describe vertex attribute layout that belongs to the same binding
// in a graphics pipeline, I use this to create a graphics pipeline.
// This class doesn't hold any Vulkan handle,
// and can be destroyed after the construction of the graphics pipeline.
class VertexInputDescriptionBuilder final
{
private:
	uint32_t          m_uStride = 0;
	VkVertexInputRate m_InputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	std::vector<VkVertexInputAttributeDescription> m_Locations;

public:
	VertexInputDescriptionBuilder& Reset();

	// Set up per Vertex Description information
	VertexInputDescriptionBuilder& SetVertexStrideAndInputRate(
		uint32_t inStride, 
		VkVertexInputRate inInputRate = VK_VERTEX_INPUT_RATE_VERTEX);

	// Add location for this vertex binding.
	// Then this location will read data from binding buffer with input format
	// inFormat: input, format of the input
	// inOffset: input, offset of the data of VBO from host
	// inLocation: input, should match layout(location = ...) in shader
	VertexInputDescriptionBuilder& AddLocation(
		VkFormat inFormat, 
		uint32_t inOffset, 
		uint32_t inLocation);

	// outBindingDescription: output, defines vertex stride and input rate
	// outAttributeDescriptions: output, associate layout(location = ...) with segments in vertex stride
	// inBinding: normally, we only binds 1 vertex buffer to a graphic pipeline to draw an object
	void BuildDescription(
		VkVertexInputBindingDescription& outBindingDescription, 
		std::vector<VkVertexInputAttributeDescription>& outAttributeDescriptions,
		uint32_t inBinding = 0) const;
};