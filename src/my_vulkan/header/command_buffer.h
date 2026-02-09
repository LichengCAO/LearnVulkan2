#pragma once
#include "common.h"
#include <queue>
#include "common_enums.h"
#include "vulkan_struct_util.h"
// https://stackoverflow.com/questions/44105058/implementing-component-system-from-unity-in-c

class GraphicsPipeline;
class RenderPass;
class Framebuffer;
class CommandPool;
class CommandBuffer;

class ICommandPoolInitializer
{
public:
	virtual void InitCommandPool(CommandPool* outPoolPtr) const = 0;
};

class ICommandBufferInitializer
{
public:
	virtual void InitCommandBuffer(CommandBuffer* outCommandBufferPtr) const = 0;
};

// class handles commandbuffer and queue submission, synchronization
class CommandSubmission
{
public:
	enum class CALLBACK_BINDING_POINT
	{
		END_RENDER_PASS,
		COMMANDS_DONE,
	};
	struct WaitInformation
	{
		VkSemaphore          waitSamaphore = VK_NULL_HANDLE;
		VkPipelineStageFlags waitPipelineStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // stages that cannot start till the semaphore is signaled
	};

private:
	VkSemaphore m_vkSemaphore = VK_NULL_HANDLE;
	std::vector<VkSemaphore> m_vkWaitSemaphores;
	std::vector<VkPipelineStageFlags> m_vkWaitStages;
	std::optional<uint32_t> m_optQueueFamilyIndex;
	std::unordered_map<CALLBACK_BINDING_POINT, std::queue<std::function<void(CommandSubmission*)>>> m_callbacks;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	bool m_isRecording = false;
	bool m_isInRenderpass = false;

public:
	VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
	VkFence vkFence = VK_NULL_HANDLE;

private:
	void _CreateSynchronizeObjects();

	void _UpdateImageLayout(VkImage vkImage, VkImageSubresourceRange range, VkImageLayout layout) const;

	// Called by RenderPass only
	void _BeginRenderPass(const VkRenderPassBeginInfo& info, VkSubpassContents content = VK_SUBPASS_CONTENTS_INLINE);

	// Do all callbacks in the queue and remove them
	void _DoCallbacks(CALLBACK_BINDING_POINT _bindingPoint);

	// Called by RayTracingAccelerationStructure
	void _AddPipelineBarrier2(const VkDependencyInfo& _dependency);

public:
	void PresetQueueFamilyIndex(uint32_t _queueFamilyIndex);
	
	std::optional<uint32_t> GetQueueFamilyIndex() const;
	
	VkQueue GetVkQueue() const;

	void Init();
	
	void Uninit();

	void WaitTillAvailable(); // make sure ALL values used in this command is update AFTER this call or we may access the value while the device still using it
	
	void StartCommands(const std::vector<WaitInformation>& _waitInfos);	
	
	void StartRenderPass(const RenderPass* pRenderPass, const Framebuffer* pFramebuffer);
	
	void EndRenderPass(); // this will change image layout
	
	void StartOneTimeCommands(const std::vector<WaitInformation>& _waitInfos);
	
	VkSemaphore SubmitCommands();

	void SubmitCommands(const std::vector<VkSemaphore>& _semaphoresToSignal);

	void SubmitCommandsAndWait();

	void AddPipelineBarrier(
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		const std::vector<VkImageMemoryBarrier>& imageBarriers); // this will change image layout
	
	void AddPipelineBarrier(
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		const std::vector<VkMemoryBarrier>& memoryBarriers); // this will change image layout
	
	void AddPipelineBarrier(
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		const std::vector<VkMemoryBarrier>& memoryBarriers, 
		const std::vector<VkImageMemoryBarrier>& imageBarriers); // this will change image layout

	void ClearColorImage(
		VkImage vkImage,
		VkImageLayout vkImageLayout,
		const VkClearColorValue& clearColor,
		const std::vector<VkImageSubresourceRange>& ranges);
	
	void FillBuffer(
		VkBuffer vkBuffer, 
		VkDeviceSize offset, 
		VkDeviceSize size, 
		uint32_t data);
	
	void BlitImage(
		VkImage srcImage, 
		VkImageLayout srcLayout,
		VkImage dstImage, 
		VkImageLayout dstLayout,
		std::vector<VkImageBlit> const& regions,
		VkFilter filter = VK_FILTER_LINEAR);  // this will change image layout

	void CopyBuffer(
		VkBuffer vkBufferFrom, 
		VkBuffer vkBufferTo, 
		std::vector<VkBufferCopy> const& copies) const;

	void CopyBufferToImage(
		VkBuffer vkBuffer, 
		VkImage vkImage, 
		VkImageLayout layout, 
		const std::vector<VkBufferImageCopy>& regions) const;

	void BuildAccelerationStructures(
		const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& buildGeomInfos,
		const std::vector<const VkAccelerationStructureBuildRangeInfoKHR*>& buildRangeInfoPtrs) const;

