#include "vulkan_struct_util.h"

ImageBarrierBuilder::ImageBarrierBuilder()
{
	Reset();
}
ImageBarrierBuilder& ImageBarrierBuilder::Reset()
{
	m_subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
	m_subresourceRange.baseArrayLayer = 0;
	m_subresourceRange.baseMipLevel = 0;
	m_subresourceRange.layerCount = 1;
	m_subresourceRange.levelCount = 1;
	m_dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	m_srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeMipLevelRange(uint32_t baseMipLevel, uint32_t levelCount)
{
	m_subresourceRange.baseMipLevel = baseMipLevel;
	m_subresourceRange.levelCount = levelCount;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_subresourceRange.baseArrayLayer = baseArrayLayer;
	m_subresourceRange.layerCount = layerCount;
	return *this;
}
ImageBarrierBuilder& ImageBarrierBuilder::CustomizeImageAspect(VkImageAspectFlags aspectMask)
{
	m_subresourceRange.aspectMask = aspectMask;
	return *this;
}

ImageBarrierBuilder& ImageBarrierBuilder::CustomizeQueueFamilyTransfer(uint32_t inQueueFamilyTransferFrom, uint32_t inQueueFamilyTransferTo)
{
	m_dstQueueFamilyIndex = inQueueFamilyTransferTo;
	m_srcQueueFamilyIndex = inQueueFamilyTransferFrom;
	return *this;
}

VkImageMemoryBarrier ImageBarrierBuilder::Build(
	VkImage _image,
	VkImageLayout _oldLayout,
	VkImageLayout _newLayout,
	VkAccessFlags _srcAccessMask,
	VkAccessFlags _dstAccessMask) const
{
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = _oldLayout;
	barrier.newLayout = _newLayout;
	barrier.srcQueueFamilyIndex = m_srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = m_dstQueueFamilyIndex;
	barrier.image = _image;
	barrier.subresourceRange = m_subresourceRange;
	barrier.srcAccessMask = _srcAccessMask;
	barrier.dstAccessMask = _dstAccessMask;
	return barrier;
}

BufferBarrierBuilder& BufferBarrierBuilder::Reset()
{
	pNext = nullptr;
	m_srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	m_dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	m_offset = 0;
	m_size = VK_WHOLE_SIZE;
}

BufferBarrierBuilder& BufferBarrierBuilder::CustomizeOffsetAndSize(VkDeviceSize inOffset, VkDeviceSize inSize)
{
	m_offset = inOffset;
	m_size = inSize;
	return *this;
}

BufferBarrierBuilder& BufferBarrierBuilder::CustomizeQueueFamilyTransfer(
	uint32_t inQueueFamilyTransferFrom,
	uint32_t inQueueFamilyTransferTo)
{
	m_dstQueueFamilyIndex = inQueueFamilyTransferTo;
	m_srcQueueFamilyIndex = inQueueFamilyTransferFrom;
	return *this;
}

VkBufferMemoryBarrier BufferBarrierBuilder::Build(
	VkBuffer inBuffer, 
	VkAccessFlags inSrcAccessMask, 
	VkAccessFlags inDstAccessMask) const
{
	VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };

	barrier.buffer = inBuffer;
	barrier.dstAccessMask = inDstAccessMask;
	barrier.dstQueueFamilyIndex = m_dstQueueFamilyIndex;
	barrier.offset = m_offset;
	barrier.size = m_size;
	barrier.srcAccessMask = inSrcAccessMask;
	barrier.srcQueueFamilyIndex = m_srcQueueFamilyIndex;

	return barrier;
}

ImageBlitBuilder::ImageBlitBuilder()
{
	Reset();
}

