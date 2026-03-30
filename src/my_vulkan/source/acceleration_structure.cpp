#include "acceleration_structure.h"
#include "device.h"
#include "buffer.h"
#include "command_buffer.h"
#include "utils.h"
#include <chrono> // For timing

namespace {
	void _InitScratchBuffer(
		const std::vector<VkAccelerationStructureBuildSizesInfoKHR>& inBuildSizesInfos,
		Buffer& outScratchBufferToInit,
		std::vector<VkDeviceAddress>& outSlotAddresses,
		bool inBuildNotUpdate = true,
		VkDeviceSize inMaxBudget = 256'000'000)
	{
		Buffer::Initializer bufferInit{};

		uint32_t	uMinAlignment = 128; /*m_rtASProperties.minAccelerationStructureScratchOffsetAlignment*/
		VkDeviceSize maxScratchChunk = 0;
		VkDeviceSize fullScratchSize = 0;
		uint64_t	uSlotCount = 0;

		bufferInit.SetBufferUsage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// setup maxScratchChunk, fullScratchSize
		for (const auto& sizeInfo : inBuildSizesInfos)
		{
			VkDeviceSize chunkSizeAligned = inBuildNotUpdate ? common_utils::AlignUp(sizeInfo.buildScratchSize, uMinAlignment) : common_utils::AlignUp(sizeInfo.updateScratchSize, uMinAlignment);

			maxScratchChunk = std::max(maxScratchChunk, chunkSizeAligned);
			fullScratchSize += chunkSizeAligned;
		}
		CHECK_TRUE(inMaxBudget >= maxScratchChunk, "Max Budget is too small!");

		// Find the scratch buffer size
		if (fullScratchSize <= inMaxBudget)
		{
			bufferInit.SetBufferSize(fullScratchSize);
			uSlotCount = inBuildSizesInfos.size();
		}
		else
		{
			uint64_t nChunkCount = std::min(inBuildSizesInfos.size(), inMaxBudget / maxScratchChunk);

			bufferInit.SetBufferSize(nChunkCount * maxScratchChunk);
			uSlotCount = nChunkCount;
		}

		bufferInit.CustomizeAlignment(uMinAlignment);

		outScratchBufferToInit.Init(&bufferInit);

		// fill slotAddresses
		outSlotAddresses.clear();
		outSlotAddresses.reserve(uSlotCount);
		if (fullScratchSize <= inMaxBudget)
		{
			VkDeviceAddress curAddress = outScratchBufferToInit.GetDeviceAddress();
			for (int i = 0; i < inBuildSizesInfos.size(); ++i)
			{
				const auto& sizeInfo = inBuildSizesInfos[i];
				VkDeviceSize chunkSizeAligned = inBuildNotUpdate ? common_utils::AlignUp(sizeInfo.buildScratchSize, uMinAlignment) : common_utils::AlignUp(sizeInfo.updateScratchSize, uMinAlignment);

				outSlotAddresses.push_back(curAddress);
				curAddress += chunkSizeAligned;
			}
		}
		else
		{
			VkDeviceAddress curAddress = outScratchBufferToInit.GetDeviceAddress();
			for (int i = 0; i < uSlotCount; ++i)
			{
				outSlotAddresses.push_back(curAddress);
				curAddress += maxScratchChunk;
			}
		}
	}
}

#pragma region BLAS

