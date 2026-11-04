#include "../ray_tracing_shader_program.h"
#include "buffer.h"
#include "device.h"
#include <algorithm>
#include <limits>

void RayTracingShaderGroupSet::Reset()
{
	m_groups.clear();
}

auto RayTracingShaderGroupSet::GetGroupData(std::string_view inName) const -> const GroupData&
{
	const auto found = m_groups.find(std::string(inName));
	CHECK_TRUE(found != m_groups.end(), "Unknown ray tracing shader group: " + std::string(inName));
	return found->second;
}

auto ShaderBindingTableCreateInfo::SetShaderGroupSet(const RayTracingShaderGroupSet& inGroupSet)
    -> ShaderBindingTableCreateInfo&
{
	m_groupSet = inGroupSet;
	return *this;
}

auto ShaderBindingTableCreateInfo::SetTableCapacity(
	ShaderTableType inType, uint32_t inRecordCount, size_t inMaxRecordDataSize)
	-> ShaderBindingTableCreateInfo&
{
	const size_t index = static_cast<size_t>(inType);
	CHECK_TRUE(index < m_capacities.size(), "Invalid shader table type!");
	m_capacities[index] = {inRecordCount, inMaxRecordDataSize};
	return *this;
}

RayTracingShaderProgram::RayTracingShaderProgram() = default;

RayTracingShaderProgram::~RayTracingShaderProgram()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE);
}

void RayTracingShaderProgram::Create(const RayTracingShaderProgramCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "RayTracingShaderProgramCreateInfo must be finalized before Create!");
	CHECK_TRUE(m_vkPipeline == VK_NULL_HANDLE && GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE,
	    "Ray tracing program already created!");
	try
	{
		_Create(inCreateInfo);
		CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE && GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE &&
		    m_shaderGroupCount != 0, "Incomplete ray tracing program!");
	}
	catch (...)
	{
		Destroy();
		throw;
	}
}

void RayTracingShaderProgram::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	m_pipelineLayout = {};
	for (auto& module : m_shaderModules)
		module->Destroy();
	m_shaderModules.clear();
	m_shaderGroupCount = 0;
	m_groupSet.Reset();
}

auto RayTracingShaderProgram::GetShaderGroupCount() const -> uint32_t
{
	return m_shaderGroupCount;
}

auto RayTracingShaderProgram::GetVkPipeline() const -> VkPipeline
{
	return m_vkPipeline;
}

ShaderBindingTable::ShaderBindingTable() = default;
ShaderBindingTable::~ShaderBindingTable()
{
	assert(m_uptrBuffer == nullptr);
}

