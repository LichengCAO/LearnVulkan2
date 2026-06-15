#include <common.h>
#include <variant>
#include "descriptor_set.h"

class Buffer;
class ImageView;
class AccelStruct;

class PipelineState
{
public:
	void SetDescriptorSet(uint32_t inSet, const VkDescriptorSet inState, const uint32_t* inDynamicOffsets, size_t inDynamicOffsetCount);
	void SetPushConstant(VkPipelineStageFlagBits inStage, const void* inData, uint32_t inSize, bool inDontCopy = false);
};

class GraphicsPipelineState : public PipelineState
{
public:
	void SetViewport(uint32_t inFirstViewport, const VkViewport* inViewports);
	void SetScissor(uint32_t inFirstScissor, const VkRect2D* inScissors);
};

class GraphicsPipelineDrawState final : public GraphicsPipelineState
{
public:
	void SetVertexBuffers(uint32_t inFirstBinding, uint32_t inBindingCount, const VkBuffer* inBuffers, const VkDeviceSize* inOffsets);
	void SetIndexBuffer(VkBuffer inBuffer, VkDeviceSize inOffset, VkIndexType inIndexType);
	void SetDrawState(uint32_t inVertexCount, uint32_t inInstanceCount, uint32_t inFirstVertex, uint32_t inFirstInstance);
	void SetIndexedDrawState(uint32_t inIndexCount, uint32_t inInstanceCount, uint32_t inFirstIndex, int32_t inVertexOffset, uint32_t inFirstInstance);
};

class GraphicsPipelineIndirectDrawState final : public GraphicsPipelineState
{
public:
	void SetIndirectDrawState(VkBuffer inIndirectBuffer, VkDeviceSize inOffset, uint32_t inDrawCount, uint32_t inStride);
};

class GraphicsPipelineMeshTaskState final : public GraphicsPipelineState
{
public:
	void SetMeshTask(uint32_t inTaskCount, uint32_t inFirstTask);
};

class ComputePipelineState final : public PipelineState
{
public:
	void SetDispatch(uint32_t inGroupCountX, uint32_t inGroupCountY, uint32_t inGroupCountZ);
};

class RayTracingPipelineState final : public PipelineState
{
public:
	void SetRayDispatch(uint32_t inX, uint32_t inY, uint32_t inZ);
	void SetRayIndirectDispatch(VkBuffer inIndirectBuffer, VkDeviceSize inOffset);
	void SetRayGenerationTable(VkDeviceAddress inDeviceAddress);
	void SetRayMissTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
	void SetRayHitTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
	void SetRayCallableTable(VkDeviceAddress inDeviceAddress, uint32_t inStride);
};