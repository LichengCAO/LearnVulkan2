#include "device.h"
#include <iostream>
#include <cstdint>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <utility>
#include <type_traits>
#include "image.h"
#include "resource/descriptor.h"
#include "pipeline/pipeline_layout.h"
#include "pipeline/compute_shader_program.h"
#include "pipeline/graphics_shader_program.h"
#include "pipeline/ray_tracing_shader_program.h"
#include "allocator/private/memory_allocator.h"
#include "allocator/private/descriptor_set_layout_allocator.h"
#include "allocator/private/render_target_cache.h"
#include "pipeline/private/render_target_description.h"
#include "allocator/private/pipeline_layout_allocator.h"
#include "allocator/private/pipeline_allocator.h"
#include "allocator/private/sampler_allocator.h"
#include "allocator/private/shader_module_allocator.h"
#include <iomanip>
#define VOLK_IMPLEMENTATION
#include <volk.h>

std::unique_ptr<MyDevice> MyDevice::s_uptrInstance = nullptr;

MyDevice::MyDevice()
{
}

MyDevice::~MyDevice()
{
	if (m_initialized) Destroy();
}

std::vector<const char*> MyDevice::_GetInstanceRequiredExtensions() const
{
	uint32_t glfwExtensionsCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
	std::vector<const char*> requiredExtensions;

	for (uint32_t i = 0; i < glfwExtensionsCount; ++i) {
		requiredExtensions.emplace_back(glfwExtensions[i]);
	}
	requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	requiredExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	/*if (enableValidationLayer)*/ requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	return requiredExtensions;
}

MyDevice::QueueFamilyIndices MyDevice::_GetQueueFamilyIndices(VkPhysicalDevice physicalDevice) const
{
	QueueFamilyIndices indices;
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}
		if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
			indices.graphicsAndComputeFamily = i;
		}
		VkBool32 supportPresent = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, vkSurface, &supportPresent);
		if (supportPresent) {
			indices.presentFamily = i;
		}
		if (!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT))
		{
			// The good news is that any queue family with VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT capabilities already implicitly support VK_QUEUE_TRANSFER_BIT operations.
			indices.transferFamily = i;
		}
		if (indices.isComplete())break;
		++i;
	}
	return indices;
}

MyDevice::SwapChainSupportDetails MyDevice::_QuerySwapchainSupport(VkPhysicalDevice phyiscalDevice, VkSurfaceKHR surface) const
{
	SwapChainSupportDetails res;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyiscalDevice, surface, &res.capabilities);

	uint32_t formatCnt = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(phyiscalDevice, surface, &formatCnt, nullptr);
	if (formatCnt != 0) {
		res.formats.resize(formatCnt);
		vkGetPhysicalDeviceSurfaceFormatsKHR(phyiscalDevice, surface, &formatCnt, res.formats.data());
	}

	uint32_t modeCnt = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(phyiscalDevice, surface, &modeCnt, nullptr);
	if (modeCnt != 0) {
		res.presentModes.resize(modeCnt);
		vkGetPhysicalDeviceSurfacePresentModesKHR(phyiscalDevice, surface, &modeCnt, res.presentModes.data());
	}
	return res;
}

VkSurfaceFormatKHR MyDevice::_ChooseSwapchainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const
{
	for (const auto& availableFormat : availableFormats) 
	{
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
		{
			return availableFormat;
		}
	}
	return availableFormats[0];
}

VkPresentModeKHR MyDevice::_ChooseSwapchainPresentMode(const std::vector<VkPresentModeKHR>& availableModes) const
{
	for (const auto& availableMode : availableModes) 
	{
		if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR) 
		{
			return availableMode;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D MyDevice::_ChooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) 
	{
		return capabilities.currentExtent;
	}
	int width, height;
	glfwGetFramebufferSize(pWindow, &width, &height);
	VkExtent2D res{
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height)
	};

	res.width = std::clamp(res.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	res.height = std::clamp(res.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	return res;
}

void MyDevice::_InitVolk()
{
	VK_CHECK(volkInitialize(), "Failed to initialize volk!");
}

void MyDevice::_InitGLFW()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	pWindow = glfwCreateWindow(800, 600, "My Vulkan", nullptr, nullptr);
	glfwSetWindowUserPointer(pWindow, this);
	glfwSetFramebufferSizeCallback(pWindow, OnFramebufferResized);
	glfwSetCursorPosCallback(pWindow, CursorPosCallBack);
}

void MyDevice::_CreateInstance()
{
	vkb::InstanceBuilder instanceBuilder;
	instanceBuilder.enable_extensions(_GetInstanceRequiredExtensions());
	instanceBuilder.request_validation_layers(true);
	instanceBuilder.set_app_name("Hello Triangle");
	instanceBuilder.set_app_version(VK_MAKE_VERSION(1, 0, 0));
	instanceBuilder.set_engine_name("No Engine");
	instanceBuilder.set_engine_version(VK_MAKE_VERSION(1, 0, 0));
	//instanceBuilder.desire_api_version(VK_API_VERSION_1_2);
	instanceBuilder.require_api_version(VK_API_VERSION_1_3);
	instanceBuilder.set_debug_callback(
		[](
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) -> VkBool32
		{
			if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				std::cout << "message based on severity" << std::endl;
				std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
			}
			return VK_FALSE;
		}
	);

	auto instanceBuildResult = instanceBuilder.build();
	if (!instanceBuildResult)
	{
		throw std::runtime_error("Failed to create instance!");
	}
	
	m_instance = instanceBuildResult.value();
	vkInstance = m_instance.instance;
	volkLoadInstance(vkInstance);
}

void MyDevice::_CreateSurface()
{
	VK_CHECK(glfwCreateWindowSurface(vkInstance, pWindow, nullptr, &vkSurface), "Failed to create window surface!");
}

void MyDevice::_SelectPhysicalDevice()
{
	vkb::PhysicalDeviceSelector physicalDeviceSelector(m_instance);

	physicalDeviceSelector.set_surface(vkSurface);
	_AddBaseExtensionsAndFeatures(physicalDeviceSelector);
	_AddMeshShaderExtensionsAndFeatures(physicalDeviceSelector);
	_AddRayTracingExtensionsAndFeatures(physicalDeviceSelector);
	_AddRayQueryExtensionsAndFeatures(physicalDeviceSelector);
	_AddBindlessExtensionsAndFeatures(physicalDeviceSelector);

	// select device based on setting
	{
		auto physicalDeviceSelectorReturn = physicalDeviceSelector.select();
		if (!physicalDeviceSelectorReturn)
		{
			std::cout << physicalDeviceSelectorReturn.error().message() << std::endl;
			throw std::runtime_error("Failed to find a suitable GPU!");
		}
		m_physicalDevice = physicalDeviceSelectorReturn.value();
		vkPhysicalDevice = m_physicalDevice.physical_device;
	}
	m_cachedRayTracingProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	properties.pNext = &m_cachedRayTracingProperties;
	vkGetPhysicalDeviceProperties2(vkPhysicalDevice, &properties);
	m_cachedPhysicalDeviceProperties = properties.properties;
}