void BottomLevelAccelStruct::Descriptor::AddGeometries(
	const BottomLevelAccelStruct::GeometryDescription* inDescriptions,
	uint32_t inCount)
{
	if (inCount == 0 || inDescriptions == nullptr) return;
	size_t offset = m_dataRanges.size();
	
	CHECK_TRUE(m_dataRanges.size() == m_dataSources.size());
	m_dataRanges.resize(offset + static_cast<size_t>(inCount), {});
	m_dataSources.resize(offset + static_cast<size_t>(inCount), { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR });
	switch (m_geometryType)
	{
	case VK_GEOMETRY_TYPE_AABBS_KHR:
	{
		for (uint32_t i = 0; i < inCount; ++i)
		{
			auto& dataRange = m_dataRanges[offset];
			auto& dataSource = m_dataSources[offset];
			const auto& inputData = inDescriptions[i];
			auto& aabbs = dataSource.geometry.aabbs;

			dataRange.primitiveCount = inputData.primitiveCount;
			dataSource.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			dataSource.flags = inputData.flags.value_or(VK_GEOMETRY_OPAQUE_BIT_KHR);
			aabbs.data.deviceAddress = inputData.aabbs.aabbBufferAddress;
			aabbs.stride = inputData.aabbs.stride;
			aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
			++offset;
		}
	}
	break;
	case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
	{
		for (uint32_t i = 0; i < inCount; ++i)
		{
			auto& dataRange = m_dataRanges[offset];
			auto& dataSource = m_dataSources[offset];
			const auto& inputData = inDescriptions[i];
			auto& trigs = dataSource.geometry.triangles;
			const auto& srcTrigs = inputData.triangles;

			dataRange.primitiveCount = inputData.primitiveCount;
			dataSource.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			dataSource.flags = inputData.flags.value_or(VK_GEOMETRY_OPAQUE_BIT_KHR);
			trigs.indexData.deviceAddress = srcTrigs.indexBufferAddress;
			trigs.indexType = srcTrigs.indexType.value_or(VK_INDEX_TYPE_UINT32);
			trigs.maxVertex = srcTrigs.vertexCount - 1;
			trigs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			trigs.transformData.deviceAddress = srcTrigs.transform.value_or(0);
			trigs.vertexData.deviceAddress = srcTrigs.vertexBufferAddress;
			trigs.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			trigs.vertexStride = srcTrigs.vertexStride;
			++offset;
		}
	}
	break;
	default:
		CHECK_TRUE(false);
		break;
	}
}

auto BottomLevelAccelStruct::Descriptor::GetBuildSizesInfo(VkBuildAccelerationStructureFlagsKHR inFlags) const noexcept->VkAccelerationStructureBuildSizesInfoKHR
{
	auto& device = MyDevice::GetInstance();
	std::vector<uint32_t> primitiveCounts;
	VkAccelerationStructureBuildSizesInfoKHR result{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };

	buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildGeometryInfo.flags = inFlags;
	buildGeometryInfo.geometryCount = GetGeometryCount();
	buildGeometryInfo.pGeometries = m_dataSources.data();

	primitiveCounts.resize(m_dataRanges.size());
	for (size_t i = 0; i < m_dataRanges.size(); ++i)
	{
		primitiveCounts[i] = m_dataRanges[i].primitiveCount;
	}

	device.GetAccelerationStructureBuildSizes(
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		buildGeometryInfo,
		primitiveCounts,
		result);

	return result;
}

void BottomLevelAccelStruct::Descriptor::UpdateGeometries(
	const BottomLevelAccelStruct::GeometryUpdateDescription* inUpdateDescriptions, 
	uint32_t inBeginIndex, 
	uint32_t inCount)
{
	if (inCount == 0 || inUpdateDescriptions == nullptr) return;
	size_t offset = static_cast<size_t>(inBeginIndex);

	CHECK_TRUE(m_dataRanges.size() == m_dataSources.size());
	CHECK_TRUE(m_dataSources.size() >= static_cast<size_t>(inBeginIndex + inCount));
	switch (m_geometryType)
	{
	case VK_GEOMETRY_TYPE_AABBS_KHR:
	{
		for (uint32_t i = 0; i < inCount; ++i)
		{
			auto& dataRange = m_dataRanges[offset];
			auto& dataSource = m_dataSources[offset];
			const auto& inputData = inUpdateDescriptions[i];
			auto& aabbs = dataSource.geometry.aabbs;

			aabbs.data.deviceAddress = inputData.aabbs.aabbBufferAddress;
			++offset;
		}
	}
	break;
	case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
	{
		for (uint32_t i = 0; i < inCount; ++i)
		{
			auto& dataRange = m_dataRanges[offset];
			auto& dataSource = m_dataSources[offset];
			const auto& inputData = inUpdateDescriptions[i];
			auto& trigs = dataSource.geometry.triangles;
			const auto& srcTrigs = inputData.triangles;

			trigs.indexData.deviceAddress = srcTrigs.indexBufferAddress;
			trigs.transformData.deviceAddress = srcTrigs.transform.value_or(0);
			trigs.vertexData.deviceAddress = srcTrigs.vertexBufferAddress;
			++offset;
		}
	}
	break;
	default:
		CHECK_TRUE(false);
		break;
	}
}

