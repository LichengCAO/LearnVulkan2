#include "ray_tracing_pipeline.h"
#include "buffer.h"
#include "device.h"
#include "push_constant_manager.h"
#include "command_buffer.h"

// RayTracingShaderGroupSet implementation
RayTracingShaderGroupSet::RayTracingShaderGroupSet()
{
	m_shaderGroupData.resize(static_cast<size_t>(ShaderTableType::Max));
	m_shaderGroupStride.resize(static_cast<size_t>(ShaderTableType::Max), 0u);
}

void RayTracingShaderGroupSet::Reset()
{
	for (auto& data : m_shaderGroupData)
	{
		data.clear();
	}
	for (auto& stride : m_shaderGroupStride)
	{
		stride = 0;
	}
}

void RayTracingShaderGroupSet::_SetShaderGroupHandlesData(ShaderTableType inShaderTableType, const uint8_t* inData, size_t inSize, size_t inStride)
{
	size_t idx = static_cast<size_t>(inShaderTableType);
	CHECK_TRUE(idx < m_shaderGroupData.size());
	CHECK_TRUE(idx < m_shaderGroupStride.size());
	CHECK_TRUE(inStride > 0);
	CHECK_TRUE((inSize % inStride) == 0);
	if (inSize > 0)
	{
		CHECK_TRUE(inData != nullptr);
		m_shaderGroupData[idx].assign(inData, inData + inSize);
	}
	else
	{
		m_shaderGroupData[idx].clear();
	}
	m_shaderGroupStride[idx] = inStride;
}

void RayTracingShaderGroupSet::GetShaderGroupHandleData(ShaderTableType inShaderTableType, ShaderGroupIndex inGroupIndex, const uint8_t*& outData, size_t& outSize) const
{
	size_t idx = static_cast<size_t>(inShaderTableType);
	CHECK_TRUE(idx < m_shaderGroupData.size());

	const auto& buffer = m_shaderGroupData[idx];
	size_t stride = (idx < m_shaderGroupStride.size()) ? m_shaderGroupStride[idx] : 0;
	CHECK_TRUE(!(stride == 0 || buffer.empty()));

	size_t count = buffer.size() / stride;
	CHECK_TRUE(inGroupIndex < count);

	outData = buffer.data() + static_cast<size_t>(inGroupIndex) * stride;
	outSize = stride;
}

void RayTracingShaderGroupSet::GetShaderGroupHandlesData(ShaderTableType inShaderTableType, const uint8_t*& outData, size_t& outSize, size_t& outStride) const
{
	size_t idx = static_cast<size_t>(inShaderTableType);
	CHECK_TRUE(idx < m_shaderGroupData.size());
	CHECK_TRUE(idx < m_shaderGroupStride.size());
	CHECK_TRUE(!m_shaderGroupData[idx].empty());

	const auto& buffer = m_shaderGroupData[idx];
	outData = buffer.data();
	outSize = buffer.size();
	outStride = m_shaderGroupStride[idx];
}

RayTracingPipeline::~RayTracingPipeline()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void RayTracingPipeline::Create(const IRayTracingPipelineInitializer* pInitializer)
{
	pInitializer->InitRayTracingPipeline(this);

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
	CHECK_TRUE(m_shaderGroupCount != 0);
}

void RayTracingPipeline::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	m_vkPipelineLayout = VK_NULL_HANDLE;
	m_uptrPushConstant.reset();
}

uint32_t RayTracingPipeline::GetShaderGroupCount() const
{
	return m_shaderGroupCount;
}

VkPipeline RayTracingPipeline::GetVkPipeline() const
{
	return m_vkPipeline;
}

void RayTracingPipeline::_SetShaderGroupHandlesData(ShaderTableType inType, const uint8_t* inData, size_t inSize, size_t inStride)
{
	m_groupSet._SetShaderGroupHandlesData(inType, inData, inSize, inStride);
}

void RayTracingPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
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
			m_vkPipelineLayout,
			0,
			static_cast<uint32_t>(pSets.size()),
			pSets.data(),
			static_cast<uint32_t>(input.optDynamicOffsets.size()),
			input.optDynamicOffsets.data());
	}

	for (const auto& _pushConst : input.pushConstants)
	{
		m_uptrPushConstant->PushConstant(commandBuffer, m_vkPipelineLayout, _pushConst.first, _pushConst.second);
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
	ShaderTableType inType, 
	uint32_t inBinding, 
	ShaderGroupIndex inGroupIndex, 
	const uint8_t* inData, 
	size_t inDataSize) -> Descriptor&
{
	size_t idx = static_cast<size_t>(inType);
	const uint8_t* handlePtr = nullptr;
	size_t handleSize = 0;
	static const size_t sShaderGroupHandleAlignment = []() -> size_t
	{
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
		MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
		return static_cast<size_t>(rtProps.shaderGroupHandleAlignment);
	}();
	static const size_t sShaderGroupBaseAlignment = []() -> size_t
	{
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
		MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
		return static_cast<size_t>(rtProps.shaderGroupBaseAlignment);
	}();
	auto alignUp = [](size_t v, size_t a) { return (v + a - 1) & ~(a - 1); };
	m_groupSet.GetShaderGroupHandleData(inType, inGroupIndex, handlePtr, handleSize);
	CHECK_TRUE(handlePtr != nullptr && handleSize != 0);
	CHECK_TRUE(sShaderGroupHandleAlignment > 0);
	CHECK_TRUE(sShaderGroupBaseAlignment > 0);

	size_t totalSize = alignUp(handleSize + inDataSize, sShaderGroupHandleAlignment);
	if (inType == ShaderTableType::RayGeneration)
	{
		totalSize = alignUp(totalSize, sShaderGroupBaseAlignment);
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
	ShaderTableType inType, 
	uint32_t inBinding, 
	ShaderGroupIndex inGroupIndex) -> Descriptor&
{
	return SetShaderTableBinding(inType, inBinding, inGroupIndex, nullptr, 0);
}

auto ShaderBindingTable::Descriptor::UpdateShaderTableBinding(
	ShaderTableType inType,
	uint32_t inBinding,
	ShaderGroupIndex inGroupIndex,
	const uint8_t* inData,
	size_t inDataSize) -> Descriptor&
{
	CHECK_TRUE(inType >= ShaderTableType::RayGeneration && inType <= ShaderTableType::Callable);

	const size_t idx = static_cast<size_t>(inType);
	CHECK_TRUE(idx < m_recordStrides.size());
	const size_t stride = m_recordStrides[idx];
	CHECK_TRUE(stride > 0, "Cannot update shader table binding before its stride is initialized.");

	const uint8_t* handlePtr = nullptr;
	size_t handleSize = 0;
	m_groupSet.GetShaderGroupHandleData(inType, inGroupIndex, handlePtr, handleSize);
	CHECK_TRUE(handlePtr != nullptr && handleSize != 0);

	static const size_t sShaderGroupHandleAlignment = []() -> size_t
	{
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
		MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
		return static_cast<size_t>(rtProps.shaderGroupHandleAlignment);
	}();
	static const size_t sShaderGroupBaseAlignment = []() -> size_t
	{
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{};
		MyDevice::GetInstance().GetPhysicalDeviceRayTracingProperties(rtProps);
		return static_cast<size_t>(rtProps.shaderGroupBaseAlignment);
	}();
	auto alignUp = [](size_t v, size_t a) { return (v + a - 1) & ~(a - 1); };

	CHECK_TRUE(sShaderGroupHandleAlignment > 0);
	CHECK_TRUE(sShaderGroupBaseAlignment > 0);

	size_t packedSize = alignUp(handleSize + inDataSize, sShaderGroupHandleAlignment);
	if (inType == ShaderTableType::RayGeneration)
	{
		packedSize = alignUp(packedSize, sShaderGroupBaseAlignment);
	}
	CHECK_TRUE(packedSize <= stride, "Updated shader table binding data exceeds the current stride.");

	return SetShaderTableBinding(inType, inBinding, inGroupIndex, inData, inDataSize);
}

auto ShaderBindingTable::Descriptor::UpdateShaderTableBinding(
	ShaderTableType inType,
	uint32_t inBinding,
	ShaderGroupIndex inGroupIndex) -> Descriptor&
{
	return UpdateShaderTableBinding(inType, inBinding, inGroupIndex, nullptr, 0);
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
	auto alignUp = [&](size_t v, size_t a) { return (v + a - 1) & ~(a - 1); };

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

void RayTracingPipeline::Builder::InitRayTracingPipeline(RayTracingPipeline* pPipeline) const
{
	auto& device = MyDevice::GetInstance();
	VkRayTracingPipelineCreateInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{};
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE, "Ray tracing pipeline needs a pipeline layout!");

	std::unique_ptr<PushConstantManager>& uptrToInit = pPipeline->m_uptrPushConstant;
	VkPipeline& pipelineToInit = pPipeline->m_vkPipeline;
	VkPipelineLayout& layoutToInit = pPipeline->m_vkPipelineLayout;
	uint32_t& countToInit = pPipeline->m_shaderGroupCount;

	countToInit = static_cast<uint32_t>(m_shaderRecords.size());
	uptrToInit = std::make_unique<PushConstantManager>();
	for (const auto& pushConstant : m_pushConstants)
	{
		uptrToInit->AddConstantRange(pushConstant.stageFlags, pushConstant.offset, pushConstant.size);
	}
	layoutToInit = m_vkPipelineLayout;

	pipelineInfo.pNext = nullptr;
	pipelineInfo.flags = 0;
	pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStageInfos.size());
	pipelineInfo.pStages = m_shaderStageInfos.data();
	pipelineInfo.groupCount = static_cast<uint32_t>(m_shaderRecords.size());
	pipelineInfo.pGroups = m_shaderRecords.data();
	pipelineInfo.maxPipelineRayRecursionDepth = m_maxRayRecursionDepth;
	pipelineInfo.pLibraryInfo = nullptr;
	pipelineInfo.pLibraryInterface = nullptr;
	pipelineInfo.pDynamicState = nullptr;
	pipelineInfo.layout = layoutToInit;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	pipelineToInit = device.CreateRayTracingPipeline(pipelineInfo);

	// Fetch shader group handles and cache them in RayTracingShaderGroupSet.
	device.GetPhysicalDeviceRayTracingProperties(rayTracingProperties);
	const uint32_t uHandleSizeHost = rayTracingProperties.shaderGroupHandleSize;
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

	std::vector<uint32_t> raygenIndices;
	std::vector<uint32_t> missIndices;
	std::vector<uint32_t> hitIndices;
	std::vector<uint32_t> callableIndices;
	raygenIndices.reserve(countToInit);
	missIndices.reserve(countToInit);
	hitIndices.reserve(countToInit);
	callableIndices.reserve(countToInit);

	for (uint32_t i = 0; i < countToInit; ++i)
	{
		const auto& shaderGroup = m_shaderRecords[i];
		if (shaderGroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
			|| shaderGroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR)
		{
			hitIndices.push_back(i);
			continue;
		}

		CHECK_TRUE(shaderGroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR);
		CHECK_TRUE(shaderGroup.generalShader != VK_SHADER_UNUSED_KHR);
		CHECK_TRUE(shaderGroup.generalShader < m_shaderStageInfos.size());

		VkShaderStageFlagBits stage = static_cast<VkShaderStageFlagBits>(m_shaderStageInfos[shaderGroup.generalShader].stage);
		switch (stage)
		{
		case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
			raygenIndices.push_back(i);
			break;
		case VK_SHADER_STAGE_MISS_BIT_KHR:
			missIndices.push_back(i);
			break;
		case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
			callableIndices.push_back(i);
			break;
		default:
			CHECK_TRUE(false, "Unsupported shader stage for general ray tracing shader group.");
			break;
		}
	}

	auto fillGroupSetData = [&](ShaderTableType inType, const std::vector<uint32_t>& inIndices)
	{
		if (inIndices.empty())
		{
			return;
		}

		std::vector<uint8_t> packedData(static_cast<size_t>(inIndices.size()) * static_cast<size_t>(uHandleSizeDevice), 0u);
		for (size_t i = 0; i < inIndices.size(); ++i)
		{
			const uint32_t groupIndex = inIndices[i];
			const uint8_t* src = groupData.data() + static_cast<size_t>(groupIndex) * static_cast<size_t>(uHandleSizeHost);
			uint8_t* dst = packedData.data() + i * static_cast<size_t>(uHandleSizeDevice);
			memcpy(dst, src, static_cast<size_t>(uHandleSizeHost));
		}

		pPipeline->_SetShaderGroupHandlesData(inType, packedData.data(), packedData.size(), uHandleSizeDevice);
	};

	pPipeline->m_groupSet.Reset();
	fillGroupSetData(ShaderTableType::RayGeneration, raygenIndices);
	fillGroupSetData(ShaderTableType::Miss, missIndices);
	fillGroupSetData(ShaderTableType::Hit, hitIndices);
	fillGroupSetData(ShaderTableType::Callable, callableIndices);
}

