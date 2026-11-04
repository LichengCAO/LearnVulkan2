#pragma once
#include "common.h"
#include "common_enums.h"
#include <VkBootstrap.h>
#include "image.h"
#include "sampler.h"

class MemoryAllocator;
class DescriptorSetLayoutAllocator;
class RenderPassAllocator;
class PipelineLayoutAllocator;
class GraphicsPipelineAllocator;
class ComputePipelineAllocator;
class RayTracingPipelineAllocator;
class SamplerAllocator;
class DescriptorSetLayout;
class DescriptorSetLayoutCreateInfo;
class PipelineLayout;
class PipelineLayoutCreateInfo;
class ComputeShaderProgram;
class ComputeShaderProgramCreateInfo;
class GraphicsShaderProgram;
class GraphicsShaderProgramCreateInfo;
class RayTracingShaderProgram;
class RayTracingShaderProgramCreateInfo;

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
    friend struct ResourceSubmissionTestProbe;
	friend struct ShaderBindingTestProbe;
	friend struct ResourceLifetimeTestProbe;
	friend struct CreateInfoFinalizeTestProbe;
	friend struct DescriptorPoolTestProbe;
	friend struct SamplerCreationTestProbe;
private:
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;
		std::optional<uint32_t> graphicsAndComputeFamily;
		std::optional<uint32_t> transferFamily;
		bool isComplete()
		{
			bool ok1 = graphicsFamily.has_value()
				&& presentFamily.has_value()
				&& graphicsAndComputeFamily.has_value()
				&& transferFamily.has_value();
			return ok1;
		}
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