void BottomLevelAccelStruct::Creator::Create(BottomLevelAccelStruct* inoutAccelStruct) const
{
	CHECK_TRUE(inoutAccelStruct != nullptr);
	CHECK_TRUE(m_descriptor != nullptr);

	// create VkAcceleartionStructure
	VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	auto buildSizesInfo = m_descriptor->GetBuildSizesInfo(m_flags);
	auto& device = MyDevice::GetInstance();

	if (inoutAccelStruct->m_uptrBuffer == nullptr)
	{
		Buffer::Initializer bufferInit{};

		bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		bufferInit.SetBufferSize(buildSizesInfo.accelerationStructureSize);
		bufferInit.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		inoutAccelStruct->m_uptrBuffer = std::make_unique<Buffer>();
		inoutAccelStruct->m_uptrBuffer->Init(&bufferInit);
	}
	else if (inoutAccelStruct->m_uptrBuffer->GetBufferInformation().size < buildSizesInfo.accelerationStructureSize)
	{
		Buffer::Initializer bufferInit{};

		bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		bufferInit.SetBufferSize(buildSizesInfo.accelerationStructureSize);
		bufferInit.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		inoutAccelStruct->m_uptrBuffer->Uninit();
		inoutAccelStruct->m_uptrBuffer->Init(&bufferInit);
	}

	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	createInfo.size = buildSizesInfo.accelerationStructureSize;
	createInfo.pNext = nullptr;
	createInfo.offset = 0;
	createInfo.buffer = inoutAccelStruct->m_uptrBuffer->GetVkBuffer();
	// following info are for replay https://registry.khronos.org/vulkan/specs/latest/man/html/VkAccelerationStructureCreateInfoKHR.html
	// createInfo.deviceAddress = 0;
	// createInfo.createFlags = 0;

	inoutAccelStruct->m_vkAccelStruct = device.CreateAccelerationStructure(createInfo);
	addrInfo.accelerationStructure = inoutAccelStruct->GetVkAccelerationStructure();
	inoutAccelStruct->m_accelStructReference = vkGetAccelerationStructureDeviceAddressKHR(MyDevice::GetInstance().vkDevice, &addrInfo);
	inoutAccelStruct->m_flags = m_flags;
}

void BottomLevelAccelStruct::BuildHelper::GetBuildInfo(
	const BottomLevelAccelStruct* inDstAccelStruct,
	VkAccelStructBuildInfo& outBuildInfo) const noexcept
{
	auto& device = MyDevice::GetInstance();
	auto& buildGeomInfo = outBuildInfo.buildGeometryInfo;
	auto buildFlag = inDstAccelStruct->m_flags;
	auto dstAccelStruct = inDstAccelStruct->GetVkAccelerationStructure();

	outBuildInfo.buildRangesInfos = m_descriptor->m_dataRanges;
	outBuildInfo.geometries = m_descriptor->m_dataSources;

	CHECK_TRUE(dstAccelStruct != VK_NULL_HANDLE);

	buildGeomInfo = VkAccelerationStructureBuildGeometryInfoKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildGeomInfo.flags = buildFlag;
	buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	buildGeomInfo.scratchData.deviceAddress = m_scratchBufferAddress;
	buildGeomInfo.dstAccelerationStructure = dstAccelStruct;
	buildGeomInfo.geometryCount = static_cast<uint32_t>(outBuildInfo.geometries.size());
	buildGeomInfo.pGeometries = outBuildInfo.geometries.data();
}