void ShaderBindingTable::Create(const ShaderBindingTableCreateInfo& inCreateInfo)
{
	CHECK_TRUE(m_uptrBuffer == nullptr, "Shader binding table already created!");
	CHECK_TRUE(inCreateInfo.m_capacities[static_cast<size_t>(ShaderTableType::RayGeneration)].recordCount > 0,
	    "Shader binding table needs at least one ray generation record!");
	try
	{
		auto& device = MyDevice::GetInstance();
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties{};
		device.GetPhysicalDeviceRayTracingProperties(properties);
		CHECK_TRUE(properties.shaderGroupHandleSize > 0 && properties.shaderGroupHandleAlignment > 0 &&
		    properties.shaderGroupBaseAlignment > 0 && properties.maxShaderGroupStride > 0,
		    "Invalid ray tracing shader binding table properties!");

		auto alignUp = [](VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize
		{
			CHECK_TRUE(alignment != 0 && (alignment & (alignment - 1)) == 0,
			    "Shader binding table alignment must be a power of two!");
			CHECK_TRUE(value <= std::numeric_limits<VkDeviceSize>::max() - (alignment - 1),
			    "Shader binding table size overflow!");
			return (value + alignment - 1) & ~(alignment - 1);
		};

		m_groupSet = inCreateInfo.m_groupSet;
		m_shaderGroupHandleSize = properties.shaderGroupHandleSize;
		VkDeviceSize totalSize = 0;
		for (size_t index = 0; index < m_tables.size(); ++index)
		{
			const auto& capacity = inCreateInfo.m_capacities[index];
			auto& table = m_tables[index];
			table.recordCount = capacity.recordCount;
			table.maxRecordDataSize = capacity.maxRecordDataSize;
			if (table.recordCount == 0)
				continue;

			CHECK_TRUE(table.maxRecordDataSize <= std::numeric_limits<VkDeviceSize>::max() - m_shaderGroupHandleSize,
			    "Shader binding table record size overflow!");
			const VkDeviceSize recordSize = m_shaderGroupHandleSize + table.maxRecordDataSize;
			const VkDeviceSize strideAlignment = index == static_cast<size_t>(ShaderTableType::RayGeneration)
			    ? std::max(properties.shaderGroupHandleAlignment, properties.shaderGroupBaseAlignment)
			    : properties.shaderGroupHandleAlignment;
			table.stride = alignUp(recordSize, strideAlignment);
			CHECK_TRUE(table.stride <= properties.maxShaderGroupStride,
			    "Shader binding table stride exceeds maxShaderGroupStride!");
			CHECK_TRUE(table.recordCount <= std::numeric_limits<VkDeviceSize>::max() / table.stride,
			    "Shader binding table region size overflow!");
			totalSize = alignUp(totalSize, properties.shaderGroupBaseAlignment);
			table.offset = totalSize;
			const VkDeviceSize tableSize = static_cast<VkDeviceSize>(table.recordCount) * table.stride;
			CHECK_TRUE(totalSize <= std::numeric_limits<VkDeviceSize>::max() - tableSize,
			    "Shader binding table size overflow!");
			totalSize += tableSize;
		}
		CHECK_TRUE(totalSize > 0, "Shader binding table needs allocated records!");
		CHECK_TRUE(totalSize <= std::numeric_limits<size_t>::max(), "Shader binding table is too large!");
		m_hostData.assign(static_cast<size_t>(totalSize), 0u);

		BufferCreateInfo bufferInfo;
		bufferInfo.SetBufferSize(totalSize)
		    .SetBufferUsage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
		    .CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
		    .CustomizeAlignment(properties.shaderGroupBaseAlignment);
		m_uptrBuffer = std::make_unique<Buffer>();
		m_uptrBuffer->Create(bufferInfo);
		CHECK_TRUE(m_uptrBuffer->GetVkBuffer() != VK_NULL_HANDLE);
	}
	catch (...)
	{
		Destroy();
		throw;
	}
}

auto ShaderBindingTable::SetShaderTableBinding(ShaderTableType inType, uint32_t inBinding,
	std::string_view inGroupName, const uint8_t* inData, size_t inDataSize) -> ShaderBindingTable&
{
	const size_t index = static_cast<size_t>(inType);
	CHECK_TRUE(index < m_tables.size(), "Invalid shader table type!");
	CHECK_TRUE(m_uptrBuffer != nullptr, "Shader binding table must be created before setting records!");
	CHECK_TRUE(inDataSize == 0 || inData != nullptr, "Shader record payload is null!");
	const auto& table = m_tables[index];
	CHECK_TRUE(inBinding < table.recordCount, "Shader binding table record index out of range!");
	CHECK_TRUE(inDataSize <= table.maxRecordDataSize, "Shader record payload exceeds the configured capacity!");
	const auto& group = m_groupSet.GetGroupData(inGroupName);
	CHECK_TRUE(group.type == inType, "Shader group type does not match the target shader table!");
	CHECK_TRUE(group.handle.size() >= m_shaderGroupHandleSize && m_shaderGroupHandleSize <= table.stride,
	    "Shader group handle does not fit the configured record stride!");

	const VkDeviceSize offset = table.offset + static_cast<VkDeviceSize>(inBinding) * table.stride;
	auto* destination = m_hostData.data() + static_cast<size_t>(offset);
	std::fill_n(destination, static_cast<size_t>(table.stride), uint8_t{0});
	memcpy(destination, group.handle.data(), m_shaderGroupHandleSize);
	if (inDataSize > 0)
		memcpy(destination + m_shaderGroupHandleSize, inData, inDataSize);
	return *this;
}

auto ShaderBindingTable::SetShaderTableBinding(
	ShaderTableType inType, uint32_t inBinding, std::string_view inGroupName) -> ShaderBindingTable&
{
	return SetShaderTableBinding(inType, inBinding, inGroupName, nullptr, 0);
}

void ShaderBindingTable::Commit()
{
	CHECK_TRUE(m_uptrBuffer != nullptr, "Shader binding table must be created before committing!");
	CHECK_TRUE(!m_hostData.empty(), "Shader binding table has no allocated records!");
	m_uptrBuffer->CopyFromHost(m_hostData.data(), 0, m_hostData.size());
}

auto ShaderBindingTable::GetShaderTableRegion(ShaderTableType inType, uint32_t inOffset) const
	-> VkStridedDeviceAddressRegionKHR
{
	const size_t index = static_cast<size_t>(inType);
	CHECK_TRUE(index < m_tables.size(), "Invalid shader table type!");
	CHECK_TRUE(m_uptrBuffer != nullptr, "Shader binding table must be created before requesting regions!");
	const auto& table = m_tables[index];
	if (inType == ShaderTableType::RayGeneration)
		CHECK_TRUE(inOffset < table.recordCount, "Ray generation shader table offset out of range!");
	else
		CHECK_TRUE(inOffset == 0, "Shader table offsets are only supported for ray generation records!");

	if (table.recordCount == 0)
		return {};
	const VkDeviceSize offset = table.offset + static_cast<VkDeviceSize>(inOffset) * table.stride;
	const VkDeviceSize size = inType == ShaderTableType::RayGeneration
	    ? table.stride : static_cast<VkDeviceSize>(table.recordCount) * table.stride;
	return {m_uptrBuffer->GetDeviceAddress() + offset, table.stride, size};
}

void ShaderBindingTable::Destroy()
{
	if (m_uptrBuffer)
	{
		m_uptrBuffer->Destroy();
		m_uptrBuffer.reset();
	}
	m_groupSet.Reset();
	m_tables = {};
	m_hostData.clear();
	m_shaderGroupHandleSize = 0;
}

void RayTracingShaderProgram::_Create(const RayTracingShaderProgramCreateInfo& inCreateInfo)
{
	auto& device = MyDevice::GetInstance();
	VkRayTracingPipelineCreateInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{};
	CHECK_TRUE(vkCreateRayTracingPipelinesKHR && vkGetRayTracingShaderGroupHandlesKHR,
	    "Ray tracing pipeline extension is unavailable!");
	const auto& finalized = *inCreateInfo.m_finalized;
	device.GetPhysicalDeviceRayTracingProperties(rayTracingProperties);
	CHECK_TRUE(finalized.recursionDepth <= rayTracingProperties.maxRayRecursionDepth,
	    "Ray tracing recursion depth exceeds device limits!");
	std::vector<VkPipelineShaderStageCreateInfo> stages;
	ShaderDetail::CreateStages(finalized.shaders, m_shaderModules, stages);
	MyDevice::GetInstance().GetOrCreatePipelineLayout(m_pipelineLayout, finalized.layout);
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> records;
	for (const auto& group : finalized.groups)
	{
		records.push_back(group.record);
	}
	VkPipeline& pipelineToInit = m_vkPipeline;
	uint32_t& countToInit = m_shaderGroupCount;

	countToInit = static_cast<uint32_t>(finalized.groups.size());
	const VkPipelineLayout layoutToInit = GetLayout().GetVkPipelineLayout();

	pipelineInfo.pNext = nullptr;
	pipelineInfo.flags = 0;
	pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineInfo.pStages = stages.data();
	pipelineInfo.groupCount = static_cast<uint32_t>(finalized.groups.size());
	pipelineInfo.pGroups = records.data();
	pipelineInfo.maxPipelineRayRecursionDepth = finalized.recursionDepth;
	pipelineInfo.pLibraryInfo = nullptr;
	pipelineInfo.pLibraryInterface = nullptr;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = layoutToInit;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	pipelineToInit = device.CreateRayTracingPipeline(pipelineInfo, inCreateInfo.m_vkPipelineCache);

	// Fetch shader group handles and cache them in RayTracingShaderGroupSet.
	const uint32_t uHandleSizeHost = rayTracingProperties.shaderGroupHandleSize;
	CHECK_TRUE(uHandleSizeHost > 0 && rayTracingProperties.shaderGroupHandleAlignment > 0,
	    "Invalid ray tracing shader group handle properties!");
	const uint32_t uHandleSizeDevice = static_cast<uint32_t>((uHandleSizeHost + rayTracingProperties.shaderGroupHandleAlignment - 1) & ~(rayTracingProperties.shaderGroupHandleAlignment - 1));
	const size_t uGroupDataSize = static_cast<size_t>(countToInit) * static_cast<size_t>(uHandleSizeHost);
	std::vector<uint8_t> groupData(uGroupDataSize, 0u);

	VK_CHECK(
		vkGetRayTracingShaderGroupHandlesKHR(
			device.vkDevice,
			pipelineToInit,
			0,
			countToInit,
			uGroupDataSize,
			groupData.data()),
		"Failed to get group handle data!");

	m_groupSet.Reset();
	for (size_t i = 0; i < finalized.groups.size(); ++i)
	{
		const auto& group = finalized.groups[i];
		RayTracingShaderGroupSet::GroupData data{group.type, std::vector<uint8_t>(uHandleSizeDevice, 0u)};
		memcpy(data.handle.data(), groupData.data() + i * uHandleSizeHost, uHandleSizeHost);
		m_groupSet.m_groups.emplace(group.name, std::move(data));
	}
}

auto RayTracingShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_pipelineLayout;
}
