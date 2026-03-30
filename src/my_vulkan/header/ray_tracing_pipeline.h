#pragma once
#include "common.h"

class Buffer;
class RayTracingPipeline;
class PushConstantManager;
class RayTracingPipeline;
class ShaderBindingTable;
class CommandBuffer;

using ShaderGroupIndex = uint32_t;

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

enum class ShaderTableType : uint8_t
{
	RayGeneration = 0,
	Miss = 1,
	Hit = 2,
	Callable = 3,
	Max = 4
};

class RayTracingShaderGroupSet
{
private:
	std::vector<std::vector<uint8_t>> m_shaderGroupData; // Each inner vector corresponds to a shader group type (raygen, miss, hit, callable) and contains the group data for that type
	std::vector<size_t> m_shaderGroupStride;
	void _SetShaderGroupHandlesData(ShaderTableType inShaderTableType, const uint8_t* inData, size_t inSize, size_t inStride);
	friend class RayTracingPipeline;

public:
	RayTracingShaderGroupSet();
	void Reset();
	void GetShaderGroupHandleData(ShaderTableType inShaderTableType, ShaderGroupIndex inGroupIndex, const uint8_t*& outData, size_t& outSize) const;
	void GetShaderGroupHandlesData(ShaderTableType inShaderTableType, const uint8_t*& outData, size_t& outSize, size_t& outStride) const;
};

// https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
class ShaderBindingTable
{
private:
	std::unique_ptr<Buffer> m_uptrBuffer;
	std::vector<size_t> m_recordStrides; // max record size for each shader table type, used for buffer allocation
	std::vector<VkStridedDeviceAddressRegionKHR> m_vkShaderTableRegions;

public:
	class Descriptor : public IShaderBindingTableInitializer
	{
	private:
		RayTracingShaderGroupSet m_groupSet;
		std::vector<std::vector<uint8_t>> m_shaderRecordData; // Each inner vector corresponds to a shader group type (raygen, miss, hit, callable) and contains the group data for that type
		std::vector<size_t> m_recordStrides; // max record size for each shader table type, used for buffer allocation

	public:
		Descriptor(RayTracingShaderGroupSet inGroupSet);
		
		template<typename T>
		auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex, const T* inData) -> Descriptor&
		{
			CHECK_TRUE(inType >= ShaderTableType::RayGeneration && inType <= ShaderTableType::Callable);
			return SetShaderTableBinding(inType, inBinding, inGroupIndex, reinterpret_cast<const uint8_t*>(inData), sizeof(T));
		}
		auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex, const uint8_t* inData, size_t inDataSize) -> Descriptor&;
		auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex) -> Descriptor&;

		template<typename T>
		auto UpdateShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex, const T* inData) -> Descriptor&
		{
			CHECK_TRUE(inType >= ShaderTableType::RayGeneration && inType <= ShaderTableType::Callable);
			return UpdateShaderTableBinding(inType, inBinding, inGroupIndex, reinterpret_cast<const uint8_t*>(inData), sizeof(T));
		}
		auto UpdateShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex, const uint8_t* inData, size_t inDataSize) -> Descriptor&;
		auto UpdateShaderTableBinding(ShaderTableType inType, uint32_t inBinding, ShaderGroupIndex inGroupIndex) -> Descriptor&;

		void GetShaderTableData(ShaderTableType inType, std::vector<uint8_t>& outData, size_t& outStride) const;
		virtual void InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const override;
	};

public:
	ShaderBindingTable(const ShaderBindingTable& _other) = delete;
	ShaderBindingTable& operator=(const ShaderBindingTable& _other) = delete;

	void Init(const IShaderBindingTableInitializer* pInit);
	auto GetShaderTableRegion(ShaderTableType inType) const -> VkStridedDeviceAddressRegionKHR;
	void Uninit();
};

class RayTracingPipeline
{
public:
	using ShaderIndex = uint32_t;

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
		auto AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo)->ShaderIndex;

		// Set ray generation shader group record, 
		// return shader group index for shader binding table build.
		auto AddRayGenerationShaderGroup(ShaderIndex inRayGenShaderIndex)->ShaderGroupIndex;

		// Add hit shader group record for triangle-based geometries, 
		// return shader group index for shader binding table build.
		auto AddTriangleHitShaderGroup(
			ShaderIndex inClosestHitShaderIndex, 
			ShaderIndex inAnyHitShaderIndex = VK_SHADER_UNUSED_KHR)->ShaderGroupIndex;

		// Add hit shader group record for AABB geometries, 
		// return shader group index for shader binding table build.
		auto AddProceduralHitShaderGroup(
			ShaderIndex inClosestHitShaderIndex, 
			ShaderIndex inIntersectionShaderIndex, 
			ShaderIndex inAnyHitShaderIndex = VK_SHADER_UNUSED_KHR)->ShaderGroupIndex;

		// Add miss shader group record, 
		// return shader group index for shader binding table build.
		auto AddMissShaderGroup(ShaderIndex inMissShaderIndex)->ShaderGroupIndex;

		// Add callable shader group record, 
		// return shader group index for shader binding table build.
		auto AddCallableShaderGroup(ShaderIndex inCallableShaderIndex)->ShaderGroupIndex;
	};

private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;
	uint32_t m_shaderGroupCount = 0;
	RayTracingShaderGroupSet m_groupSet;
	void _SetShaderGroupHandlesData(ShaderTableType inType, const uint8_t* inData, size_t inSize, size_t inStride);

public:
	~RayTracingPipeline();

	void Init(const IRayTracingPipelineInitializer* pInitializer);

	void Uninit();

	// Size of total shader group stored in this pipeline;
	// This may not equal to number of shaders we add to this pipeline.
	uint32_t GetShaderGroupCount() const;

	VkPipeline GetVkPipeline() const;

	auto GetShaderGroupSet() const -> const RayTracingShaderGroupSet& { return m_groupSet; }

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};