void BottomLevelAccelStruct::BuildHelper::GetUpdateInfo(
	const BottomLevelAccelStruct* inSrcAccelStruct,
	const BottomLevelAccelStruct* inDstAccelStruct,
	VkAccelStructBuildInfo& outBuildInfo) const noexcept
{
	auto& device = MyDevice::GetInstance();
	auto& buildGeomInfo = outBuildInfo.buildGeometryInfo;
	auto buildFlag = inDstAccelStruct->m_flags;
	auto srcAccelStruct = inSrcAccelStruct->GetVkAccelerationStructure();
	auto dstAccelStruct = inDstAccelStruct->GetVkAccelerationStructure();

	outBuildInfo.buildRangesInfos = m_descriptor->m_dataRanges;
	outBuildInfo.geometries = m_descriptor->m_dataSources;

	CHECK_TRUE(srcAccelStruct != VK_NULL_HANDLE);
	CHECK_TRUE(dstAccelStruct != VK_NULL_HANDLE);

	buildGeomInfo = VkAccelerationStructureBuildGeometryInfoKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildGeomInfo.flags = buildFlag;
	buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	buildGeomInfo.srcAccelerationStructure = srcAccelStruct;
	buildGeomInfo.scratchData.deviceAddress = m_scratchBufferAddress;
	buildGeomInfo.dstAccelerationStructure = dstAccelStruct;
	buildGeomInfo.geometryCount = static_cast<uint32_t>(outBuildInfo.geometries.size());
	buildGeomInfo.pGeometries = outBuildInfo.geometries.data();
}

void BottomLevelAccelStruct::Init(const BottomLevelAccelStruct::Creator* inInitializer)
{
	Uninit();

	inInitializer->Create(this);

	CHECK_TRUE(m_vkAccelStruct != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrBuffer != nullptr);
	CHECK_TRUE(m_accelStructReference != 0);
}

void BottomLevelAccelStruct::Uninit()
{
	if (m_vkAccelStruct != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		vkDestroyAccelerationStructureKHR(device.vkDevice, m_vkAccelStruct, nullptr);
		m_vkAccelStruct = VK_NULL_HANDLE;
	}

	if (m_uptrBuffer != nullptr)
	{
		m_uptrBuffer->Uninit();
		m_uptrBuffer.reset();
	}

	m_accelStructReference = 0;
}

#pragma endregion

#pragma region TLAS

void TopLevelAccelStruct::Init(const TopLevelAccelStruct::Creator* inInitializer)
{
	Uninit();

	inInitializer->Create(this);

	CHECK_TRUE(m_vkAccelStruct != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrBuffer != nullptr);
}

void TopLevelAccelStruct::Uninit()
{
	if (m_vkAccelStruct != VK_NULL_HANDLE)
	{
		auto& device = MyDevice::GetInstance();

		vkDestroyAccelerationStructureKHR(device.vkDevice, m_vkAccelStruct, nullptr);
		m_vkAccelStruct = VK_NULL_HANDLE;
	}

	if (m_uptrBuffer != nullptr)
	{
		m_uptrBuffer->Uninit();
		m_uptrBuffer.reset();
	}
}

auto TopLevelAccelStruct::Descriptor::AddInstances(const InstanceDescription* inDescriptions, uint32_t inCount) -> uint32_t
{
	if (inCount == 0 || inDescriptions == nullptr) {
		return static_cast<uint32_t>(m_accelStructInstances.size());
	}

	const uint32_t currentSize = static_cast<uint32_t>(m_accelStructInstances.size());
	const uint64_t newSize = static_cast<uint64_t>(currentSize) + static_cast<uint64_t>(inCount);

	if (newSize > UINT32_MAX) {
		throw std::overflow_error("Instance count exceeds uint32_t maximum limit");
	}

	const uint32_t result = currentSize;
	m_accelStructInstances.resize(static_cast<size_t>(newSize));

	SetInstances(
		reinterpret_cast<const InstanceUpdateDescription*>(inDescriptions),
		currentSize, 
		inCount);

	return result;
}

