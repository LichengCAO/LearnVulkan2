#pragma once
#include "common.h"
#include "vk_struct.h"
#include <VkBootstrap.h>
#include "pipeline_io.h"
#include "image.h"
#include "sampler.h"

class MemoryAllocator;

struct UserInput
{
	bool W = false;
	bool S = false;
	bool A = false;
	bool D = false;
	bool Q = false;
	bool E = false;
	bool LMB = false;
	bool RMB = false;
	double xPos = 0.0;
	double yPos = 0.0;
};

class MyDevice final
{
private:
	static std::unique_ptr<MyDevice> s_uptrInstance;

	vkb::Instance		m_instance;
	vkb::PhysicalDevice m_physicalDevice;
	vkb::Device			m_device;
	vkb::DispatchTable	m_dispatchTable;
	vkb::Swapchain		m_swapchain;
	VkQueue				m_vkPresentQueue = VK_NULL_HANDLE;
	VkQueue				m_vkGraphicsQueue = VK_NULL_HANDLE;
	VkQueue				m_vkComputeQueue = VK_NULL_HANDLE;
	VkQueue				m_vkTransferQueue = VK_NULL_HANDLE;
	bool				m_needRecreate = false;
	bool				m_initialized = false;
	UserInput			m_userInput{};
	std::vector<std::unique_ptr<Image>> m_uptrSwapchainImages;
	std::unique_ptr<MemoryAllocator> m_uptrMemoryAllocator;

private:
	MyDevice();
	MyDevice(const MyDevice& _other) = delete;

	std::vector<const char*> _GetInstanceRequiredExtensions() const;
	QueueFamilyIndices		 _GetQueueFamilyIndices(VkPhysicalDevice physicalDevice) const;
	SwapChainSupportDetails  _QuerySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
	VkSurfaceFormatKHR		 _ChooseSwapchainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
	VkPresentModeKHR		 _ChooseSwapchainPresentMode(const std::vector<VkPresentModeKHR>& availableModes) const;
	VkExtent2D				 _ChooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	void _InitVolk();
	void _InitGLFW();
	void _CreateInstance();
	void _CreateSurface();
	void _SelectPhysicalDevice();
	void _CreateLogicalDevice();
	void _CreateCommandPools();
	void _DestroyCommandPools();
	void _InitDescriptorAllocator();
	void _CreateSwapchain();
	void _DestroySwapchain();
	void _CreateMemoryAllocator();
	void _DestroyMemoryAllocator();

	// Add required extensions to the device, before select physical device
	void _AddBaseExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const;
	void _AddRayTracingExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const;
	void _AddMeshShaderExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const;
	void _AddRayQueryExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const;
	void _AddBindlessExtensionsAndFeatures(vkb::PhysicalDeviceSelector& _selector) const;

	// get VkImages in current swapchain
	void _GetVkSwapchainImages(std::vector<VkImage>& _vkImages) const;

	// update m_uptrSwapchainImages
	void _UpdateSwapchainImages();

public:
	GLFWwindow*			pWindow = nullptr;
	VkInstance			vkInstance = VK_NULL_HANDLE;
	VkSurfaceKHR		vkSurface = VK_NULL_HANDLE;
	VkPhysicalDevice	vkPhysicalDevice = VK_NULL_HANDLE;
	VkDevice			vkDevice = VK_NULL_HANDLE;
	QueueFamilyIndices	queueFamilyIndices{};
	VkSwapchainKHR		vkSwapchain = VK_NULL_HANDLE;
	SamplerPool         samplerPool{};
	DescriptorSetAllocator descriptorAllocator{};
	std::unordered_map<uint32_t, VkCommandPool>		vkCommandPools;
	
	~MyDevice();

	void Init();
	void Uninit();
	
	void RecreateSwapchain();
	std::vector<Image> GetSwapchainImages() const; //deprecated
	void GetSwapchainImagePointers(std::vector<Image*>& _output) const;
	std::optional<uint32_t> AquireAvailableSwapchainImageIndex(VkSemaphore finishSignal);
	void PresentSwapchainImage(const std::vector<VkSemaphore>& waitSemaphores, uint32_t imageIdx);
	bool NeedRecreateSwapchain() const;
	bool NeedCloseWindow() const;
	void StartFrame() const;
	VkExtent2D GetSwapchainExtent() const;
	VkFormat FindSupportFormat(const std::vector<VkFormat>& candidates, VkImageTiling tilling, VkFormatFeatureFlags features) const;
	VkFormat GetDepthFormat() const;
	VkFormat GetSwapchainFormat() const;
	UserInput GetUserInput() const;

