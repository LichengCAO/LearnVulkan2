#pragma once
#include <common.h>

class GraphicsPipelineAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipeline> m_mapHashToVkPipeline;

public:
	GraphicsPipelineAllocator() = default;
	GraphicsPipelineAllocator(const GraphicsPipelineAllocator&) = delete;
	GraphicsPipelineAllocator& operator=(const GraphicsPipelineAllocator&) = delete;
	~GraphicsPipelineAllocator();

	void Create(VkDevice inDevice);
	auto AllocateGraphicsPipelineWithResult(
		const VkGraphicsPipelineCreateInfo* inCreateInfo,
		VkPipelineCache inCache) -> std::pair<VkPipeline, VkResult>;
	auto AllocateGraphicsPipeline(
		const VkGraphicsPipelineCreateInfo* inCreateInfo,
		VkPipelineCache inCache) -> VkPipeline;
	bool FreeGraphicsPipeline(VkPipeline& inoutPipeline);
	bool HasGraphicsPipeline(VkPipeline inPipeline) const;
	void Destroy();
};

class ComputePipelineAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipeline> m_mapHashToVkPipeline;

public:
	ComputePipelineAllocator() = default;
	ComputePipelineAllocator(const ComputePipelineAllocator&) = delete;
	ComputePipelineAllocator& operator=(const ComputePipelineAllocator&) = delete;
	~ComputePipelineAllocator();

	void Create(VkDevice inDevice);
	auto AllocateComputePipelineWithResult(
		const VkComputePipelineCreateInfo* inCreateInfo,
		VkPipelineCache inCache) -> std::pair<VkPipeline, VkResult>;
	auto AllocateComputePipeline(
		const VkComputePipelineCreateInfo* inCreateInfo,
		VkPipelineCache inCache) -> VkPipeline;
	bool FreeComputePipeline(VkPipeline& inoutPipeline);
	bool HasComputePipeline(VkPipeline inPipeline) const;
	void Destroy();
};

class RayTracingPipelineAllocator final
{
private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipeline> m_mapHashToVkPipeline;

public:
	RayTracingPipelineAllocator() = default;
	RayTracingPipelineAllocator(const RayTracingPipelineAllocator&) = delete;
	RayTracingPipelineAllocator& operator=(const RayTracingPipelineAllocator&) = delete;
	~RayTracingPipelineAllocator();

	void Create(VkDevice inDevice);
	auto AllocateRayTracingPipelineWithResult(
		const VkRayTracingPipelineCreateInfoKHR* inCreateInfo,
		VkPipelineCache inCache,
		VkDeferredOperationKHR inDeferredOperation) -> std::pair<VkPipeline, VkResult>;
	auto AllocateRayTracingPipeline(
		const VkRayTracingPipelineCreateInfoKHR* inCreateInfo,
		VkPipelineCache inCache,
		VkDeferredOperationKHR inDeferredOperation) -> VkPipeline;
	bool FreeRayTracingPipeline(VkPipeline& inoutPipeline);
	bool HasRayTracingPipeline(VkPipeline inPipeline) const;
	void Destroy();
};