void MyDevice::_CreateLogicalDevice()
{
	vkb::DeviceBuilder deviceBuilder{ m_physicalDevice };

	std::vector<vkb::CustomQueueDescription> queueDescription;
	float priority = 1.0f;
	queueFamilyIndices = _GetQueueFamilyIndices(vkPhysicalDevice);
	if (!queueFamilyIndices.isComplete())
	{
		throw std::runtime_error("Failed to find all queue families");
	}
	std::unordered_set<uint32_t> uniqueQueueFamilies{ 
		queueFamilyIndices.graphicsFamily.value(), 
		queueFamilyIndices.presentFamily.value(), 
		queueFamilyIndices.graphicsAndComputeFamily.value(), 
		queueFamilyIndices.transferFamily.value() 
	};
	for (auto queueFamily : uniqueQueueFamilies) 
	{
		queueDescription.emplace_back(vkb::CustomQueueDescription{ queueFamily, {priority} });
	}
	deviceBuilder.custom_queue_setup(queueDescription);
	
	auto deviceBuilderReturn = deviceBuilder.build();
	if (!deviceBuilderReturn)
	{
		std::cout << deviceBuilderReturn.error().message() << std::endl;
		throw std::runtime_error("Failed to create logic device!");
	}
	
	m_device = deviceBuilderReturn.value();
	vkDevice = m_device.device;
	m_dispatchTable = m_device.make_table();
	volkLoadDevice(vkDevice);

	// store vkQueues
	{
		uint32_t queueFamilyIndex = queueFamilyIndices.presentFamily.value();
		vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &m_vkPresentQueue);

		queueFamilyIndex = GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
		vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &m_vkGraphicsQueue);

		queueFamilyIndex = GetQueueFamilyIndexOfType(QueueFamilyType::COMPUTE);
		vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &m_vkComputeQueue);

		queueFamilyIndex = GetQueueFamilyIndexOfType(QueueFamilyType::TRANSFER);
		vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &m_vkTransferQueue);
	}
}

void MyDevice::_CreateSwapchain(VkSwapchainKHR inOldSwapchain)
{
	SwapChainSupportDetails swapChainSupport = _QuerySwapchainSupport(vkPhysicalDevice, vkSurface);
	uint32_t imageCnt = swapChainSupport.capabilities.minImageCount + 1;
	VkSurfaceFormatKHR surfaceFormat = _ChooseSwapchainFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = _ChooseSwapchainPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = _ChooseSwapchainExtent(swapChainSupport.capabilities);
	vkb::SwapchainBuilder swapchainBuilder{ m_device };
	swapchainBuilder.set_old_swapchain(inOldSwapchain);
	swapchainBuilder.set_desired_format(surfaceFormat);
	swapchainBuilder.set_desired_present_mode(presentMode);
	swapchainBuilder.set_composite_alpha_flags(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
	swapchainBuilder.set_desired_min_image_count(imageCnt);
	swapchainBuilder.set_desired_extent(extent.width, extent.height);
	swapchainBuilder.set_image_array_layer_count(1);
	auto swapchainBuilderReturn = swapchainBuilder.build();
	if (!swapchainBuilderReturn)
	{
		throw std::runtime_error(swapchainBuilderReturn.error().message());
	}
	m_swapchain = swapchainBuilderReturn.value();
	vkSwapchain = m_swapchain.swapchain;
	try
	{
		m_swapchainResource = MakeIntrusive<VulkanDetail::SwapchainResource>(vkSwapchain,
			[device = vkDevice](VkSwapchainKHR inSwapchain) { vkDestroySwapchainKHR(device, inSwapchain, nullptr); });
	}
	catch (...)
	{
		vkb::destroy_swapchain(m_swapchain);
		vkSwapchain = VK_NULL_HANDLE;
		throw;
	}
	m_needRecreate = false;
	_UpdateSwapchainImages();
}

void MyDevice::RecreateSwapchain()
{
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(pWindow, &width, &height);
	while (width == 0 || height == 0) 
	{
		glfwGetFramebufferSize(pWindow, &width, &height);
		glfwWaitEvents();
	}
	WaitIdle();
	const auto oldSwapchain = m_swapchainResource;
	_DestroySwapchain();
	_CreateSwapchain(oldSwapchain ? oldSwapchain->GetHandle() : VK_NULL_HANDLE);
}

auto MyDevice::GetNextAvailableSwapchainImage(VkSemaphore acquireSemaphore) -> SwapchainImage*
{
	CHECK_TRUE(acquireSemaphore != VK_NULL_HANDLE, "Swapchain acquire semaphore is invalid!");
	uint32_t imageIndex = 0;
	const VkResult result = vkAcquireNextImageKHR(
		vkDevice, vkSwapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_needRecreate = true;
		return nullptr;
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("Failed to acquire swap chain image!");
	}
	if (result == VK_SUBOPTIMAL_KHR)
	{
		m_needRecreate = true;
	}
	CHECK_TRUE(imageIndex < m_uptrSwapchainImages.size(), "Acquired swapchain image index is out of range!");
	SwapchainImage* image = m_uptrSwapchainImages[imageIndex].get();
	image->_SetAcquireSemaphore(acquireSemaphore);
	return image;
}

void MyDevice::PresentSwapchainImage(const SwapchainImage& image)
{
	const auto iter = std::find_if(m_uptrSwapchainImages.begin(), m_uptrSwapchainImages.end(),
		[&image](const std::unique_ptr<SwapchainImage>& current) { return current.get() == &image; });
	CHECK_TRUE(iter != m_uptrSwapchainImages.end(), "Presented image is not in the current swapchain!");
	CHECK_TRUE(image.m_acquireSemaphore != VK_NULL_HANDLE && image.m_acquireWaitSubmitted,
		"Swapchain acquire wait must be submitted before present!");
	const uint32_t imageIdx = image.GetIndex();
	const VkSemaphore renderFinished = image.GetRenderFinishedSemaphore();
	VkSwapchainKHR swapChains[] = { vkSwapchain };
	VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderFinished;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIdx;
	presentInfo.pResults = nullptr;

	VkResult result = vkQueuePresentKHR(m_vkPresentQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) 
	{
		m_needRecreate = true;
	}
	else if (result != VK_SUCCESS) 
	{
		throw std::runtime_error("Failed to present swapchain!");
	}
	const_cast<SwapchainImage&>(image)._MarkPresented();
}

bool MyDevice::NeedRecreateSwapchain() const
{
	return m_needRecreate;
}

bool MyDevice::NeedCloseWindow() const
{
    return glfwWindowShouldClose(pWindow);
}

void MyDevice::StartFrame() const
{
	glfwPollEvents();
}

void MyDevice::_DestroySwapchain()
{
	for (auto& uptrImage : m_uptrSwapchainImages)
	{
		if (uptrImage.get() != nullptr)
		{
			uptrImage.reset();
		}
	}
	m_uptrSwapchainImages.clear();
	m_swapchainResource.reset();
	vkSwapchain = VK_NULL_HANDLE;
	m_swapchain = {};
}

void MyDevice::_CreateMemoryAllocator()
{
	m_uptrMemoryAllocator = std::move(std::unique_ptr<MemoryAllocator>{ new MemoryAllocator() });
}

void MyDevice::_DestroyMemoryAllocator()
{
	m_uptrMemoryAllocator.reset();
}


// pCreateInfo->pNext chain includes a VkPhysicalDeviceVulkan12Features structure,
// then it must not include a VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES structure.
// The Vulkan spec states : 
// If the pNext chain includes a VkPhysicalDeviceVulkan12Features structure,
// then it must not include a 
//	VkPhysicalDevice8BitStorageFeatures,
//	VkPhysicalDeviceShaderAtomicInt64Features,
//	VkPhysicalDeviceShaderFloat16Int8Features,
//	VkPhysicalDeviceDescriptorIndexingFeatures,
//	VkPhysicalDeviceScalarBlockLayoutFeatures,
//	VkPhysicalDeviceImagelessFramebufferFeatures,
//	VkPhysicalDeviceUniformBufferStandardLayoutFeatures,
//	VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures,
//	VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures,
//	VkPhysicalDeviceHostQueryResetFeatures,
//	VkPhysicalDeviceTimelineSemaphoreFeatures,
//	VkPhysicalDeviceBufferDeviceAddressFeatures,
//	or VkPhysicalDeviceVulkanMemoryModelFeatures structure
void MyDevice::_AddBaseExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const
{
	VkPhysicalDeviceFeatures requiredFeatures{};
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2Feature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
	
	requiredFeatures.geometryShader = VK_TRUE;
	requiredFeatures.samplerAnisotropy = VK_TRUE;
	requiredFeatures.sampleRateShading = VK_TRUE;
	requiredFeatures.fragmentStoresAndAtomics = VK_TRUE;
	requiredFeatures.shaderInt64 = VK_TRUE;
	requiredFeatures.shaderFloat64 = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
	sync2Feature.synchronization2 = VK_TRUE;
	
	_selector.add_required_extensions(
		{ 
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_MAINTENANCE1_EXTENSION_NAME,
			VK_KHR_MAINTENANCE3_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
			VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
		});
	_selector.set_required_features(requiredFeatures);
	_selector.add_required_extension_features(sync2Feature);
	//_selector.add_required_extension_features(physicalDeviceDescriptorIndexingFeatures);
}

void MyDevice::_AddRayTracingExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const
{
	// Ray tracing
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
	VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES };
	VkPhysicalDeviceVulkan12Features vulkan12Featrues{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

	accelerationStructureFeatures.accelerationStructure = VK_TRUE;
	rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	hostQueryResetFeatures.hostQueryReset = VK_TRUE;
	vulkan12Featrues.bufferDeviceAddress = VK_TRUE;
	vulkan12Featrues.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	vulkan12Featrues.runtimeDescriptorArray = VK_TRUE;
	vulkan12Featrues.descriptorBindingVariableDescriptorCount = VK_TRUE;
	vulkan12Featrues.hostQueryReset = VK_TRUE;

	_selector.add_required_extensions(
		{ 
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		});
	_selector.add_required_extension_features(accelerationStructureFeatures);
	_selector.add_required_extension_features(rayTracingPipelineFeatures);
	//_selector.add_required_extension_features(hostQueryResetFeatures);
	_selector.add_required_extension_features(vulkan12Featrues);
}

void MyDevice::_AddMeshShaderExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const
{
	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
	VkPhysicalDeviceVulkan12Features vulkan12Featrues{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

	meshShaderFeatures.taskShader = VK_TRUE;
	meshShaderFeatures.meshShader = VK_TRUE;
	vulkan12Featrues.shaderInt8 = VK_TRUE;
	vulkan12Featrues.storageBuffer8BitAccess = VK_TRUE;
	vulkan12Featrues.descriptorIndexing = VK_TRUE;

	_selector.add_required_extensions(
		{
			VK_EXT_MESH_SHADER_EXTENSION_NAME,
			VK_KHR_SPIRV_1_4_EXTENSION_NAME,
			VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
		});
	_selector.add_required_extension_features(meshShaderFeatures);
	_selector.add_required_extension_features(vulkan12Featrues);
}

void MyDevice::_AddRayQueryExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const
{
	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };

	rayQueryFeatures.rayQuery = VK_TRUE;

	_selector.add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	_selector.add_required_extension_features(rayQueryFeatures);
}

void MyDevice::_AddBindlessExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const
{
	// Bindless
	VkPhysicalDeviceVulkan12Features vulkan12Featrues{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

	vulkan12Featrues.runtimeDescriptorArray = VK_TRUE;
	vulkan12Featrues.descriptorBindingPartiallyBound = VK_TRUE;

	_selector.add_required_extension_features(vulkan12Featrues);
}

void MyDevice::_GetVkSwapchainImages(std::vector<VkImage>& _vkImages) const
{
	uint32_t uImageCount = 0;
	_vkImages.clear();
	vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &uImageCount, nullptr);
	_vkImages.resize(static_cast<size_t>(uImageCount));
	vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &uImageCount, _vkImages.data());
}

