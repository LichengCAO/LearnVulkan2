#pragma once
#include "common.h"

class Buffer;
class Image;
class ImageView;
class MemoryAllocator;

// commands that will change image layout:
//	vkCreateRenderPass vkMapMemory vkQueuePresentKHR 
//	vkQueueSubmit vkCmdCopyImage vkCmdCopyImageToBuffer 
//	vkCmdWaitEvents VkCmdPipelineBarrier
// https://gpuopen.com/download/Vulkanised2019_06_optimising_aaa_vulkan_title_on_desktop.pdf

class IImageInitializer
{
public:
	virtual ~IImageInitializer() {};
	virtual void InitImage(Image* outImagePtr) const = 0;
};

class Image
{
public:
	struct Information
	{
		uint32_t width;
		uint32_t height;
		uint32_t depth;
		uint32_t mipLevels;
		uint32_t arrayLayers;
		VkFormat format;
		VkImageType imageType;
		VkImageTiling tiling;
		VkImageUsageFlags usage;
		VkSampleCountFlagBits samples;
		VkMemoryPropertyFlags memoryProperty;			  // image memory
		bool isSwapchainImage = false;
	};

	class Initializer : public IImageInitializer
	{
	private:
		VkImageUsageFlags m_usage = 0;
		VkImageType m_type = VK_IMAGE_TYPE_2D;
		std::optional<uint32_t> m_optWidth;							// optional, if unset image will be swapchain size
		std::optional<uint32_t> m_optHeight;						// optional, don't set it if image isn't 2D
		std::optional<uint32_t> m_optDepth;							// optional, don't set it if image isn't 3D
		std::optional<uint32_t> m_optMipLevels;						// optional, default value: 1
		std::optional<uint32_t> m_optArrayLayers;					// optional, default value: 1
		std::optional<VkFormat> m_optFormat;						// optional, default value: VK_FORMAT_R32G32B32A32_SFLOAT;
		std::optional<VkImageTiling> m_optTiling;					// optional, default value: VK_IMAGE_TILING_OPTIMAL;
		std::optional<VkMemoryPropertyFlags> m_optMemoryProperty;	// optional, default value: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		std::optional<VkSampleCountFlagBits> m_optSampleCount;		// optional, default value: VK_SAMPLE_COUNT_1_BIT;

	public:
		virtual void InitImage(Image* outImagePtr) const override;

		Image::Initializer& Reset();

		// Set usage of image, mandatory
		Image::Initializer& SetUsage(VkImageUsageFlags usage);

		// Optional, default: swapchain image size 2D image
		Image::Initializer& CustomizeSize1D(uint32_t width);

		// Optional, default: swapchain image size 2D image
		Image::Initializer& CustomizeSize2D(uint32_t width, uint32_t height);

		// Optional, default: swapchain image size 2D image
		Image::Initializer& CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth);

		// Optional, default: 1
		Image::Initializer& CustomizeMipLevels(uint32_t mipLevelCount);

		// Optional, default: 1
		Image::Initializer& CustomizeArrayLayers(uint32_t layerCount);

		// Optional, default: VK_FORMAT_R32G32B32A32_SFLOAT
		Image::Initializer& CustomizeFormat(VkFormat format);

		// Optional, default: VK_IMAGE_TILING_OPTIMAL
		Image::Initializer& CustomizeImageTiling(VkImageTiling tiling);

		// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		Image::Initializer& CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty);

		// Optional, default: VK_SAMPLE_COUNT_1_BIT
		Image::Initializer& CustomizeSampleCount(VkSampleCountFlagBits sampleCount);
	};

	class SwapchainImageInit : public IImageInitializer
	{
	private:
		VkImage m_vkHandle = VK_NULL_HANDLE;
		VkImageUsageFlags m_usage = 0;
		VkFormat m_format = VK_FORMAT_UNDEFINED;

	public:
		virtual void InitImage(Image* outImagePtr) const override;

		Image::SwapchainImageInit& SetUp(VkImage inSwapchain, VkImageUsageFlags inUsage, VkFormat inFormat);
	};

private:
	Information m_imageInformation{};
	VkImage m_vkImage = VK_NULL_HANDLE;

private:
	void _AllocateMemory();
	
	void _FreeMemory();

	MemoryAllocator* _GetMemoryAllocator() const;

public:
	~Image();

	void Init(const IImageInitializer* pInit);

	void Uninit();

	const Image::Information& GetImageInformation() const;

	VkExtent3D GetImageSize() const;

	VkImage GetVkImage() const;
};

class IImageViewInitializer
{
public:
	virtual void InitImageView(ImageView* outViewPtr) const = 0;
};

class ImageView
{
public:
	struct Information
	{
		uint32_t width;
		uint32_t height;
		uint32_t depth;
		uint32_t baseMipLevel = 0;
		uint32_t levelCount = VK_REMAINING_MIP_LEVELS;
		uint32_t baseArrayLayer = 0;
		uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS;
		VkFormat format = VK_FORMAT_UNDEFINED; // https://stackoverflow.com/questions/58600143/why-would-vkimageview-format-differ-from-the-underlying-vkimage-format
		VkImageAspectFlags aspectMask;
		VkImageViewType type;
		VkImage vkImage = VK_NULL_HANDLE;
	};

	class ImageViewInit : public IImageViewInitializer
	{
	private:
		std::optional<VkFormat> m_format;
		VkImageViewType m_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		const Image* m_pImage = nullptr;
		uint32_t m_baseMipLevel = 0;
		uint32_t m_levelCount = VK_REMAINING_MIP_LEVELS;
		uint32_t m_baseArrayLayer = 0;
		uint32_t m_layerCount = VK_REMAINING_ARRAY_LAYERS;
		VkImageAspectFlags m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	public:
		virtual void InitImageView(ImageView* outViewPtr) const override;

		ImageView::ImageViewInit& Reset();

		ImageView::ImageViewInit& SetImage(const Image* pImage);

		// Optional, default: VK_IMAGE_ASPECT_COLOR_BIT
		ImageView::ImageViewInit& CustomizeImageAspect(VkImageAspectFlags inAspect);

		// Optional, default: 0, VK_REMAINING_MIP_LEVELS
		ImageView::ImageViewInit& CustomizeMipLevels(uint32_t inBaseLevel, uint32_t levelCount = VK_REMAINING_MIP_LEVELS);

		// Optional, default: 0, VK_REMAINING_ARRAY_LAYERS
		ImageView::ImageViewInit& CustomizeArrayLayers(uint32_t inBaseLayer, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

		// Optional, default: format of source image, 
		// only possible when image is created with VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT and compatible formats
		ImageView::ImageViewInit& CustomizeFormat(VkFormat inFormat);
	};

private:
	Information m_viewInformation{};
	VkImageView m_vkImageView = VK_NULL_HANDLE;

public:
	~ImageView();

	void Init(const IImageViewInitializer* inInitPtr);

	void Uninit();

	const Information& GetImageViewInformation() const;

	VkImageSubresourceRange GetImageSubresourceRange() const;

	VkImageView GetVkImageView() const;
};