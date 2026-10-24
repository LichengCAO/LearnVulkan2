#include "ray_tracing_shader_program.h"
#include "buffer.h"
#include "device.h"
#include "push_constant_manager.h"
#include "command_buffer.h"
#include <algorithm>

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

RayTracingShaderProgram::RayTracingShaderProgram() = default;

RayTracingShaderProgram::~RayTracingShaderProgram()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE);
}

void RayTracingShaderProgram::Create(const IRayTracingShaderProgramInitializer* pInitializer)
{
	CHECK_TRUE(pInitializer, "No ray tracing program initializer!");
	CHECK_TRUE(m_vkPipeline == VK_NULL_HANDLE && GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE,
	    "Ray tracing program already created!");
	try
	{
		pInitializer->InitRayTracingShaderProgram(this);
		CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE && GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE &&
		    m_uptrPushConstant && m_shaderGroupCount != 0, "Incomplete ray tracing program!");
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
	m_shaderProgramLayout.Destroy();
	m_uptrPushConstant.reset();
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

void RayTracingShaderProgram::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
	CHECK_TRUE(input.pShaderBindingTable, "No shader binding table!");
	const auto& pSets = input.vkDescriptorSets;
	const ShaderBindingTable& inShaderBindingTable = *input.pShaderBindingTable;
	auto raygenRegion = inShaderBindingTable.GetShaderTableRegion(ShaderTableType::RayGeneration);
	auto missRegion = inShaderBindingTable.GetShaderTableRegion(ShaderTableType::Miss);
	auto hitRegion = inShaderBindingTable.GetShaderTableRegion(ShaderTableType::Hit);
	auto callRegion = inShaderBindingTable.GetShaderTableRegion(ShaderTableType::Callable);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_vkPipeline);

	if (pSets.size() > 0)
	{
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
			GetLayout().GetVkPipelineLayout(),
			0,
			static_cast<uint32_t>(pSets.size()),
			pSets.data(),
			static_cast<uint32_t>(input.optDynamicOffsets.size()),
			input.optDynamicOffsets.data());
	}

	for (const auto& _pushConst : input.pushConstants)
	{
		m_uptrPushConstant->PushConstant(commandBuffer, GetLayout().GetVkPipelineLayout(), _pushConst.first, _pushConst.second);
	}

	vkCmdTraceRaysKHR(
		commandBuffer,
		&raygenRegion,
		&missRegion,
		&hitRegion,
		&callRegion,
		input.uWidth,
		input.uHeight,
		input.uDepth);
}

ShaderBindingTable::ShaderBindingTable() = default;
ShaderBindingTable::~ShaderBindingTable()
{
	assert(m_uptrBuffer == nullptr);
}

void ShaderBindingTable::Create(const IShaderBindingTableInitializer* pInit)
{
	pInit->InitShaderBindingTable(this);

	CHECK_TRUE(m_uptrBuffer != nullptr);
	CHECK_TRUE(m_uptrBuffer->GetVkBuffer() != VK_NULL_HANDLE);
}

auto ShaderBindingTable::GetShaderTableRegion(ShaderTableType inType) const -> VkStridedDeviceAddressRegionKHR
{
	size_t idx = static_cast<size_t>(inType);
	CHECK_TRUE(idx < m_vkShaderTableRegions.size());
	return m_vkShaderTableRegions[idx];
}

void ShaderBindingTable::Destroy()
{
	if (m_uptrBuffer)
	{
		m_uptrBuffer->Destroy();
		m_uptrBuffer.reset();
	}
	m_recordStrides.clear();
	m_vkShaderTableRegions.clear();
}

// ShaderBindingTable::Descriptor implementation
ShaderBindingTable::Descriptor::Descriptor(RayTracingShaderGroupSet inGroupSet)
	: m_groupSet(std::move(inGroupSet))
{
	m_shaderRecordData.resize(static_cast<size_t>(ShaderTableType::Max));
	m_recordStrides.resize(static_cast<size_t>(ShaderTableType::Max), 0u);
}

