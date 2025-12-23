#include "image.h"
#include "device.h"
#include "buffer.h"
#include "commandbuffer.h"
#include "memory_allocator.h"
//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif defined in tinyglTF

Image::~Image()
{
	assert(m_vkImage == VK_NULL_HANDLE);
}

void Image::Init(const IImageInitializer* pInit)
{
	pInit->InitImage(this);
}

void Image::Uninit()
{
	if (m_imageInformation.isSwapchainImage)
	{
		if (m_vkImage != VK_NULL_HANDLE)
		{
			m_vkImage = VK_NULL_HANDLE;
		}
	}
	else
	{
		if (m_vkImage != VK_NULL_HANDLE)
		{
			_FreeMemory();
			vkDestroyImage(MyDevice::GetInstance().vkDevice, m_vkImage, nullptr);
			m_vkImage = VK_NULL_HANDLE;
		}
	}
}

void Image::_AllocateMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->AllocateForVkImage(m_vkImage, m_imageInformation.memoryProperty);
}

void Image::_FreeMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->FreeMemory(m_vkImage);
}

MemoryAllocator* Image::_GetMemoryAllocator() const
{
	return MyDevice::GetInstance().GetMemoryAllocator();
}

const Image::Information& Image::GetImageInformation() const
{
	return m_imageInformation;
}

VkExtent3D Image::GetImageSize() const
{
	VkExtent3D extent{};
	extent.width = m_imageInformation.width;
	extent.height = m_imageInformation.height;
	extent.depth = m_imageInformation.depth;
	return extent;
}

VkImage Image::GetVkImage() const
{
	return m_vkImage;
}

ImageView::~ImageView()
{
	assert(m_vkImageView == VK_NULL_HANDLE);
}

void ImageView::Init(const IImageViewInitializer* inInitPtr)
{
	inInitPtr->InitImageView(this);
}

void ImageView::Uninit()
{
	if (m_vkImageView != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();
		device.DestroyImageView(m_vkImageView);
		m_vkImageView = VK_NULL_HANDLE;
	}
	m_viewInformation = ImageView::Information{};
}

const ImageView::Information& ImageView::GetImageViewInformation() const
{
	return m_viewInformation;
}

VkImageSubresourceRange ImageView::GetImageSubresourceRange() const
{
	VkImageSubresourceRange subRange{};
	subRange.aspectMask = m_viewInformation.aspectMask;
	subRange.baseMipLevel = m_viewInformation.baseMipLevel;
	subRange.levelCount = m_viewInformation.levelCount;
	subRange.baseArrayLayer = m_viewInformation.baseArrayLayer;
	subRange.layerCount = m_viewInformation.layerCount;

	return subRange;
}

VkImageView ImageView::GetVkImageView() const
{
	return m_vkImageView;
}