void MyDevice::_UpdateSwapchainImages()
{
	std::vector<VkImage> vkImages;
	for (std::unique_ptr<SwapchainImage>& uptrImage : m_uptrSwapchainImages)
	{
		if (uptrImage.get() != nullptr)
		{
			uptrImage.reset();
		}
	}
	_GetVkSwapchainImages(vkImages);

	m_uptrSwapchainImages.resize(vkImages.size());
	for (size_t i = 0; i < vkImages.size(); ++i)
	{
		std::unique_ptr<SwapchainImage>& uptrImage = m_uptrSwapchainImages[i];
		uptrImage = std::unique_ptr<SwapchainImage>(new SwapchainImage(static_cast<uint32_t>(i)));
		uptrImage->_ImportSwapchain(vkImages[i], m_swapchain.image_usage_flags,
			m_swapchain.image_format, m_swapchain.extent, m_swapchainResource);
		uptrImage->_CreateRenderFinishedSemaphore();
	}
}

VkExtent2D MyDevice::GetSwapchainExtent() const
{
	return m_swapchain.extent;
}

VkFormat MyDevice::GetDepthFormat() const
{
	return FindSupportFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkFormat MyDevice::GetSwapchainFormat() const
{
	return m_swapchain.image_format;
}

VkFormat MyDevice::FindSupportFormat(const std::vector<VkFormat>& candidates, VkImageTiling tilling, VkFormatFeatureFlags features) const
{
	for (auto format : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(vkPhysicalDevice, format, &props);
		if (tilling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
		{
			return format;
		}
		else if (tilling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
		{
			return format;
		}
	}
	throw std::runtime_error("Failed to find supported format!");
}

void MyDevice::_CreateDescriptorSetLayoutAllocator()
{
	m_uptrDescriptorSetLayoutAllocator = std::make_unique<DescriptorSetLayoutAllocator>();
	m_uptrDescriptorSetLayoutAllocator->Create(vkDevice);
}

void MyDevice::_DestroyDescriptorSetLayoutAllocator()
{
	if (m_uptrDescriptorSetLayoutAllocator != nullptr)
	{
		m_uptrDescriptorSetLayoutAllocator->Destroy();
		m_uptrDescriptorSetLayoutAllocator.reset();
	}
}

void MyDevice::_CreateRenderTargetCache()
{
	std::lock_guard lock(m_renderTargetCacheMutex);
	m_uptrRenderTargetCache = std::make_unique<VulkanDetail::RenderTargetCache>();
	m_uptrRenderTargetCache->Create(vkDevice);
}

void MyDevice::_DestroyRenderTargetCache()
{
	std::lock_guard lock(m_renderTargetCacheMutex);
	if (m_uptrRenderTargetCache != nullptr)
	{
		m_uptrRenderTargetCache->Destroy();
		m_uptrRenderTargetCache.reset();
	}
}

void MyDevice::_CreatePipelineLayoutAllocator()
{
	m_uptrPipelineLayoutAllocator = std::make_unique<PipelineLayoutAllocator>();
	m_uptrPipelineLayoutAllocator->Create(vkDevice);
}

void MyDevice::_DestroyPipelineLayoutAllocator()
{
	if (m_uptrPipelineLayoutAllocator != nullptr)
	{
		m_uptrPipelineLayoutAllocator->Destroy();
		m_uptrPipelineLayoutAllocator.reset();
	}
}

void MyDevice::_CreateShaderModuleAllocator()
{
    m_uptrShaderModuleAllocator = std::make_unique<VulkanDetail::ShaderModuleAllocator>();
    m_uptrShaderModuleAllocator->Create(vkDevice);
}
void MyDevice::_DestroyShaderModuleAllocator()
{
    m_uptrShaderModuleAllocator.reset();
}
void MyDevice::_GetOrCreateShaderModule(ShaderModule& target, const ShaderModuleCreateInfo& info)
{
    CHECK_TRUE(info.IsFinalized(), "ShaderModuleCreateInfo must be finalized before creation!");
    CHECK_TRUE(target.m_vkShaderModule == VK_NULL_HANDLE && target.m_reflector == nullptr,
        "Shader module is already created!");
    CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Shader module creation requires a live device!");
    CHECK_TRUE(m_uptrShaderModuleAllocator != nullptr && m_uptrShaderModuleAllocator->IsCreated(),
        "Shader module allocator is not created!");
    CHECK_TRUE(vkCreateShaderModule != nullptr && vkDestroyShaderModule != nullptr,
        "Shader module native entrypoints are unavailable!");
    auto snapshot = info.m_finalized;
    const auto module = m_uptrShaderModuleAllocator->GetOrCreate(info.GetCode());
    target.m_reflector = std::move(snapshot);
    target.m_vkShaderModule = module;
}

void MyDevice::_CreatePipelineAllocators()
{
	m_uptrGraphicsPipelineAllocator = std::make_unique<GraphicsPipelineAllocator>();
	m_uptrGraphicsPipelineAllocator->Create(vkDevice);

	m_uptrComputePipelineAllocator = std::make_unique<ComputePipelineAllocator>();
	m_uptrComputePipelineAllocator->Create(vkDevice);

	m_uptrRayTracingPipelineAllocator = std::make_unique<RayTracingPipelineAllocator>();
	m_uptrRayTracingPipelineAllocator->Create(vkDevice);
}

void MyDevice::_DestroyPipelineAllocators()
{
	if (m_uptrRayTracingPipelineAllocator != nullptr)
	{
		m_uptrRayTracingPipelineAllocator->Destroy();
		m_uptrRayTracingPipelineAllocator.reset();
	}

	if (m_uptrComputePipelineAllocator != nullptr)
	{
		m_uptrComputePipelineAllocator->Destroy();
		m_uptrComputePipelineAllocator.reset();
	}

	if (m_uptrGraphicsPipelineAllocator != nullptr)
	{
		m_uptrGraphicsPipelineAllocator->Destroy();
		m_uptrGraphicsPipelineAllocator.reset();
	}
}

void MyDevice::_CreateSamplerAllocator()
{
	m_uptrSamplerAllocator = std::make_unique<SamplerAllocator>();
	m_uptrSamplerAllocator->Create();
}

void MyDevice::_DestroySamplerAllocator()
{
	if (m_uptrSamplerAllocator != nullptr)
	{
		m_uptrSamplerAllocator->Destroy();
		m_uptrSamplerAllocator.reset();
	}
}

void MyDevice::Create()
{
	_InitVolk();
	_InitGLFW();
	_CreateInstance();
	_CreateSurface();
	_SelectPhysicalDevice();
	_CreateLogicalDevice();
	_CreateMemoryAllocator();
	_CreateSamplerAllocator();
	_CreateRenderTargetCache();
	_CreatePipelineLayoutAllocator();
	_CreatePipelineAllocators();
    _CreateShaderModuleAllocator();
	_CreateSwapchain();
	_CreateDescriptorSetLayoutAllocator();
	m_initialized = true;
}

void MyDevice::Destroy()
{
	_DestroyDescriptorSetLayoutAllocator();
	_DestroySwapchain();
	_DestroyPipelineAllocators();
    _DestroyShaderModuleAllocator();
	_DestroyPipelineLayoutAllocator();
	_DestroyRenderTargetCache();
	_DestroySamplerAllocator();
	_DestroyMemoryAllocator();
	vkb::destroy_device(m_device);
	vkb::destroy_surface(m_instance, vkSurface);
	vkb::destroy_instance(m_instance);
	glfwDestroyWindow(pWindow);
	glfwTerminate();
	m_initialized = false;
}

UserInput MyDevice::GetUserInput() const
{
	UserInput ret = m_userInput;
	ret.W = (glfwGetKey(pWindow, GLFW_KEY_W) == GLFW_PRESS);
	ret.S = (glfwGetKey(pWindow, GLFW_KEY_S) == GLFW_PRESS);
	ret.A = (glfwGetKey(pWindow, GLFW_KEY_A) == GLFW_PRESS);
	ret.D = (glfwGetKey(pWindow, GLFW_KEY_D) == GLFW_PRESS);
	ret.Q = (glfwGetKey(pWindow, GLFW_KEY_Q) == GLFW_PRESS);
	ret.E = (glfwGetKey(pWindow, GLFW_KEY_E) == GLFW_PRESS);
	ret.LMB = (glfwGetMouseButton(pWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
	ret.RMB = (glfwGetMouseButton(pWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
	return ret;
}

void MyDevice::WaitIdle() const
{
	VK_CHECK(vkDeviceWaitIdle(vkDevice), "Failed to wait for device idle!");
}

double MyDevice::GetTime() const
{
    return glfwGetTime();
}

MemoryAllocator* MyDevice::GetMemoryAllocator()
{
	return m_uptrMemoryAllocator.get();
}

auto MyDevice::GetOrCreateRenderPass(const RenderPassDesc& inDescription) -> VkRenderPass
{
	CHECK_TRUE(inDescription.IsFinalized(), "Render pass description must be finalized before creation!");
	return _GetOrCreateRenderPass(inDescription._GetSnapshot());
}

auto MyDevice::GetOrCreateFramebuffer(const RenderPassDesc& inRenderPass,
	const FramebufferDesc& inDescription) -> VkFramebuffer
{
	CHECK_TRUE(inRenderPass.IsFinalized(), "Render pass description must be finalized before framebuffer creation!");
	const auto snapshot = inRenderPass._GetSnapshot();
	const auto resolved = inDescription._Resolve(*snapshot.description);
	return _GetOrCreateFramebuffer(snapshot, resolved);
}

auto MyDevice::_GetOrCreateRenderPass(const VulkanDetail::RenderPassSnapshot& inSnapshot) -> VkRenderPass
{
	std::lock_guard lock(m_renderTargetCacheMutex);
	CHECK_TRUE(inSnapshot.description, "Render pass description must be finalized before creation!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Render pass creation requires a live device!");
	CHECK_TRUE(m_uptrRenderTargetCache != nullptr && m_uptrRenderTargetCache->IsCreated(),
		"Render target cache is not created!");
	return m_uptrRenderTargetCache->GetOrCreateRenderPass(inSnapshot);
}

auto MyDevice::_GetOrCreateFramebuffer(const VulkanDetail::RenderPassSnapshot& inSnapshot,
	const VulkanDetail::ResolvedFramebufferDesc& inDescription) -> VkFramebuffer
{
	std::lock_guard lock(m_renderTargetCacheMutex);
	CHECK_TRUE(inSnapshot.description, "Render pass description must be finalized before framebuffer creation!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Framebuffer creation requires a live device!");
	CHECK_TRUE(m_uptrRenderTargetCache != nullptr && m_uptrRenderTargetCache->IsCreated(),
		"Render target cache is not created!");
	const auto renderPass = m_uptrRenderTargetCache->GetOrCreateRenderPass(inSnapshot);
	return m_uptrRenderTargetCache->GetOrCreateFramebuffer(renderPass, inDescription);
}

void MyDevice::GetOrCreateDescriptorSetLayout(DescriptorSetLayout& outLayout,
	const DescriptorSetLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(outLayout.m_vkDescriptorSetLayout == VK_NULL_HANDLE, "Descriptor set layout is already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Descriptor set layout creation requires a live device!");
	CHECK_TRUE(m_uptrDescriptorSetLayoutAllocator != nullptr, "Descriptor set layout allocator is not created!");
	auto bindings = inCreateInfo.m_descriptorBindings;
	std::vector<VkDescriptorSetLayoutBinding> sorted;
	const auto* nativeBindings = inCreateInfo.m_finalizedBindings.get();
	if (nativeBindings == nullptr)
	{
		sorted.reserve(bindings.size());
		for (const auto& [index, binding] : bindings) sorted.push_back(binding);
		std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) -> bool { return lhs.binding < rhs.binding; });
		nativeBindings = &sorted;
	}
	const auto handle = m_uptrDescriptorSetLayoutAllocator->GetOrCreate(*nativeBindings);
	outLayout.m_descriptorBindings.swap(bindings);
	outLayout.m_vkDescriptorSetLayout = handle;
}

void MyDevice::GetOrCreatePipelineLayout(PipelineLayout& outLayout,
	const PipelineLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "Pipeline layout create info must be finalized!");
	CHECK_TRUE(outLayout.m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Pipeline layout creation requires a live device!");
	CHECK_TRUE(m_uptrPipelineLayoutAllocator != nullptr && m_uptrPipelineLayoutAllocator->IsCreated(),
		"Pipeline layout allocator is not created!");
	const auto& data = *inCreateInfo.m_finalized;
	CHECK_TRUE(data.layouts.empty() || (m_uptrDescriptorSetLayoutAllocator != nullptr &&
		m_uptrDescriptorSetLayoutAllocator->IsCreated()), "Descriptor set layout allocator is not created!");
	for (const auto& range : data.pushConstants)
	{
		CHECK_TRUE(range.offset <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize &&
			range.size <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize - range.offset,
			"Push constant range exceeds device limits!");
	}

	PipelineLayout layout;
	layout.m_pushConstantRanges = data.pushConstants;
	layout.m_descriptorBindings = data.bindings;
	layout.m_nameToSetBinding = data.names;
	layout.m_ambiguousNames = data.ambiguousNames;
	layout.m_descriptorSetLayouts.resize(data.layouts.size());
	std::vector<VkDescriptorSetLayout> nativeLayouts(data.layouts.size());
	for (size_t index = 0; index < data.layouts.size(); ++index)
	{
		// Empty layouts preserve sparse set indices. Already cached children remain
		// owned by their cache if a later creation fails.
		GetOrCreateDescriptorSetLayout(layout.m_descriptorSetLayouts[index], data.layouts[index]);
		nativeLayouts[index] = layout.m_descriptorSetLayouts[index].GetVkDescriptorSetLayout();
	}
	VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	info.setLayoutCount = static_cast<uint32_t>(nativeLayouts.size());
	info.pSetLayouts = nativeLayouts.data();
	info.pushConstantRangeCount = static_cast<uint32_t>(layout.m_pushConstantRanges.size());
	info.pPushConstantRanges = layout.m_pushConstantRanges.data();
	layout.m_vkPipelineLayout = m_uptrPipelineLayoutAllocator->GetOrCreate(info);
	outLayout = std::move(layout);
}

void MyDevice::CreateComputeShaderProgram(ComputeShaderProgram& outProgram,
	const ComputeShaderProgramCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "ComputeShaderProgramCreateInfo must be finalized before creation!");
	CHECK_TRUE(outProgram.m_vkPipeline == VK_NULL_HANDLE && outProgram.m_uptrShaderModule == nullptr &&
		outProgram.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE, "Compute shader program already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Compute shader program creation requires a live device!");
    CHECK_TRUE(m_uptrShaderModuleAllocator != nullptr && m_uptrShaderModuleAllocator->IsCreated(),
        "Shader module allocator is not created!");
	CHECK_TRUE(m_uptrDescriptorSetLayoutAllocator != nullptr && m_uptrDescriptorSetLayoutAllocator->IsCreated(),
		"Descriptor set layout allocator is not created!");
	CHECK_TRUE(m_uptrPipelineLayoutAllocator != nullptr && m_uptrPipelineLayoutAllocator->IsCreated(),
		"Pipeline layout allocator is not created!");
	CHECK_TRUE(m_uptrComputePipelineAllocator != nullptr && m_uptrComputePipelineAllocator->IsCreated(),
		"Compute pipeline allocator is not created!");
	// Validate device limits before even creating a shader module. Finalize owns
	// the layout snapshot; the normal layout entry checks these limits again.
	for (const auto& range : inCreateInfo.m_finalized->layout.m_finalized->pushConstants)
	{
		CHECK_TRUE(range.offset <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize &&
			range.size <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize - range.offset,
			"Push constant range exceeds device limits!");
	}

	ComputeShaderProgram program;
	program._Create(inCreateInfo);
	// All potentially failing work is complete. Transfer ownership without
	// enabling public moves of a Program or releasing any cached native object.
	static_assert(std::is_nothrow_move_assignable_v<PipelineLayout>);
	static_assert(std::is_nothrow_move_assignable_v<std::unique_ptr<ShaderModule>>);
	outProgram.m_uptrShaderModule = std::move(program.m_uptrShaderModule);
	outProgram.m_pipelineLayout = std::move(program.m_pipelineLayout);
	outProgram.m_vkPipeline = std::exchange(program.m_vkPipeline, VK_NULL_HANDLE);
}

void MyDevice::CreateGraphicsShaderProgram(GraphicsShaderProgram& outProgram,
	const GraphicsShaderProgramCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "GraphicsShaderProgramCreateInfo must be finalized before creation!");
	CHECK_TRUE(outProgram.m_shaderModules.empty() && outProgram.m_shaderStages.empty() &&
		outProgram.m_cachedGraphicsPipelines.empty() && outProgram.m_defaultRenderPass == nullptr &&
		outProgram.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE,
		"Graphics shader program already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Graphics shader program creation requires a live device!");
    CHECK_TRUE(m_uptrShaderModuleAllocator != nullptr && m_uptrShaderModuleAllocator->IsCreated(),
        "Shader module allocator is not created!");
	CHECK_TRUE(m_uptrDescriptorSetLayoutAllocator != nullptr && m_uptrDescriptorSetLayoutAllocator->IsCreated(),
		"Descriptor set layout allocator is not created!");
	CHECK_TRUE(m_uptrPipelineLayoutAllocator != nullptr && m_uptrPipelineLayoutAllocator->IsCreated(),
		"Pipeline layout allocator is not created!");
	CHECK_TRUE(m_uptrGraphicsPipelineAllocator != nullptr && m_uptrGraphicsPipelineAllocator->IsCreated(),
		"Graphics pipeline allocator is not created!");
	const auto& data = *inCreateInfo.m_finalized;
	if (data.defaultRenderPass != nullptr)
	{
		std::lock_guard lock(m_renderTargetCacheMutex);
		CHECK_TRUE(m_uptrRenderTargetCache != nullptr && m_uptrRenderTargetCache->IsCreated(),
			"Render target cache is not created!");
	}
	for (const auto& range : data.layout.m_finalized->pushConstants)
	{
		CHECK_TRUE(range.offset <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize &&
			range.size <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize - range.offset,
			"Push constant range exceeds device limits!");
	}

	GraphicsShaderProgram program;
	program._Create(inCreateInfo);
	static_assert(std::is_nothrow_move_assignable_v<PipelineLayout>);
	static_assert(std::is_nothrow_move_assignable_v<std::vector<std::unique_ptr<ShaderModule>>>);
	static_assert(std::is_nothrow_move_assignable_v<std::vector<VkPipelineShaderStageCreateInfo>>);
	static_assert(std::is_nothrow_move_assignable_v<decltype(program.m_cachedGraphicsPipelines)>);
	static_assert(std::is_nothrow_move_assignable_v<decltype(program.m_defaultRenderPass)>);
	static_assert(std::is_nothrow_move_assignable_v<GraphicsPipelineStateInfo>);
	outProgram.m_shaderModules = std::move(program.m_shaderModules);
	outProgram.m_shaderStages = std::move(program.m_shaderStages);
	outProgram.m_pipelineLayout = std::move(program.m_pipelineLayout);
	outProgram.m_cachedGraphicsPipelines = std::move(program.m_cachedGraphicsPipelines);
	outProgram.m_defaultRenderPass = std::move(program.m_defaultRenderPass);
	outProgram.m_defaultState = std::move(program.m_defaultState);
	outProgram.m_automaticVertexInputSupported = program.m_automaticVertexInputSupported;
}

void MyDevice::CreateRayTracingShaderProgram(RayTracingShaderProgram& outProgram,
	const RayTracingShaderProgramCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "RayTracingShaderProgramCreateInfo must be finalized before creation!");
	CHECK_TRUE(outProgram.m_shaderModules.empty() && outProgram.m_vkPipeline == VK_NULL_HANDLE &&
		outProgram.m_shaderGroupCount == 0 && outProgram.m_groupSet.m_groups.empty() &&
		outProgram.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE,
		"Ray tracing program already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Ray tracing program creation requires a live device!");
    CHECK_TRUE(m_uptrShaderModuleAllocator != nullptr && m_uptrShaderModuleAllocator->IsCreated(),
        "Shader module allocator is not created!");
	CHECK_TRUE(m_uptrDescriptorSetLayoutAllocator != nullptr && m_uptrDescriptorSetLayoutAllocator->IsCreated(),
		"Descriptor set layout allocator is not created!");
	CHECK_TRUE(m_uptrPipelineLayoutAllocator != nullptr && m_uptrPipelineLayoutAllocator->IsCreated(),
		"Pipeline layout allocator is not created!");
	CHECK_TRUE(m_uptrRayTracingPipelineAllocator != nullptr && m_uptrRayTracingPipelineAllocator->IsCreated(),
		"Ray tracing pipeline allocator is not created!");
	CHECK_TRUE(vkCreateRayTracingPipelinesKHR != nullptr && vkGetRayTracingShaderGroupHandlesKHR != nullptr,
		"Ray tracing pipeline extension is unavailable!");
	const auto& finalized = *inCreateInfo.m_finalized;
	CHECK_TRUE(finalized.recursionDepth <= m_cachedRayTracingProperties.maxRayRecursionDepth,
		"Ray tracing recursion depth exceeds device limits!");
	const uint32_t handleSize = m_cachedRayTracingProperties.shaderGroupHandleSize;
	const uint32_t handleAlignment = m_cachedRayTracingProperties.shaderGroupHandleAlignment;
	CHECK_TRUE(handleSize > 0 && handleAlignment > 0 && (handleAlignment & (handleAlignment - 1)) == 0,
		"Invalid ray tracing shader group handle properties!");
	CHECK_TRUE(handleSize <= std::numeric_limits<uint32_t>::max() - (handleAlignment - 1),
		"Ray tracing shader group handle size overflows!");
	CHECK_TRUE(finalized.groups.size() <= std::numeric_limits<uint32_t>::max() &&
		finalized.groups.size() <= std::numeric_limits<size_t>::max() / handleSize,
		"Ray tracing shader group data size overflows!");
	for (const auto& range : finalized.layout.m_finalized->pushConstants)
	{
		CHECK_TRUE(range.offset <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize &&
			range.size <= m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize - range.offset,
			"Push constant range exceeds device limits!");
	}

	RayTracingShaderProgram program;
	program._Create(inCreateInfo);
	static_assert(std::is_nothrow_move_assignable_v<PipelineLayout>);
	static_assert(std::is_nothrow_move_assignable_v<std::vector<std::unique_ptr<ShaderModule>>>);
	static_assert(std::is_nothrow_move_assignable_v<RayTracingShaderGroupSet>);
	outProgram.m_shaderModules = std::move(program.m_shaderModules);
	outProgram.m_pipelineLayout = std::move(program.m_pipelineLayout);
	outProgram.m_vkPipeline = std::exchange(program.m_vkPipeline, VK_NULL_HANDLE);
	outProgram.m_shaderGroupCount = std::exchange(program.m_shaderGroupCount, 0);
	outProgram.m_groupSet = std::move(program.m_groupSet);
}

void MyDevice::GetOrCreateSampler(Sampler& outSampler, const SamplerCreateInfo& inCreateInfo)
{
	CHECK_TRUE(outSampler.m_vkSampler == VK_NULL_HANDLE, "Sampler already created!");
	CHECK_TRUE(vkDevice != VK_NULL_HANDLE, "Sampler creation requires a live device!");
	CHECK_TRUE(m_uptrSamplerAllocator != nullptr, "Sampler allocator is not created!");
	outSampler.m_vkSampler = m_uptrSamplerAllocator->AllocateSampler(inCreateInfo);
}

VkFence MyDevice::CreateVkFence(
	VkFenceCreateFlags inFlags, 
	const void* inNextPtr, 
	const VkAllocationCallbacks* inCallbacks)
{
	VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence result = VK_NULL_HANDLE;

	fenceInfo.flags = inFlags;
	fenceInfo.pNext = inNextPtr;

	VK_CHECK(vkCreateFence(vkDevice, &fenceInfo, inCallbacks, &result), "Failed to create the availability fence!");

	return result;
}

void MyDevice::DestroyVkFence(VkFence& _fence)
{
	vkDestroyFence(vkDevice, _fence, nullptr);
	_fence = VK_NULL_HANDLE;
}

VkSemaphore MyDevice::CreateVkSemaphore(const VkSemaphoreCreateInfo* _pCreateInfo)
{
	VkSemaphore vkSemaphore = VK_NULL_HANDLE;
	if (_pCreateInfo)
	{
		VK_CHECK(vkCreateSemaphore(vkDevice, _pCreateInfo, nullptr, &vkSemaphore), "Failed to create the signal semaphore!");
	}
	else
	{
		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &vkSemaphore), "Failed to create the signal semaphore!");
	}
	return vkSemaphore;
}

