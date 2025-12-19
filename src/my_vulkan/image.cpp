#include "image.h"
#include "device.h"
#include "buffer.h"
#include "commandbuffer.h"
#include "memory_allocator.h"
//#ifndef STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
//#endif defined in tinyglTF
#include "stb_image.h"

void Image::_InitAsSwapchainImage(VkImage _vkImage, VkImageUsageFlags _usage, VkFormat _format)
{
	CreateInformation createInfo{};

	createInfo.usage = _usage;
	createInfo.optFormat = _format;

	PresetCreateInformation(createInfo);

	m_imageInformation.isSwapchainImage = true;
	vkImage = _vkImage;
	Init();
}

Image::~Image()
{
	if (!m_initCalled) return;
	assert(vkImage == VK_NULL_HANDLE);
	//assert(vkDeviceMemory == VK_NULL_HANDLE);
}

void Image::PresetCreateInformation(const CreateInformation& imageInfo)
{
	CHECK_TRUE(vkImage == VK_NULL_HANDLE, "Image is already initialized!");
	bool useSwapchainSize = !imageInfo.optWidth.has_value();

	if (useSwapchainSize)
	{
		m_imageInformation.width = MyDevice::GetInstance().GetSwapchainExtent().width;
		m_imageInformation.height = MyDevice::GetInstance().GetSwapchainExtent().height;
		m_imageInformation.depth = 1;
		m_imageInformation.imageType = VK_IMAGE_TYPE_2D;
	}
	else
	{
		m_imageInformation.width =  imageInfo.optWidth.value();
		m_imageInformation.height = imageInfo.optHeight.has_value() ? imageInfo.optHeight.value() : 1;
		m_imageInformation.depth = imageInfo.optDepth.has_value() ? imageInfo.optDepth.value() : 1;
		if (imageInfo.optDepth.has_value())
		{
			m_imageInformation.imageType = VK_IMAGE_TYPE_3D;
		}
		else if (imageInfo.optHeight.has_value())
		{
			m_imageInformation.imageType = VK_IMAGE_TYPE_2D;
		}
		else
		{
			m_imageInformation.imageType = VK_IMAGE_TYPE_1D;
		}
	}
	m_imageInformation.mipLevels = imageInfo.optMipLevels.has_value() ? imageInfo.optMipLevels.value() : 1;
	m_imageInformation.arrayLayers = imageInfo.optArrayLayers.has_value() ? imageInfo.optArrayLayers.value() : 1;
	m_imageInformation.format = imageInfo.optFormat.has_value() ? imageInfo.optFormat.value() : VK_FORMAT_R32G32B32A32_SFLOAT;
	m_imageInformation.tiling = imageInfo.optTiling.has_value() ? imageInfo.optTiling.value() : VK_IMAGE_TILING_OPTIMAL;
	m_imageInformation.usage = imageInfo.usage;
	m_imageInformation.samples = imageInfo.optSampleCount.has_value() ? imageInfo.optSampleCount.value() : VK_SAMPLE_COUNT_1_BIT;
	m_imageInformation.memoryProperty = imageInfo.optMemoryProperty.has_value() ? imageInfo.optMemoryProperty.value() : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	m_imageInformation.layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void Image::Init()
{
	if (m_initCalled) return;
	m_initCalled = true;
	if (!m_imageInformation.isSwapchainImage)
	{
		if (vkImage != VK_NULL_HANDLE) return;
		VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imgInfo.imageType = m_imageInformation.imageType;
		imgInfo.extent.width = m_imageInformation.width;
		imgInfo.extent.height = m_imageInformation.height;
		imgInfo.extent.depth = m_imageInformation.depth;
		imgInfo.mipLevels = m_imageInformation.mipLevels;
		imgInfo.arrayLayers = m_imageInformation.arrayLayers;
		imgInfo.format = m_imageInformation.format;
		imgInfo.tiling = m_imageInformation.tiling;
		//CHECK_TRUE(m_imageInformation.initialLayout == VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED || m_imageInformation.initialLayout == VkImageLayout::VK_IMAGE_LAYOUT_PREINITIALIZED,
		//	"According to the Vulkan specification, VkImageCreateInfo::initialLayout must be set to VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED at image creation.");
		imgInfo.initialLayout = m_imageInformation.layout;
		imgInfo.usage = m_imageInformation.usage;
		imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imgInfo.samples = m_imageInformation.samples;
		imgInfo.flags = 0;
		CHECK_TRUE(vkImage == VK_NULL_HANDLE, "VkImage is already created!");
		VK_CHECK(vkCreateImage(MyDevice::GetInstance().vkDevice, &imgInfo, nullptr, &vkImage), "Failed to crate image!");
		_AddImageLayout();
		_AllocateMemory();
	}
	else
	{
		_AddImageLayout();
	}
}

void Image::Uninit()
{
	if (!m_initCalled) return;
	m_initCalled = false;
	if (m_imageInformation.isSwapchainImage)
	{
		if (vkImage != VK_NULL_HANDLE)
		{
			_RemoveImageLayout();
			vkImage = VK_NULL_HANDLE;
		}
	}
	else
	{
		if (vkImage != VK_NULL_HANDLE)
		{
			_RemoveImageLayout();
			_FreeMemory();
			vkDestroyImage(MyDevice::GetInstance().vkDevice, vkImage, nullptr);
			vkImage = VK_NULL_HANDLE;
		}
	}
}

void Image::_AllocateMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->AllocateForVkImage(vkImage, m_imageInformation.memoryProperty);
}

