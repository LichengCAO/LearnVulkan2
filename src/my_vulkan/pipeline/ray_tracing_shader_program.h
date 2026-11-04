#pragma once
#include "common.h"
#include "pipeline_layout.h"
#include "private/shader.h"
#include <string_view>

class Buffer;
class RayTracingShaderProgram;
class RayTracingShaderProgramCreateInfo;
class CommandBuffer;

#include "shader_program_create_info.h"

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
	friend class MyDevice;
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

#include "shader_program_create_info.h"

class RayTracingShaderProgram
{
	friend class MyDevice;
	friend class CommandBuffer;
private:
	void _Create(const RayTracingShaderProgramCreateInfo& inCreateInfo);
	void _Destroy();

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
	RayTracingShaderProgram(const RayTracingShaderProgram&) = delete;
	auto operator=(const RayTracingShaderProgram&) -> RayTracingShaderProgram& = delete;
	RayTracingShaderProgram(RayTracingShaderProgram&&) = delete;
	auto operator=(RayTracingShaderProgram&&) -> RayTracingShaderProgram& = delete;

	// Size of total shader group stored in this pipeline;
	// This may not equal to number of shaders we add to this pipeline.
	auto GetShaderGroupCount() const -> uint32_t;

	auto GetVkPipeline() const -> VkPipeline;

	auto GetLayout() const -> const PipelineLayout&;

	auto GetShaderGroupSet() const -> const RayTracingShaderGroupSet& { return m_groupSet; }
};