RayTracingPipeline::Builder& RayTracingPipeline::Builder::CustomizeMaxRecursionDepth(uint32_t inMaxDepth)
{
	m_maxRayRecursionDepth = inMaxDepth;

	return *this;
}

RayTracingPipeline::Builder& RayTracingPipeline::Builder::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;

	return *this;
}

RayTracingPipeline::Builder& RayTracingPipeline::Builder::SetPipelineLayout(VkPipelineLayout inPipelineLayout)
{
	CHECK_TRUE(inPipelineLayout != VK_NULL_HANDLE, "Invalid ray tracing pipeline layout!");

	m_vkPipelineLayout = inPipelineLayout;

	return *this;
}

RayTracingPipeline::Builder& RayTracingPipeline::Builder::AddPushConstant(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	VkPushConstantRange pushConstant{};

	pushConstant.stageFlags = _stages;
	pushConstant.offset = _offset;
	pushConstant.size = _size;

	m_pushConstants.push_back(pushConstant);

	return *this;
}

uint32_t RayTracingPipeline::Builder::AddShader(const VkPipelineShaderStageCreateInfo& shaderInfo)
{
	uint32_t uIndex = static_cast<uint32_t>(m_shaderStageInfos.size());
	m_shaderStageInfos.push_back(shaderInfo);
	return uIndex;
}