	void WaitIdle() const;
	
	// get time, in seconds
	double GetTime() const;

	MemoryAllocator* GetMemoryAllocator();

	DescriptorSetAllocator* GetDescriptorSetAllocator();

	// Get queue family index by the function,
	// https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/queues.adoc
	uint32_t GetQueueFamilyIndexOfType(QueueFamilyType inType) const;

	// Get queue by the queue family type, we only have one queue for one type.
	// One queue can only be submit from one thread each time
	VkQueue GetQueueOfType(QueueFamilyType inType) const;

	// Get queue family index by VkCommandPool handle
	uint32_t GetQueueFamilyIndex(VkCommandPool inVkCommandPool) const;

	struct CommandPoolRequireInfo
	{
		uint32_t queueFamilyIndex;
		// thread id maybe?
	};
	// Get VkCommandPool by several info
	VkCommandPool GetCommandPool(const CommandPoolRequireInfo& inRequireInfo) const;

	bool IsPipelineCacheValid(const VkPipelineCacheHeaderVersionOne* inCacheHeaderPtr) const;

	void GetPhysicalDeviceRayTracingProperties(VkPhysicalDeviceRayTracingPipelinePropertiesKHR& outProperties) const;

	// Thin wraps for device Vulkan functions
	//---------------------------------------------
	// Create a VkFence, _pCreateInfo is optional, if it's not nullptr, VkFence will be created based on it
	VkFence CreateFence(
		VkFenceCreateFlags inFlags = VK_FENCE_CREATE_SIGNALED_BIT, 
		const void* inNextPtr = nullptr, 
		const VkAllocationCallbacks* inCallbacks = nullptr);

	// Destroy _fence on device, set it to VK_NULL_HANDLE
	void DestroyVkFence(VkFence& _fence);

	// Create a VkSemaphore, _pCreateInfo is optional, if it's not nullptr, VkSemaphore will be created based on it
	VkSemaphore CreateVkSemaphore(const VkSemaphoreCreateInfo* _pCreateInfo = nullptr);

	// Destroy _semaphore on device, set it to VK_NULL_HANDLE
	void DestroyVkSemaphore(VkSemaphore& _semaphore);

	// Allocate VkCommandBuffer
	VkCommandBuffer AllocateCommandBuffer(
		VkCommandPool inCommandPool, 
		VkCommandBufferLevel inBufferLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		const void* inNextPtr = nullptr);

	void FreeCommandBuffer(VkCommandPool inCommandPool, VkCommandBuffer inCommandBuffer);

	VkDescriptorSetLayout CreateDescriptorSetLayout(
		const VkDescriptorSetLayoutCreateInfo& inCreateInfo, 
		const VkAllocationCallbacks* inCallbacks = nullptr);

	void DestroyDescriptorSetLayout(
		VkDescriptorSetLayout inDescriptorSetLayout,
		const VkAllocationCallbacks* inCallbacks = nullptr);

	VkDescriptorPool CreateDescriptorPool(
		const VkDescriptorPoolCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* inCallbacks = nullptr);
	
	VkDescriptorPool CreateDescriptorPool(
		const VkDescriptorPoolCreateInfo& inCreateInfo,
		VkResult& outResult,
		const VkAllocationCallbacks* inCallbacks = nullptr);

	void DestroyDescriptorPool(
		VkDescriptorPool inPoolToDestroy, 
		const VkAllocationCallbacks* pCallbacks = nullptr);

	VkDescriptorSet AllocateDescriptorSet(
		VkDescriptorSetLayout inLayout,
		VkDescriptorPool inPool,
		const void* inNextPtr = nullptr);
	VkDescriptorSet AllocateDescriptorSet(
		VkDescriptorSetLayout inLayout,
		VkDescriptorPool inPool,
		VkResult& outResult,
		const void* inNextPtr = nullptr);

	VkResult ResetDescriptorPool(VkDescriptorPool inDescriptorPool, VkDescriptorPoolResetFlags inFlags = 0);