void Image::Initializer::InitImage(Image* outImage) const
{
	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	auto& device = MyDevice::GetInstance();
	auto& infoToFill = outImage->m_imageInformation;
	const auto swapchainSize = device.GetSwapchainExtent();

	imageInfo.imageType = m_type;
	imageInfo.arrayLayers = m_optArrayLayers.value_or(1);
	imageInfo.flags = 0;
	imageInfo.extent.width = m_optWidth.value_or(swapchainSize.width);
	imageInfo.extent.height = m_optHeight.value_or(swapchainSize.height);
	imageInfo.extent.depth = m_optDepth.value_or(1);
	imageInfo.format = m_optFormat.value_or(VK_FORMAT_R32G32B32A32_SFLOAT);
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.mipLevels = m_optMipLevels.value_or(1);
	imageInfo.samples = m_optSampleCount.value_or(VK_SAMPLE_COUNT_1_BIT);
	imageInfo.usage = m_usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.tiling = m_optTiling.value_or(VK_IMAGE_TILING_OPTIMAL);
	
	infoToFill.imageType = imageInfo.imageType;
	infoToFill.width = imageInfo.extent.width;
	infoToFill.height = imageInfo.extent.height;
	infoToFill.depth = imageInfo.extent.depth;
	infoToFill.mipLevels = imageInfo.mipLevels;
	infoToFill.arrayLayers = imageInfo.arrayLayers;
	infoToFill.format = imageInfo.format;
	infoToFill.tiling = imageInfo.tiling;
	infoToFill.layout = imageInfo.initialLayout;
	infoToFill.usage = imageInfo.usage;
	infoToFill.samples = imageInfo.samples;
	infoToFill.isSwapchainImage = false;
	infoToFill.memoryProperty = m_optMemoryProperty.value_or(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	outImage->m_vkImage = device.CreateImage(imageInfo);
	outImage->_AllocateMemory();
}

Image::Initializer& Image::Initializer::Reset()
{
	*this = Image::Initializer{};
	return *this;
}

Image::Initializer& Image::Initializer::SetUsage(VkImageUsageFlags usage)
{
	m_usage = usage;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeSize1D(uint32_t width)
{
	m_optWidth = width;
	m_type = VK_IMAGE_TYPE_1D;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeSize2D(uint32_t width, uint32_t height)
{
	m_optWidth = width;
	m_optHeight = height;
	m_type = VK_IMAGE_TYPE_2D;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth)
{
	m_optWidth = width;
	m_optHeight = height;
	m_optDepth = depth;
	m_type = VK_IMAGE_TYPE_3D;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeMipLevels(uint32_t mipLevelCount)
{
	m_optMipLevels = mipLevelCount;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeArrayLayers(uint32_t layerCount)
{
	m_optArrayLayers = layerCount;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeFormat(VkFormat format)
{
	m_optFormat = format;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeImageTiling(VkImageTiling tiling)
{
	m_optTiling = tiling;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty)
{
	m_optMemoryProperty = memoryProperty;
	return *this;
}

Image::Initializer& Image::Initializer::CustomizeSampleCount(VkSampleCountFlagBits sampleCount)
{
	m_optSampleCount = sampleCount;
	return *this;
}

void Image::SwapchainImageInit::InitImage(Image* outImagePtr) const
{
	auto& infoToFill = outImagePtr->m_imageInformation;
	auto& device = MyDevice::GetInstance();
	const auto size2D = device.GetSwapchainExtent();

	infoToFill.arrayLayers = 1;
	infoToFill.depth = 1;
	infoToFill.format = m_format;
	infoToFill.height = size2D.height;
	infoToFill.imageType = VK_IMAGE_TYPE_2D;
	infoToFill.isSwapchainImage = true;
	infoToFill.layout = VK_IMAGE_LAYOUT_UNDEFINED; // TODO
	infoToFill.memoryProperty = 0; // TODO
	infoToFill.mipLevels = 1;
	infoToFill.samples = VK_SAMPLE_COUNT_1_BIT;
	infoToFill.tiling = VK_IMAGE_TILING_LINEAR;
	infoToFill.usage = m_usage;
	infoToFill.width = size2D.width;

	outImagePtr->m_vkImage = m_vkHandle;
}

Image::SwapchainImageInit& Image::SwapchainImageInit::SetUp(VkImage inSwapchain, VkImageUsageFlags inUsage, VkFormat inFormat)
{
	m_vkHandle = inSwapchain;
	m_usage = inUsage;
	m_format = inFormat;

	return *this;
}

void ImageView::ImageViewInit::InitImageView(ImageView* outViewPtr) const
{
	CHECK_TRUE(m_pImage != nullptr, "No image!");
	auto& infoToFill = outViewPtr->m_viewInformation;
	auto& device = MyDevice::GetInstance();
	VkImageViewCreateInfo viewInfo{};
	const auto& imageInfo = m_pImage->GetImageInformation();

	infoToFill.layerCount = (m_layerCount == VK_REMAINING_ARRAY_LAYERS) ? (imageInfo.arrayLayers - m_baseArrayLayer) : m_layerCount;
	infoToFill.aspectMask = m_aspectMask;
	infoToFill.baseMipLevel = m_baseMipLevel;
	infoToFill.levelCount = m_levelCount;
	infoToFill.baseArrayLayer = m_baseArrayLayer;
	infoToFill.layerCount = m_layerCount;
	infoToFill.width = imageInfo.width;
	infoToFill.height = imageInfo.height;
	infoToFill.depth = imageInfo.depth;
	switch (imageInfo.imageType)
	{
	case VK_IMAGE_TYPE_1D:
	{
		infoToFill.type = infoToFill.layerCount == 1 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
	}
	break;
	case VK_IMAGE_TYPE_2D:
	{
		infoToFill.type = infoToFill.layerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	}
	break;
	case VK_IMAGE_TYPE_3D:
	{
		CHECK_TRUE(infoToFill.layerCount == 1);
		infoToFill.type = VK_IMAGE_VIEW_TYPE_3D;
	}
	break;
	default:
		CHECK_TRUE(false, "Unhandled image type!");
		break;
	}

	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.pNext = nullptr;  // Ensure this is set explicitly if pNext isn't used.
	viewInfo.flags = 0;     // Set flags to 0 unless specific flags are required.
	viewInfo.image = m_pImage->GetVkImage();
	viewInfo.format = m_format.value_or(imageInfo.format);
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;  // Default swizzle values if not specified.
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.subresourceRange.aspectMask = m_aspectMask;
	viewInfo.subresourceRange.baseMipLevel = m_baseMipLevel;
	viewInfo.subresourceRange.levelCount = m_levelCount;
	viewInfo.subresourceRange.baseArrayLayer = m_baseArrayLayer;
	viewInfo.subresourceRange.layerCount = m_layerCount;
	viewInfo.viewType = infoToFill.type;

	outViewPtr->m_vkImageView = device.CreateImageView(viewInfo);
}

ImageView::ImageViewInit& ImageView::ImageViewInit::Reset()
{
	*this = ImageView::ImageViewInit{};

	return *this;
}

ImageView::ImageViewInit& ImageView::ImageViewInit::SetImage(const Image* pImage)
{
	m_pImage = pImage;

	return *this;
}

ImageView::ImageViewInit& ImageView::ImageViewInit::CustomizeImageAspect(VkImageAspectFlags inAspect)
{
	m_aspectMask = inAspect;

	return *this;
}

ImageView::ImageViewInit& ImageView::ImageViewInit::CustomizeMipLevels(uint32_t inBaseLevel, uint32_t levelCount)
{
	m_baseMipLevel = inBaseLevel;
	m_layerCount = levelCount;

	return *this;
}

ImageView::ImageViewInit& ImageView::ImageViewInit::CustomizeArrayLayers(uint32_t inBaseLayer, uint32_t layerCount)
{
	m_baseArrayLayer = inBaseLayer;
	m_layerCount = layerCount;

	return *this;
}

ImageView::ImageViewInit& ImageView::ImageViewInit::CustomizeFormat(VkFormat inFormat)
{
	m_format = inFormat;

	return *this;
}
