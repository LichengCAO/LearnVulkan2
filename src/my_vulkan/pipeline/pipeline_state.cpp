#include "pipeline_state.h"

void PipelineState::SetDescriptorSet(
	uint32_t inSet,
	const VkDescriptorSet inState,
	const uint32_t* inDynamicOffsets,
	size_t inDynamicOffsetCount)
{
	if (m_descriptorSets.size() <= inSet)
	{
		m_descriptorSets.resize(static_cast<size_t>(inSet) + 1);
	}

	VulkanDescriptorSetBinding& binding = m_descriptorSets[inSet];
	binding.setIndex = inSet;
	binding.descriptorSet = inState;
	binding.dynamicOffsets.clear();
	if (inDynamicOffsets != nullptr && inDynamicOffsetCount > 0)
	{
		binding.dynamicOffsets.assign(inDynamicOffsets, inDynamicOffsets + inDynamicOffsetCount);
	}
}

void PipelineState::SetPushConstant(
	VkPipelineStageFlagBits inStage,
	const void* inData,
	uint32_t inSize,
	bool inDontCopy)
{
	CHECK_TRUE(inData != nullptr || inSize == 0);

	VulkanPushConstantBinding binding{};
	binding.stages = inStage;
	binding.size = inSize;
	if (inDontCopy)
	{
		binding.externalData = inData;
	}
	else if (inSize > 0)
	{
		const uint8_t* src = static_cast<const uint8_t*>(inData);
		binding.data.assign(src, src + inSize);
	}

	m_pushConstants.push_back(std::move(binding));
}

void PipelineState::ResetPipelineState()
{
	m_descriptorSets.clear();
	m_pushConstants.clear();
}

const std::vector<VulkanDescriptorSetBinding>& PipelineState::GetDescriptorSetBindings() const
{
	return m_descriptorSets;
}

const std::vector<VulkanPushConstantBinding>& PipelineState::GetPushConstantBindings() const
{
	return m_pushConstants;
}

void GraphicsPipelineState::SetViewport(uint32_t inFirstViewport, const VkViewport* inViewports)
{
	CHECK_TRUE(inViewports != nullptr);
	if (m_viewports.size() <= inFirstViewport)
	{
		m_viewports.resize(static_cast<size_t>(inFirstViewport) + 1);
	}
	m_viewports[inFirstViewport] = *inViewports;
}

void GraphicsPipelineState::SetScissor(uint32_t inFirstScissor, const VkRect2D* inScissors)
{
	CHECK_TRUE(inScissors != nullptr);
	if (m_scissors.size() <= inFirstScissor)
	{
		m_scissors.resize(static_cast<size_t>(inFirstScissor) + 1);
	}
	m_scissors[inFirstScissor] = *inScissors;
}

void GraphicsPipelineState::ResetGraphicsPipelineState()
{
	ResetPipelineState();
	m_viewports.clear();
	m_scissors.clear();
}

const std::vector<VkViewport>& GraphicsPipelineState::GetViewports() const
{
	return m_viewports;
}

const std::vector<VkRect2D>& GraphicsPipelineState::GetScissors() const
{
	return m_scissors;
}

void GraphicsPipelineDrawState::SetVertexBuffers(
	uint32_t inFirstBinding,
	uint32_t inBindingCount,
	const VkBuffer* inBuffers,
	const VkDeviceSize* inOffsets)
{
	CHECK_TRUE(inBuffers != nullptr || inBindingCount == 0);

	const size_t requiredSize = static_cast<size_t>(inFirstBinding) + inBindingCount;
	if (m_vertexBuffers.size() < requiredSize)
	{
		m_vertexBuffers.resize(requiredSize, VK_NULL_HANDLE);
		m_vertexBufferOffsets.resize(requiredSize, 0);
	}

	for (uint32_t i = 0; i < inBindingCount; ++i)
	{
		const size_t index = static_cast<size_t>(inFirstBinding) + i;
		m_vertexBuffers[index] = inBuffers[i];
		m_vertexBufferOffsets[index] = inOffsets != nullptr ? inOffsets[i] : 0;
	}
}

void GraphicsPipelineDrawState::SetIndexBuffer(
	VkBuffer inBuffer,
	VkDeviceSize inOffset,
	VkIndexType inIndexType)
{
	m_indexBuffer = inBuffer;
	m_indexBufferOffset = inOffset;
	m_indexType = inIndexType;
}

void GraphicsPipelineDrawState::SetDrawState(
	uint32_t inVertexCount,
	uint32_t inInstanceCount,
	uint32_t inFirstVertex,
	uint32_t inFirstInstance)
{
	m_drawState = DrawState{
		inVertexCount,
		inInstanceCount,
		inFirstVertex,
		inFirstInstance
	};
	m_indexedDrawState.reset();
}

void GraphicsPipelineDrawState::SetIndexedDrawState(
	uint32_t inIndexCount,
	uint32_t inInstanceCount,
	uint32_t inFirstIndex,
	int32_t inVertexOffset,
	uint32_t inFirstInstance)
{
	m_indexedDrawState = IndexedDrawState{
		inIndexCount,
		inInstanceCount,
		inFirstIndex,
		inVertexOffset,
		inFirstInstance
	};
	m_drawState.reset();
}

void GraphicsPipelineDrawState::ResetDrawState()
{
	ResetGraphicsPipelineState();
	m_vertexBuffers.clear();
	m_vertexBufferOffsets.clear();
	m_indexBuffer = VK_NULL_HANDLE;
	m_indexBufferOffset = 0;
	m_indexType = VK_INDEX_TYPE_UINT32;
	m_drawState.reset();
	m_indexedDrawState.reset();
}