void TopLevelAccelStruct::Descriptor::SetInstances(const InstanceUpdateDescription* inUpdateDescriptions, uint32_t inBeginIndex, uint32_t inCount)
{
	if (inUpdateDescriptions == nullptr) {
		throw std::invalid_argument("Instance update descriptions pointer is null");
	}

	const size_t endIndex = static_cast<size_t>(inBeginIndex) + static_cast<size_t>(inCount);
	if (endIndex > m_accelStructInstances.size()) {
		throw std::out_of_range("Instance index range exceeds container size");
	}

	for (uint32_t i = 0; i < inCount; ++i) {
		const uint32_t currentIndex = inBeginIndex + i;
		auto& vkInst = m_accelStructInstances[currentIndex];
		const auto& desc = inUpdateDescriptions[i];

		vkInst.accelerationStructureReference = desc.accelStruct->GetReference();
		vkInst.flags = desc.flags;
		vkInst.instanceCustomIndex = desc.instanceCustomIndex;
		vkInst.instanceShaderBindingTableRecordOffset = desc.shaderGroupIndexOffset;
		vkInst.mask = desc.mask;
		vkInst.transform = common_utils::ToTransformMatrixKHR(desc.transform);
	}
}

auto TopLevelAccelStruct::Descriptor::GetBuildSizesInfo(VkBuildAccelerationStructureFlagsKHR inFlags) const->VkAccelerationStructureBuildSizesInfoKHR
{
	auto& device = MyDevice::GetInstance();
	std::vector<uint32_t> primitiveCounts;
	VkAccelerationStructureBuildSizesInfoKHR result{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureGeometryInstancesDataKHR geometryInstData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };

	geometry.flags = 0; // instance flags serves the same purpose, so leave it 0
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = geometryInstData; // no need to set device address, VkDeviceOrHostAddressConstKHR is ignored: https://github.khronos.org/Vulkan-Site/refpages/latest/refpages/source/vkGetAccelerationStructureBuildSizesKHR.html

	buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildGeometryInfo.flags = inFlags;
	buildGeometryInfo.geometryCount = 1; // must be 1 for TLAS
	buildGeometryInfo.pGeometries = &geometry;

	primitiveCounts.emplace_back(static_cast<uint32_t>(GetInstanceCount()));

	device.GetAccelerationStructureBuildSizes(
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		buildGeometryInfo,
		primitiveCounts,
		result);

	return result;
}

void TopLevelAccelStruct::Creator::Create(TopLevelAccelStruct* inoutAccelStruct) const
{
	// create VkAcceleartionStructure
	VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	auto buildSizesInfo = m_descriptor->GetBuildSizesInfo(m_flags);
	auto& device = MyDevice::GetInstance();

	if (inoutAccelStruct->m_uptrBuffer == nullptr)
	{
		Buffer::Initializer bufferInit{};

		bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		bufferInit.SetBufferSize(buildSizesInfo.accelerationStructureSize);
		bufferInit.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		inoutAccelStruct->m_uptrBuffer = std::make_unique<Buffer>();
		inoutAccelStruct->m_uptrBuffer->Init(&bufferInit);
	}
	else if (inoutAccelStruct->m_uptrBuffer->GetBufferInformation().size < buildSizesInfo.accelerationStructureSize)
	{
		Buffer::Initializer bufferInit{};

		bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		bufferInit.SetBufferSize(buildSizesInfo.accelerationStructureSize);
		bufferInit.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		inoutAccelStruct->m_uptrBuffer->Uninit();
		inoutAccelStruct->m_uptrBuffer->Init(&bufferInit);
	}

	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	createInfo.size = buildSizesInfo.accelerationStructureSize;
	createInfo.pNext = nullptr;
	createInfo.offset = 0;
	createInfo.buffer = inoutAccelStruct->m_uptrBuffer->GetVkBuffer();
	// following info are for replay https://registry.khronos.org/vulkan/specs/latest/man/html/VkAccelerationStructureCreateInfoKHR.html
	// createInfo.deviceAddress = 0;
	// createInfo.createFlags = 0;

	inoutAccelStruct->m_vkAccelStruct = device.CreateAccelerationStructure(createInfo);
	inoutAccelStruct->m_flags = m_flags;
}