void MyDevice::DestroyVkSemaphore(VkSemaphore& _semaphore)
{
	vkDestroySemaphore(vkDevice, _semaphore, nullptr);
	_semaphore = VK_NULL_HANDLE;
}

uint32_t MyDevice::GetQueueFamilyIndexOfType(QueueFamilyType inType) const
{
	uint32_t result = ~0;

	switch (inType)
	{
	case QueueFamilyType::COMPUTE:
	{
		result = queueFamilyIndices.graphicsAndComputeFamily.value_or(~0);
	}
	break;
	case QueueFamilyType::GRAPHICS:
	{
		result = queueFamilyIndices.graphicsFamily.value_or(~0);
	}
	break;
	case QueueFamilyType::TRANSFER:
	{
		result = queueFamilyIndices.transferFamily.value_or(~0);
	}
	break;
	}
	CHECK_TRUE(result != ~0);

	return result;
}

VkQueue MyDevice::GetQueueOfType(QueueFamilyType inType) const
{
	VkQueue result = VK_NULL_HANDLE;

	switch (inType)
	{
	case QueueFamilyType::GRAPHICS:
		result = m_vkGraphicsQueue;
		break;
	case QueueFamilyType::COMPUTE:
		result = m_vkComputeQueue;
		break;
	case QueueFamilyType::TRANSFER:
		result = m_vkTransferQueue;
		break;
	default:
		break;
	}

	CHECK_TRUE(result != VK_NULL_HANDLE);

	return result;
}