auto ShaderBindingTable::Descriptor::SetShaderTableBinding(
	uint32_t inBinding,
	std::string_view inGroupName,
	const uint8_t* inData, 
	size_t inDataSize) -> Descriptor&
{
	const auto& group = m_groupSet.GetGroupData(inGroupName);
	const auto inType = group.type;
	const size_t idx = static_cast<size_t>(inType);
	const auto* handlePtr = group.handle.data();
	const size_t handleSize = group.handle.size();
	CHECK_TRUE(inDataSize == 0 || inData != nullptr, "Shader record payload is null!");
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
	MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
	const size_t handleAlignment = rtProps.shaderGroupHandleAlignment;
	const size_t baseAlignment = rtProps.shaderGroupBaseAlignment;
	auto alignUp = [](size_t v, size_t a) -> size_t { return (v + a - 1) & ~(a - 1); };
	CHECK_TRUE(handlePtr != nullptr && handleSize != 0);
	CHECK_TRUE(handleAlignment > 0);
	CHECK_TRUE(baseAlignment > 0);

	size_t totalSize = alignUp(handleSize + inDataSize, handleAlignment);
	if (inType == ShaderTableType::RayGeneration)
	{
		totalSize = alignUp(totalSize, baseAlignment);
	}
	size_t oldStride = m_recordStrides[idx];
	size_t newStride = std::max(oldStride, totalSize);
	size_t oldCount = (oldStride == 0) ? 0 : (m_shaderRecordData[idx].size() / oldStride);
	size_t newCount = std::max(oldCount, static_cast<size_t>(inBinding) + 1);

	if (oldStride == newStride && oldStride != 0)
	{
		size_t requiredSize = (static_cast<size_t>(inBinding) + 1) * oldStride;
		if (m_shaderRecordData[idx].size() < requiredSize)
		{
			m_shaderRecordData[idx].resize(requiredSize, 0u);
		}
		uint8_t* dst = m_shaderRecordData[idx].data() + static_cast<size_t>(inBinding) * oldStride;
		std::fill_n(dst, newStride, uint8_t{0});
		memcpy(dst, handlePtr, handleSize);
		if (inData != nullptr && inDataSize > 0)
		{
			memcpy(dst + handleSize, inData, inDataSize);
		}
	}
	else
	{
		std::vector<uint8_t> newBuffer(newCount * newStride, 0u);

		for (size_t i = 0; i < oldCount; ++i)
		{
			memcpy(newBuffer.data() + i * newStride, m_shaderRecordData[idx].data() + i * oldStride, oldStride);
		}

		uint8_t* dst = newBuffer.data() + static_cast<size_t>(inBinding) * newStride;
		std::fill_n(dst, newStride, uint8_t{0});
		memcpy(dst, handlePtr, handleSize);
		if (inData != nullptr && inDataSize > 0)
		{
			memcpy(dst + handleSize, inData, inDataSize);
		}

		m_shaderRecordData[idx].swap(newBuffer);
		m_recordStrides[idx] = newStride;
	}

	return *this;
}

auto ShaderBindingTable::Descriptor::SetShaderTableBinding(
	uint32_t inBinding,
	std::string_view inGroupName) -> Descriptor&
{
	return SetShaderTableBinding(inBinding, inGroupName, nullptr, 0);
}

auto ShaderBindingTable::Descriptor::UpdateShaderTableBinding(
	uint32_t inBinding,
	std::string_view inGroupName,
	const uint8_t* inData,
	size_t inDataSize) -> Descriptor&
{
	const auto& group = m_groupSet.GetGroupData(inGroupName);
	const auto inType = group.type;
	CHECK_TRUE(inDataSize == 0 || inData != nullptr, "Shader record payload is null!");

	const size_t idx = static_cast<size_t>(inType);
	CHECK_TRUE(idx < m_recordStrides.size());
	const size_t stride = m_recordStrides[idx];
	CHECK_TRUE(stride > 0, "Cannot update shader table binding before its stride is initialized.");

	const auto* handlePtr = group.handle.data();
	const size_t handleSize = group.handle.size();
	CHECK_TRUE(handlePtr != nullptr && handleSize != 0);

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
	MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
	const size_t handleAlignment = rtProps.shaderGroupHandleAlignment;
	const size_t baseAlignment = rtProps.shaderGroupBaseAlignment;
	auto alignUp = [](size_t v, size_t a) -> size_t { return (v + a - 1) & ~(a - 1); };

	CHECK_TRUE(handleAlignment > 0);
	CHECK_TRUE(baseAlignment > 0);

	size_t packedSize = alignUp(handleSize + inDataSize, handleAlignment);
	if (inType == ShaderTableType::RayGeneration)
	{
		packedSize = alignUp(packedSize, baseAlignment);
	}
	CHECK_TRUE(packedSize <= stride, "Updated shader table binding data exceeds the current stride.");

	return SetShaderTableBinding(inBinding, inGroupName, inData, inDataSize);
}

