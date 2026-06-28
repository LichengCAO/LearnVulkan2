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

class ImageCreateInfo final
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

	friend class Image;

public:
	ImageCreateInfo& Reset();

	// Set usage of image, mandatory
	ImageCreateInfo& SetUsage(VkImageUsageFlags usage);

	// Optional, default: swapchain image size 2D image
	ImageCreateInfo& CustomizeSize1D(uint32_t width);

	// Optional, default: swapchain image size 2D image
	ImageCreateInfo& CustomizeSize2D(uint32_t width, uint32_t height);

	// Optional, default: swapchain image size 2D image
	ImageCreateInfo& CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth);

	// Optional, default: 1
	ImageCreateInfo& CustomizeMipLevels(uint32_t mipLevelCount);

	// Optional, default: 1
	ImageCreateInfo& CustomizeArrayLayers(uint32_t layerCount);

	// Optional, default: VK_FORMAT_R32G32B32A32_SFLOAT
	ImageCreateInfo& CustomizeFormat(VkFormat format);

	// Optional, default: VK_IMAGE_TILING_OPTIMAL
	ImageCreateInfo& CustomizeImageTiling(VkImageTiling tiling);

	// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	ImageCreateInfo& CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty);

	// Optional, default: VK_SAMPLE_COUNT_1_BIT
	ImageCreateInfo& CustomizeSampleCount(VkSampleCountFlagBits sampleCount);
};

class SwapchainImageCreateInfo final
{
private:
	VkImage m_vkHandle = VK_NULL_HANDLE;
	VkImageUsageFlags m_usage = 0;
	VkFormat m_format = VK_FORMAT_UNDEFINED;

	friend class Image;

public:
	SwapchainImageCreateInfo& SetUp(VkImage inSwapchain, VkImageUsageFlags inUsage, VkFormat inFormat);
};

class ImageViewInfo final
{
private:
	friend class ImageView;
	friend class Image;

	uint32_t m_baseMipLevel = 0;
	uint32_t m_levelCount = VK_REMAINING_MIP_LEVELS;
	uint32_t m_baseArrayLayer = 0;
	uint32_t m_layerCount = VK_REMAINING_ARRAY_LAYERS;

public:
	void Reset()
	{
		*this = ImageViewInfo{};
	}

	// Optional, default: 0, VK_REMAINING_MIP_LEVELS
	void CustomizeMipLevels(uint32_t inBaseLevel, uint32_t levelCount = VK_REMAINING_MIP_LEVELS)
	{
		m_baseMipLevel = inBaseLevel;
		m_levelCount = levelCount;
	}

	// Optional, default: 0, VK_REMAINING_ARRAY_LAYERS
	void CustomizeArrayLayers(uint32_t inBaseLayer, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS)
	{
		m_baseArrayLayer = inBaseLayer;
		m_layerCount = layerCount;
	}
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
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags aspectMask;
		VkImageViewType type;
		VkImage vkImage = VK_NULL_HANDLE;
	};

private:
	Information m_viewInformation{};
	VkImageView m_vkImageView = VK_NULL_HANDLE;

private:
	void Create(const Image* inImage, const ImageViewInfo* inCreateInfo);

	void Destroy();

public:
	~ImageView();

	const Information& GetImageViewInformation() const;

	VkImageSubresourceRange GetImageSubresourceRange() const;

	VkImageView GetVkImageView() const;

	friend class Image;
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

private:
	Information m_imageInformation{};
	VkImage m_vkImage = VK_NULL_HANDLE;
	std::vector<std::unique_ptr<ImageView>> m_uptrImageViews;

private:
	void _AllocateMemory();
	
	void _FreeMemory();

	MemoryAllocator* _GetMemoryAllocator() const;

	ImageView* _FindView(const ImageViewInfo& inCreateInfo) const;

	void _DestroyViews();

public:
	~Image();

	void Create(const ImageCreateInfo* inCreateInfo);

	void Create(const SwapchainImageCreateInfo* inCreateInfo);

	void Destroy();

	const ImageView* View();

	const ImageView* View(const ImageViewInfo& inCreateInfo);

	const Image::Information& GetImageInformation() const;

	VkExtent3D GetImageSize() const;

	VkImage GetVkImage() const;
};