VkQueue MyDevice::GetQueueByQueueFamilyIndex(uint32_t inQueueFamilyIndex) const
{
	VkQueue result = VK_NULL_HANDLE;
	const std::vector<std::pair<uint32_t, VkQueue>> keyFamilyIndex
	{
		{queueFamilyIndices.graphicsFamily.value_or(~0), m_vkGraphicsQueue },
		{queueFamilyIndices.graphicsAndComputeFamily.value_or(~0), m_vkComputeQueue},
		{queueFamilyIndices.transferFamily.value_or(~0), m_vkTransferQueue }
	};
	if (inQueueFamilyIndex != ~0)
	{
		for (const auto& p : keyFamilyIndex)
		{
			if (p.first == inQueueFamilyIndex)
			{
				result = p.second;
				break;
			}
		}
	}

    return result;
}


auto MyDevice::IsPipelineCacheValid(const VkPipelineCacheHeaderVersionOne& inCacheHeader) const -> bool
{
	bool result = false;

	result = (m_physicalDevice.properties.deviceID == inCacheHeader.deviceID)
		&& (m_physicalDevice.properties.vendorID == inCacheHeader.vendorID)
		&& (memcmp(inCacheHeader.pipelineCacheUUID, m_physicalDevice.properties.pipelineCacheUUID, VK_UUID_SIZE) == 0);

	return result;
}