void Image::_FreeMemory()
{
	MemoryAllocator* pAllocator = _GetMemoryAllocator();
	pAllocator->FreeMemory(vkImage);
}

void Image::_AddImageLayout() const
{
	MyDevice& device = MyDevice::GetInstance();
	ImageLayout layout{};
	layout.Reset(m_imageInformation.arrayLayers, m_imageInformation.mipLevels, m_imageInformation.layout);
	device.imageLayouts.insert({ vkImage, layout });
}

void Image::_RemoveImageLayout() const
{
	MyDevice& device = MyDevice::GetInstance();
	device.imageLayouts.erase(vkImage);
}

VkImageLayout Image::_GetImageLayout() const
{
	MyDevice& device = MyDevice::GetInstance();
	auto itr = device.imageLayouts.find(vkImage);
	CHECK_TRUE(itr != device.imageLayouts.end(), "Layout is not recorded!");
	return itr->second.GetLayout(0, m_imageInformation.arrayLayers, 0, m_imageInformation.mipLevels);
}

MemoryAllocator* Image::_GetMemoryAllocator() const
{
	return MyDevice::GetInstance().GetMemoryAllocator();
}

void Image::_NewImageView(
	ImageView& _pOutputImageView, 
	VkImageAspectFlags _aspect, 
	uint32_t _baseMipLevel, 
	uint32_t _levelCount, 
	uint32_t _baseArrayLayer, 
	uint32_t _layerCount) const
{
	ImageView::Information& viewInfo = _pOutputImageView.m_viewInformation;

	CHECK_TRUE(vkImage != VK_NULL_HANDLE, "Image is not initialized!");

	viewInfo.aspectMask = _aspect;
	viewInfo.baseArrayLayer = _baseArrayLayer;
	viewInfo.baseMipLevel = _baseMipLevel;
	viewInfo.format = m_imageInformation.format;
	viewInfo.layerCount = _layerCount;
	viewInfo.levelCount = _levelCount;
	viewInfo.vkImage = vkImage;

	if (_levelCount == VK_REMAINING_MIP_LEVELS)
	{
		CHECK_TRUE(m_imageInformation.mipLevels > _baseMipLevel, "Wrong base mip level!");
		viewInfo.levelCount = m_imageInformation.mipLevels - _baseMipLevel;
	}
	if (_layerCount == VK_REMAINING_ARRAY_LAYERS)
	{
		CHECK_TRUE(m_imageInformation.arrayLayers > _baseArrayLayer, "Wrong base array layer!");
		viewInfo.layerCount = m_imageInformation.arrayLayers - _baseArrayLayer;
	}
	CHECK_TRUE((viewInfo.baseArrayLayer + viewInfo.layerCount) <= m_imageInformation.arrayLayers, "Image doesn't have these layers!");
	CHECK_TRUE((viewInfo.baseMipLevel + viewInfo.levelCount) <= m_imageInformation.mipLevels, "Image doesn't have these mipmap levels!");

	_pOutputImageView.pImage = this;

	if (viewInfo.layerCount > 1)
	{
		switch (m_imageInformation.imageType)
		{
		case VK_IMAGE_TYPE_1D:
			viewInfo.type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			break;
		case VK_IMAGE_TYPE_2D:
			viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case VK_IMAGE_TYPE_3D:
			CHECK_TRUE(false, "No 3D image array!");
			break;
		default:
			CHECK_TRUE(false, "Unhandled image type!");
			break;
		}
	}
	else
	{
		switch (m_imageInformation.imageType)
		{
		case VK_IMAGE_TYPE_1D:
			viewInfo.type = VK_IMAGE_VIEW_TYPE_1D;
			break;
		case VK_IMAGE_TYPE_2D:
			viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
			break;
		case VK_IMAGE_TYPE_3D:
			viewInfo.type = VK_IMAGE_VIEW_TYPE_3D;
			break;
		default:
			CHECK_TRUE(false, "Unhandled image type!");
			break;
		}
	}
}

