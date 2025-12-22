#pragma once
#include "common.h"
#include "graphics_pipeline.h"
class Buffer;
class DescriptorSetLayout;
class DescriptorSet;
class VertexInputLayout;
class RenderPass;
class ComputePipeline;
class RayTracingPipeline;
class PipelineCache;

class IPipelineCacheInitializer
{
public:
	virtual void InitPipelineCache(PipelineCache* outPipelineCachePtr) const = 0;
};

class IComputePipelineInitializer
{
public:
	virtual void InitComputePipeline(ComputePipeline* pPipeline) const = 0;
};

class IRayTracingPipelineInitializer
{
public:
	virtual void InitRayTracingPipeline(RayTracingPipeline* pPipeline) const = 0;
};

class ComputePipeline
{
public:
	struct PipelineInput
	{
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

private:
	VkPipelineShaderStageCreateInfo m_shaderStageInfo{};
	std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
	PushConstantManager m_pushConstant;
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline vkPipeline = VK_NULL_HANDLE;

private:

public:
	~ComputePipeline();

	void AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo);
	void AddDescriptorSetLayout(const DescriptorSetLayout* pSetLayout);
	void AddDescriptorSetLayout(VkDescriptorSetLayout _vkLayout);
	void AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);

	// Optional, if cache is not empty, this will facilitate pipeline creation,
	// if not, cache will be filled with pipeline information for next creation
	void PreSetPipelineCache(VkPipelineCache inCache);

	void Init();
	void Uninit();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};

class RayTracingPipeline
{
private:
	// https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
	class ShaderBindingTable
	{
	private:
		std::unique_ptr<Buffer> m_uptrBuffer;
		uint32_t              m_uRayGenerationIndex = ~0;
		std::vector<uint32_t> m_uMissIndices;
		std::vector<uint32_t> m_uHitIndices;
		std::vector<uint32_t> m_uCallableIndices;

	private:
		ShaderBindingTable(const ShaderBindingTable& _other) = delete;
		ShaderBindingTable& operator=(const ShaderBindingTable& _other) = delete;

	public:
		ShaderBindingTable();

		void SetRayGenRecord(uint32_t index);
		void AddMissRecord(uint32_t index);
		void AddHitRecord(uint32_t index);
		void AddCallableRecord(uint32_t index);
		void Init(const RayTracingPipeline* pRayTracingPipeline);
		void Uninit();

		VkStridedDeviceAddressRegionKHR vkRayGenRegion{};
		VkStridedDeviceAddressRegionKHR vkMissRegion{};
		VkStridedDeviceAddressRegionKHR vkHitRegion{};
		VkStridedDeviceAddressRegionKHR vkCallRegion{};
	};

public:
	struct PipelineInput
	{
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		// for simplicity, i bind SBT in RT pipeline
		uint32_t uWidth = 0u;
		uint32_t uHeight = 0u;
		uint32_t uDepth = 1u;
	};

private:
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStageInfos;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderRecords;
	std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
	ShaderBindingTable m_SBT{};
	uint32_t m_maxRayRecursionDepth = 1u;
	PushConstantManager m_pushConstant;
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline vkPipeline = VK_NULL_HANDLE;

public:
	RayTracingPipeline();
	~RayTracingPipeline();

	uint32_t AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo);
	
	void AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout);
	
	void AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);
	
	// Set Ray Generation shader record in self shader binding table,
	// return the index of this record in all ShaderRecords,
	// i.e. index of element in pGroups of VkRayTracingPipelineCreateInfoKHR
	uint32_t SetRayGenerationShaderRecord(uint32_t rayGen); 
	
	// Add Hit shader record in self shader binding table for triangle-based geometries,
	// return the index of this record in all ShaderRecords,
	// i.e. index of element in pGroups of VkRayTracingPipelineCreateInfoKHR
	uint32_t AddTriangleHitShaderRecord(uint32_t closestHit, uint32_t anyHit = VK_SHADER_UNUSED_KHR);

	// Add Hit shader record in self shader binding table for AABB geometries,
	// return the index of this record in all ShaderRecords,
	// i.e. index of element in pGroups of VkRayTracingPipelineCreateInfoKHR
	uint32_t AddProceduralHitShaderRecord(uint32_t closestHit, uint32_t intersection, uint32_t anyHit = VK_SHADER_UNUSED_KHR);
	
	// Add Miss shader record in self shader binding table,
	// return the index of this record in all ShaderRecords,
	// i.e. index of element in pGroups of VkRayTracingPipelineCreateInfoKHR
	uint32_t AddMissShaderRecord(uint32_t miss);

	// Add Callable shader record in self shader binding table,
	// return the index of this record in all ShaderRecords,
	// i.e. index of element in pGroups of VkRayTracingPipelineCreateInfoKHR
	uint32_t AddCallableShaderRecord(uint32_t _callable);

	void SetMaxRecursion(uint32_t maxRecur);

	// Optional, if cache is not empty, this will facilitate pipeline creation,
	// if not, cache will be filled with pipeline information for next creation
	void PreSetPipelineCache(VkPipelineCache inCache);

	void Init();

	void Uninit();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};

class PipelineCache
{
private:
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
	bool m_fileCacheValid = false;

public:
	class Initializer : public IPipelineCacheInitializer
	{
	private:
		std::string& m_file;

		void _LoadBinary(const std::string& inPath, std::vector<uint8_t>& outData) const;

	public:
		virtual void InitPipelineCache(PipelineCache* outPipelineCachePtr) const override;

		PipelineCache::Initializer& Reset();

		PipelineCache::Initializer& CustomizeSourceFile(const std::string& inFilePath);
	};

public:
	~PipelineCache();

	// Init device object
	void Init(const IPipelineCacheInitializer* inInitPtr);

	// Return device handle
	VkPipelineCache GetVkPipelineCache() const;

	// Return whether the current cache is empty and will be written by pipeline creation.
	// Return false if we set source file before initialization, and we successfully load a valid pipeline cache from it
	bool IsEmptyCache() const;

	// Destroy device object
	void Uninit();

	static void SaveCacheToFile(VkPipelineCache inCacheToStore, const std::string& inDstFilePath);
};