void MyDevice::GetPhysicalDeviceRayTracingProperties(VkPhysicalDeviceRayTracingPipelinePropertiesKHR& outProperties) const
{
	outProperties = m_cachedRayTracingProperties;
}

auto MyDevice::GetCachedPhysicalDeviceProperties() const -> const VkPhysicalDeviceProperties&
{
	return m_cachedPhysicalDeviceProperties;
}

VkCommandBuffer MyDevice::AllocateCommandBuffer(VkCommandPool inCommandPool, VkCommandBufferLevel inBufferLevel, const void* inNextPtr)
{
	VkCommandBufferAllocateInfo allocateInfo{};
	VkCommandBuffer result = VK_NULL_HANDLE;

	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.pNext = inNextPtr;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.commandPool = inCommandPool;
	allocateInfo.level = inBufferLevel;

	VK_CHECK(vkAllocateCommandBuffers(vkDevice, &allocateInfo, &result), "Failed to allocate command buffer!");
	return result;
}

void MyDevice::FreeCommandBuffer(VkCommandPool inCommandPool, VkCommandBuffer inCommandBuffer)
{
	vkFreeCommandBuffers(vkDevice, inCommandPool, 1, &inCommandBuffer);
}

VkDescriptorSetLayout MyDevice::CreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& inCreateInfo, const VkAllocationCallbacks* inCallbacks)
{
	VkDescriptorSetLayout result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateDescriptorSetLayout(vkDevice, &inCreateInfo, inCallbacks, &result), "Failed to create descriptor set layout!");

	return result;
}