auto ShaderBindingTable::Descriptor::UpdateShaderTableBinding(
	uint32_t inBinding,
	std::string_view inGroupName) -> Descriptor&
{
	return UpdateShaderTableBinding(inBinding, inGroupName, nullptr, 0);
}

void ShaderBindingTable::Descriptor::GetShaderTableData(
	ShaderTableType inType, 
	std::vector<uint8_t>& outData, 
	size_t& outStride) const
{
	size_t idx = static_cast<size_t>(inType);
	outData = m_shaderRecordData[idx];
	outStride = m_recordStrides[idx];
}

void ShaderBindingTable::Descriptor::InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const
{
	auto& device = MyDevice::GetInstance();
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
	device.GetPhysicalDeviceRayTracingProperties(rtProps);
	auto alignUp = [](size_t v, size_t a) -> size_t { return (v + a - 1) & ~(a - 1); };

	// Compute total SBT buffer size directly from record data.
	size_t totalSize = 0;
	for (size_t t = 0; t < static_cast<size_t>(ShaderTableType::Max); ++t)
	{
		size_t alignedRecordSize = alignUp(m_shaderRecordData[t].size(), rtProps.shaderGroupBaseAlignment);
		totalSize += alignedRecordSize;
	}

	// Always refresh destination state.
	pShaderBindingTable->m_recordStrides = m_recordStrides;
	pShaderBindingTable->m_vkShaderTableRegions.resize(static_cast<size_t>(ShaderTableType::Max));

	if (pShaderBindingTable->m_uptrBuffer != nullptr)
	{
		pShaderBindingTable->m_uptrBuffer->Destroy();
		pShaderBindingTable->m_uptrBuffer.reset();
	}

	CHECK_TRUE(totalSize != 0); // We currently require at least one shader group, can be relaxed if we want to support empty SBT in the future.

	// Allocate device buffer only. Data upload is handled elsewhere.
	BufferCreateInfo createInfo{};
	createInfo.CustomizeAlignment(std::max(rtProps.shaderGroupHandleAlignment, rtProps.shaderGroupBaseAlignment));
	createInfo.SetBufferSize(totalSize);
	createInfo.SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR);
	pShaderBindingTable->m_uptrBuffer = std::make_unique<Buffer>();
	pShaderBindingTable->m_uptrBuffer->Create(&createInfo);

	VkDeviceAddress baseAddr = pShaderBindingTable->m_uptrBuffer->GetDeviceAddress();
	VkDeviceAddress curAddr = baseAddr;
	for (size_t t = 0; t < static_cast<size_t>(ShaderTableType::Max); ++t)
	{
		auto& region = pShaderBindingTable->m_vkShaderTableRegions[t];
		size_t alignedRecordSize = alignUp(m_shaderRecordData[t].size(), rtProps.shaderGroupBaseAlignment);
		if (alignedRecordSize == 0)
		{
			region.deviceAddress = 0;
			region.stride = 0;
			region.size = 0;
			continue;
		}
		region.deviceAddress = curAddr;
		region.stride = static_cast<VkDeviceSize>(m_recordStrides[t]);
		region.size = static_cast<VkDeviceSize>(alignedRecordSize);
		curAddr += region.size;
	}
}

