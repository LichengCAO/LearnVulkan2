#pragma once
#include <common.h>
#include <variant>
#include "resource/descriptor_set.h"
#include "vulkan_common_types.h"

class Buffer;
class ImageView;
class AccelStruct;

struct VulkanDescriptorSetBinding
{
	uint32_t setIndex = 0;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	std::vector<uint32_t> dynamicOffsets;
};

struct VulkanPushConstantBinding
{
	VkPipelineStageFlags stages = 0;
	std::vector<uint8_t> data;
	const void* externalData = nullptr;
	uint32_t size = 0;

	const void* GetData() const
	{
		return externalData != nullptr ? externalData : data.data();
	}
};

struct VulkanGraphicsPipelineStateDesc
{
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	std::vector<VkVertexInputBindingDescription> vertexBindings;
	std::vector<VkVertexInputAttributeDescription> vertexAttributes;
	std::vector<VkDynamicState> dynamicStates;
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	std::vector<VkPushConstantRange> pushConstantRanges;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	VkPipelineRasterizationStateCreateInfo rasterization{};
	VkPipelineMultisampleStateCreateInfo multisample{};
	VkPipelineViewportStateCreateInfo viewport{};
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	uint32_t subpassIndex = 0;
	std::string debugName;
};

struct VulkanComputePipelineStateDesc
{
	VkPipelineShaderStageCreateInfo shaderStage{};
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	std::vector<VkPushConstantRange> pushConstantRanges;
	std::string debugName;
};

struct VulkanRayTracingPipelineStateDesc
{
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
	std::vector<VkPushConstantRange> pushConstantRanges;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	uint32_t maxRayRecursionDepth = 1;
	std::string debugName;
};

class PipelineState
{
private:
	std::vector<VulkanDescriptorSetBinding> m_descriptorSets;
	std::vector<VulkanPushConstantBinding> m_pushConstants;

public:
	void SetDescriptorSet(uint32_t inSet, const VkDescriptorSet inState, const uint32_t* inDynamicOffsets, size_t inDynamicOffsetCount);
	void SetPushConstant(VkPipelineStageFlagBits inStage, const void* inData, uint32_t inSize, bool inDontCopy = false);
	void ResetPipelineState();

	const std::vector<VulkanDescriptorSetBinding>& GetDescriptorSetBindings() const;
	const std::vector<VulkanPushConstantBinding>& GetPushConstantBindings() const;
};

class GraphicsPipelineState : public PipelineState
{
private:
	std::vector<VkViewport> m_viewports;
	std::vector<VkRect2D> m_scissors;

public:
	void SetViewport(uint32_t inFirstViewport, const VkViewport* inViewports);
	void SetScissor(uint32_t inFirstScissor, const VkRect2D* inScissors);
	void SetViewport(uint32_t inFirstViewport, const VulkanViewport* inViewports);
	void SetScissor(uint32_t inFirstScissor, const VulkanScissorRect* inScissors);
	void ResetGraphicsPipelineState();

	const std::vector<VkViewport>& GetViewports() const;
	const std::vector<VkRect2D>& GetScissors() const;
};

class GraphicsPipelineDrawState final : public GraphicsPipelineState
{
private:
	struct DrawState
	{
		uint32_t vertexCount = 0;
		uint32_t instanceCount = 1;
		uint32_t firstVertex = 0;
		uint32_t firstInstance = 0;
	};

	struct IndexedDrawState
	{
		uint32_t indexCount = 0;
		uint32_t instanceCount = 1;
		uint32_t firstIndex = 0;
		int32_t vertexOffset = 0;
		uint32_t firstInstance = 0;
	};