void MyDevice::DestroyDescriptorSetLayout(VkDescriptorSetLayout inDescriptorSetLayout, const VkAllocationCallbacks* inCallbacks)
{
	vkDestroyDescriptorSetLayout(vkDevice, inDescriptorSetLayout, inCallbacks);
}

VkDescriptorPool MyDevice::CreateDescriptorPool(
	const VkDescriptorPoolCreateInfo& inCreateInfo,
	const VkAllocationCallbacks* inCallbacks)
{
	VkDescriptorPool result;
	VkResult processResult;

	result = CreateDescriptorPool(inCreateInfo, processResult, inCallbacks);
	VK_CHECK(processResult);

	return result;
}

VkDescriptorPool MyDevice::CreateDescriptorPool(
	const VkDescriptorPoolCreateInfo& inCreateInfo, 
	VkResult& outResult, 
	const VkAllocationCallbacks* inCallbacks)
{
	VkDescriptorPool result = VK_NULL_HANDLE;

	outResult = vkCreateDescriptorPool(vkDevice, &inCreateInfo, inCallbacks, &result);

	return result;
}

void MyDevice::DestroyDescriptorPool(VkDescriptorPool inPoolToDestroy, const VkAllocationCallbacks* pCallbacks)
{
	vkDestroyDescriptorPool(vkDevice, inPoolToDestroy, pCallbacks);
}

VkDescriptorSet MyDevice::AllocateDescriptorSet(VkDescriptorSetLayout inLayout, VkDescriptorPool inPool, const void* inNextPtr)
{
	VkDescriptorSet result;
	VkResult processResult;

	result = AllocateDescriptorSet(inLayout, inPool, processResult, inNextPtr);
	VK_CHECK(processResult);

	return result;
}

VkDescriptorSet MyDevice::AllocateDescriptorSet(VkDescriptorSetLayout inLayout, VkDescriptorPool inPool, VkResult& outResult, const void* inNextPtr)
{
	VkDescriptorSet result = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

	allocInfo.descriptorPool = inPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pNext = inNextPtr;
	allocInfo.pSetLayouts = &inLayout;

	outResult = vkAllocateDescriptorSets(vkDevice, &allocInfo, &result);

	return result;
}

VkResult MyDevice::ResetDescriptorPool(VkDescriptorPool inDescriptorPool, VkDescriptorPoolResetFlags inFlags)
{
	return vkResetDescriptorPool(vkDevice, inDescriptorPool, inFlags);
}

auto MyDevice::FreeDescriptorSet(VkDescriptorPool inPool, VkDescriptorSet inSet) -> VkResult
{
	return vkFreeDescriptorSets(vkDevice, inPool, 1, &inSet);
}

void MyDevice::UpdateDescriptorSets(const std::vector<VkWriteDescriptorSet>& inWriteUpdates, const std::vector<VkCopyDescriptorSet>& inCopyUpdates)
{
	vkUpdateDescriptorSets(
		vkDevice, 
		static_cast<uint32_t>(inWriteUpdates.size()), 
		inWriteUpdates.data(), 
		static_cast<uint32_t>(inCopyUpdates.size()), 
		inCopyUpdates.data());
}

void MyDevice::UpdateDescriptorSets(const std::vector<VkWriteDescriptorSet>& inWriteUpdates)
{
	UpdateDescriptorSets(inWriteUpdates, {});
}

void MyDevice::UpdateDescriptorSets(const std::vector<VkCopyDescriptorSet>& inCopyUpdates)
{
	UpdateDescriptorSets({}, inCopyUpdates);
}

VkPipelineCache MyDevice::CreatePipelineCache(const VkPipelineCacheCreateInfo& inCreateInfo, const VkAllocationCallbacks* pCallbacks)
{
	VkResult processResult;
	VkPipelineCache result;

	result = CreatePipelineCache(inCreateInfo, processResult, pCallbacks);

	VK_CHECK(processResult);

	return result;
}

VkPipelineCache MyDevice::CreatePipelineCache(const VkPipelineCacheCreateInfo& inCreateInfo, VkResult& outResult, const VkAllocationCallbacks* pCallbacks)
{
	VkPipelineCache result = VK_NULL_HANDLE;
	
	outResult = vkCreatePipelineCache(vkDevice, &inCreateInfo, pCallbacks, &result);
	
	return result;
}

void MyDevice::DestroyPipelineCache(VkPipelineCache inPipelineCache, const VkAllocationCallbacks* pCallback)
{
	vkDestroyPipelineCache(vkDevice, inPipelineCache, pCallback);
}

VkResult MyDevice::GetPipelineCacheData(VkPipelineCache inPipelineCache, std::vector<char>& outCacheData)
{
	VkResult result;
	size_t dataSize;

	result = vkGetPipelineCacheData(vkDevice, inPipelineCache, &dataSize, nullptr);
	if (result == VK_SUCCESS)
	{
		outCacheData.clear();
		outCacheData.resize(dataSize);
		result = vkGetPipelineCacheData(vkDevice, inPipelineCache, &dataSize, outCacheData.data());
	}

	return result;
}

VkPipeline MyDevice::CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& inCreateInfo, VkPipelineCache inCache, const VkAllocationCallbacks* pAllocator)
{
	VkPipeline result = VK_NULL_HANDLE;

	if (pAllocator == nullptr)
	{
		CHECK_TRUE(m_uptrGraphicsPipelineAllocator != nullptr, "Graphics pipeline allocator is not created!");
		result = m_uptrGraphicsPipelineAllocator->AllocateGraphicsPipeline(inCreateInfo, inCache);
	}
	else
	{
		VK_CHECK(vkCreateGraphicsPipelines(vkDevice, inCache, 1, &inCreateInfo, pAllocator, &result), "Failed to create graphics pipeline!");
	}

    return result;
}

VkPipeline MyDevice::CreateComputePipeline(const VkComputePipelineCreateInfo& inCreateInfo, VkPipelineCache inCache, const VkAllocationCallbacks* pAllocator)
{
	VkPipeline result = VK_NULL_HANDLE;

	if (pAllocator == nullptr)
	{
		CHECK_TRUE(m_uptrComputePipelineAllocator != nullptr, "Compute pipeline allocator is not created!");
		result = m_uptrComputePipelineAllocator->AllocateComputePipeline(inCreateInfo, inCache);
	}
	else
	{
		VK_CHECK(vkCreateComputePipelines(vkDevice, inCache, 1, &inCreateInfo, pAllocator, &result), "Failed to create compute pipeline!");
	}

	return result;
}