ImageBlitBuilder& ImageBlitBuilder::Reset()
{
	m_srcSubresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	m_srcSubresourceLayers.baseArrayLayer = 0;
	m_srcSubresourceLayers.layerCount = 1;

	m_dstSubresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	m_dstSubresourceLayers.baseArrayLayer = 0;
	m_dstSubresourceLayers.layerCount = 1;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeSrcAspect(VkImageAspectFlags aspectMask)
{
	m_srcSubresourceLayers.aspectMask = aspectMask;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeDstAspect(VkImageAspectFlags aspectMask)
{
	m_dstSubresourceLayers.aspectMask = aspectMask;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeSrcArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_srcSubresourceLayers.baseArrayLayer = baseArrayLayer;
	m_srcSubresourceLayers.layerCount = layerCount;

	return *this;
}

ImageBlitBuilder& ImageBlitBuilder::CustomizeDstArrayLayerRange(uint32_t baseArrayLayer, uint32_t layerCount)
{
	m_dstSubresourceLayers.baseArrayLayer = baseArrayLayer;
	m_dstSubresourceLayers.layerCount = layerCount;

	return *this;
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkOffset3D& srcOffsetUL,
	const VkOffset3D& srcOffsetLR,
	uint32_t srcMipLevel,
	const VkOffset3D& dstOffsetUL,
	const VkOffset3D& dstOffsetLR,
	uint32_t dstMipLevel) const
{
	VkImageBlit blit{};
	blit.srcOffsets[0] = srcOffsetUL;
	blit.srcOffsets[1] = srcOffsetLR;
	blit.dstOffsets[0] = dstOffsetUL;
	blit.dstOffsets[1] = dstOffsetLR;
	blit.srcSubresource = m_srcSubresourceLayers;
	blit.srcSubresource.mipLevel = srcMipLevel;
	blit.dstSubresource = m_dstSubresourceLayers;
	blit.dstSubresource.mipLevel = dstMipLevel;
	return blit;
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkExtent2D& srcImageSize,
	uint32_t srcMipLevel,
	const VkExtent2D& dstImageSize,
	uint32_t dstMipLevel) const
{
	VkOffset3D srcOffset;
	VkOffset3D dstOffset;

	srcOffset.z = 1;
	dstOffset.z = 1;

	srcOffset.x = static_cast<int32_t>(srcImageSize.width);
	srcOffset.y = static_cast<int32_t>(srcImageSize.height);
	dstOffset.x = static_cast<int32_t>(dstImageSize.width);
	dstOffset.y = static_cast<int32_t>(dstImageSize.height);

	return NewBlit(
		{ 0, 0, 0 },
		srcOffset,
		srcMipLevel,
		{ 0, 0, 0 },
		dstOffset,
		dstMipLevel);
}

VkImageBlit ImageBlitBuilder::NewBlit(
	const VkOffset2D& inSrcOffsetUpperLeftXY,
	const VkOffset2D& inSrcOffsetLowerRightXY,
	uint32_t inSrcMipLevel,
	const VkOffset2D& inDstOffsetUpperLeftXY,
	const VkOffset2D& inDstOffsetLowerRightXY,
	uint32_t inDstMipLevel) const
{
	return NewBlit(
		{ inSrcOffsetUpperLeftXY.x, inSrcOffsetUpperLeftXY.y, 0 },
		{ inSrcOffsetLowerRightXY.x, inSrcOffsetLowerRightXY.y, 1 },
		inSrcMipLevel,
		{ inDstOffsetUpperLeftXY.x, inDstOffsetUpperLeftXY.y, 0 },
		{ inDstOffsetLowerRightXY.x, inDstOffsetLowerRightXY.y, 1 },
		inDstMipLevel);
}

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::Reset()
{
	*this = VertexInputDescriptionBuilder{};

	return *this;
}

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::SetVertexStrideAndInputRate(uint32_t _stride, VkVertexInputRate _inputRate)
{
	m_uStride = _stride;
	m_InputRate = _inputRate;

	return *this;
}

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::AddLocation(VkFormat _format, uint32_t _offset, uint32_t _location)
{
	VkVertexInputAttributeDescription attr{};

	attr.binding = ~0; // unset
	attr.format = _format;
	attr.offset = _offset;
	attr.location = _location;

	m_Locations.push_back(attr);

	return *this;
}

void VertexInputDescriptionBuilder::BuildDescription(
	VkVertexInputBindingDescription& outBindingDescription, 
	std::vector<VkVertexInputAttributeDescription>& outAttributeDescriptions, 
	uint32_t inBinding) const
{
	std::vector<VkVertexInputAttributeDescription> ret = m_Locations;

	outBindingDescription.binding = inBinding;
	outBindingDescription.stride = m_uStride;
	outBindingDescription.inputRate = m_InputRate;

	for (size_t i = 0; i < ret.size(); ++i)
	{
		ret[i].binding = inBinding;
	}

	outAttributeDescriptions.insert(outAttributeDescriptions.end(), ret.begin(), ret.end());
}