uint32_t RayTracingPipeline::Builder::AddRayGenerationShaderGroup(uint32_t inRayGenShaderIndex)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = inRayGenShaderIndex;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);
	return uRet;
}

uint32_t RayTracingPipeline::Builder::AddTriangleHitShaderGroup(uint32_t inClosestHitShaderIndex, uint32_t inAnyHitShaderIndex)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	shaderRecord.generalShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.closestHitShader = inClosestHitShaderIndex;
	shaderRecord.anyHitShader = inAnyHitShaderIndex;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);

	return uRet;
}

uint32_t RayTracingPipeline::Builder::AddProceduralHitShaderGroup(
	uint32_t inClosestHitShaderIndex, 
	uint32_t inIntersectionShaderIndex, 
	uint32_t inAnyHitShaderIndex)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
	shaderRecord.generalShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.closestHitShader = inClosestHitShaderIndex;
	shaderRecord.anyHitShader = inAnyHitShaderIndex;
	shaderRecord.intersectionShader = inIntersectionShaderIndex;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);

	return uRet;
}

uint32_t RayTracingPipeline::Builder::AddMissShaderGroup(uint32_t inMissShaderIndex)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = inMissShaderIndex;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);

	return uRet;
}

uint32_t RayTracingPipeline::Builder::AddCallableShaderGroup(uint32_t inCallableShaderIndex)
{
	VkRayTracingShaderGroupCreateInfoKHR shaderRecord{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	uint32_t uRet = static_cast<uint32_t>(m_shaderRecords.size());

	shaderRecord.pNext = nullptr;
	shaderRecord.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shaderRecord.generalShader = inCallableShaderIndex;
	shaderRecord.closestHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.anyHitShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderRecord.pShaderGroupCaptureReplayHandle = nullptr;

	m_shaderRecords.push_back(shaderRecord);

	return uRet;
}
