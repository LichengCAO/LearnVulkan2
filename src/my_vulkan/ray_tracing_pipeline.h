#pragma once
#include "common.h"

class Buffer;
class RayTracingPipeline;
class PushConstantManager;
class RayTracingPipeline;
class ShaderBindingTable;

class IRayTracingPipelineInitializer
{
public:
	virtual void InitRayTracingPipeline(RayTracingPipeline* pPipeline) const = 0;
};

class IShaderBindingTableInitializer
{
public:
	virtual void InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const = 0;
};

// https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
class ShaderBindingTable
{
private:
	std::unique_ptr<Buffer> m_uptrBuffer;
	VkStridedDeviceAddressRegionKHR m_vkRayGenRegion{};
	VkStridedDeviceAddressRegionKHR m_vkMissRegion{};
	VkStridedDeviceAddressRegionKHR m_vkHitRegion{};
	VkStridedDeviceAddressRegionKHR m_vkCallRegion{};

public:
	class Builder : public IShaderBindingTableInitializer
	{
	private:
		uint32_t              m_rayGenerationIndex = ~0;
		std::vector<uint32_t> m_missIndices;
		std::vector<uint32_t> m_hitIndices;
		std::vector<uint32_t> m_callableIndices;
		const RayTracingPipeline* m_pPipeline = nullptr;

	public:
		virtual void InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const override;

		ShaderBindingTable::Builder& Reset();

		ShaderBindingTable::Builder& SetRayGenerationShaderGroupBinding(uint32_t inGroupIndex);
		
		// Return offset of the binding of this type in the binding table
		uint32_t AddMissShaderGroupBinding(uint32_t inGroupIndex);

		// Return offset of the binding of this type in the binding table
		uint32_t AddHitShaderGroupBinding(uint32_t inGroupIndex);
		
		// Return offset of the binding of this type in the binding table
		uint32_t AddCallableShaderGroupBinding(uint32_t inGroupIndex);
		
		ShaderBindingTable::Builder& SetRayTracingPipeline(const RayTracingPipeline* inPipelinePtr);
	};

public:
	ShaderBindingTable(const ShaderBindingTable& _other) = delete;
	ShaderBindingTable& operator=(const ShaderBindingTable& _other) = delete;

	void Init(const IShaderBindingTableInitializer* pInit);

	const VkStridedDeviceAddressRegionKHR& GetRayGenerationShaderRegion() const;
	
	const VkStridedDeviceAddressRegionKHR& GetMissShaderRegion() const;
	
	const VkStridedDeviceAddressRegionKHR& GetHitShaderRegion() const;
	
	const VkStridedDeviceAddressRegionKHR& GetCallableShaderRegion() const;

	void Uninit();
};

class RayTracingPipeline
{
public:
	struct PipelineInput
	{
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;
		
		const ShaderBindingTable* pShaderBindingTable = nullptr;
		
		uint32_t uWidth = 0u;
		uint32_t uHeight = 0u;
		uint32_t uDepth = 1u;
	};

	class Builder : public IRayTracingPipelineInitializer
	{
	private:
		std::vector<VkPipelineShaderStageCreateInfo> m_shaderStageInfos;
		std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderRecords;
		std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
		std::vector<VkPushConstantRange> m_pushConstants;
		uint32_t m_maxRayRecursionDepth = 1u;
		VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

	public:
		virtual void InitRayTracingPipeline(RayTracingPipeline* pPipeline) const override;

		// Optional, default: 1
		RayTracingPipeline::Builder& CustomizeMaxRecursionDepth(uint32_t inMaxDepth);

		// Optional, default: VK_NULL_HANDLE
		RayTracingPipeline::Builder& CustomizePipelineCache(VkPipelineCache inCache);

		RayTracingPipeline::Builder& AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout);

		RayTracingPipeline::Builder& AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);

		// Add shader to pipeline, return shader index for shader group build
		uint32_t AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo);

		// Set ray generation shader group record, 
		// return shader group index for shader binding table build.
		uint32_t SetRayGenerationShaderGroup(uint32_t inRayGenShaderIndex);

		// Add hit shader group record for triangle-based geometries, 
		// return shader group index for shader binding table build.
		uint32_t AddTriangleHitShaderGroup(
			uint32_t inClosestHitShaderIndex, 
			uint32_t inAnyHitShaderIndex = VK_SHADER_UNUSED_KHR);

		// Add hit shader group record for AABB geometries, 
		// return shader group index for shader binding table build.
		uint32_t AddProceduralHitShaderGroup(
			uint32_t inClosestHitShaderIndex, 
			uint32_t inIntersectionShaderIndex, 
			uint32_t inAnyHitShaderIndex = VK_SHADER_UNUSED_KHR);

		// Add miss shader group record, 
		// return shader group index for shader binding table build.
		uint32_t AddMissShaderGroup(uint32_t inMissShaderIndex);

		// Add callable shader group record, 
		// return shader group index for shader binding table build.
		uint32_t AddCallableShaderGroup(uint32_t inCallableShaderIndex);
	};

private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;
	uint32_t m_shaderGroupCount = 0;

public:
	~RayTracingPipeline();

	void Init(const IRayTracingPipelineInitializer* pInitializer);

	void Uninit();

	// Size of total shader group stored in this pipeline;
	// This may not equal to number of shaders we add to this pipeline.
	uint32_t GetShaderGroupCount() const;

	VkPipeline GetVkPipeline() const;

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};