void RayTracingShaderProgram::Builder::InitRayTracingShaderProgram(RayTracingShaderProgram* pPipeline) const
{
	auto& device = MyDevice::GetInstance();
	VkRayTracingPipelineCreateInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{};
	CHECK_TRUE(vkCreateRayTracingPipelinesKHR && vkGetRayTracingShaderGroupHandlesKHR,
	    "Ray tracing pipeline extension is unavailable!");
	CHECK_TRUE(!m_shaderGroups.empty(), "Ray tracing pipeline needs shader groups!");
	CHECK_TRUE(std::any_of(m_shaderGroups.begin(), m_shaderGroups.end(), [](const auto& group) -> bool
	    { return group.type == ShaderTableType::RayGeneration; }),
	    "Ray tracing pipeline needs a ray generation group!");
	auto resolved = ShaderDetail::Resolve(m_shaders, pPipeline->m_shaderModules);
	pPipeline->m_shaderProgramLayout.Create(resolved.reflection);
	const auto& stages = resolved.stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> records;
	for (const auto& group : m_shaderGroups)
	{
		auto record = group.record;
		for (auto* index : {&record.generalShader, &record.closestHitShader, &record.intersectionShader, &record.anyHitShader})
			if (*index != VK_SHADER_UNUSED_KHR)
				*index = resolved.indices.at(*index);
		records.push_back(record);
	}
	device.GetPhysicalDeviceRayTracingProperties(rayTracingProperties);
	CHECK_TRUE(m_maxRayRecursionDepth > 0 && m_maxRayRecursionDepth <= rayTracingProperties.maxRayRecursionDepth,
	    "Ray tracing recursion depth exceeds device limits!");
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device.vkPhysicalDevice, &properties);

	std::unique_ptr<PushConstantManager>& uptrToInit = pPipeline->m_uptrPushConstant;
	VkPipeline& pipelineToInit = pPipeline->m_vkPipeline;
	uint32_t& countToInit = pPipeline->m_shaderGroupCount;

	countToInit = static_cast<uint32_t>(m_shaderGroups.size());
	uptrToInit = std::make_unique<PushConstantManager>(properties.limits.maxPushConstantsSize);
	const auto& ranges = pPipeline->m_shaderProgramLayout.GetPushConstantRanges();
	for (const auto& pushConstant : ranges)
	{
		uptrToInit->AddConstantRange(pushConstant.stageFlags, pushConstant.offset, pushConstant.size);
	}
	const VkPipelineLayout layoutToInit = pPipeline->GetLayout().GetVkPipelineLayout();

	pipelineInfo.pNext = nullptr;
	pipelineInfo.flags = 0;
	pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineInfo.pStages = stages.data();
	pipelineInfo.groupCount = static_cast<uint32_t>(m_shaderGroups.size());
	pipelineInfo.pGroups = records.data();
	pipelineInfo.maxPipelineRayRecursionDepth = m_maxRayRecursionDepth;
	pipelineInfo.pLibraryInfo = nullptr;
	pipelineInfo.pLibraryInterface = nullptr;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = layoutToInit;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	pipelineToInit = device.CreateRayTracingPipeline(pipelineInfo, m_vkPipelineCache);

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

	pPipeline->m_groupSet.Reset();
	for (size_t i = 0; i < m_shaderGroups.size(); ++i)
	{
		const auto& group = m_shaderGroups[i];
		RayTracingShaderGroupSet::GroupData data{group.type, std::vector<uint8_t>(uHandleSizeDevice, 0u)};
		memcpy(data.handle.data(), groupData.data() + i * uHandleSizeHost, uHandleSizeHost);
		pPipeline->m_groupSet.m_groups.emplace(group.name, std::move(data));
	}
}

auto RayTracingShaderProgram::Builder::CustomizeMaxRecursionDepth(uint32_t inMaxDepth) -> Builder&
{
	m_maxRayRecursionDepth = inMaxDepth;

	return *this;
}

auto RayTracingShaderProgram::Builder::CustomizePipelineCache(VkPipelineCache inCache) -> Builder&
{
	m_vkPipelineCache = inCache;

	return *this;
}

auto RayTracingShaderProgram::GetLayout() const -> const PipelineLayout&
{
	return m_shaderProgramLayout.GetLayout();
}

auto RayTracingShaderProgram::GetNameToSetBinding() const
    -> const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>&
{
	return m_shaderProgramLayout.GetNameToSetBinding();
}

void RayTracingShaderProgram::Builder::_ValidateGroupName(std::string_view inName) const
{
	CHECK_TRUE(!inName.empty(), "Ray tracing shader group name is empty!");
	CHECK_TRUE(std::none_of(m_shaderGroups.begin(), m_shaderGroups.end(), [&](const auto& group) -> bool
	    { return group.name == inName; }), "Duplicate ray tracing shader group name: " + std::string(inName));
}

auto RayTracingShaderProgram::Builder::_RegisterShader(const ShaderDetail::Request& shader) -> uint32_t
{
	const auto index = static_cast<uint32_t>(m_shaders.size());
	m_shaders.push_back(shader);
	return index;
}