VkPipeline MyDevice::CreateRayTracingPipeline(const VkRayTracingPipelineCreateInfoKHR& inCreateInfo, VkPipelineCache inCache, VkDeferredOperationKHR inDeferredOperation, const VkAllocationCallbacks* pAllocator)
{
	VkPipeline result = VK_NULL_HANDLE;

	if (pAllocator == nullptr)
	{
		CHECK_TRUE(m_uptrRayTracingPipelineAllocator != nullptr, "Ray tracing pipeline allocator is not created!");
		result = m_uptrRayTracingPipelineAllocator->AllocateRayTracingPipeline(inCreateInfo, inCache, inDeferredOperation);
	}
	else
	{
		VK_CHECK(
			vkCreateRayTracingPipelinesKHR(vkDevice, inDeferredOperation, inCache, 1, &inCreateInfo, pAllocator, &result),
			"Failed to create ray tracing pipeline!");
	}

	return result;
}

void MyDevice::DestroyPipeline(VkPipeline inPipeline, const VkAllocationCallbacks* pAllocator)
{
	if (inPipeline == VK_NULL_HANDLE)
	{
		return;
	}

	if (pAllocator == nullptr)
	{
		VkPipeline pipelineToFree = inPipeline;
		if (m_uptrGraphicsPipelineAllocator != nullptr && m_uptrGraphicsPipelineAllocator->FreeGraphicsPipeline(pipelineToFree))
		{
			return;
		}
		if (m_uptrComputePipelineAllocator != nullptr && m_uptrComputePipelineAllocator->FreeComputePipeline(pipelineToFree))
		{
			return;
		}
		if (m_uptrRayTracingPipelineAllocator != nullptr && m_uptrRayTracingPipelineAllocator->FreeRayTracingPipeline(pipelineToFree))
		{
			return;
		}
	}

	vkDestroyPipeline(vkDevice, inPipeline, pAllocator);
}

VkResult MyDevice::GetRayTracingShaderGroupHandles(VkPipeline inPipeline, uint32_t inFirstGroup, uint32_t inGroupCount, size_t inDataSize, void* outDataPtr)
{
	return vkGetRayTracingShaderGroupHandlesKHR(vkDevice, inPipeline, inFirstGroup, inGroupCount, inDataSize, outDataPtr);
}

VkResult MyDevice::GetFenceStatus(VkFence inFence)
{
	return vkGetFenceStatus(vkDevice, inFence);
}

VkResult MyDevice::WaitForFences(const std::vector<VkFence>& inFencesToWait, bool inWaitAll, uint64_t inTimeout)
{
	return vkWaitForFences(
		vkDevice, 
		static_cast<uint32_t>(inFencesToWait.size()), 
		inFencesToWait.data(), 
		inWaitAll ? VK_TRUE : VK_FALSE, 
		inTimeout);
}

auto MyDevice::CreateCommandPool(const VkCommandPoolCreateInfo& inCreateInfo, const VkAllocationCallbacks* pAllocator) -> VkCommandPool
{
	VkCommandPool result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateCommandPool(vkDevice, &inCreateInfo, pAllocator, &result));


	return result;
}

auto MyDevice::ResetCommandPool(VkCommandPool inCommandPool, VkCommandPoolResetFlags inFlags) -> VkResult
{
	return vkResetCommandPool(vkDevice, inCommandPool, inFlags);
}

void MyDevice::DestroyCommandPool(VkCommandPool inCommandPoolToDestroy, const VkAllocationCallbacks* pAllocator)
{

	vkDestroyCommandPool(vkDevice, inCommandPoolToDestroy, pAllocator);
}

VkBuffer MyDevice::CreateBuffer(const VkBufferCreateInfo& inCreateInfo, const VkAllocationCallbacks* pAllocator)
{
	VkBuffer result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateBuffer(vkDevice, &inCreateInfo, pAllocator, &result), "Failed to create a VkBuffer!");

	return result;
}

VkDeviceAddress MyDevice::GetBufferDeviceAddress(const VkBufferDeviceAddressInfo& inAddrInfo)
{
	return vkGetBufferDeviceAddress(vkDevice, &inAddrInfo);
}

void MyDevice::DestroyBuffer(VkBuffer inBufferToDestroy, const VkAllocationCallbacks* pAllocator)
{
	vkDestroyBuffer(vkDevice, inBufferToDestroy, pAllocator);
}

VkBufferView MyDevice::CreateBufferView(const VkBufferViewCreateInfo& inCreateInfo, const VkAllocationCallbacks* pAllocator)
{
	VkBufferView result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateBufferView(vkDevice, &inCreateInfo, pAllocator, &result));

	return result;
}

void MyDevice::DestroyBufferView(VkBufferView inBufferView, const VkAllocationCallbacks* pAllocator)
{
	vkDestroyBufferView(vkDevice, inBufferView, pAllocator);
}

VkImage MyDevice::CreateImage(const VkImageCreateInfo& inCreateInfo, const VkAllocationCallbacks* pAllocator)
{
	VkImage result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImage(vkDevice, &inCreateInfo, pAllocator, &result));

	return result;
}

void MyDevice::DestroyImage(VkImage image, const VkAllocationCallbacks* pAllocator)
{
	vkDestroyImage(vkDevice, image, pAllocator);
}

auto MyDevice::CreateImageView(const VkImageViewCreateInfo& inCreateInfo, const VkAllocationCallbacks* pAllocator) -> VkImageView
{
	VkImageView result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateImageView(vkDevice, &inCreateInfo, pAllocator, &result), "Failed to create image view!");

    return result;
}

void MyDevice::DestroyImageView(VkImageView inView, const VkAllocationCallbacks* pAllocator)
{
	vkDestroyImageView(vkDevice, inView, pAllocator);
}

void MyDevice::GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildTypeKHR inBuildType, const VkAccelerationStructureBuildGeometryInfoKHR& inBuildInfo, const std::vector<uint32_t>& inMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR& outSizeInfo)
{
	vkGetAccelerationStructureBuildSizesKHR(vkDevice, inBuildType, &inBuildInfo, inMaxPrimitiveCounts.data(), &outSizeInfo);
}

VkAccelerationStructureKHR MyDevice::CreateAccelerationStructure(const VkAccelerationStructureCreateInfoKHR& inCreateInfo, const VkAllocationCallbacks* pAllocator)
{
	VkAccelerationStructureKHR result = VK_NULL_HANDLE;

	VK_CHECK(vkCreateAccelerationStructureKHR(vkDevice, &inCreateInfo, pAllocator, &result), "Failed to create acceleration structure!");
	
	return result;
}

VkDeviceAddress MyDevice::GetAccelerationStructureDeviceAddress(const VkAccelerationStructureDeviceAddressInfoKHR& inInfo)
{
	// Provided by VK_KHR_acceleration_structure
	return vkGetAccelerationStructureDeviceAddressKHR(vkDevice, &inInfo);
}

MyDevice& MyDevice::GetInstance()
{
	if (s_uptrInstance.get() == nullptr)
	{
		s_uptrInstance = std::make_unique<MyDevice>();
	}
	return *s_uptrInstance;
}

void MyDevice::OnFramebufferResized(GLFWwindow* _pWindow, int width, int height)
{
	auto mydevice = reinterpret_cast<MyDevice*>(glfwGetWindowUserPointer(_pWindow));
	mydevice->m_needRecreate = true;
}

void MyDevice::CursorPosCallBack(GLFWwindow* _pWindow, double _xPos, double _yPos)
{
	// only called when the pos changes
	auto mydevice = reinterpret_cast<MyDevice*>(glfwGetWindowUserPointer(_pWindow));
	auto& userInput = mydevice->m_userInput;
	userInput.xPos = _xPos;
	userInput.yPos = _yPos;
}
