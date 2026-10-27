#pragma once
#include "common.h"
#include "vulkan_handle_resource.h"

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
	void Reset();

	// Set usage of image, mandatory
	void SetUsage(VkImageUsageFlags usage);

	// Optional, default: swapchain image size 2D image
	void CustomizeSize1D(uint32_t width);

	// Optional, default: swapchain image size 2D image
	void CustomizeSize2D(uint32_t width, uint32_t height);

	// Optional, default: swapchain image size 2D image
	void CustomizeSize3D(uint32_t width, uint32_t height, uint32_t depth);

	// Optional, default: 1
	void CustomizeMipLevels(uint32_t mipLevelCount);

	// Optional, default: 1
	void CustomizeArrayLayers(uint32_t layerCount);

	// Optional, default: VK_FORMAT_R32G32B32A32_SFLOAT
	void CustomizeFormat(VkFormat format);

	// Optional, default: VK_IMAGE_TILING_OPTIMAL
	void CustomizeImageTiling(VkImageTiling tiling);

	// Optional, default: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	void CustomizeMemoryProperty(VkMemoryPropertyFlags memoryProperty);

	// Optional, default: VK_SAMPLE_COUNT_1_BIT
	void CustomizeSampleCount(VkSampleCountFlagBits sampleCount);
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
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 0;
		uint32_t baseMipLevel = 0;
		uint32_t levelCount = 0;
		uint32_t baseArrayLayer = 0;
		uint32_t layerCount = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags aspectMask = 0;
		VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImage vkImage = VK_NULL_HANDLE;
	};

private:
	Information m_viewInformation{};
	IntrusivePtr<VulkanDetail::ImageViewResource> m_resource;

private:
	void _Create(const Image& inImage, const ImageViewInfo& inCreateInfo);

	auto _GetResourceReference() const -> VulkanDetail::ResourceReference;

public:
	ImageView() = default;
	ImageView(const ImageView&) noexcept = default;
	ImageView(ImageView&&) noexcept = default;
	auto operator=(const ImageView&) noexcept -> ImageView& = default;
	auto operator=(ImageView&&) noexcept -> ImageView& = default;

	auto GetImageViewInformation() const -> const Information&;

	auto GetImageSubresourceRange() const -> VkImageSubresourceRange;

	auto GetVkImageView() const noexcept -> VkImageView;
	auto GetVkImage() const noexcept -> VkImage;
	auto GetViewSize() const -> VkExtent3D;
	auto IsValid() const noexcept -> bool;
	void Reset() noexcept;
	auto operator==(const ImageView& inOther) const noexcept -> bool;

	friend class Image;
	friend class RenderPass;
	friend class DynamicDescriptorSetAllocator;
	friend class Framebuffer;
	friend class CommandBuffer;
	friend class RenderGraphInstance;
	friend struct ResourceLifetimeTestProbe;
};

class Image : public RefCounted
{
	friend struct CommandBufferTestProbe;
	friend struct ResourceLifetimeTestProbe;
	friend class ImageView;
	friend class CommandBuffer;
	friend class RenderGraphInstance;
	friend class MyDevice;
public:
	struct Information
	{
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 0;
		uint32_t mipLevels = 0;
		uint32_t arrayLayers = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageType imageType = VK_IMAGE_TYPE_2D;
		VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkMemoryPropertyFlags memoryProperty = 0;			  // image memory
		bool isSwapchainImage = false;
	};

private:
	Information m_imageInformation{};
	IntrusivePtr<VulkanDetail::ImageResource> m_resource;
	mutable std::vector<ImageView> m_imageViews;

private:
	auto _FindView(const ImageViewInfo& inCreateInfo) const -> const ImageView*;
	auto _GetResourceReference() const -> VulkanDetail::ResourceReference;
	void _ImportSwapchain(VkImage inImage, VkImageUsageFlags inUsage, VkFormat inFormat,
		VkExtent2D inExtent, const IntrusivePtr<VulkanDetail::SwapchainResource>& inOwner);

public:
	Image() = default;
	Image(const Image&) = delete;
	Image(Image&&) = delete;
	auto operator=(const Image&) -> Image& = delete;
	auto operator=(Image&&) -> Image& = delete;
	virtual ~Image();

	void Create(const ImageCreateInfo& inCreateInfo);

	void Destroy();
	void ClearViewCache();

	auto View(const ImageViewInfo& inCreateInfo = {}) const -> ImageView;

	auto GetImageInformation() const -> const Image::Information&;

	auto GetImageSize() const -> VkExtent3D;

	auto GetVkImage() const -> VkImage;
	auto IsValid() const noexcept -> bool;
};

class SwapchainImage final : public Image
{
	friend class MyDevice;
	friend class DeviceContext;

private:
	uint32_t m_index;
	// Borrowed from the acquire call; the swapchain image does not own it.
	VkSemaphore m_acquireSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	bool m_acquireWaitSubmitted = false;

	void _SetAcquireSemaphore(VkSemaphore inSemaphore);
	void _CreateRenderFinishedSemaphore();
	void _CommitAcquireWait();
	void _MarkPresented();
	explicit SwapchainImage(uint32_t inIndex);

public:
	~SwapchainImage();

	auto GetIndex() const -> uint32_t;
	auto GetAcquireSemaphore() const -> VkSemaphore;
	auto GetRenderFinishedSemaphore() const -> VkSemaphore;
};