auto RayTracingShaderProgram::Builder::_AddGeneralShaderGroup(std::string_view inName,
    const ShaderDetail::Request& inInfo, ShaderTableType inType) -> Builder&
{
	_ValidateGroupName(inName);
	const auto previousShaderCount = m_shaders.size();
	try
	{
		VkRayTracingShaderGroupCreateInfoKHR record{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
		record.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		record.generalShader = _RegisterShader(inInfo);
		record.closestHitShader = VK_SHADER_UNUSED_KHR;
		record.anyHitShader = VK_SHADER_UNUSED_KHR;
		record.intersectionShader = VK_SHADER_UNUSED_KHR;
		m_shaderGroups.push_back({std::string(inName), inType, record});
	}
	catch (...)
	{
		m_shaders.resize(previousShaderCount);
		throw;
	}
	return *this;
}

auto RayTracingShaderProgram::Builder::_AddHitShaderGroup(std::string_view inName,
    const std::optional<ShaderDetail::Request>& inClosestHit, const ShaderDetail::Request* inIntersection,
    const std::optional<ShaderDetail::Request>& inAnyHit) -> Builder&
{
	_ValidateGroupName(inName);
	const auto previousShaderCount = m_shaders.size();
	try
	{
		VkRayTracingShaderGroupCreateInfoKHR record{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
		record.type = inIntersection ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR :
		    VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		record.generalShader = VK_SHADER_UNUSED_KHR;
		record.closestHitShader = inClosestHit ? _RegisterShader(*inClosestHit) : VK_SHADER_UNUSED_KHR;
		record.intersectionShader = inIntersection ? _RegisterShader(*inIntersection) : VK_SHADER_UNUSED_KHR;
		record.anyHitShader = inAnyHit ? _RegisterShader(*inAnyHit) : VK_SHADER_UNUSED_KHR;
		m_shaderGroups.push_back({std::string(inName), ShaderTableType::Hit, record});
	}
	catch (...)
	{
		m_shaders.resize(previousShaderCount);
		throw;
	}
	return *this;
}

auto RayTracingShaderProgram::Builder::AddRayGenerationShaderGroup(
    std::string_view name, std::string_view file, std::optional<std::string_view> entry) -> Builder&
{
	return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_RAYGEN_BIT_KHR, entry), ShaderTableType::RayGeneration);
}

auto RayTracingShaderProgram::Builder::AddMissShaderGroup(
    std::string_view name, std::string_view file, std::optional<std::string_view> entry) -> Builder&
{
	return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_MISS_BIT_KHR, entry), ShaderTableType::Miss);
}

auto RayTracingShaderProgram::Builder::AddCallableShaderGroup(
    std::string_view name, std::string_view file, std::optional<std::string_view> entry) -> Builder&
{
	return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_CALLABLE_BIT_KHR, entry), ShaderTableType::Callable);
}

auto RayTracingShaderProgram::Builder::AddTriangleHitShaderGroup(std::string_view name,
    std::optional<std::string_view> closestFile, std::optional<std::string_view> closestEntry,
    std::optional<std::string_view> anyFile, std::optional<std::string_view> anyEntry) -> Builder&
{
	const auto closest = ShaderDetail::MakeOptionalRequest(closestFile, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestEntry);
	const auto any = ShaderDetail::MakeOptionalRequest(anyFile, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, anyEntry);
	return _AddHitShaderGroup(name, closest, nullptr, any);
}

auto RayTracingShaderProgram::Builder::AddProceduralHitShaderGroup(std::string_view name,
    std::optional<std::string_view> closestFile, std::optional<std::string_view> closestEntry,
    std::string_view intersectionFile, std::optional<std::string_view> intersectionEntry,
    std::optional<std::string_view> anyFile, std::optional<std::string_view> anyEntry) -> Builder&
{
	const auto closest = ShaderDetail::MakeOptionalRequest(closestFile, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closestEntry);
	const auto intersection = ShaderDetail::MakeRequest(intersectionFile, VK_SHADER_STAGE_INTERSECTION_BIT_KHR, intersectionEntry);
	const auto any = ShaderDetail::MakeOptionalRequest(anyFile, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, anyEntry);
	return _AddHitShaderGroup(name, closest, &intersection, any);
}