	void WriteAccelerationStructuresProperties(
		const std::vector<VkAccelerationStructureKHR>& vkAccelerationStructs,
		VkQueryType queryType,
		VkQueryPool queryPool,
		uint32_t firstQuery) const;

	void CopyAccelerationStructure(const VkCopyAccelerationStructureInfoKHR& copyInfo) const;

	// Bind callback to a function,
	// the callback will be called only once
	void BindCallback(CALLBACK_BINDING_POINT bindPoint, std::function<void(CommandSubmission*)>&& callback);

	friend class RenderPass;
	friend class GraphicsPipeline;
	friend class RayTracingAccelerationStructure;
};

class CommandBuffer
{
private:
	VkCommandBuffer		 m_vkCommandBuffer = VK_NULL_HANDLE;
	VkCommandPool		 m_vkCommandPool = VK_NULL_HANDLE;
	VkCommandBufferLevel m_vkCommandBufferLevel = VK_COMMAND_BUFFER_LEVEL_MAX_ENUM;
	uint32_t			 m_queueFamily = ~0;
	bool				 m_isRecording = false;
	bool				 m_isInRenderPass = false;
	bool				 m_isFullyRecorded = false;

private:
	void _ProcessInCmdScope(const std::function<void()>& inFunction);

public:
	class Initializer : public ICommandBufferInitializer
	{
	private:
		VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
		VkCommandBufferLevel m_bufferLevel = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	public:
		virtual void InitCommandBuffer(CommandBuffer* outCommandBufferPtr) const override;

		CommandBuffer::Initializer& Reset();

		// Set command pool to allocate buffer, mandatory
		CommandBuffer::Initializer& SetCommandPool(VkCommandPool inCommandPool);

		// Optional, default is VK_COMMAND_BUFFER_LEVEL_PRIMARY
		CommandBuffer::Initializer& CustomizeCommandBufferLevel(VkCommandBufferLevel inBufferLevel);
	};

public:
	~CommandBuffer();

	void Init(const ICommandBufferInitializer* inInitializerPtr);

	// Free command buffer to pool. It's not mandatory, since command buffer will
	// be freed when its parent command pool is destroyed.
	// According to spec, the allocated command buffer will not be freed until
	// the command pool is reset, even with this function invoked.
	void Uninit();

	uint32_t GetQueueFamliyIndex() const;

	VkCommandBuffer GetVkCommandBuffer() const;

	VkCommandBufferLevel GetCommandBufferLevel() const;

	bool IsRecording() const;

	// Reset commands in command buffer
	void Reset(VkCommandBufferResetFlags inFlags = 0);

	// Commands to record
	// ------------------------------------------------------

	// Begin recording commands
	CommandBuffer& BeginCommands(
		VkCommandBufferUsageFlags inFlags = 0, 
		const std::optional<VkCommandBufferInheritanceInfo>& inInheritanceInfo = {},
		const void* inNextPtr = nullptr);

	// End recording commands
	CommandBuffer& EndCommands();

	// Record commands from an external function that takes a VkCommandBuffer as input.
	CommandBuffer& CmdFromExternal(std::function<void(VkCommandBuffer)> inExternalRecord);

	// Starts a render pass, providing detailed settings for the render pass context.
	CommandBuffer& CmdBeginRenderPass(
		const VkRenderPassBeginInfo& inRenderPassBeginInfo,
		VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE);

	// Overloaded version of CmdBeginRenderPass: Simplifies setup by directly passing
	// the render pass, framebuffer, dimensions, clear values, and optional next pointer for extensibility.
	CommandBuffer& CmdBeginRenderPass(
		VkRenderPass inRenderPass,
		VkFramebuffer inFramebuffer,
		int32_t offsetX,
		int32_t offsetY,
		uint32_t width,
		uint32_t height,
		const std::vector<VkClearValue>& inClearValues,
		VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE,
		const void* inNextPtr = nullptr);

	// Ends the current render pass. Should be called after all subpasses and commands within the render pass are complete.
	CommandBuffer& CmdEndRenderPass();

	// Inserts a pipeline barrier between two sets of pipeline stages to synchronize operations.
	// This overload handles memory barriers specifically.
	CommandBuffer& CmdPipelineBarrier(
		VkPipelineStageFlags inSrcStageMask,
		VkPipelineStageFlags inDstStageMask,
		const std::vector<VkMemoryBarrier>& inMemoryBarriers,
		VkDependencyFlags inFlags = 0);

	// Inserts a pipeline barrier for buffer-specific synchronization.
	CommandBuffer& CmdPipelineBarrier(
		VkPipelineStageFlags inSrcStageMask,
		VkPipelineStageFlags inDstStageMask,
		const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
		VkDependencyFlags inFlags = 0);

	// Inserts a pipeline barrier for image-specific synchronization.
	CommandBuffer& CmdPipelineBarrier(
		VkPipelineStageFlags inSrcStageMask,
		VkPipelineStageFlags inDstStageMask,
		const std::vector<VkImageMemoryBarrier>& inImageBarriers,
		VkDependencyFlags inFlags = 0);

