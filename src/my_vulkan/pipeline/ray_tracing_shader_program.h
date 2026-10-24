#pragma once
#include "common.h"
#include "shader_program_layout.h"
#include "shader.h"
#include <string_view>

class Buffer;
class RayTracingShaderProgram;
class PushConstantManager;
class ShaderBindingTable;
class CommandBuffer;

class IRayTracingShaderProgramInitializer
{
public:
	virtual void InitRayTracingShaderProgram(RayTracingShaderProgram* pPipeline) const = 0;
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
public:
	struct GroupData
	{
		ShaderTableType type;
		std::vector<uint8_t> handle;
	};

private:
	std::unordered_map<std::string, GroupData> m_groups;
	friend class RayTracingShaderProgram;

public:
	void Reset();
	auto GetGroupData(std::string_view inName) const -> const GroupData&;
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
		auto SetShaderTableBinding(uint32_t inBinding, std::string_view inGroupName, const T* inData) -> Descriptor&
		{
			return SetShaderTableBinding(inBinding, inGroupName, reinterpret_cast<const uint8_t*>(inData), sizeof(T));
		}
		auto SetShaderTableBinding(uint32_t inBinding, std::string_view inGroupName, const uint8_t* inData, size_t inDataSize) -> Descriptor&;
		auto SetShaderTableBinding(uint32_t inBinding, std::string_view inGroupName) -> Descriptor&;

		template<typename T>
		auto UpdateShaderTableBinding(uint32_t inBinding, std::string_view inGroupName, const T* inData) -> Descriptor&
		{
			return UpdateShaderTableBinding(inBinding, inGroupName, reinterpret_cast<const uint8_t*>(inData), sizeof(T));
		}
		auto UpdateShaderTableBinding(uint32_t inBinding, std::string_view inGroupName, const uint8_t* inData, size_t inDataSize) -> Descriptor&;
		auto UpdateShaderTableBinding(uint32_t inBinding, std::string_view inGroupName) -> Descriptor&;

		void GetShaderTableData(ShaderTableType inType, std::vector<uint8_t>& outData, size_t& outStride) const;
		virtual void InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const override;
	};

public:
	ShaderBindingTable();
	~ShaderBindingTable();
	ShaderBindingTable(const ShaderBindingTable& _other) = delete;
	auto operator=(const ShaderBindingTable& _other) -> ShaderBindingTable& = delete;

	void Create(const IShaderBindingTableInitializer* pInit);
	auto GetShaderTableRegion(ShaderTableType inType) const -> VkStridedDeviceAddressRegionKHR;
	void Destroy();
};

class RayTracingShaderProgram
{
	friend class CommandBuffer;
public:
	RayTracingShaderProgram();

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

	class Builder : public IRayTracingShaderProgramInitializer
	{
	private:
		struct ShaderGroup
		{
			std::string name;
			ShaderTableType type;
			VkRayTracingShaderGroupCreateInfoKHR record;
		};
		std::vector<ShaderDetail::Request> m_shaders;
		std::vector<ShaderGroup> m_shaderGroups;
		uint32_t m_maxRayRecursionDepth = 1u;
		VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

		void _ValidateGroupName(std::string_view inName) const;
		auto _RegisterShader(const ShaderDetail::Request& inShader) -> uint32_t;
		auto _AddGeneralShaderGroup(std::string_view inName, const ShaderDetail::Request& inShader, ShaderTableType inType) -> Builder&;
		auto _AddHitShaderGroup(std::string_view inName, const std::optional<ShaderDetail::Request>& inClosestHit,
		    const ShaderDetail::Request* inIntersection, const std::optional<ShaderDetail::Request>& inAnyHit) -> Builder&;

	public:
		virtual void InitRayTracingShaderProgram(RayTracingShaderProgram* pPipeline) const override;

		// Optional, default: 1
		auto CustomizeMaxRecursionDepth(uint32_t inMaxDepth) -> Builder&;

		// Optional, default: VK_NULL_HANDLE
		auto CustomizePipelineCache(VkPipelineCache inCache) -> Builder&;

		auto AddRayGenerationShaderGroup(std::string_view inName, std::string_view inSpirv,
		    std::optional<std::string_view> inEntry = std::nullopt) -> Builder&;
		auto AddMissShaderGroup(std::string_view inName, std::string_view inSpirv,
		    std::optional<std::string_view> inEntry = std::nullopt) -> Builder&;
		auto AddCallableShaderGroup(std::string_view inName, std::string_view inSpirv,
		    std::optional<std::string_view> inEntry = std::nullopt) -> Builder&;
		auto AddTriangleHitShaderGroup(std::string_view inName,
		    std::optional<std::string_view> inClosestHitSpirv = std::nullopt,
		    std::optional<std::string_view> inClosestHitEntry = std::nullopt,
		    std::optional<std::string_view> inAnyHitSpirv = std::nullopt,
		    std::optional<std::string_view> inAnyHitEntry = std::nullopt) -> Builder&;
		auto AddProceduralHitShaderGroup(std::string_view inName,
		    std::optional<std::string_view> inClosestHitSpirv,
		    std::optional<std::string_view> inClosestHitEntry,
		    std::string_view inIntersectionSpirv,
		    std::optional<std::string_view> inIntersectionEntry = std::nullopt,
		    std::optional<std::string_view> inAnyHitSpirv = std::nullopt,
		    std::optional<std::string_view> inAnyHitEntry = std::nullopt) -> Builder&;
	};

private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	ShaderProgramLayout m_shaderProgramLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;
	uint32_t m_shaderGroupCount = 0;
	RayTracingShaderGroupSet m_groupSet;

public:
	~RayTracingShaderProgram();

	void Create(const IRayTracingShaderProgramInitializer* pInitializer);

	void Destroy();

	// Size of total shader group stored in this pipeline;
	// This may not equal to number of shaders we add to this pipeline.
	auto GetShaderGroupCount() const -> uint32_t;

	auto GetVkPipeline() const -> VkPipeline;
	auto GetNameToSetBinding() const
	    -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&;

	auto GetLayout() const -> const PipelineLayout&;

	auto GetShaderGroupSet() const -> const RayTracingShaderGroupSet& { return m_groupSet; }

	void Do(VkCommandBuffer commandBuffer, const PipelineInput& input);
};
