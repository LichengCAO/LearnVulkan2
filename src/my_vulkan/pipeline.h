#pragma once
#include "common.h"

class Buffer;
class DescriptorSetLayout;
class DescriptorSet;
class VertexInputLayout;
class RenderPass;
class GraphicsPipeline;
class ComputePipeline;
class RayTracingPipeline;

class PushConstantManager
{
private:
	uint32_t m_currentSize = 0u;
	std::unordered_map<VkShaderStageFlags, std::pair<uint32_t, uint32_t>> m_mapRange;
	VkShaderStageFlags m_usedStages = 0;

private:
	PushConstantManager();

	static std::vector<VkShaderStageFlagBits> _GetBitsFromStageFlags(VkShaderStageFlags _flags);

public:
	// Add a push constant in the input stage, the stage is not allowed to assigned twice
	void AddConstantRange(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);

	// Push constant to the input stage
	void PushConstant(VkCommandBuffer _cmd, VkPipelineLayout _layout, VkShaderStageFlags _stage, const void* _data) const;

	// Get VkPushConstantRanges managed
	void GetVkPushConstantRanges(std::vector<VkPushConstantRange>& _outRanges) const;

	friend class GraphicsPipeline;
	friend class ComputePipeline;
	friend class RayTracingPipeline;
};

class GraphicsPipeline
{
public:
	// For pipelines that don't bind vertex buffers, e.g. pass through
	struct PipelineInput_Draw
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t vertexCount = 0u;

		std::vector<VkBuffer>						vertexBuffers;			// can be empty
		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
	};

	// For general graphics pipelines that use vertex buffers
	struct PipelineInput_DrawIndexed
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		std::vector<VkBuffer>	vertexBuffers;								// not sure, but I think order matters
		VkBuffer				indexBuffer = VK_NULL_HANDLE;
		VkIndexType				vkIndexType = VK_INDEX_TYPE_UINT32;
		uint32_t				indexCount = 0u;

		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
		std::optional<VkDeviceSize>					optIndexBufferOffset;
	};

	// For mesh shader pipelines
	struct PipelineInput_Mesh
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

private:
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStageInfos;
	std::vector<VkVertexInputBindingDescription> m_vertBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> m_vertAttributeDescriptions;
	std::vector<VkDynamicState> m_dynamicStates;
	VkPipelineViewportStateCreateInfo m_viewportStateInfo{};
	std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachmentStates;
	VkPipelineColorBlendStateCreateInfo m_colorBlendStateInfo{};
	std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
	VkPipelineDepthStencilStateCreateInfo m_depthStencilInfo{};
	const RenderPass* m_pRenderPass = nullptr;
	uint32_t m_subpass = 0;
	PushConstantManager m_pushConstant;
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

public:
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo{};
	VkPipelineRasterizationStateCreateInfo rasterizerStateInfo{};
	VkPipelineMultisampleStateCreateInfo multisampleStateInfo{};

private:
	void _InitCreateInfos();
	void _DoCommon(
		VkCommandBuffer _cmd,
		const VkExtent2D& _imageSize,
		const std::vector<VkDescriptorSet>& _vkDescriptorSets,
		const std::vector<uint32_t>& _dynamicOffsets,
		const std::vector<std::pair<VkShaderStageFlags, const void*>>& _pushConstants);

public:
	GraphicsPipeline();
	~GraphicsPipeline();

	void AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo);
	void AddVertexInputLayout(const VertexInputLayout* pVertLayout);
	void AddVertexInputLayout(
		const VkVertexInputBindingDescription& _bindingDescription, 
		const std::vector<VkVertexInputAttributeDescription>& _attributeDescriptions);
	void AddDescriptorSetLayout(const DescriptorSetLayout* pSetLayout);
	void AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout);
	void BindToSubpass(const RenderPass* pRenderPass, uint32_t subpass);
	void SetColorAttachmentAsAdd(int idx);
	void AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size);

	// Optional, if cache is not empty, this will facilitate pipeline creation,
	// if not, cache will be filled with pipeline information for next creation
	void PreSetPipelineCache(VkPipelineCache inCache);

	void Init();
	void Uninit();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput_DrawIndexed& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Mesh& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Draw& input);
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
	std::string m_filePath;
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
	VkPipelineCacheCreateFlags m_createFlags = 0;
	bool m_fileCacheValid = false;

private:
	void _LoadBinary(std::vector<uint8_t>& outData) const;

public:
	~PipelineCache();

	// Optional, use it when we try to load previously stored cache from file
	PipelineCache& PreSetSourceFilePath(const std::string& inFilePath);

	// Optional, do not know what's for for now
	PipelineCache& PreSetCreateFlags(VkPipelineCacheCreateFlags inFlags);

	// Init device object
	void Init();

	// Return device handle
	VkPipelineCache GetVkPipelineCache() const;

	// Return whether the current cache is empty and will be written by pipeline creation.
	// Return false if we set source file before intialization, and we successfully load a valid pipeline cache from it
	bool IsEmptyCache() const;

	// Destroy device object
	void Uninit();

	static void SaveCacheToFile(VkPipelineCache inCacheToStore, const std::string& inDstFilePath);
};