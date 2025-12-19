#pragma once
#include "common.h"

class Buffer;
class Image;
class Texture;
class CommandSubmission;
class MemoryAllocator;
class MyDevice;

class ImageView
{
public:
	struct Information
	{
		uint32_t baseMipLevel				= 0;
		uint32_t levelCount					= VK_REMAINING_MIP_LEVELS;
		uint32_t baseArrayLayer				= 0;
		uint32_t layerCount					= VK_REMAINING_ARRAY_LAYERS;
		VkFormat format						= VK_FORMAT_UNDEFINED; // https://stackoverflow.com/questions/58600143/why-would-vkimageview-format-differ-from-the-underlying-vkimage-format
		VkImageAspectFlags aspectMask;
		VkImageViewType type;
		VkImage vkImage						= VK_NULL_HANDLE;
	};

private:
	Information m_viewInformation{};

public:
	const Image* pImage = nullptr;
	VkImageView vkImageView = VK_NULL_HANDLE;
	
public:
	~ImageView();
	
	void Init();

	void Uninit();
	
	const Information& GetImageViewInformation() const;
	
	VkImageSubresourceRange GetRange() const;

	VkDescriptorImageInfo GetDescriptorInfo(VkSampler _sampler, VkImageLayout _layout) const;

	friend class Image;
};

class ImageLayout
{
// commands that will change image layout:
//	vkCreateRenderPass vkMapMemory vkQueuePresentKHR vkQueueSubmit vkCmdCopyImage vkCmdCopyImageToBuffer vkCmdWaitEvents VkCmdPipelineBarrier
private:
	class ImageLayoutEntry
	{
	private:
		std::vector<std::pair<VkImageAspectFlagBits, VkImageLayout>> m_layouts{
			{VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
			{VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
			{VK_IMAGE_ASPECT_STENCIL_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
		};
	public:
		VkImageLayout GetLayout(VkImageAspectFlags aspect) const;
		void          SetLayout(VkImageLayout layout, VkImageAspectFlags aspect);
	};
	std::vector<std::vector<ImageLayoutEntry>> m_entries;
	bool _IsInRange(uint32_t baseLayer, uint32_t& layerCount, uint32_t baseLevel, uint32_t& levelCount) const;
public:
	void Reset(uint32_t layerCount, uint32_t levelCount, VkImageLayout layout);

	VkImageLayout GetLayout(VkImageSubresourceRange range) const;
	VkImageLayout GetLayout(
		uint32_t baseLayer, 
		uint32_t layerCount, 
		uint32_t baseLevel, 
		uint32_t levelCount, 
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) const;

	void SetLayout(VkImageLayout layout, VkImageSubresourceRange range);
	void SetLayout(VkImageLayout layout,
		uint32_t baseLayer, uint32_t layerCount,
		uint32_t baseLevel, uint32_t levelCount,
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
};

class Image
{
public:
	struct CreateInformation
	{
		VkImageUsageFlags usage = 0;

		std::optional<uint32_t> optWidth;					// optional, if unset image will be swapchain size
		std::optional<uint32_t> optHeight;				// optional, don't set it if image isn't 2D
		std::optional<uint32_t> optDepth;					// optional, don't set it if image isn't 3D
		std::optional<uint32_t> optMipLevels;			// optional, default value: 1
		std::optional<uint32_t> optArrayLayers;			// optional, default value: 1
		std::optional<VkFormat> optFormat;				// optional, default value: VK_FORMAT_R32G32B32A32_SFLOAT;
		std::optional<VkImageTiling> optTiling;			// optional, default value: VK_IMAGE_TILING_OPTIMAL;
		std::optional<VkMemoryPropertyFlags> optMemoryProperty; // optional, default value: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		std::optional<VkSampleCountFlagBits> optSampleCount;		// optional, default value: VK_SAMPLE_COUNT_1_BIT;
	};
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
		VkMemoryPropertyFlags memoryProperty;						// image memory
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // transfer layout
		bool isSwapchainImage = false;
	};

private:
	bool m_initCalled = false;
	Information m_imageInformation{};

public:
	VkImage vkImage = VK_NULL_HANDLE;

private:
	void _AllocateMemory();
	
	void _FreeMemory();
	
	void _AddImageLayout() const;
	
	void _RemoveImageLayout() const;
	
	VkImageLayout _GetImageLayout() const;
	
	MemoryAllocator* _GetMemoryAllocator() const;

	void _NewImageView(
		ImageView& _pOutputImageView,
		VkImageAspectFlags _aspect,
		uint32_t _baseMipLevel,
		uint32_t _levelCount,
		uint32_t _baseArrayLayer,
		uint32_t _layerCount) const;

	void _InitAsSwapchainImage(VkImage _vkImage, VkImageUsageFlags _usage, VkFormat _format);

public:
	~Image();

	void PresetCreateInformation(const CreateInformation& imageInfo);

	void Init();

	void Uninit();
	
	void TransitLayout(VkImageLayout newLayout);

	void CopyFromBuffer(const Buffer& stagingBuffer);

	// Fill image range with clear color,
	// if pCmd is nullptr, it will create a command buffer and wait till this action done,
	// else, it will record the command in the command buffer, and user need to manage the synchronization
	void Fill(
		const VkClearColorValue& clearColor,
		const VkImageSubresourceRange& range,
		CommandSubmission* pCmd = nullptr);
	
	// Fill image range with clear color,
	// if pCmd is nullptr, it will create a command buffer and wait till this action done,
	// else, it will record the command in the command buffer, and user need to manage the synchronization
	void Fill(
		const VkClearColorValue& clearColor,
		CommandSubmission* pCmd = nullptr);

	// Change image's layout and then fill it with clear color,
	// if pCmd is nullptr, it will create a command buffer and wait till this action done,
	// else, it will record the command in the command buffer, and user need to manage the synchronization
	void ChangeLayoutAndFill(
		VkImageLayout finalLayout,
		const VkClearColorValue& clearColor,
		CommandSubmission* pCmd = nullptr);

	// Return a image view of this image, 
	// the view returned is NOT initialized yet
	ImageView NewImageView(
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
		uint32_t baseMipLevel = 0,
		uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
		uint32_t baseArrayLayer = 0,
		uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS) const;

	// New an uninitialized image view pointer of this image.
	// _pOutputImageView: return pointer, allocate memory inside, user needs to delete it
	void NewImageView(
		ImageView*& _pOutputImageView,
		VkImageAspectFlags _aspect = VK_IMAGE_ASPECT_COLOR_BIT,
		uint32_t _baseMipLevel = 0,
		uint32_t _levelCount = VK_REMAINING_MIP_LEVELS,
		uint32_t _baseArrayLayer = 0,
		uint32_t _layerCount = VK_REMAINING_ARRAY_LAYERS) const;

	const Information& GetImageInformation() const;

	VkExtent3D GetImageSize() const;

	friend class Texture;
	friend class MyDevice;
};

class Texture
{
private:
	std::string m_filePath;
public:
	Image     image{};
	ImageView imageView{};
	VkSampler vkSampler = VK_NULL_HANDLE;
	~Texture();
	void SetFilePath(std::string path);
	void Init();
	void Uninit();
	VkDescriptorImageInfo GetVkDescriptorImageInfo() const;
};