	// A generalized pipeline barrier that can handle multiple types of barriers (memory, buffer, image) simultaneously.
	CommandBuffer& CmdPipelineBarrier(
		VkPipelineStageFlags inSrcStageMask,
		VkPipelineStageFlags inDstStageMask,
		const std::vector<VkMemoryBarrier>& inMemoryBarriers,
		const std::vector<VkBufferMemoryBarrier>& inBufferBarriers,
		const std::vector<VkImageMemoryBarrier>& inImageBarriers,
		VkDependencyFlags inFlags = 0);

	// Clears a color image using specific clear values for the given subresource ranges.
	CommandBuffer& CmdClearColorImage(
		VkImage inImage,
		VkImageLayout inImageLayout,
		const VkClearColorValue& inClearColor,
		const std::vector<VkImageSubresourceRange>& inRanges);

	// Fills a buffer with a specified value, starting at a given offset and spanning the specified size.
	CommandBuffer& CmdFillBuffer(
		VkBuffer inBuffer,
		VkDeviceSize inOffset,
		VkDeviceSize inSize,
		uint32_t inValue);

	// Performs an image blit operation, copying data from one image to another with optional filtering.
	CommandBuffer& CmdBlitImage(
		VkImage inSrcImage, 
		VkImageLayout inSrcLayout,
		VkImage inDstImage, 
		VkImageLayout inDstLayout,
		const std::vector<VkImageBlit>& inRegions,
		VkFilter inFilter = VK_FILTER_LINEAR);

	// Copies data from a buffer to an image, using the provided regions to specify the layout of the copy operation.
	CommandBuffer& CmdCopyBufferToImage(
		VkBuffer inSrcBuffer,
		VkImage inDstImage,
		VkImageLayout inDstLayout,
		const std::vector<VkBufferImageCopy>& inRegions);

	// Copies data from a buffer to another buffer, using the provided regions to specify the layout of the copy operation.
	CommandBuffer& CmdCopyBuffer(
		VkBuffer inSrcBuffer,
		VkBuffer inDstBuffer,
		const std::vector<VkBufferCopy>& inRegions);

	// Builds acceleration structures for ray tracing pipelines using provided geometry and range information.
	CommandBuffer& CmdBuildAccelerationStructuresKHR(
		const std::vector<VkAccelerationStructureBuildGeometryInfoKHR>& inBuildInfos,
		const std::vector<VkAccelerationStructureBuildRangeInfoKHR*>& inBuildRanges);

	// Writes properties of specified acceleration structures into a query pool.
	CommandBuffer& CmdWriteAccelerationStructuresPropertiesKHR(
		const std::vector<VkAccelerationStructureKHR>& inAccelerationStructures,
		VkQueryType inQueryType,
		VkQueryPool inQueryPool,
		uint32_t inFirstQuery);

	// Copies an acceleration structure using detailed copy information.
	CommandBuffer& CmdCopyAccelerationStructureKHR(
		const VkCopyAccelerationStructureInfoKHR& inCopyInfo);

	// Overloaded version of CmdCopyAccelerationStructureKHR: Allows copying from source to destination
	// acceleration structures with the specified mode and optional extensibility via inNextPtr.
	CommandBuffer& CmdCopyAccelerationStructureKHR(
		VkAccelerationStructureKHR inSrcAS,
		VkAccelerationStructureKHR inDstAS,
		VkCopyAccelerationStructureModeKHR inMode,
		const void* inNextPtr = nullptr);

	// Executes secondary command buffers within the current primary command buffer.
	CommandBuffer& CmdExecuteCommands(
		const std::vector<VkCommandBuffer>& inCommandBuffers);

	// end of commands
	// ------------------------------------------------------

	friend class CommandPool;
};

class CommandPool
{
private:
	VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = ~0;

public:
	class Initializer : public ICommandPoolInitializer
	{
	private:
		uint32_t m_queueFamilyIndex = ~0;
		VkCommandPoolCreateFlags m_createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	public:
		virtual void InitCommandPool(CommandPool* outPoolPtr) const override;

		CommandPool::Initializer& Reset();

		// Optional, default: VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
		CommandPool::Initializer& CustomizeCommandPoolCreateFlags(VkCommandPoolCreateFlags inFlags);

		// Optional, default: queue family index of the primary queue
		CommandPool::Initializer& CustomizeQueueFamilyIndex(uint32_t inIndex);
	};

public:
	~CommandPool();

	void Init(const ICommandPoolInitializer* inInitializerPtr);

	VkCommandPool GetVkCommandPool() const;

	// Every command buffer allocated by this are reset, 
	// command buffers from this pool are all reset to initial state
	CommandPool& ResetPool();

	// Return command buffer to this pool
	CommandPool& FreeCommandBuffer(CommandBuffer* inoutBufferReturnedPtr);

	uint32_t GetQueueFamilyIndex() const;

	void Uninit();
};