void Image::TransitLayout(VkImageLayout newLayout)
{
	VkImageLayout oldLayout = _GetImageLayout();
	if (oldLayout == newLayout) return;
	CommandSubmission cmdSubmit;
	ImageBarrierBuilder barrierBuilder{};
	VkImageMemoryBarrier barrier{};
	VkAccessFlags aspect = VK_IMAGE_ASPECT_NONE;

	cmdSubmit.Init();
	cmdSubmit.StartOneTimeCommands({});

	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) 
	{
		bool hasStencilComponent = 
			(m_imageInformation.format == VK_FORMAT_D32_SFLOAT_S8_UINT 
				|| m_imageInformation.format == VK_FORMAT_D24_UNORM_S8_UINT);
		if (hasStencilComponent) 
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		else
		{
			aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		}
	}
	else
	{
		aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	barrierBuilder.SetAspect(aspect);
	barrier = barrierBuilder.NewBarrier(vkImage, oldLayout, newLayout, VK_ACCESS_NONE, VK_ACCESS_NONE);

	// one time submit command will wait to be done anyway
	cmdSubmit.AddPipelineBarrier(
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ barrier }
	);

	cmdSubmit.SubmitCommands();
}

void Image::CopyFromBuffer(const Buffer& stagingBuffer)
{
	CHECK_TRUE(vkImage != VK_NULL_HANDLE, "Image is not initialized!");

	CommandSubmission cmdSubmit;
	
	cmdSubmit.Init();

	cmdSubmit.StartOneTimeCommands({});

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = 0;
	region.imageOffset.y = 0;
	region.imageOffset.z = 0;
	region.imageExtent.width = m_imageInformation.width;
	region.imageExtent.height = m_imageInformation.height;
	region.imageExtent.depth = 1;

	cmdSubmit.CopyBufferToImage(
		stagingBuffer.vkBuffer,
		vkImage,
		_GetImageLayout(),
		{ region });

	cmdSubmit.SubmitCommands();
}

void Image::Fill(const VkClearColorValue& clearColor, const VkImageSubresourceRange& range, CommandSubmission* pCmd)
{
	if (pCmd == nullptr)
	{
		CommandSubmission cmd{};
		cmd.Init();
		cmd.StartOneTimeCommands({});
		cmd.ClearColorImage(vkImage, _GetImageLayout(), clearColor, { range });
		cmd.SubmitCommands();
		cmd.Uninit();
	}
	else
	{
		pCmd->ClearColorImage(vkImage, _GetImageLayout(), clearColor, { range });
	}
}

