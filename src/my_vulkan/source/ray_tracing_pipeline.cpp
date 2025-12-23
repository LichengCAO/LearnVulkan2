#include "ray_tracing_pipeline.h"
#include "buffer.h"
#include "device.h"
#include "push_constant_manager.h"

RayTracingPipeline::~RayTracingPipeline()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void RayTracingPipeline::Init(const IRayTracingPipelineInitializer* pInitializer)
{
	pInitializer->InitRayTracingPipeline(this);

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_vkPipelineLayout != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
	CHECK_TRUE(m_shaderGroupCount != 0);
}

void RayTracingPipeline::Uninit()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	if (m_vkPipelineLayout != VK_NULL_HANDLE)
	{
		device.DestroyPipelineLayout(m_vkPipelineLayout);
		m_vkPipelineLayout = VK_NULL_HANDLE;
	}
}

uint32_t RayTracingPipeline::GetShaderGroupCount() const
{
	return m_shaderGroupCount;
}

VkPipeline RayTracingPipeline::GetVkPipeline() const
{
	return m_vkPipeline;
}

void RayTracingPipeline::Do(VkCommandBuffer commandBuffer, const PipelineInput& input)
{
	const auto& pSets = input.vkDescriptorSets;
	const ShaderBindingTable& inShaderBindingTable = *input.pShaderBindingTable;
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
		&inShaderBindingTable.GetRayGenerationShaderRegion(),
		&inShaderBindingTable.GetMissShaderRegion(),
		&inShaderBindingTable.GetHitShaderRegion(),
		&inShaderBindingTable.GetCallableShaderRegion(),
		input.uWidth, 
		input.uHeight, 
		input.uDepth);
}

void ShaderBindingTable::Init(const IShaderBindingTableInitializer* pInit)
{
	pInit->InitShaderBindingTable(this);

	CHECK_TRUE(m_uptrBuffer != nullptr);
	CHECK_TRUE(m_uptrBuffer->GetVkBuffer() != VK_NULL_HANDLE);
}

const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::GetRayGenerationShaderRegion() const
{
	return m_vkRayGenRegion;
}

const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::GetMissShaderRegion() const
{
	return m_vkMissRegion;
}

const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::GetHitShaderRegion() const
{
	return m_vkMissRegion;
}

const VkStridedDeviceAddressRegionKHR& ShaderBindingTable::GetCallableShaderRegion() const
{
	return m_vkCallRegion;
}

void ShaderBindingTable::Uninit()
{
	if (m_uptrBuffer)
	{
		m_uptrBuffer->Uninit();
		m_uptrBuffer.reset();
	}
}