	std::vector<VkBuffer> m_vertexBuffers;
	std::vector<VkDeviceSize> m_vertexBufferOffsets;
	VkBuffer m_indexBuffer = VK_NULL_HANDLE;
	VkDeviceSize m_indexBufferOffset = 0;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	std::optional<DrawState> m_drawState;
	std::optional<IndexedDrawState> m_indexedDrawState;

public:
	void SetVertexBuffers(uint32_t inFirstBinding, uint32_t inBindingCount, const VkBuffer* inBuffers, const VkDeviceSize* inOffsets);
	void SetIndexBuffer(VkBuffer inBuffer, VkDeviceSize inOffset, VkIndexType inIndexType);
	void SetDrawState(uint32_t inVertexCount, uint32_t inInstanceCount, uint32_t inFirstVertex, uint32_t inFirstInstance);
	void SetIndexedDrawState(uint32_t inIndexCount, uint32_t inInstanceCount, uint32_t inFirstIndex, int32_t inVertexOffset, uint32_t inFirstInstance);
	void ResetDrawState();

	const std::vector<VkBuffer>& GetVertexBuffers() const;
	const std::vector<VkDeviceSize>& GetVertexBufferOffsets() const;
	VkBuffer GetIndexBuffer() const;
	VkDeviceSize GetIndexBufferOffset() const;
	VkIndexType GetIndexType() const;
	bool HasDrawState() const;
	bool HasIndexedDrawState() const;
	uint32_t GetVertexCount() const;
	uint32_t GetIndexCount() const;
};

class GraphicsPipelineIndirectDrawState final : public GraphicsPipelineState
{
private:
	VkBuffer m_indirectBuffer = VK_NULL_HANDLE;
	VkDeviceSize m_offset = 0;
	uint32_t m_drawCount = 0;
	uint32_t m_stride = 0;

public:
	void SetIndirectDrawState(VkBuffer inIndirectBuffer, VkDeviceSize inOffset, uint32_t inDrawCount, uint32_t inStride);
	VkBuffer GetIndirectBuffer() const;
	VkDeviceSize GetIndirectBufferOffset() const;
	uint32_t GetDrawCount() const;
	uint32_t GetStride() const;
};

class GraphicsPipelineMeshTaskState final : public GraphicsPipelineState
{
private:
	uint32_t m_taskCount = 0;
	uint32_t m_firstTask = 0;

public:
	void SetMeshTask(uint32_t inTaskCount, uint32_t inFirstTask);
	uint32_t GetTaskCount() const;
	uint32_t GetFirstTask() const;
};

class ComputePipelineState final : public PipelineState
{
private:
	uint32_t m_groupCountX = 1;
	uint32_t m_groupCountY = 1;
	uint32_t m_groupCountZ = 1;

public:
	void SetDispatch(uint32_t inGroupCountX, uint32_t inGroupCountY, uint32_t inGroupCountZ);
	uint32_t GetGroupCountX() const;
	uint32_t GetGroupCountY() const;
	uint32_t GetGroupCountZ() const;
};

class RayTracingPipelineState final : public PipelineState
{
private:
	struct RayDispatch
	{
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t z = 0;
	};

	std::optional<RayDispatch> m_rayDispatch;
	VkBuffer m_indirectBuffer = VK_NULL_HANDLE;
	VkDeviceSize m_indirectOffset = 0;
	VkDeviceAddress m_rayGenerationTable = 0;
	VkDeviceAddress m_rayMissTable = 0;
	uint32_t m_rayMissStride = 0;
	VkDeviceAddress m_rayHitTable = 0;
	uint32_t m_rayHitStride = 0;
	VkDeviceAddress m_rayCallableTable = 0;
	uint32_t m_rayCallableStride = 0;

public:
	void SetRayDispatch(uint32_t inX, uint32_t inY, uint32_t inZ);
	void SetRayIndirectDispatch(VkBuffer inIndirectBuffer, VkDeviceSize inOffset);
	void SetRayGenerationTable(VkDeviceAddress inDeviceAddress);
	void SetRayMissTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
	void SetRayHitTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
	void SetRayCallableTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
	bool HasDirectRayDispatch() const;
	bool HasIndirectRayDispatch() const;
	uint32_t GetRayDispatchX() const;
	uint32_t GetRayDispatchY() const;
	uint32_t GetRayDispatchZ() const;
};