const std::vector<VkBuffer>& GraphicsPipelineDrawState::GetVertexBuffers() const
{
	return m_vertexBuffers;
}

const std::vector<VkDeviceSize>& GraphicsPipelineDrawState::GetVertexBufferOffsets() const
{
	return m_vertexBufferOffsets;
}

VkBuffer GraphicsPipelineDrawState::GetIndexBuffer() const
{
	return m_indexBuffer;
}

VkDeviceSize GraphicsPipelineDrawState::GetIndexBufferOffset() const
{
	return m_indexBufferOffset;
}

VkIndexType GraphicsPipelineDrawState::GetIndexType() const
{
	return m_indexType;
}

bool GraphicsPipelineDrawState::HasDrawState() const
{
	return m_drawState.has_value();
}

bool GraphicsPipelineDrawState::HasIndexedDrawState() const
{
	return m_indexedDrawState.has_value();
}

uint32_t GraphicsPipelineDrawState::GetVertexCount() const
{
	return m_drawState.has_value() ? m_drawState->vertexCount : 0;
}

uint32_t GraphicsPipelineDrawState::GetIndexCount() const
{
	return m_indexedDrawState.has_value() ? m_indexedDrawState->indexCount : 0;
}

void GraphicsPipelineIndirectDrawState::SetIndirectDrawState(
	VkBuffer inIndirectBuffer,
	VkDeviceSize inOffset,
	uint32_t inDrawCount,
	uint32_t inStride)
{
	m_indirectBuffer = inIndirectBuffer;
	m_offset = inOffset;
	m_drawCount = inDrawCount;
	m_stride = inStride;
}

VkBuffer GraphicsPipelineIndirectDrawState::GetIndirectBuffer() const
{
	return m_indirectBuffer;
}

VkDeviceSize GraphicsPipelineIndirectDrawState::GetIndirectBufferOffset() const
{
	return m_offset;
}

uint32_t GraphicsPipelineIndirectDrawState::GetDrawCount() const
{
	return m_drawCount;
}

uint32_t GraphicsPipelineIndirectDrawState::GetStride() const
{
	return m_stride;
}

void GraphicsPipelineMeshTaskState::SetMeshTask(uint32_t inTaskCount, uint32_t inFirstTask)
{
	m_taskCount = inTaskCount;
	m_firstTask = inFirstTask;
}

uint32_t GraphicsPipelineMeshTaskState::GetTaskCount() const
{
	return m_taskCount;
}

uint32_t GraphicsPipelineMeshTaskState::GetFirstTask() const
{
	return m_firstTask;
}

void ComputePipelineState::SetDispatch(
	uint32_t inGroupCountX,
	uint32_t inGroupCountY,
	uint32_t inGroupCountZ)
{
	m_groupCountX = inGroupCountX;
	m_groupCountY = inGroupCountY;
	m_groupCountZ = inGroupCountZ;
}

uint32_t ComputePipelineState::GetGroupCountX() const
{
	return m_groupCountX;
}

uint32_t ComputePipelineState::GetGroupCountY() const
{
	return m_groupCountY;
}

uint32_t ComputePipelineState::GetGroupCountZ() const
{
	return m_groupCountZ;
}

void RayTracingPipelineState::SetRayDispatch(uint32_t inX, uint32_t inY, uint32_t inZ)
{
	m_rayDispatch = RayDispatch{ inX, inY, inZ };
	m_indirectBuffer = VK_NULL_HANDLE;
	m_indirectOffset = 0;
}

void RayTracingPipelineState::SetRayIndirectDispatch(VkBuffer inIndirectBuffer, VkDeviceSize inOffset)
{
	m_indirectBuffer = inIndirectBuffer;
	m_indirectOffset = inOffset;
	m_rayDispatch.reset();
}

void RayTracingPipelineState::SetRayGenerationTable(VkDeviceAddress inDeviceAddress)
{
	m_rayGenerationTable = inDeviceAddress;
}

void RayTracingPipelineState::SetRayMissTable(VkDeviceAddress inDeviceAddress, uint32_t inStride)
{
	m_rayMissTable = inDeviceAddress;
	m_rayMissStride = inStride;
}

void RayTracingPipelineState::SetRayHitTable(VkDeviceAddress inDeviceAddress, uint32_t inStride)
{
	m_rayHitTable = inDeviceAddress;
	m_rayHitStride = inStride;
}

void RayTracingPipelineState::SetRayCallableTable(VkDeviceAddress inDeviceAddress, uint32_t inStride)
{
	m_rayCallableTable = inDeviceAddress;
	m_rayCallableStride = inStride;
}

bool RayTracingPipelineState::HasDirectRayDispatch() const
{
	return m_rayDispatch.has_value();
}

bool RayTracingPipelineState::HasIndirectRayDispatch() const
{
	return m_indirectBuffer != VK_NULL_HANDLE;
}

uint32_t RayTracingPipelineState::GetRayDispatchX() const
{
	return m_rayDispatch.has_value() ? m_rayDispatch->x : 0;
}

uint32_t RayTracingPipelineState::GetRayDispatchY() const
{
	return m_rayDispatch.has_value() ? m_rayDispatch->y : 0;
}

uint32_t RayTracingPipelineState::GetRayDispatchZ() const
{
	return m_rayDispatch.has_value() ? m_rayDispatch->z : 0;
}