void ShaderBindingTable::Builder::InitShaderBindingTable(ShaderBindingTable* pShaderBindingTable) const
{
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{};
	uint32_t uRecordCount = m_pPipeline->GetShaderGroupCount();
	uint32_t uHandleSizeHost = 0u;
	uint32_t uHandleSizeDevice = 0u;
	size_t uGroupDataSize = 0u;
	std::vector<uint8_t> groupData;
	auto& device = MyDevice::GetInstance();
	auto& outShaderBindingTable = *pShaderBindingTable;
	static auto funcAlignUp = [](uint32_t uToAlign, uint32_t uAlignRef)
		{
			return (uToAlign + uAlignRef - 1) & ~(uAlignRef - 1);
		};

	device.GetPhysicalDeviceRayTracingProperties(rayTracingProperties);

	uHandleSizeHost = rayTracingProperties.shaderGroupHandleSize;
	uHandleSizeDevice = funcAlignUp(uHandleSizeHost, rayTracingProperties.shaderGroupHandleAlignment);

	outShaderBindingTable.m_vkRayGenRegion.size = funcAlignUp(
		uHandleSizeDevice * 1u, 
		rayTracingProperties.shaderGroupBaseAlignment);

	outShaderBindingTable.m_vkMissRegion.size = funcAlignUp(
		uHandleSizeDevice * static_cast<uint32_t>(m_missIndices.size()),
		rayTracingProperties.shaderGroupBaseAlignment);

	outShaderBindingTable.m_vkHitRegion.size = funcAlignUp(
		uHandleSizeDevice * static_cast<uint32_t>(m_hitIndices.size()),
		rayTracingProperties.shaderGroupBaseAlignment);

	outShaderBindingTable.m_vkCallRegion.size = funcAlignUp(
		uHandleSizeDevice * static_cast<uint32_t>(m_callableIndices.size()), 
		rayTracingProperties.shaderGroupBaseAlignment);

	outShaderBindingTable.m_vkRayGenRegion.stride = outShaderBindingTable.m_vkRayGenRegion.size; // The size member of pRayGenShaderBindingTable must be equal to its stride member
	outShaderBindingTable.m_vkMissRegion.stride = uHandleSizeDevice;
	outShaderBindingTable.m_vkHitRegion.stride = uHandleSizeDevice;
	outShaderBindingTable.m_vkCallRegion.stride = uHandleSizeDevice;

	uGroupDataSize = static_cast<size_t>(uRecordCount * uHandleSizeHost);
	groupData.resize(uGroupDataSize);

	VK_CHECK(
		device.GetRayTracingShaderGroupHandles(
			m_pPipeline->GetVkPipeline(),
			0,
			uRecordCount,
			uGroupDataSize,
			groupData.data()),
		"Failed to get group handle data!");

	// bind shader group(shader record) handle to table on device
	{
		Buffer::Initializer bufferInit{};
		VkDeviceAddress SBTAddress = 0;
		std::vector<uint8_t> dataToDevice;
		auto funcGetGroupHandle = [&](int i) { return groupData.data() + i * static_cast<int>(uHandleSizeHost); };
		size_t bufferSize = outShaderBindingTable.m_vkRayGenRegion.size
			+ outShaderBindingTable.m_vkMissRegion.size
			+ outShaderBindingTable.m_vkHitRegion.size
			+ outShaderBindingTable.m_vkCallRegion.size;
		auto& uptrBufferToInit = outShaderBindingTable.m_uptrBuffer;

		uptrBufferToInit = std::make_unique<Buffer>();

		bufferInit.SetBufferUsage(
			VK_BUFFER_USAGE_TRANSFER_DST_BIT 
			| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT 
			| VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR);
		bufferInit.SetBufferSize(bufferSize);

		uptrBufferToInit->Init(&bufferInit);

		SBTAddress = uptrBufferToInit->GetDeviceAddress();
		dataToDevice.resize(bufferSize, 0u);

		// set rayGen
		uint8_t* pEntryPoint = dataToDevice.data();
		outShaderBindingTable.m_vkRayGenRegion.deviceAddress = SBTAddress;
		memcpy(pEntryPoint, funcGetGroupHandle(m_rayGenerationIndex), static_cast<size_t>(uHandleSizeHost));

		// set Miss
		outShaderBindingTable.m_vkMissRegion.deviceAddress = SBTAddress 
			+ outShaderBindingTable.m_vkRayGenRegion.size;
		pEntryPoint = dataToDevice.data() 
			+ outShaderBindingTable.m_vkRayGenRegion.size;
		for (const auto index : m_missIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}

		// set Hit
		outShaderBindingTable.m_vkHitRegion.deviceAddress = SBTAddress 
			+ outShaderBindingTable.m_vkRayGenRegion.size 
			+ outShaderBindingTable.m_vkMissRegion.size;
		pEntryPoint = dataToDevice.data()
			+ outShaderBindingTable.m_vkRayGenRegion.size
			+ outShaderBindingTable.m_vkMissRegion.size;
		for (const auto index : m_hitIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}

		// set Callable
		outShaderBindingTable.m_vkCallRegion.deviceAddress = SBTAddress
			+ outShaderBindingTable.m_vkRayGenRegion.size
			+ outShaderBindingTable.m_vkMissRegion.size
			+ outShaderBindingTable.m_vkHitRegion.size;
		pEntryPoint = dataToDevice.data()
			+ outShaderBindingTable.m_vkRayGenRegion.size
			+ outShaderBindingTable.m_vkMissRegion.size 
			+ outShaderBindingTable.m_vkHitRegion.size;
		for (const auto index : m_callableIndices)
		{
			memcpy(pEntryPoint, funcGetGroupHandle(index), static_cast<size_t>(uHandleSizeHost));
			pEntryPoint += static_cast<int>(uHandleSizeDevice);
		}
		uptrBufferToInit->CopyFromHost(dataToDevice.data());
	}
}

ShaderBindingTable::Builder& ShaderBindingTable::Builder::Reset()
{
	*this = ShaderBindingTable::Builder{};

	return *this;
}

ShaderBindingTable::Builder& ShaderBindingTable::Builder::SetRayGenerationShaderGroupBinding(uint32_t inGroupIndex)
{
	m_rayGenerationIndex = inGroupIndex;

	return *this;
}

uint32_t ShaderBindingTable::Builder::AddMissShaderGroupBinding(uint32_t inGroupIndex)
{
	uint32_t result = static_cast<uint32_t>(m_missIndices.size());
	
	m_missIndices.push_back(inGroupIndex);

	return result;
}

uint32_t ShaderBindingTable::Builder::AddHitShaderGroupBinding(uint32_t inGroupIndex)
{
	uint32_t result = static_cast<uint32_t>(m_hitIndices.size());

	m_hitIndices.push_back(inGroupIndex);

	return result;
}

uint32_t ShaderBindingTable::Builder::AddCallableShaderGroupBinding(uint32_t inGroupIndex)
{
	uint32_t result = static_cast<uint32_t>(m_callableIndices.size());

	m_callableIndices.push_back(inGroupIndex);

	return result;
}

ShaderBindingTable::Builder& ShaderBindingTable::Builder::SetRayTracingPipeline(const RayTracingPipeline* inPipelinePtr)
{
	m_pPipeline = inPipelinePtr;

	return *this;
}

void RayTracingPipeline::Builder::InitRayTracingPipeline(RayTracingPipeline* pPipeline) const
{
	auto& device = MyDevice::GetInstance();
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkRayTracingPipelineCreateInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	std::vector<VkPushConstantRange> pushConstantRanges{};

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
	uptrToInit->GetVkPushConstantRanges(pushConstantRanges);

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

	layoutToInit = device.CreatePipelineLayout(pipelineLayoutInfo);

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

RayTracingPipeline::Builder& RayTracingPipeline::Builder::AddDescriptorSetLayout(VkDescriptorSetLayout vkDSetLayout)
{
	m_descriptorSetLayouts.push_back(vkDSetLayout);

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

uint32_t RayTracingPipeline::Builder::SetRayGenerationShaderGroup(uint32_t inRayGenShaderIndex)
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