void Image::Fill(const VkClearColorValue& clearColor, CommandSubmission* pCmd)
{
	std::unique_ptr<VkImageSubresourceRange> uptrRange = std::make_unique<VkImageSubresourceRange>(VkImageSubresourceRange{});

	uptrRange->aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	uptrRange->baseArrayLayer = 0u;
	uptrRange->baseMipLevel = 0u;
	uptrRange->layerCount = VK_REMAINING_ARRAY_LAYERS;
	uptrRange->levelCount = VK_REMAINING_MIP_LEVELS;

	Fill(clearColor, *uptrRange, pCmd);
}

void Image::ChangeLayoutAndFill(VkImageLayout finalLayout, const VkClearColorValue& clearColor, CommandSubmission* pCmd)
{
	ImageBarrierBuilder barrierBuilder{};
	VkImageMemoryBarrier barrier{};
	VkImageSubresourceRange range{};

	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.baseArrayLayer = 0u;
	range.baseMipLevel = 0u;
	range.layerCount = VK_REMAINING_ARRAY_LAYERS;
	range.levelCount = VK_REMAINING_MIP_LEVELS;

	barrierBuilder.SetArrayLayerRange(range.baseArrayLayer, range.layerCount);
	barrierBuilder.SetAspect(range.aspectMask);
	barrierBuilder.SetMipLevelRange(range.baseMipLevel, range.levelCount);
	barrier = barrierBuilder.NewBarrier(vkImage, _GetImageLayout(), finalLayout, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT);

	if (pCmd == nullptr)
	{
		CommandSubmission cmd{};
		cmd.Init();
		cmd.StartOneTimeCommands({});
		cmd.AddPipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { barrier });
		cmd.ClearColorImage(vkImage, finalLayout, clearColor, { range });
		cmd.SubmitCommands();
		cmd.Uninit();
	}
	else
	{
		pCmd->AddPipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { barrier });
		pCmd->ClearColorImage(vkImage, finalLayout, clearColor, { range });
	}
}

ImageView Image::NewImageView(VkImageAspectFlags aspect, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount) const
{
	ImageView val{};
	
	_NewImageView(val, aspect, baseMipLevel, levelCount, baseArrayLayer, layerCount);

	return val;
}

