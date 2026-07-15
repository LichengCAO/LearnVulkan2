#include "image.h"
#include "device.h"
#include "buffer.h"
#include "command_buffer.h"
#include "memory_allocator.h"
//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif defined in tinyglTF

Image::~Image()
{
	assert(m_uptrImageViews.empty());
	assert(m_vkImage == VK_NULL_HANDLE);
}

void Image::Create(const ImageCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No image create info!");

	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	auto& device = MyDevice::GetInstance();
	auto& infoToFill = m_imageInformation;
	const auto swapchainSize = device.GetSwapchainExtent();

	imageInfo.imageType = inCreateInfo->m_type;
	imageInfo.arrayLayers = inCreateInfo->m_optArrayLayers.value_or(1);
	imageInfo.flags = 0;
	imageInfo.extent.width = inCreateInfo->m_optWidth.value_or(swapchainSize.width);
	imageInfo.extent.height = inCreateInfo->m_optHeight.value_or(swapchainSize.height);
	imageInfo.extent.depth = inCreateInfo->m_optDepth.value_or(1);
	imageInfo.format = inCreateInfo->m_optFormat.value_or(VK_FORMAT_R32G32B32A32_SFLOAT);
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.mipLevels = inCreateInfo->m_optMipLevels.value_or(1);
	imageInfo.samples = inCreateInfo->m_optSampleCount.value_or(VK_SAMPLE_COUNT_1_BIT);
	imageInfo.usage = inCreateInfo->m_usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.tiling = inCreateInfo->m_optTiling.value_or(VK_IMAGE_TILING_OPTIMAL);

	infoToFill.imageType = imageInfo.imageType;
	infoToFill.width = imageInfo.extent.width;
	infoToFill.height = imageInfo.extent.height;
	infoToFill.depth = imageInfo.extent.depth;
	infoToFill.mipLevels = imageInfo.mipLevels;
	infoToFill.arrayLayers = imageInfo.arrayLayers;
	infoToFill.format = imageInfo.format;
	infoToFill.tiling = imageInfo.tiling;
	infoToFill.usage = imageInfo.usage;
	infoToFill.samples = imageInfo.samples;
	infoToFill.isSwapchainImage = false;
	infoToFill.memoryProperty = inCreateInfo->m_optMemoryProperty.value_or(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_vkImage = device.CreateImage(imageInfo);
	_AllocateMemory();
}

void Image::Create(const SwapchainImageCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No swapchain image create info!");

	auto& infoToFill = m_imageInformation;
	auto& device = MyDevice::GetInstance();
	const auto size2D = device.GetSwapchainExtent();

	infoToFill.arrayLayers = 1;
	infoToFill.depth = 1;
	infoToFill.format = inCreateInfo->m_format;
	infoToFill.height = size2D.height;
	infoToFill.imageType = VK_IMAGE_TYPE_2D;
	infoToFill.isSwapchainImage = true;
	infoToFill.memoryProperty = 0; // TODO
	infoToFill.mipLevels = 1;
	infoToFill.samples = VK_SAMPLE_COUNT_1_BIT;
	infoToFill.tiling = VK_IMAGE_TILING_LINEAR;
	infoToFill.usage = inCreateInfo->m_usage;
	infoToFill.width = size2D.width;

	m_vkImage = inCreateInfo->m_vkHandle;
}

void Image::Destroy()
{
	_DestroyViews();

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

ImageView* Image::_FindView(const ImageViewInfo& inCreateInfo) const
{
	CHECK_TRUE(m_vkImage != VK_NULL_HANDLE, "Image must be created before requesting an image view!");
	CHECK_TRUE(inCreateInfo.m_baseMipLevel < m_imageInformation.mipLevels, "Base mip level out of range!");
	CHECK_TRUE(inCreateInfo.m_baseArrayLayer < m_imageInformation.arrayLayers, "Base array layer out of range!");

	const uint32_t resolvedLevelCount = (inCreateInfo.m_levelCount == VK_REMAINING_MIP_LEVELS)
		? (m_imageInformation.mipLevels - inCreateInfo.m_baseMipLevel)
		: inCreateInfo.m_levelCount;
	const uint32_t resolvedLayerCount = (inCreateInfo.m_layerCount == VK_REMAINING_ARRAY_LAYERS)
		? (m_imageInformation.arrayLayers - inCreateInfo.m_baseArrayLayer)
		: inCreateInfo.m_layerCount;

	CHECK_TRUE(resolvedLevelCount > 0, "Image view level count must be greater than 0!");
	CHECK_TRUE(resolvedLayerCount > 0, "Image view layer count must be greater than 0!");
	CHECK_TRUE((inCreateInfo.m_baseMipLevel + resolvedLevelCount) <= m_imageInformation.mipLevels, "Mip range out of range!");
	CHECK_TRUE((inCreateInfo.m_baseArrayLayer + resolvedLayerCount) <= m_imageInformation.arrayLayers, "Array layer range out of range!");

	for (const auto& uptrView : m_uptrImageViews)
	{
		const auto& viewInfo = uptrView->GetImageViewInformation();
		if (viewInfo.baseMipLevel == inCreateInfo.m_baseMipLevel
			&& viewInfo.levelCount == resolvedLevelCount
			&& viewInfo.baseArrayLayer == inCreateInfo.m_baseArrayLayer
			&& viewInfo.layerCount == resolvedLayerCount)
		{
			return uptrView.get();
		}
	}

	return nullptr;
}

void Image::_DestroyViews()
{
	for (auto& uptrView : m_uptrImageViews)
	{
		if (uptrView != nullptr)
		{
			uptrView->Destroy();
		}
	}
	m_uptrImageViews.clear();
}

const ImageView* Image::View()
{
	ImageViewInfo createInfo{};

	return View(createInfo);
}

const ImageView* Image::View(const ImageViewInfo& inCreateInfo)
{
	if (ImageView* existingView = _FindView(inCreateInfo))
	{
		return existingView;
	}

	auto uptrView = std::make_unique<ImageView>();
	uptrView->Create(this, &inCreateInfo);
	ImageView* result = uptrView.get();
	m_uptrImageViews.push_back(std::move(uptrView));

	return result;
}

ImageView::~ImageView()
{
	assert(m_vkImageView == VK_NULL_HANDLE);
}

namespace
{
VkImageAspectFlags _GetDefaultAspectMask(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}
}

void ImageView::Create(const Image* inImage, const ImageViewInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No image view create info!");
	CHECK_TRUE(inImage != nullptr, "No image!");
	CHECK_TRUE(m_vkImageView == VK_NULL_HANDLE, "Image view already created!");

	auto& infoToFill = m_viewInformation;
	auto& device = MyDevice::GetInstance();
	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	const auto& imageInfo = inImage->GetImageInformation();
	CHECK_TRUE(inCreateInfo->m_baseMipLevel < imageInfo.mipLevels, "Base mip level out of range!");
	CHECK_TRUE(inCreateInfo->m_baseArrayLayer < imageInfo.arrayLayers, "Base array layer out of range!");
	const uint32_t resolvedLevelCount = (inCreateInfo->m_levelCount == VK_REMAINING_MIP_LEVELS)
		? (imageInfo.mipLevels - inCreateInfo->m_baseMipLevel)
		: inCreateInfo->m_levelCount;
	const uint32_t resolvedLayerCount = (inCreateInfo->m_layerCount == VK_REMAINING_ARRAY_LAYERS)
		? (imageInfo.arrayLayers - inCreateInfo->m_baseArrayLayer)
		: inCreateInfo->m_layerCount;
	const VkImageAspectFlags aspectMask = _GetDefaultAspectMask(imageInfo.format);
	CHECK_TRUE(resolvedLevelCount > 0, "Image view level count must be greater than 0!");
	CHECK_TRUE(resolvedLayerCount > 0, "Image view layer count must be greater than 0!");
	CHECK_TRUE((inCreateInfo->m_baseMipLevel + resolvedLevelCount) <= imageInfo.mipLevels, "Mip range out of range!");
	CHECK_TRUE((inCreateInfo->m_baseArrayLayer + resolvedLayerCount) <= imageInfo.arrayLayers, "Array layer range out of range!");

	infoToFill.width = imageInfo.width;
	infoToFill.height = imageInfo.height;
	infoToFill.depth = imageInfo.depth;
	infoToFill.baseMipLevel = inCreateInfo->m_baseMipLevel;
	infoToFill.levelCount = resolvedLevelCount;
	infoToFill.baseArrayLayer = inCreateInfo->m_baseArrayLayer;
	infoToFill.layerCount = resolvedLayerCount;
	infoToFill.format = imageInfo.format;
	infoToFill.aspectMask = aspectMask;
	infoToFill.vkImage = inImage->GetVkImage();

	switch (imageInfo.imageType)
	{
	case VK_IMAGE_TYPE_1D:
	{
		infoToFill.type = resolvedLayerCount == 1 ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
	}
	break;
	case VK_IMAGE_TYPE_2D:
	{
		infoToFill.type = resolvedLayerCount == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	}
	break;
	case VK_IMAGE_TYPE_3D:
	{
		CHECK_TRUE(resolvedLayerCount == 1);
		infoToFill.type = VK_IMAGE_VIEW_TYPE_3D;
	}
	break;
	default:
		CHECK_TRUE(false, "Unhandled image type!");
		break;
	}

	viewInfo.image = infoToFill.vkImage;
	viewInfo.viewType = infoToFill.type;
	viewInfo.format = infoToFill.format;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.subresourceRange.aspectMask = infoToFill.aspectMask;
	viewInfo.subresourceRange.baseMipLevel = infoToFill.baseMipLevel;
	viewInfo.subresourceRange.levelCount = infoToFill.levelCount;
	viewInfo.subresourceRange.baseArrayLayer = infoToFill.baseArrayLayer;
	viewInfo.subresourceRange.layerCount = infoToFill.layerCount;

	m_vkImageView = device.CreateImageView(viewInfo);
}

void ImageView::Destroy()
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

ImageCreateInfo& ImageCreateInfo::Reset()
{
	*this = ImageCreateInfo{};
	return *this;
}

ImageCreateInfo& ImageCreateInfo::SetUsage(VkImageUsageFlags usage)
{
	m_usage = usage;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeSize1D(uint32_t width)
{
	m_optWidth = width;
	m_type = VK_IMAGE_TYPE_1D;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeSize2D(uint32_t width, uint32_t height)
{
	m_optWidth = width;
	m_optHeight = height;
	m_type = VK_IMAGE_TYPE_2D;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth)
{
	m_optWidth = width;
	m_optHeight = height;
	m_optDepth = depth;
	m_type = VK_IMAGE_TYPE_3D;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeMipLevels(uint32_t mipLevelCount)
{
	m_optMipLevels = mipLevelCount;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeArrayLayers(uint32_t layerCount)
{
	m_optArrayLayers = layerCount;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeFormat(VkFormat format)
{
	m_optFormat = format;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeImageTiling(VkImageTiling tiling)
{
	m_optTiling = tiling;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty)
{
	m_optMemoryProperty = memoryProperty;
	return *this;
}

ImageCreateInfo& ImageCreateInfo::CustomizeSampleCount(VkSampleCountFlagBits sampleCount)
{
	m_optSampleCount = sampleCount;
	return *this;
}

SwapchainImageCreateInfo& SwapchainImageCreateInfo::SetUp(VkImage inSwapchain, VkImageUsageFlags inUsage, VkFormat inFormat)
{
	m_vkHandle = inSwapchain;
	m_usage = inUsage;
	m_format = inFormat;

	return *this;
}
