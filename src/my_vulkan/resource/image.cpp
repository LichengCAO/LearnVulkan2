#include "image.h"
#include "device.h"
#include "memory_allocator.h"
#include <algorithm>
//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif defined in tinyglTF

Image::~Image()
{
	Destroy();
}

void Image::Create(const ImageCreateInfo& inCreateInfo)
{
	VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	auto& device = MyDevice::GetInstance();
	Information infoToFill{};
	const auto swapchainSize = device.GetSwapchainExtent();

	imageInfo.imageType = inCreateInfo.m_type;
	imageInfo.arrayLayers = inCreateInfo.m_optArrayLayers.value_or(1);
	imageInfo.flags = 0;
	imageInfo.extent.width = inCreateInfo.m_optWidth.value_or(swapchainSize.width);
	imageInfo.extent.height = inCreateInfo.m_optHeight.value_or(swapchainSize.height);
	imageInfo.extent.depth = inCreateInfo.m_optDepth.value_or(1);
	imageInfo.format = inCreateInfo.m_optFormat.value_or(VK_FORMAT_R32G32B32A32_SFLOAT);
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.mipLevels = inCreateInfo.m_optMipLevels.value_or(1);
	imageInfo.samples = inCreateInfo.m_optSampleCount.value_or(VK_SAMPLE_COUNT_1_BIT);
	imageInfo.usage = inCreateInfo.m_usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.tiling = inCreateInfo.m_optTiling.value_or(VK_IMAGE_TILING_OPTIMAL);

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
	infoToFill.memoryProperty = inCreateInfo.m_optMemoryProperty.value_or(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	const VkImage image = device.CreateImage(imageInfo);
	auto* allocator = device.GetMemoryAllocator();
	bool allocated = false;
	IntrusivePtr<VulkanDetail::ImageResource> resource;
	try
	{
		allocator->AllocateForVkImage(image, infoToFill.memoryProperty);
		allocated = true;
		resource = MakeIntrusive<VulkanDetail::ImageResource>(image,
			[vkDevice = device.vkDevice, allocator](VkImage inImage)
			{
				vkDestroyImage(vkDevice, inImage, nullptr);
				allocator->FreeMemory(inImage);
			});
	}
	catch (...)
	{
		vkDestroyImage(device.vkDevice, image, nullptr);
		if (allocated)
		{
			allocator->FreeMemory(image);
		}
		throw;
	}
	ClearViewCache();
	m_resource = std::move(resource);
	m_imageInformation = infoToFill;
}

void Image::_ImportSwapchain(VkImage inImage, VkImageUsageFlags inUsage, VkFormat inFormat,
	VkExtent2D inExtent, const IntrusivePtr<VulkanDetail::SwapchainResource>& inOwner)
{
	CHECK_TRUE(inImage != VK_NULL_HANDLE && inOwner, "Imported image requires its swapchain owner!");
	Information infoToFill{};

	infoToFill.arrayLayers = 1;
	infoToFill.depth = 1;
	infoToFill.format = inFormat;
	infoToFill.height = inExtent.height;
	infoToFill.imageType = VK_IMAGE_TYPE_2D;
	infoToFill.isSwapchainImage = true;
	infoToFill.memoryProperty = 0; // TODO
	infoToFill.mipLevels = 1;
	infoToFill.samples = VK_SAMPLE_COUNT_1_BIT;
	infoToFill.tiling = VK_IMAGE_TILING_LINEAR;
	infoToFill.usage = inUsage;
	infoToFill.width = inExtent.width;

	auto resource = MakeIntrusive<VulkanDetail::ImageResource>(inImage, nullptr,
		std::vector<VulkanDetail::ResourceReference>{ inOwner });
	ClearViewCache();
	m_resource = std::move(resource);
	m_imageInformation = infoToFill;
}

void Image::Destroy()
{
	ClearViewCache();
	m_resource.reset();
	m_imageInformation = {};
}

void Image::ClearViewCache()
{
	m_imageViews.clear();
}

auto Image::_GetResourceReference() const -> VulkanDetail::ResourceReference
{
	return m_resource;
}

auto Image::GetImageInformation() const -> const Image::Information&
{
	return m_imageInformation;
}

auto Image::GetImageSize() const -> VkExtent3D
{
	VkExtent3D extent{};
	extent.width = m_imageInformation.width;
	extent.height = m_imageInformation.height;
	extent.depth = m_imageInformation.depth;
	return extent;
}

auto Image::GetVkImage() const -> VkImage
{
	return m_resource ? m_resource->GetHandle() : VK_NULL_HANDLE;
}

auto Image::IsValid() const noexcept -> bool { return static_cast<bool>(m_resource); }

SwapchainImage::SwapchainImage(uint32_t inIndex)
	: m_index(inIndex)
{
}

SwapchainImage::~SwapchainImage()
{
	if (m_renderFinishedSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(MyDevice::GetInstance().vkDevice, m_renderFinishedSemaphore, nullptr);
	}
}

void SwapchainImage::_CreateRenderFinishedSemaphore()
{
	CHECK_TRUE(m_renderFinishedSemaphore == VK_NULL_HANDLE, "Render-finished semaphore already exists!");
	VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK(vkCreateSemaphore(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &m_renderFinishedSemaphore),
		"Failed to create render-finished semaphore!");
}

void SwapchainImage::_SetAcquireSemaphore(VkSemaphore inSemaphore)
{
	CHECK_TRUE(inSemaphore != VK_NULL_HANDLE, "Swapchain acquire semaphore is invalid!");
	CHECK_TRUE(m_acquireSemaphore == VK_NULL_HANDLE, "Swapchain image is already acquired!");
	CHECK_TRUE(m_renderFinishedSemaphore != VK_NULL_HANDLE, "Render-finished semaphore is missing!");
	m_acquireSemaphore = inSemaphore;
	m_acquireWaitSubmitted = false;
}

void SwapchainImage::_CommitAcquireWait()
{
	CHECK_TRUE(m_acquireSemaphore != VK_NULL_HANDLE, "Swapchain image is not acquired!");
	CHECK_TRUE(!m_acquireWaitSubmitted, "Swapchain acquire wait was already submitted!");
	m_acquireWaitSubmitted = true;
}

void SwapchainImage::_MarkPresented()
{
	CHECK_TRUE(m_acquireSemaphore != VK_NULL_HANDLE, "Swapchain image is not acquired!");
	CHECK_TRUE(m_acquireWaitSubmitted, "Swapchain acquire wait was not submitted!");
	m_acquireSemaphore = VK_NULL_HANDLE;
	m_acquireWaitSubmitted = false;
}

auto SwapchainImage::GetIndex() const -> uint32_t
{
	return m_index;
}

auto SwapchainImage::GetAcquireSemaphore() const -> VkSemaphore
{
	CHECK_TRUE(m_acquireSemaphore != VK_NULL_HANDLE, "Swapchain image has not been acquired!");
	CHECK_TRUE(!m_acquireWaitSubmitted, "Swapchain acquire semaphore was already consumed!");
	return m_acquireSemaphore;
}

auto SwapchainImage::GetRenderFinishedSemaphore() const -> VkSemaphore
{
	CHECK_TRUE(m_renderFinishedSemaphore != VK_NULL_HANDLE, "Render-finished semaphore is missing!");
	return m_renderFinishedSemaphore;
}

auto Image::_FindView(const ImageViewInfo& inCreateInfo) const -> const ImageView*
{
	CHECK_TRUE(m_resource, "Image must be created before requesting an image view!");
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
	CHECK_TRUE(resolvedLevelCount <= m_imageInformation.mipLevels - inCreateInfo.m_baseMipLevel, "Mip range out of range!");
	CHECK_TRUE(resolvedLayerCount <= m_imageInformation.arrayLayers - inCreateInfo.m_baseArrayLayer, "Array layer range out of range!");

	for (const auto& view : m_imageViews)
	{
		const auto& viewInfo = view.GetImageViewInformation();
		if (viewInfo.baseMipLevel == inCreateInfo.m_baseMipLevel
			&& viewInfo.levelCount == resolvedLevelCount
			&& viewInfo.baseArrayLayer == inCreateInfo.m_baseArrayLayer
			&& viewInfo.layerCount == resolvedLayerCount)
		{
			return &view;
		}
	}

	return nullptr;
}

auto Image::View(const ImageViewInfo& inCreateInfo) const -> ImageView
{
	if (const ImageView* existingView = _FindView(inCreateInfo))
	{
		return *existingView;
	}

	ImageView view;
	view._Create(*this, inCreateInfo);
	m_imageViews.push_back(view);
	return view;
}

namespace
{
auto _GetDefaultAspectMask(VkFormat format) -> VkImageAspectFlags
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

void ImageView::_Create(const Image& inImage, const ImageViewInfo& inCreateInfo)
{
	CHECK_TRUE(!m_resource, "Image view already created!");

	auto& infoToFill = m_viewInformation;
	auto& device = MyDevice::GetInstance();
	VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	const auto& imageInfo = inImage.GetImageInformation();
	CHECK_TRUE(inCreateInfo.m_baseMipLevel < imageInfo.mipLevels, "Base mip level out of range!");
	CHECK_TRUE(inCreateInfo.m_baseArrayLayer < imageInfo.arrayLayers, "Base array layer out of range!");
	const uint32_t resolvedLevelCount = (inCreateInfo.m_levelCount == VK_REMAINING_MIP_LEVELS)
		? (imageInfo.mipLevels - inCreateInfo.m_baseMipLevel)
		: inCreateInfo.m_levelCount;
	const uint32_t resolvedLayerCount = (inCreateInfo.m_layerCount == VK_REMAINING_ARRAY_LAYERS)
		? (imageInfo.arrayLayers - inCreateInfo.m_baseArrayLayer)
		: inCreateInfo.m_layerCount;
	const VkImageAspectFlags aspectMask = _GetDefaultAspectMask(imageInfo.format);
	CHECK_TRUE(resolvedLevelCount > 0, "Image view level count must be greater than 0!");
	CHECK_TRUE(resolvedLayerCount > 0, "Image view layer count must be greater than 0!");
	CHECK_TRUE(resolvedLevelCount <= imageInfo.mipLevels - inCreateInfo.m_baseMipLevel, "Mip range out of range!");
	CHECK_TRUE(resolvedLayerCount <= imageInfo.arrayLayers - inCreateInfo.m_baseArrayLayer, "Array layer range out of range!");

	infoToFill.width = imageInfo.width;
	infoToFill.height = imageInfo.height;
	infoToFill.depth = imageInfo.depth;
	infoToFill.baseMipLevel = inCreateInfo.m_baseMipLevel;
	infoToFill.levelCount = resolvedLevelCount;
	infoToFill.baseArrayLayer = inCreateInfo.m_baseArrayLayer;
	infoToFill.layerCount = resolvedLayerCount;
	infoToFill.format = imageInfo.format;
	infoToFill.aspectMask = aspectMask;
	infoToFill.vkImage = inImage.GetVkImage();
	infoToFill.usage = imageInfo.usage;
	infoToFill.samples = imageInfo.samples;

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

	const VkImageView imageView = device.CreateImageView(viewInfo);
	try
	{
		m_resource = MakeIntrusive<VulkanDetail::ImageViewResource>(imageView,
			[vkDevice = device.vkDevice](VkImageView inView) { vkDestroyImageView(vkDevice, inView, nullptr); },
			std::vector<VulkanDetail::ResourceReference>{ inImage._GetResourceReference() });
	}
	catch (...)
	{
		device.DestroyImageView(imageView);
		throw;
	}
}

void ImageView::Reset() noexcept
{
	m_resource.reset();
	m_viewInformation = ImageView::Information{};
}

auto ImageView::GetImageViewInformation() const -> const Information&
{
	return m_viewInformation;
}

auto ImageView::GetImageSubresourceRange() const -> VkImageSubresourceRange
{
	VkImageSubresourceRange subRange{};
	subRange.aspectMask = m_viewInformation.aspectMask;
	subRange.baseMipLevel = m_viewInformation.baseMipLevel;
	subRange.levelCount = m_viewInformation.levelCount;
	subRange.baseArrayLayer = m_viewInformation.baseArrayLayer;
	subRange.layerCount = m_viewInformation.layerCount;

	return subRange;
}

auto ImageView::GetVkImageView() const noexcept -> VkImageView
{
	return m_resource ? m_resource->GetHandle() : VK_NULL_HANDLE;
}

auto ImageView::GetVkImage() const noexcept -> VkImage { return m_resource ? m_viewInformation.vkImage : VK_NULL_HANDLE; }
auto ImageView::IsValid() const noexcept -> bool { return static_cast<bool>(m_resource); }
auto ImageView::_GetResourceReference() const -> VulkanDetail::ResourceReference { return m_resource; }
auto ImageView::operator==(const ImageView& inOther) const noexcept -> bool { return m_resource.get() == inOther.m_resource.get(); }

auto ImageView::GetViewSize() const -> VkExtent3D
{
	if (!IsValid())
	{
		return {};
	}
	const auto mipSize = [mip = m_viewInformation.baseMipLevel](uint32_t inSize) -> uint32_t
	{
		return mip >= 32 ? 1 : std::max(1u, inSize >> mip);
	};
	return { mipSize(m_viewInformation.width), mipSize(m_viewInformation.height), mipSize(m_viewInformation.depth) };
}

void ImageCreateInfo::Reset()
{
	*this = ImageCreateInfo{};
}

void ImageCreateInfo::SetUsage(VkImageUsageFlags usage)
{
	m_usage = usage;
}

void ImageCreateInfo::CustomizeSize1D(uint32_t width)
{
	m_optWidth = width;
	m_optHeight = 1;
	m_optDepth = 1;
	m_type = VK_IMAGE_TYPE_1D;
}

void ImageCreateInfo::CustomizeSize2D(uint32_t width, uint32_t height)
{
	m_optWidth = width;
	m_optHeight = height;
	m_optDepth = 1;
	m_type = VK_IMAGE_TYPE_2D;
}

void ImageCreateInfo::CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth)
{
	m_optWidth = width;
	m_optHeight = height;
	m_optDepth = depth;
	m_type = VK_IMAGE_TYPE_3D;
}

void ImageCreateInfo::CustomizeMipLevels(uint32_t mipLevelCount)
{
	m_optMipLevels = mipLevelCount;
}

void ImageCreateInfo::CustomizeArrayLayers(uint32_t layerCount)
{
	m_optArrayLayers = layerCount;
}

void ImageCreateInfo::CustomizeFormat(VkFormat format)
{
	m_optFormat = format;
}

void ImageCreateInfo::CustomizeImageTiling(VkImageTiling tiling)
{
	m_optTiling = tiling;
}

void ImageCreateInfo::CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty)
{
	m_optMemoryProperty = memoryProperty;
}

void ImageCreateInfo::CustomizeSampleCount(VkSampleCountFlagBits sampleCount)
{
	m_optSampleCount = sampleCount;
}