void TopLevelAccelStruct::BuildHelper::GetBuildInfo(
	const TopLevelAccelStruct* inDstAccelStruct,
	VkAccelStructBuildInfo& outBuildInfo) const noexcept
{
	VkAccelerationStructureGeometryInstancesDataKHR geometryInstData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	auto& device = MyDevice::GetInstance();
	auto& buildGeomInfo = outBuildInfo.buildGeometryInfo;
	auto buildFlag = inDstAccelStruct->m_flags;
	auto dstAccelStruct = inDstAccelStruct->GetVkAccelerationStructure();

	outBuildInfo.buildRangesInfos.resize(1);
	outBuildInfo.geometries.resize(1);
	auto& geometry = outBuildInfo.geometries[0];
	auto& buildRangeInfo = outBuildInfo.buildRangesInfos[0];
	buildRangeInfo = VkAccelerationStructureBuildRangeInfoKHR{};
	geometry = VkAccelerationStructureGeometryKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	buildGeomInfo = VkAccelerationStructureBuildGeometryInfoKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };

	geometryInstData.data.deviceAddress = m_instanceBuffer;

	geometry.flags = 0; // instance flags serves the same purpose, so leave it 0
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = geometryInstData;

	CHECK_TRUE(dstAccelStruct != VK_NULL_HANDLE);

	buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildGeomInfo.flags = buildFlag;
	buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	buildGeomInfo.scratchData.deviceAddress = m_scratchBuffer;
	buildGeomInfo.dstAccelerationStructure = dstAccelStruct;
	buildGeomInfo.geometryCount = static_cast<uint32_t>(outBuildInfo.geometries.size());
	buildGeomInfo.pGeometries = outBuildInfo.geometries.data();
	buildRangeInfo.primitiveCount = static_cast<uint32_t>(m_descriptor->GetInstanceCount());
}

void TopLevelAccelStruct::BuildHelper::GetUpdateInfo(
	const TopLevelAccelStruct* inSrcAccelStruct,
	const TopLevelAccelStruct* inDstAccelStruct,
	VkAccelStructBuildInfo& outBuildInfo) const noexcept
{
	VkAccelerationStructureGeometryInstancesDataKHR geometryInstData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	auto& device = MyDevice::GetInstance();
	auto& buildGeomInfo = outBuildInfo.buildGeometryInfo;
	auto buildFlag = inDstAccelStruct->m_flags;
	auto srcAccelStruct = inSrcAccelStruct->GetVkAccelerationStructure();
	auto dstAccelStruct = inDstAccelStruct->GetVkAccelerationStructure();

	outBuildInfo.buildRangesInfos.resize(1);
	outBuildInfo.geometries.resize(1);
	auto& geometry = outBuildInfo.geometries[0];
	auto& buildRangeInfo = outBuildInfo.buildRangesInfos[0];
	buildRangeInfo = VkAccelerationStructureBuildRangeInfoKHR{};
	geometry = VkAccelerationStructureGeometryKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	buildGeomInfo = VkAccelerationStructureBuildGeometryInfoKHR{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };

	geometryInstData.data.deviceAddress = m_instanceBuffer;

	geometry.flags = 0;	// instance flags serves the same purpose, so leave it 0
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = geometryInstData;

	CHECK_TRUE(srcAccelStruct != VK_NULL_HANDLE);
	CHECK_TRUE(dstAccelStruct != VK_NULL_HANDLE);

	buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildGeomInfo.flags = buildFlag;
	buildGeomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	buildGeomInfo.srcAccelerationStructure = srcAccelStruct;
	buildGeomInfo.scratchData.deviceAddress = m_scratchBuffer;
	buildGeomInfo.dstAccelerationStructure = dstAccelStruct;
	buildGeomInfo.geometryCount = static_cast<uint32_t>(outBuildInfo.geometries.size());
	buildGeomInfo.pGeometries = outBuildInfo.geometries.data();
	buildRangeInfo.primitiveCount = static_cast<uint32_t>(m_descriptor->GetInstanceCount());
}

#pragma endregion