	// https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html
	void UpdateDescriptorSets(
		const std::vector<VkWriteDescriptorSet>& inWriteUpdates, 
		const std::vector<VkCopyDescriptorSet>& inCopyUpdates);
	void UpdateDescriptorSets(
		const std::vector<VkWriteDescriptorSet>& inWriteUpdates);
	void UpdateDescriptorSets(
		const std::vector<VkCopyDescriptorSet>& inCopyUpdates);

	// https://docs.vulkan.org/refpages/latest/refpages/source/vkCreatePipelineCache.html
	VkPipelineCache CreatePipelineCache(
		const VkPipelineCacheCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pCallbacks = nullptr);
	VkPipelineCache CreatePipelineCache(
		const VkPipelineCacheCreateInfo& inCreateInfo,
		VkResult& outResult,
		const VkAllocationCallbacks* pCallbacks = nullptr);

	void DestroyPipelineCache(VkPipelineCache inPipelineCache, const VkAllocationCallbacks* pCallback = nullptr);

	VkResult GetPipelineCacheData(VkPipelineCache inPipelineCache, std::vector<uint8_t>& outCacheData);

	VkPipeline CreateGraphicsPipeline(
		const VkGraphicsPipelineCreateInfo& inCreateInfo,
		VkPipelineCache inCache = VK_NULL_HANDLE,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkPipeline CreateComputePipeline(
		const VkComputePipelineCreateInfo& inCreateInfo,
		VkPipelineCache inCache = VK_NULL_HANDLE,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkPipeline CreateRayTracingPipeline(
		const VkRayTracingPipelineCreateInfoKHR& inCreateInfo,
		VkPipelineCache inCache = VK_NULL_HANDLE,
		VkDeferredOperationKHR inDeferredOperation = VK_NULL_HANDLE,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyPipeline(
		VkPipeline inPipeline,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkPipelineLayout CreatePipelineLayout(
		const VkPipelineLayoutCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkPipelineLayout CreatePipelineLayout(
		const VkPipelineLayoutCreateInfo& inCreateInfo,
		VkResult& outProcessResult,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyPipelineLayout(
		VkPipelineLayout inLayout,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkResult GetRayTracingShaderGroupHandles(
		VkPipeline inPipeline,
		uint32_t inFirstGroup,
		uint32_t inGroupCount,
		size_t inDataSize,
		void* outDataPtr);

	// VK_SUCCESS: fence signaled, VK_NOT_READY: fence unsignaled
	VkResult GetFenceStatus(VkFence inFence);

	VkResult WaitForFences(
		const std::vector<VkFence>& inFencesToWait, 
		bool inWaitAll = true, 
		uint64_t inTimeout = UINT64_MAX);

	VkCommandPool CreateCommandPool(
		const VkCommandPoolCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkResult ResetCommandPool(VkCommandPool inCommandPool, VkCommandPoolResetFlags inFlags = 0);

	void DestroyCommandPool(
		VkCommandPool inCommandPoolToDestroy,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkBuffer CreateBuffer(
		const VkBufferCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkDeviceAddress GetBufferDeviceAddress(
		const VkBufferDeviceAddressInfo& inAddrInfo);

	void DestroyBuffer(
		VkBuffer inBufferToDestroy,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkBufferView CreateBufferView(
		const VkBufferViewCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyBufferView(
		VkBufferView inBufferView, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkImage CreateImage(
		const VkImageCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyImage(
		VkImage image,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkImageView CreateImageView(
		const VkImageViewCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyImageView(
		VkImageView inView, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkRenderPass CreateRenderPass(
		const VkRenderPassCreateInfo& inCreateInfo, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyRenderPass(
		VkRenderPass inRenderPass, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkFramebuffer CreateFramebuffer(
		const VkFramebufferCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	void DestroyFramebuffer(
		VkFramebuffer inFramebuffer, 
		const VkAllocationCallbacks* pAllocator = nullptr);
	//---------------------------------------------
public:
	static MyDevice& GetInstance();
	static void OnFramebufferResized(GLFWwindow* _pWindow, int width, int height);
	static void CursorPosCallBack(GLFWwindow* _pWindow, double _xPos, double _yPos);

	// do this or use static std::unique_ptr<MyDevice> instance{ new MyDevice() };
	friend std::unique_ptr<MyDevice> std::make_unique<MyDevice>();
};