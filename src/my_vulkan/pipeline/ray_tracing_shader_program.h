#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include <string_view>

class Buffer;
class RayTracingShaderProgram;
class RayTracingShaderProgramCreateInfo;
class CommandBuffer;

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

class ShaderBindingTableCreateInfo final
{
private:
	struct TableCapacity
	{
		uint32_t recordCount = 0;
		size_t maxRecordDataSize = 0;
	};
	RayTracingShaderGroupSet m_groupSet;
	std::array<TableCapacity, static_cast<size_t>(ShaderTableType::Max)> m_capacities{};

	friend class ShaderBindingTable;

public:
	auto SetShaderGroupSet(const RayTracingShaderGroupSet& inGroupSet) -> ShaderBindingTableCreateInfo&;
	auto SetTableCapacity(ShaderTableType inType, uint32_t inRecordCount, size_t inMaxRecordDataSize)
	    -> ShaderBindingTableCreateInfo&;
};

// https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
class ShaderBindingTable
{
	friend struct CommandBufferTestProbe;
private:
	struct TableData
	{
		uint32_t recordCount = 0;
		size_t maxRecordDataSize = 0;
		VkDeviceSize offset = 0;
		VkDeviceSize stride = 0;
	};
	RayTracingShaderGroupSet m_groupSet;
	std::unique_ptr<Buffer> m_uptrBuffer;
	std::array<TableData, static_cast<size_t>(ShaderTableType::Max)> m_tables{};
	std::vector<uint8_t> m_hostData;
	size_t m_shaderGroupHandleSize = 0;

public:
	ShaderBindingTable();
	~ShaderBindingTable();
	ShaderBindingTable(const ShaderBindingTable& _other) = delete;
	auto operator=(const ShaderBindingTable& _other) -> ShaderBindingTable& = delete;

	void Create(const ShaderBindingTableCreateInfo& inCreateInfo);
	auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, std::string_view inGroupName,
	    const uint8_t* inData, size_t inDataSize) -> ShaderBindingTable&;
	auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, std::string_view inGroupName)
	    -> ShaderBindingTable&;
	template<typename T>
	auto SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding, std::string_view inGroupName,
	    const T& inData) -> ShaderBindingTable&
	{
		return SetShaderTableBinding(inType, inBinding, inGroupName,
		    reinterpret_cast<const uint8_t*>(&inData), sizeof(T));
	}
	void Commit();
	auto GetShaderTableRegion(ShaderTableType inType, uint32_t inOffset = 0) const -> VkStridedDeviceAddressRegionKHR;
	void Destroy();
};

class RayTracingShaderProgramCreateInfo final
{
	friend class RayTracingShaderProgram;
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
	auto _AddGeneralShaderGroup(std::string_view inName, const ShaderDetail::Request& inShader, ShaderTableType inType) -> RayTracingShaderProgramCreateInfo&;
	auto _AddHitShaderGroup(std::string_view inName, const std::optional<ShaderDetail::Request>& inClosestHit,
	    const ShaderDetail::Request* inIntersection, const std::optional<ShaderDetail::Request>& inAnyHit) -> RayTracingShaderProgramCreateInfo&;

public:
	// Optional, default: 1
	auto CustomizeMaxRecursionDepth(uint32_t inMaxDepth) -> RayTracingShaderProgramCreateInfo&;

	// Optional, default: VK_NULL_HANDLE
	auto CustomizePipelineCache(VkPipelineCache inCache) -> RayTracingShaderProgramCreateInfo&;

	auto AddRayGenerationShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
	auto AddMissShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
	auto AddCallableShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
	auto AddTriangleHitShaderGroup(std::string_view inName,
	    std::optional<std::string_view> inClosestHitSpirv = std::nullopt,
	    std::optional<std::string_view> inAnyHitSpirv = std::nullopt) -> RayTracingShaderProgramCreateInfo&;
	auto AddProceduralHitShaderGroup(std::string_view inName,
	    std::optional<std::string_view> inClosestHitSpirv,
	    std::string_view inIntersectionSpirv,
	    std::optional<std::string_view> inAnyHitSpirv = std::nullopt) -> RayTracingShaderProgramCreateInfo&;
};

class RayTracingShaderProgram
{
	friend class CommandBuffer;
private:
	void _Create(const RayTracingShaderProgramCreateInfo& inCreateInfo);

public:
	RayTracingShaderProgram();

private:
	std::vector<std::unique_ptr<ShaderModule>> m_shaderModules;
	PipelineLayout m_pipelineLayout;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;
	uint32_t m_shaderGroupCount = 0;
	RayTracingShaderGroupSet m_groupSet;

public:
	~RayTracingShaderProgram();

	void Create(const RayTracingShaderProgramCreateInfo& inCreateInfo);

	void Destroy();

	// Size of total shader group stored in this pipeline;
	// This may not equal to number of shaders we add to this pipeline.
	auto GetShaderGroupCount() const -> uint32_t;

	auto GetVkPipeline() const -> VkPipeline;

	auto GetLayout() const -> const PipelineLayout&;

	auto GetShaderGroupSet() const -> const RayTracingShaderGroupSet& { return m_groupSet; }
};