void Image::NewImageView(
	ImageView*& _pOutputImageView, 
	VkImageAspectFlags _aspect, 
	uint32_t _baseMipLevel, 
	uint32_t _levelCount, 
	uint32_t _baseArrayLayer, 
	uint32_t _layerCount) const
{
	_pOutputImageView = new ImageView();

	_NewImageView(*_pOutputImageView, _aspect, _baseMipLevel, _levelCount, _baseArrayLayer, _layerCount);
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

ImageView::~ImageView()
{
	assert(vkImageView == VK_NULL_HANDLE);
}

void ImageView::Init()
{
	CHECK_TRUE(pImage != nullptr, "No image!");
	CHECK_TRUE(m_viewInformation.layerCount != 0, "No layer count!");
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.pNext = nullptr;  // Ensure this is set explicitly if pNext isn't used.
	viewInfo.flags = 0;     // Set flags to 0 unless specific flags are required.
	viewInfo.image = pImage->vkImage;
	viewInfo.viewType = m_viewInformation.type;
	viewInfo.format = pImage->GetImageInformation().format;

	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;  // Default swizzle values if not specified.
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	viewInfo.subresourceRange.aspectMask = m_viewInformation.aspectMask;
	viewInfo.subresourceRange.baseMipLevel = m_viewInformation.baseMipLevel;
	viewInfo.subresourceRange.levelCount = m_viewInformation.levelCount;
	viewInfo.subresourceRange.baseArrayLayer = m_viewInformation.baseArrayLayer;
	viewInfo.subresourceRange.layerCount = m_viewInformation.layerCount;
	CHECK_TRUE(vkImageView == VK_NULL_HANDLE, "VkImageView is already created!");
	VK_CHECK(vkCreateImageView(MyDevice::GetInstance().vkDevice, &viewInfo, nullptr, &vkImageView), "Failed to create image view!");
}

void ImageView::Uninit()
{
	if (vkImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(MyDevice::GetInstance().vkDevice, vkImageView, nullptr);
		vkImageView = VK_NULL_HANDLE;
	}
	pImage = nullptr;
}

const ImageView::Information& ImageView::GetImageViewInformation() const
{
	return m_viewInformation;
}

VkImageSubresourceRange ImageView::GetRange() const
{
	VkImageSubresourceRange subRange{};
	subRange.aspectMask = m_viewInformation.aspectMask;
	subRange.baseMipLevel = m_viewInformation.baseMipLevel;
	subRange.levelCount = m_viewInformation.levelCount;
	subRange.baseArrayLayer = m_viewInformation.baseArrayLayer;
	subRange.layerCount = m_viewInformation.layerCount;

	return subRange;
}

VkDescriptorImageInfo ImageView::GetDescriptorInfo(VkSampler _sampler, VkImageLayout _layout) const
{
	VkDescriptorImageInfo info{};

	info.imageLayout = _layout;
	info.imageView = vkImageView;
	info.sampler = _sampler;

	return info;
}

void Texture::SetFilePath(std::string path)
{
	m_filePath = path;
}

Texture::~Texture()
{
	assert(vkSampler == VK_NULL_HANDLE);
}

void Texture::Init()
{
	CHECK_TRUE(!m_filePath.empty(), "No image file!");
	// load image
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(m_filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	CHECK_TRUE(pixels, "Failed to load texture image!");
	VkDeviceSize imageSize = texWidth * texHeight * 4;

	// copy host image to device
	Buffer stagingBuffer;
	Buffer::CreateInformation bufferInfo;
	bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBuffer.PresetCreateInformation(bufferInfo);
	stagingBuffer.Init();
	stagingBuffer.CopyFromHost(pixels);

	stbi_image_free(pixels);

	Image::CreateInformation imageInfo;
	imageInfo.optWidth = static_cast<uint32_t>(texWidth);
	imageInfo.optHeight = static_cast<uint32_t>(texHeight);
	imageInfo.optFormat = VK_FORMAT_R8G8B8A8_SRGB;
	imageInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image.PresetCreateInformation(imageInfo);
	image.Init();
	image.TransitLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	image.CopyFromBuffer(stagingBuffer);
	//image.TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	image.TransitLayout(VK_IMAGE_LAYOUT_GENERAL);

	stagingBuffer.Uninit();

	// create image view
	imageView = image.NewImageView();
	imageView.Init();

	// create sampler
	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(MyDevice::GetInstance().vkPhysicalDevice, &properties);
	// samplerInfo.anisotropyEnable = VK_TRUE;
	// samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;
	CHECK_TRUE(vkSampler == VK_NULL_HANDLE, "VkSampler is already created!");
	VK_CHECK(vkCreateSampler(MyDevice::GetInstance().vkDevice, &samplerInfo, nullptr, &vkSampler), "Failed to create texture sampler!");
}

void Texture::Uninit()
{
	vkDestroySampler(MyDevice::GetInstance().vkDevice, vkSampler, nullptr);
	vkSampler = VK_NULL_HANDLE;
	m_filePath = "";
	imageView.Uninit();
	image.Uninit();
}

VkDescriptorImageInfo Texture::GetVkDescriptorImageInfo() const
{
	VkDescriptorImageInfo info;
	info.imageLayout = image._GetImageLayout();
	info.sampler = vkSampler;
	info.imageView = imageView.vkImageView;
	return info;
}

bool ImageLayout::_IsInRange(uint32_t baseLayer, uint32_t& layerCount, uint32_t baseLevel, uint32_t& levelCount) const
{
	CHECK_TRUE(m_entries.size() > 0 && m_entries[0].size() > 0, "Image Layout doesn't have range!");
	uint32_t maxLayer = static_cast<uint32_t>(m_entries.size());
	uint32_t maxLevel = static_cast<uint32_t>(m_entries[0].size());
	if (layerCount == VK_REMAINING_ARRAY_LAYERS)
	{
		CHECK_TRUE(baseLayer < maxLayer, "Wrong base layer!");
		layerCount = maxLayer - baseLayer;
	}
	if (levelCount == VK_REMAINING_MIP_LEVELS)
	{
		CHECK_TRUE(baseLevel < maxLevel, "Wrong base level!");
		levelCount = maxLevel - baseLevel;
	}
	bool layerOk = (baseLayer + layerCount) <= m_entries.size();
	bool levelOK = (baseLevel + levelCount) <= m_entries[0].size();
	return layerOk && levelOK;
}
void ImageLayout::Reset(uint32_t layerCount, uint32_t levelCount, VkImageLayout layout)
{
	m_entries.resize(layerCount);
	for (auto& entry : m_entries)
	{
		entry.resize(levelCount);
	}
	for (int i = 0; i < layerCount; ++i)
	{
		for (int j = 0; j < levelCount; ++j)
		{
			m_entries[i][j].SetLayout(layout, VK_IMAGE_ASPECT_FLAG_BITS_MAX_ENUM);
		}
	}
}
VkImageLayout ImageLayout::GetLayout(VkImageSubresourceRange range) const
{
	return GetLayout(range.baseArrayLayer, range.layerCount, range.baseMipLevel, range.levelCount, range.aspectMask);
}
VkImageLayout ImageLayout::GetLayout(uint32_t baseLayer, uint32_t layerCount, uint32_t baseLevel, uint32_t levelCount, VkImageAspectFlags aspect) const
{
	VkImageLayout ret = VK_IMAGE_LAYOUT_UNDEFINED;
	bool found = false;
	CHECK_TRUE(_IsInRange(baseLayer, layerCount, baseLevel, levelCount), "Layout out of range");
	for (int i = baseLayer; i < baseLayer + layerCount; ++i)
	{
		for (int j = baseLevel; j < baseLevel + levelCount; ++j)
		{
			if (!found)
			{
				ret = m_entries[i][j].GetLayout(aspect);
				found = true;
			}
			else
			{
				CHECK_TRUE(ret == m_entries[i][j].GetLayout(aspect), "Subresource doesn't share the same layout!");
			}
		}
	}
	return ret;
}
void ImageLayout::SetLayout(VkImageLayout layout, VkImageSubresourceRange range)
{
	SetLayout(layout, range.baseArrayLayer, range.layerCount, range.baseMipLevel, range.levelCount, range.aspectMask);
}
void ImageLayout::SetLayout(VkImageLayout layout, uint32_t baseLayer, uint32_t layerCount, uint32_t baseLevel, uint32_t levelCount, VkImageAspectFlags aspect)
{
	CHECK_TRUE(_IsInRange(baseLayer, layerCount, baseLevel, levelCount), "Layout out of range");
	for (int i = baseLayer; i < baseLayer + layerCount; ++i)
	{
		for (int j = baseLevel; j < baseLevel + levelCount; ++j)
		{
			m_entries[i][j].SetLayout(layout, aspect);
		}
	}
}

VkImageLayout ImageLayout::ImageLayoutEntry::GetLayout(VkImageAspectFlags aspect) const
{
	VkImageLayout ret = VK_IMAGE_LAYOUT_UNDEFINED;
	bool found = false;
	for (const auto& p : m_layouts)
	{
		if (aspect & p.first)
		{
			if (!found)
			{
				ret = p.second;
				found = true;
			}
			else
			{
				CHECK_TRUE(ret == p.second, "Not the same layout");
			}
		}
	}
	return ret;
}
void ImageLayout::ImageLayoutEntry::SetLayout(VkImageLayout layout, VkImageAspectFlags aspect)
{
	for (auto& p : m_layouts)
	{
		if (p.first & aspect)
		{
			p.second = layout;
		}
	}
}