private:
	static std::unique_ptr<MyDevice> s_uptrInstance;

	vkb::Instance		m_instance;
	vkb::PhysicalDevice m_physicalDevice;
	VkPhysicalDeviceProperties m_cachedPhysicalDeviceProperties{};
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_cachedRayTracingProperties{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
	vkb::Device			m_device;
	vkb::DispatchTable	m_dispatchTable;
	vkb::Swapchain		m_swapchain;
	IntrusivePtr<VulkanDetail::SwapchainResource> m_swapchainResource;
	VkQueue				m_vkPresentQueue = VK_NULL_HANDLE;
	VkQueue				m_vkGraphicsQueue = VK_NULL_HANDLE;
	VkQueue				m_vkComputeQueue = VK_NULL_HANDLE;
	VkQueue				m_vkTransferQueue = VK_NULL_HANDLE;
	bool				m_needRecreate = false;
	bool				m_initialized = false;
	UserInput			m_userInput{};
	std::vector<std::unique_ptr<SwapchainImage>> m_uptrSwapchainImages;
	std::unique_ptr<MemoryAllocator> m_uptrMemoryAllocator;
	std::unique_ptr<DescriptorSetLayoutAllocator> m_uptrDescriptorSetLayoutAllocator;
	std::unique_ptr<RenderPassAllocator> m_uptrRenderPassAllocator;
	std::unique_ptr<PipelineLayoutAllocator> m_uptrPipelineLayoutAllocator;
	std::unique_ptr<GraphicsPipelineAllocator> m_uptrGraphicsPipelineAllocator;
	std::unique_ptr<ComputePipelineAllocator> m_uptrComputePipelineAllocator;
	std::unique_ptr<RayTracingPipelineAllocator> m_uptrRayTracingPipelineAllocator;
	std::unique_ptr<SamplerAllocator> m_uptrSamplerAllocator;
	std::unordered_map<VkCommandPool, uint32_t> m_mapPoolToQueueFamily;

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
	void _CreateDescriptorSetLayoutAllocator();
	void _DestroyDescriptorSetLayoutAllocator();
	void _CreateRenderPassAllocator();
	void _DestroyRenderPassAllocator();
	void _CreatePipelineLayoutAllocator();
	void _DestroyPipelineLayoutAllocator();
	void _CreatePipelineAllocators();
	void _DestroyPipelineAllocators();
	void _CreateSwapchain(VkSwapchainKHR inOldSwapchain = VK_NULL_HANDLE);
	void _DestroySwapchain();
	void _CreateMemoryAllocator();
	void _DestroyMemoryAllocator();
	void _CreateSamplerAllocator();
	void _DestroySamplerAllocator();

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
	
	~MyDevice();

	void Create();
	
	void Destroy();
	
	void RecreateSwapchain();
	
	auto GetNextAvailableSwapchainImage(VkSemaphore acquireSemaphore) -> SwapchainImage*;
	void PresentSwapchainImage(const SwapchainImage& inImage);
	
	bool NeedRecreateSwapchain() const;
	
	bool NeedCloseWindow() const;
	
	void StartFrame() const;
	
	VkExtent2D GetSwapchainExtent() const;
	
	VkFormat FindSupportFormat(const std::vector<VkFormat>& candidates, VkImageTiling tilling, VkFormatFeatureFlags features) const;
	
	VkFormat GetDepthFormat() const;
	
	VkFormat GetSwapchainFormat() const;
	
	UserInput GetUserInput() const;

	void WaitIdle() const;
	
	// Get time, in seconds
	double GetTime() const;

	MemoryAllocator* GetMemoryAllocator();
	auto GetRenderPassAllocator() -> RenderPassAllocator*;

	// Caller excludes concurrent access to this device's descriptor layout cache,
	// including cache destruction. Wrapper value operations do not access it.
	// The target must be empty; success publishes its handle and binding snapshot.
	void GetOrCreateDescriptorSetLayout(DescriptorSetLayout& outLayout,
		const DescriptorSetLayoutCreateInfo& inCreateInfo);

	// Caller serializes this device's pipeline-layout cache and, when used,
	// descriptor-layout cache with all other accesses, including teardown.
	// Requires finalized input and an empty target; publishes only on success.
	void GetOrCreatePipelineLayout(PipelineLayout& outLayout,
		const PipelineLayoutCreateInfo& inCreateInfo);

	// Synchronous creation; requires finalized input, an empty target and live
	// Device/allocators. Failure leaves the target unchanged and retryable.
	// Caller coordinates both layout caches, the compute pipeline cache and the
	// supplied VkPipelineCache with every other access, including cache teardown.
	// Program destruction calls DestroyPipeline, which also searches the graphics
	// cache (and the ray tracing cache if preceding searches miss). Coordinate
	// those accesses too. No internal locking or automatic GPU synchronization.
	void CreateComputeShaderProgram(ComputeShaderProgram& outProgram,
		const ComputeShaderProgramCreateInfo& inCreateInfo);

	// Caller coordinates the layout caches, graphics pipeline cache, RenderPass
	// cache/snapshots and all related release paths. DestroyPipeline probes the
	// Graphics, Compute and RayTracing caches. GetVkPipeline mutates program-local
	// state and must be serialized with access to that instance. No internal
	// locking or automatic GPU synchronization.
	void CreateGraphicsShaderProgram(GraphicsShaderProgram& outProgram,
		const GraphicsShaderProgramCreateInfo& inCreateInfo);

	// Caller coordinates both layout caches, the ray tracing pipeline cache and
	// supplied VkPipelineCache with all related accesses and teardown. Destruction
	// probes the Graphics, Compute and RayTracing caches through DestroyPipeline.
	// No internal locking or GPU wait.
	void CreateRayTracingShaderProgram(RayTracingShaderProgram& outProgram,
		const RayTracingShaderProgramCreateInfo& inCreateInfo);

	// Caller excludes concurrent access to this device's sampler cache, including
	// cache destruction. Borrowed Sampler value operations do not access it.
	void GetOrCreateSampler(Sampler& outSampler, const SamplerCreateInfo& inCreateInfo);

	// Get queue family index by the function,
	// https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/queues.adoc
	uint32_t GetQueueFamilyIndexOfType(QueueFamilyType inType) const;

	// Get queue by the queue family type, we only have one queue for one type.
	// One queue can only be submit from one thread each time
	VkQueue GetQueueOfType(QueueFamilyType inType) const;

	VkQueue GetQueueByQueueFamilyIndex(uint32_t inQueueFamilyIndex) const;

	// Get queue family index by VkCommandPool handle
	uint32_t GetQueueFamilyIndex(VkCommandPool inVkCommandPool) const;

	// Return if the loaded pipeline cache is valid
	auto IsPipelineCacheValid(const VkPipelineCacheHeaderVersionOne& inCacheHeader) const -> bool;

	void GetPhysicalDeviceRayTracingProperties(VkPhysicalDeviceRayTracingPipelinePropertiesKHR& outProperties) const;
	auto GetCachedPhysicalDeviceProperties() const -> const VkPhysicalDeviceProperties&;

	auto GetVkDevice()->VkDevice { return vkDevice; };

#pragma region VulkanFunctions
	// Thin wraps for device Vulkan functions
	//---------------------------------------------
	// Create a VkFence, _pCreateInfo is optional, if it's not nullptr, VkFence will be created based on it
	VkFence CreateVkFence(
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
	auto FreeDescriptorSet(VkDescriptorPool inPool, VkDescriptorSet inSet) -> VkResult;

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

	VkResult GetPipelineCacheData(VkPipelineCache inPipelineCache, std::vector<char>& outCacheData);

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

	auto CreateImageView(
		const VkImageViewCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr) -> VkImageView;

	void DestroyImageView(
		VkImageView inView, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	auto CreateRenderPass(
		const VkRenderPassCreateInfo& inCreateInfo, 
		const VkAllocationCallbacks* pAllocator = nullptr) -> VkRenderPass;

	void DestroyRenderPass(
		VkRenderPass inRenderPass, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	auto CreateFramebuffer(
		const VkFramebufferCreateInfo& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr) -> VkFramebuffer;

	void DestroyFramebuffer(
		VkFramebuffer inFramebuffer, 
		const VkAllocationCallbacks* pAllocator = nullptr);

	void GetAccelerationStructureBuildSizes(
		VkAccelerationStructureBuildTypeKHR inBuildType, 
		const VkAccelerationStructureBuildGeometryInfoKHR& inBuildInfo,
		const std::vector<uint32_t>& inMaxPrimitiveCounts,
		VkAccelerationStructureBuildSizesInfoKHR& outSizeInfo);

	VkAccelerationStructureKHR CreateAccelerationStructure(
		const VkAccelerationStructureCreateInfoKHR& inCreateInfo,
		const VkAllocationCallbacks* pAllocator = nullptr);

	VkDeviceAddress GetAccelerationStructureDeviceAddress(const VkAccelerationStructureDeviceAddressInfoKHR& inInfo);

	//---------------------------------------------
#pragma endregion

public:
	static MyDevice& GetInstance();
	
	static void OnFramebufferResized(GLFWwindow* _pWindow, int width, int height);
	
	static void CursorPosCallBack(GLFWwindow* _pWindow, double _xPos, double _yPos);

	// do this or use static std::unique_ptr<MyDevice> instance{ new MyDevice() };
	friend std::unique_ptr<MyDevice> std::make_unique<MyDevice>();
};
