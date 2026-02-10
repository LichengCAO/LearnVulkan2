#include "acceleration_structure.h"
#include "device.h"
#include "buffer.h"
#include "command_buffer.h"
#include "utils.h"
#include <chrono> // For timing

void RayTracingAccelerationStructure::_InitScratchBuffer(
	VkDeviceSize maxBudget,
	const std::vector<VkAccelerationStructureBuildSizesInfoKHR>& buildSizeInfo,
	bool bForBuild,
	Buffer& scratchBufferToInit,
	std::vector<VkDeviceAddress>& slotAddresses)
{
	Buffer::CreateInformation scratchBufferInfo{};
	uint32_t	uMinAlignment = 128; /*m_rtASProperties.minAccelerationStructureScratchOffsetAlignment*/
	VkDeviceSize maxScratchChunk = 0;
	VkDeviceSize fullScratchSize = 0;
	uint64_t	uSlotCount = 0;
	scratchBufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	scratchBufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	// setup maxScratchChunk, fullScratchSize
	for (const auto& sizeInfo : buildSizeInfo)
	{
		VkDeviceSize chunkSizeAligned = bForBuild ? common_utils::AlignUp(sizeInfo.buildScratchSize, uMinAlignment) : common_utils::AlignUp(sizeInfo.updateScratchSize, uMinAlignment);

		maxScratchChunk = std::max(maxScratchChunk, chunkSizeAligned);
		fullScratchSize += chunkSizeAligned;
	}
	CHECK_TRUE(maxBudget >= maxScratchChunk, "Max Budget is too small!");

	// Find the scratch buffer size
	if (fullScratchSize <= maxBudget)
	{
		scratchBufferInfo.size = fullScratchSize;
		uSlotCount = buildSizeInfo.size();
	}
	else
	{
		uint64_t nChunkCount = std::min(buildSizeInfo.size(), maxBudget / maxScratchChunk);
		scratchBufferInfo.size = nChunkCount * maxScratchChunk;
		uSlotCount = nChunkCount;
	}

	scratchBufferInfo.optAlignment = static_cast<VkDeviceSize>(uMinAlignment);
	scratchBufferToInit.PresetCreateInformation(scratchBufferInfo);
	scratchBufferToInit.Init();

	// fill slotAddresses
	slotAddresses.clear();
	slotAddresses.reserve(uSlotCount);
	if (fullScratchSize <= maxBudget)
	{
		VkDeviceAddress curAddress = scratchBufferToInit.GetDeviceAddress();
		for (int i = 0; i < buildSizeInfo.size(); ++i)
		{
			const auto& sizeInfo = buildSizeInfo[i];
			VkDeviceSize chunkSizeAligned = bForBuild ? common_utils::AlignUp(sizeInfo.buildScratchSize, uMinAlignment) : common_utils::AlignUp(sizeInfo.updateScratchSize, uMinAlignment);

			slotAddresses.push_back(curAddress);
			curAddress += chunkSizeAligned;
		}
	}
	else
	{
		VkDeviceAddress curAddress = scratchBufferToInit.GetDeviceAddress();
		for (int i = 0; i < uSlotCount; ++i)
		{
			slotAddresses.push_back(curAddress);	
			curAddress += maxScratchChunk;
		}
	}
}

void RayTracingAccelerationStructure::_BuildBLASs()
{
	std::chrono::steady_clock::time_point start{};
	std::chrono::steady_clock::time_point end{};
	std::chrono::microseconds duration{};

	if (m_compactBLAS)
	{
		for (auto& _BLASInput : m_BLASInputs)
		{
			_BLASInput.vkBuildFlags = _BLASInput.vkBuildFlags | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
		}
	}

	// Start measuring time
	std::cout << "Start build BLASes..." << std::endl;
	start = std::chrono::high_resolution_clock::now();

	_BuildOrUpdateBLASes(m_BLASInputs, m_uptrBLASes, nullptr);

	// Stop measuring time
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	std::cout << "Execution Time: " << duration.count() << " microseconds\n";
}

void RayTracingAccelerationStructure::_BuildOrUpdateBLASes(const std::vector<BLASInput>& _inputs, std::vector<std::unique_ptr<BLAS>>& _BLASes, CommandSubmission* _pCmd, VkDeviceSize maxBudget)
{
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeomInfos;
	std::vector<VkAccelerationStructureBuildSizesInfoKHR> buildSizeInfos;
	std::vector<VkDeviceAddress> scratchAddresses;
	std::shared_ptr<Buffer> scratchBuffer = std::make_shared<Buffer>();
	bool bNeedCompact = false;
	bool bBuildAS = _BLASes.size() == 0;
	size_t  nAllBLASCount = _inputs.size();

	// setup scratch buffer that doesn't exceed maxBudget
	buildGeomInfos.reserve(nAllBLASCount);
	buildSizeInfos.reserve(nAllBLASCount);
	for (size_t i = 0; i < nAllBLASCount; ++i)
	{
		std::vector<uint32_t> maxPrimCount(_inputs[i].vkASBuildRangeInfos.size());
		VkAccelerationStructureBuildSizesInfoKHR	buildSizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildGeomInfo.mode = bBuildAS ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
		buildGeomInfo.flags = bBuildAS ? _inputs[i].vkBuildFlags : _BLASes[i]->vkBuildFlags;
		buildGeomInfo.geometryCount = static_cast<uint32_t>(_inputs[i].vkASGeometries.size());
		buildGeomInfo.pGeometries = _inputs[i].vkASGeometries.data();
		buildGeomInfo.ppGeometries = nullptr;
		buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
		buildGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE; // we don't have AS yet, so we do this later, see Task1
		// buildGeomInfo.scratchData: we don't have scratch buffer yet, so we do this later, see Task2

		// setup maxPrimCount
		for (size_t j = 0; j < _inputs[i].vkASBuildRangeInfos.size(); ++j)
		{
			maxPrimCount[j] = _inputs[i].vkASBuildRangeInfos[j].primitiveCount;
		}

		// https://registry.khronos.org/vulkan/specs/latest/man/html/vkGetAccelerationStructureBuildSizesKHR.html
		assert(maxPrimCount.size() == buildGeomInfo.geometryCount);
		vkGetAccelerationStructureBuildSizesKHR(MyDevice::GetInstance().vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeomInfo, maxPrimCount.data(), &buildSizeInfo);

		bNeedCompact = (buildGeomInfo.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) || bNeedCompact;

		buildGeomInfos.push_back(buildGeomInfo);
		buildSizeInfos.push_back(buildSizeInfo);
	}
	_InitScratchBuffer(maxBudget, buildSizeInfos, bBuildAS, *scratchBuffer, scratchAddresses);

	// Task2: now we have scratch buffer, we can set scratch data for buildGeomInfos
	for (size_t i = 0; i < nAllBLASCount; ++i)
	{
		auto& buildGeomInfo = buildGeomInfos[i];
		buildGeomInfo.scratchData.deviceAddress = scratchAddresses[i % scratchAddresses.size()];
	}

	// create BLASes and build them
	if (bBuildAS)
	{
		size_t BLASIndex = 0;
		size_t loopCount = (nAllBLASCount + scratchAddresses.size() - 1) / scratchAddresses.size();
		std::vector<std::unique_ptr<BLAS>> sparseBLASes;
		CommandSubmission localCmd{};

		localCmd.Init();
		localCmd.StartCommands({});
		
		_BLASes.reserve(nAllBLASCount);
		sparseBLASes.reserve(nAllBLASCount);

		// build BLASes
		for (size_t i = 0; i < loopCount; ++i)
		{
			VkMemoryBarrier vkMemBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfosToProcess;
			std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeomInfosToProcess;

			buildRangeInfosToProcess.reserve(scratchAddresses.size());
			buildGeomInfosToProcess.reserve(scratchAddresses.size());

			// try to create as many BLASes as scratch buffer can hold
			for (size_t j = 0; j < scratchAddresses.size() && BLASIndex < nAllBLASCount; ++j)
			{
				auto curBLAS = std::make_unique<BLAS>();

				// create BLAS
				curBLAS->Init(buildSizeInfos[BLASIndex].accelerationStructureSize);

				curBLAS->vkBuildFlags = _inputs[BLASIndex].vkBuildFlags;

				// Task1: now we have AS, we can set dstAS for buildGeom
				buildGeomInfos[BLASIndex].dstAccelerationStructure = curBLAS->vkAccelerationStructure;

				// use scratch buffer to build BLAS
				buildRangeInfosToProcess.push_back(_inputs[BLASIndex].vkASBuildRangeInfos.data());
				buildGeomInfosToProcess.push_back(buildGeomInfos[BLASIndex]);
				sparseBLASes.push_back(std::move(curBLAS));

				BLASIndex++;
			}

			// build BLASes
			localCmd.BuildAccelerationStructures(buildGeomInfosToProcess, buildRangeInfosToProcess);

			// wait build to be done
			vkMemBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkMemBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			localCmd.AddPipelineBarrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, { vkMemBarrier });
		}

		// when build finish scratch buffer should be no longer useful
		localCmd.BindCallback(CommandSubmission::CALLBACK_BINDING_POINT::COMMANDS_DONE,
			[sptr = std::move(scratchBuffer)](CommandSubmission* _cmd) mutable
			{
				sptr->Uninit();
			});

		// Wait BLASes to be ready and fill the output BLASes
		if (bNeedCompact)
		{
			VkQueryPool vkQueryPool = VK_NULL_HANDLE;
			std::vector<VkAccelerationStructureKHR> BLASesToQuery;
			std::vector<VkDeviceSize> compactSizes(nAllBLASCount);
			VkQueryPoolCreateInfo qpci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

			qpci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
			qpci.queryCount = nAllBLASCount;
			vkCreateQueryPool(MyDevice::GetInstance().vkDevice, &qpci, nullptr, &vkQueryPool);
			vkResetQueryPool(MyDevice::GetInstance().vkDevice, vkQueryPool, 0, nAllBLASCount);

			BLASesToQuery.reserve(nAllBLASCount);
			for (auto& uptrBLAS : sparseBLASes)
			{
				BLASesToQuery.push_back(uptrBLAS->vkAccelerationStructure);
			}

			// previous memory barrier ensures the build done, so we query here
			localCmd.WriteAccelerationStructuresProperties(BLASesToQuery, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, vkQueryPool, 0u);
			
			// wait query to be finished
			localCmd.SubmitCommandsAndWait();

			vkGetQueryPoolResults(
				MyDevice::GetInstance().vkDevice,
				vkQueryPool,
				0,
				static_cast<uint32_t>(compactSizes.size()),
				compactSizes.size() * sizeof(VkDeviceSize),
				compactSizes.data(),
				sizeof(VkDeviceSize),
				VK_QUERY_RESULT_WAIT_BIT
			);

			localCmd.StartCommands({});

			for (size_t i = 0; i < nAllBLASCount; ++i)
			{
				VkDeviceSize compactSize = compactSizes[i];
				if (compactSize > 0)
				{
					auto compactBLAS = std::make_unique<BLAS>();
					VkCopyAccelerationStructureInfoKHR copyInfo{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };

					compactBLAS->Init(compactSize);

					compactBLAS->vkBuildFlags = sparseBLASes[i]->vkBuildFlags;

					copyInfo.pNext = nullptr;
					copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
					copyInfo.src = sparseBLASes[i]->vkAccelerationStructure;
					copyInfo.dst = compactBLAS->vkAccelerationStructure;

					localCmd.CopyAccelerationStructure(copyInfo);
					_BLASes.push_back(std::move(compactBLAS));
				}
			}

			localCmd.SubmitCommandsAndWait();

			// we already push the compacted BLAS copy into the m_uptrBLASes, therefore BLASes in sparseBLASes is useless
			for (auto& uselessBLAS : sparseBLASes)
			{
				uselessBLAS->Uninit();
			}
			sparseBLASes.clear();
			vkDestroyQueryPool(MyDevice::GetInstance().vkDevice, vkQueryPool, nullptr);
			vkQueryPool = VK_NULL_HANDLE;
		}
		else
		{
			localCmd.SubmitCommandsAndWait();
			for (size_t i = 0; i < sparseBLASes.size(); ++i)
			{
				_BLASes.push_back(std::move(sparseBLASes[i]));
			}
		}

		localCmd.Uninit();
	}
	else
	{
		size_t BLASIndex = 0;
		size_t loopCount = (nAllBLASCount + scratchAddresses.size() - 1) / scratchAddresses.size();
		for (size_t i = 0; i < loopCount; ++i)
		{
			VkMemoryBarrier vkMemBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> buildRangeInfosToProcess;
			std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeomInfosToProcess;

			buildRangeInfosToProcess.reserve(nAllBLASCount);
			buildGeomInfosToProcess.reserve(nAllBLASCount);

			// try to update as many BLASes as scratch buffer can hold
			for (size_t j = 0; j < scratchAddresses.size() && BLASIndex < nAllBLASCount; ++j)
			{
				auto& curBLAS = _BLASes[BLASIndex];

				// Task1: now we have AS, we can set dstAS for buildGeom
				buildGeomInfos[BLASIndex].srcAccelerationStructure = curBLAS->vkAccelerationStructure;
				buildGeomInfos[BLASIndex].dstAccelerationStructure = curBLAS->vkAccelerationStructure;

				// use scratch buffer to build BLAS
				buildRangeInfosToProcess.push_back(_inputs[BLASIndex].vkASBuildRangeInfos.data());
				buildGeomInfosToProcess.push_back(buildGeomInfos[BLASIndex]);

				BLASIndex++;
			}

			// build BLASes
			_pCmd->BuildAccelerationStructures(buildGeomInfosToProcess, buildRangeInfosToProcess);

			// wait build to be done, this will wait till previous commands be done, so that scratch buffer is available
			vkMemBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkMemBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			_pCmd->AddPipelineBarrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, { vkMemBarrier });
		}
		_pCmd->BindCallback(
			CommandSubmission::CALLBACK_BINDING_POINT::COMMANDS_DONE,
			[sptr = std::move(scratchBuffer)](CommandSubmission* _cmd) mutable
		{
			sptr->Uninit();
		});
	}
}

void RayTracingAccelerationStructure::_BuildTLAS()
{
	std::chrono::steady_clock::time_point start{};
	std::chrono::steady_clock::time_point end{};
	std::chrono::microseconds duration{};
	CommandSubmission tmpCmd{};

	// Start measuring time
	std::cout << "Start build TLAS..." << std::endl;
	start = std::chrono::high_resolution_clock::now();

	if (m_uptrTLAS.get() != nullptr)
	{
		m_uptrTLAS->Uninit();
		m_uptrTLAS.reset();
	}
	m_uptrTLAS = std::make_unique<TLAS>();
	tmpCmd.Init();
	tmpCmd.StartOneTimeCommands({});
	_BuildOrUpdateTLAS(m_TLASInput, m_uptrTLAS.get(), &tmpCmd);
	tmpCmd.SubmitCommands();
	this->vkAccelerationStructure = m_uptrTLAS->vkAccelerationStructure;

	// Stop measuring time
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	std::cout << "Execution Time: " << duration.count() << " microseconds\n";

	// From GPT:
	// Instance buffer data - unlike triangle data - can be destroy after the AS is built. For triangle data, it depends on GPU vendor's implementations
	//
	// Instance Data (in VkAccelerationStructureGeometryInstancesDataKHR.data)
	// When building a Top - Level Acceleration Structure(TLAS), the instance data(which specifies the references to BLASes, their transforms, and other metadata) 
	// is always copied into the TLAS during the build process.
	// This ensures that the instance information is stored entirely within the TLASand does not require access to the original buffer after the build.
	// Vulkan explicitly guarantees this behavior in the specification : the GPU will copy all required instance data into the TLAS memory during the build process.
	// Therefore, the buffer used for data.deviceAddress only needs to be valid during the TLAS build operation, and it can be safely destroyed afterward.
	//
	// Triangle Vertex Data(in VkAccelerationStructureGeometryTrianglesDataKHR.vertexData)
	// When building a Bottom - Level Acceleration Structure(BLAS) from triangle geometry, the behavior is implementation - dependent :
	// Some Vulkan implementations may copy vertex data directly into the BLAS, making the original vertex buffer unnecessary after the build.
	// Other implementations may reference the original vertex data at runtime, meaning the vertex buffer must remain valid for as long as the BLAS is used.
	// Because this behavior depends on the GPU driverand hardware, 
	// you cannot assume that the vertex buffer can always be destroyed after the BLAS is built unless you¡¯ve confirmed the implementation¡¯s behavior.
	m_TLASInput.Reset();
}

void RayTracingAccelerationStructure::_FillTLASInput(const std::vector<InstanceData>& instData, TLASInput& inputToFill) const
{
	uint32_t gl_InstanceCustomIndex = 0u;
	std::vector<VkAccelerationStructureInstanceKHR> ASInstances;
	VkAccelerationStructureGeometryInstancesDataKHR geomInstancesData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	VkAccelerationStructureGeometryKHR				geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	VkAccelerationStructureBuildRangeInfoKHR		buildRangeInfo{};

	inputToFill.Reset();

	// prepare instance data
	ASInstances.reserve(instData.size());
	for (const auto& instInfo : instData)
	{
		VkAccelerationStructureInstanceKHR vkASInst{};
		vkASInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		vkASInst.mask = 0xFF;
		vkASInst.instanceCustomIndex = instInfo.optCustomIndex.has_value() ? instInfo.optCustomIndex.value() : gl_InstanceCustomIndex;
		vkASInst.accelerationStructureReference = m_uptrBLASes[instInfo.uBLASIndex]->vkDeviceAddress;
		vkASInst.instanceShaderBindingTableRecordOffset = instInfo.uHitShaderGroupIndexOffset;
		vkASInst.transform = common_utils::ToTransformMatrixKHR(instInfo.transformMatrix);

		gl_InstanceCustomIndex++;
		ASInstances.push_back(vkASInst);
	}

	// setup instance buffer
	{
		Buffer::CreateInformation bufferInfo{};
		inputToFill.uptrInstanceBuffer = std::make_unique<Buffer>();

		bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.size = ASInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);
		inputToFill.uptrInstanceBuffer->PresetCreateInformation(bufferInfo);
		inputToFill.uptrInstanceBuffer->Init();
		inputToFill.uptrInstanceBuffer->CopyFromHost(ASInstances.data());
	}

	buildRangeInfo.primitiveCount = static_cast<uint32_t>(instData.size());
	geomInstancesData.data.deviceAddress = inputToFill.uptrInstanceBuffer->GetDeviceAddress();
	geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geom.geometry.instances = geomInstancesData;

	inputToFill.vkASBuildRangeInfo = buildRangeInfo;
	inputToFill.vkASGeometry = geom;
}

void RayTracingAccelerationStructure::_BuildOrUpdateTLAS(const TLASInput& _input, TLAS* _pTLAS, CommandSubmission* _pCmd)
{
	std::shared_ptr<Buffer> pScratchBufferUsed = std::make_shared<Buffer>();
	VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	uint32_t uMaxPrimitiveCount = 0u; // we only have one TLAS here, so I use a uint32_t instead of a vector
	std::vector<VkDeviceAddress> slotAddresses; // size should be one
	bool bBuildAS = _pTLAS->vkAccelerationStructure == VK_NULL_HANDLE;

	buildGeomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildGeomInfo.flags = bBuildAS ? _input.vkBuildFlags : _pTLAS->vkBuildFlags;
	buildGeomInfo.mode = bBuildAS ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	buildGeomInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	buildGeomInfo.dstAccelerationStructure = VK_NULL_HANDLE;
	buildGeomInfo.geometryCount = 1u;
	buildGeomInfo.pGeometries = &_input.vkASGeometry;

	uMaxPrimitiveCount = _input.vkASBuildRangeInfo.primitiveCount;

	vkGetAccelerationStructureBuildSizesKHR(MyDevice::GetInstance().vkDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeomInfo, &uMaxPrimitiveCount, &buildSizeInfo);

	// init scratch buffer
	if (bBuildAS)
	{
		_InitScratchBuffer(buildSizeInfo.buildScratchSize, { buildSizeInfo }, bBuildAS, *pScratchBufferUsed, slotAddresses);
		_pTLAS->Init(buildSizeInfo.accelerationStructureSize);
		_pTLAS->vkBuildFlags = _input.vkBuildFlags;
		buildGeomInfo.dstAccelerationStructure = _pTLAS->vkAccelerationStructure;
		buildGeomInfo.scratchData.deviceAddress = pScratchBufferUsed->GetDeviceAddress();
	}
	else
	{
		_InitScratchBuffer(buildSizeInfo.updateScratchSize, { buildSizeInfo }, bBuildAS, *pScratchBufferUsed, slotAddresses);
		buildGeomInfo.srcAccelerationStructure = _pTLAS->vkAccelerationStructure;
		buildGeomInfo.dstAccelerationStructure = _pTLAS->vkAccelerationStructure;
		buildGeomInfo.scratchData.deviceAddress = pScratchBufferUsed->GetDeviceAddress();
	}

	// record command buffer
	CHECK_TRUE(_pCmd != nullptr, "A valid command buffer pointer needs to be set here!");
	{
		_pCmd->BuildAccelerationStructures({ buildGeomInfo }, { &_input.vkASBuildRangeInfo });
		_pCmd->BindCallback(
			CommandSubmission::CALLBACK_BINDING_POINT::COMMANDS_DONE,
			[sptr = std::move(pScratchBufferUsed)](CommandSubmission* pCmd)
			mutable {
			sptr->Uninit();
			//std::cout << "Scratch buffer destroyed when updated." << std::endl;
		});
	}
}

void RayTracingAccelerationStructure::_FillBLASInput(const std::vector<TriangleData>& _trigData, BLASInput& _inputToFill)
{
	for (const auto& geom : _trigData)
	{
		VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
		triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		triangles.vertexData.deviceAddress = geom.vkDeviceAddressVertex;
		triangles.vertexStride = geom.uVertexStride;
		triangles.indexType = geom.vkIndexType;
		triangles.indexData.deviceAddress = geom.vkDeviceAddressIndex;
		triangles.transformData = {}; // transform
		triangles.maxVertex = geom.uVertexCount - 1;
		triangles.pNext = nullptr;

		VkAccelerationStructureGeometryKHR asGeom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		asGeom.geometry.triangles = triangles;
		asGeom.pNext = nullptr;

		VkAccelerationStructureBuildRangeInfoKHR offset;
		offset.firstVertex = 0u;
		offset.primitiveOffset = 0u;
		offset.primitiveCount = geom.uIndexCount / 3u;
		offset.transformOffset = 0u;

		_inputToFill.vkASGeometries.push_back(asGeom);
		_inputToFill.vkASBuildRangeInfos.push_back(offset);
	}
}

void RayTracingAccelerationStructure::_FillBLASInput(const std::vector<AABBData>& _AABBData, BLASInput& _inputToFill)
{
	for (const auto& geom : _AABBData)
	{
		VkAccelerationStructureGeometryAabbsDataKHR aabb{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };
		aabb.data.deviceAddress = geom.vkDeviceAddressAABB;
		aabb.stride = geom.uAABBStride;

		VkAccelerationStructureGeometryKHR asGeom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		asGeom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
		asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		asGeom.geometry.aabbs = aabb;
		asGeom.pNext = nullptr;

		VkAccelerationStructureBuildRangeInfoKHR offset;
		offset.firstVertex = 0u;
		offset.primitiveOffset = 0u;
		offset.primitiveCount = geom.uAABBCount;
		offset.transformOffset = 0u;

		_inputToFill.vkASGeometries.push_back(asGeom);
		_inputToFill.vkASBuildRangeInfos.push_back(offset);
	}
}

RayTracingAccelerationStructure::RayTracingAccelerationStructure()
{
}

RayTracingAccelerationStructure::RayTracingAccelerationStructure(RayTracingAccelerationStructure&& _other)
{
	this->m_BLASInputs = std::move(_other.m_BLASInputs);
	this->m_compactBLAS = std::move(_other.m_compactBLAS);
	this->m_TLASInput = std::move(_other.m_TLASInput);
	this->m_uptrBLASes = std::move(_other.m_uptrBLASes);
	this->m_uptrTLAS = std::move(_other.m_uptrTLAS);
	this->vkAccelerationStructure = _other.vkAccelerationStructure;
	_other.vkAccelerationStructure = VK_NULL_HANDLE;
	_other.Uninit();
}

RayTracingAccelerationStructure& RayTracingAccelerationStructure::operator=(RayTracingAccelerationStructure&& _other) noexcept
{
	this->m_BLASInputs = std::move(_other.m_BLASInputs);
	this->m_compactBLAS = std::move(_other.m_compactBLAS);
	this->m_uptrBLASes = std::move(_other.m_uptrBLASes);
	this->m_TLASInput = std::move(_other.m_TLASInput);
	this->m_uptrTLAS = std::move(_other.m_uptrTLAS);
	this->vkAccelerationStructure = _other.vkAccelerationStructure;
	_other.vkAccelerationStructure = VK_NULL_HANDLE;
	return *this;
}

RayTracingAccelerationStructure::~RayTracingAccelerationStructure()
{
	assert(vkAccelerationStructure == VK_NULL_HANDLE);
}

uint32_t RayTracingAccelerationStructure::PreAddBLAS(const std::vector<TriangleData>& geomData, VkBuildAccelerationStructureFlagsKHR flags)
{
	uint32_t uRet = static_cast<uint32_t>(m_BLASInputs.size());
	BLASInput blas{};

	_FillBLASInput(geomData, blas);
	blas.vkBuildFlags = flags;

	if ((flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) != 0)
	{
		m_compactBLAS = true;
	}

	m_BLASInputs.push_back(blas);

    return uRet;
}

uint32_t RayTracingAccelerationStructure::PreAddBLAS(const std::vector<AABBData>& geomData, VkBuildAccelerationStructureFlagsKHR flags)
{
	uint32_t uRet = static_cast<uint32_t>(m_BLASInputs.size());
	BLASInput blas{};

	_FillBLASInput(geomData, blas);
	blas.vkBuildFlags = flags;

	if ((flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) != 0)
	{
		m_compactBLAS = true;
	}

	m_BLASInputs.push_back(blas);

	return uRet;
}

void RayTracingAccelerationStructure::PresetTLAS(const std::vector<InstanceData>& instData, VkBuildAccelerationStructureFlagsKHR flags)
{	
	_BuildBLASs();
	m_BLASInputs.clear();

	_FillTLASInput(instData, m_TLASInput);
	m_TLASInput.vkBuildFlags = flags;
}

void RayTracingAccelerationStructure::Init()
{
	_BuildTLAS();
}

void RayTracingAccelerationStructure::UpdateBLAS(const std::vector<TriangleData>& _geomData, uint32_t _BLASIndex, CommandSubmission* _pCmd)
{
	BLASInput input{};
	std::vector<std::unique_ptr<BLAS>> BLASesToUpdate{};
	BLASesToUpdate.push_back(std::move(m_uptrBLASes[_BLASIndex]));
	_FillBLASInput(_geomData, input);
	if (_pCmd == nullptr)
	{
		CommandSubmission tmpCmd{};
		tmpCmd.Init();
		tmpCmd.StartCommands({});
		_BuildOrUpdateBLASes({ input }, BLASesToUpdate, &tmpCmd);
		tmpCmd.SubmitCommandsAndWait();
		tmpCmd.Uninit();
	}
	else
	{
		_BuildOrUpdateBLASes({ input }, BLASesToUpdate, _pCmd);
	}
	m_uptrBLASes[_BLASIndex] = std::move(BLASesToUpdate[0]);
}

void RayTracingAccelerationStructure::UpdateTLAS(const std::vector<InstanceData>& instData, CommandSubmission* pCmd)
{
	std::shared_ptr<TLASInput> tlasInput = std::make_shared<TLASInput>();
	_FillTLASInput(instData, *tlasInput);
	if (pCmd != nullptr)
	{
		_BuildOrUpdateTLAS(*tlasInput, m_uptrTLAS.get(), pCmd);
		pCmd->BindCallback(CommandSubmission::CALLBACK_BINDING_POINT::COMMANDS_DONE,
			[sptr = std::move(tlasInput)](CommandSubmission* _pCmd)
			mutable{
			sptr->Reset();
		});
	}
	else
	{
		CommandSubmission tmpCmd{};
		tmpCmd.Init();
		tmpCmd.StartOneTimeCommands({});
		_BuildOrUpdateTLAS(*tlasInput, m_uptrTLAS.get(), &tmpCmd);
		tmpCmd.SubmitCommands();
		tlasInput->Reset();
	}
}

void RayTracingAccelerationStructure::Uninit()
{
	if (m_uptrTLAS.get() != nullptr)
	{
		m_uptrTLAS->Uninit();
	}
	m_uptrTLAS.reset();
	vkAccelerationStructure = VK_NULL_HANDLE;
	m_TLASInput.Reset();
	for (auto& curBLAS : m_uptrBLASes)
	{
		curBLAS->Uninit();
	}
	m_uptrBLASes.clear();
	m_BLASInputs.clear();
}

void RayTracingAccelerationStructure::RecordPipelineBarrier(CommandSubmission* pCmd)
{
	VkDependencyInfo dependencyInfo = {};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

	VkMemoryBarrier2 bufferMemoryBarrier = {};
	bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	bufferMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	bufferMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
	bufferMemoryBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	bufferMemoryBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;

	dependencyInfo.memoryBarrierCount = 1;
	dependencyInfo.pMemoryBarriers = &bufferMemoryBarrier;

	pCmd->_AddPipelineBarrier2(dependencyInfo);
}

void RayTracingAccelerationStructure::BLAS::Init(VkDeviceSize size)
{
	Buffer::CreateInformation bufferInfo{};
	VkAccelerationStructureCreateInfoKHR info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR vkASAddrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };

	info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	info.size = size;
	uptrBuffer = std::make_unique<Buffer>();

	bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	bufferInfo.size = info.size;
	uptrBuffer->PresetCreateInformation(bufferInfo);
	uptrBuffer->Init();
	info.buffer = uptrBuffer->m_vkBuffer;

	VK_CHECK(vkCreateAccelerationStructureKHR(MyDevice::GetInstance().vkDevice, &info, nullptr, &vkAccelerationStructure), "Failed to create BLAS!");

	vkASAddrInfo.accelerationStructure = vkAccelerationStructure;
	vkASAddrInfo.pNext = nullptr;
	vkDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(MyDevice::GetInstance().vkDevice, &vkASAddrInfo);
}

void RayTracingAccelerationStructure::BLAS::Uninit()
{
	if (uptrBuffer.get() != nullptr)
	{
		uptrBuffer->Uninit();
		uptrBuffer.reset();
	}
	if (vkAccelerationStructure != VK_NULL_HANDLE)
	{
		vkDestroyAccelerationStructureKHR(MyDevice::GetInstance().vkDevice, vkAccelerationStructure, nullptr);
		vkAccelerationStructure = VK_NULL_HANDLE;
	}
}

void RayTracingAccelerationStructure::TLAS::Init(VkDeviceSize ASSize)
{
	// create VkAcceleartionStructure
	VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	Buffer::CreateInformation bufferInfo{};

	if (uptrBuffer.get() != nullptr)
	{
		uptrBuffer->Uninit();
		uptrBuffer.reset();
	}
	uptrBuffer = std::make_unique<Buffer>();
	bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.size = ASSize;
	bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	uptrBuffer->PresetCreateInformation(bufferInfo);
	uptrBuffer->Init();

	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	createInfo.size = ASSize;
	createInfo.pNext = nullptr;
	createInfo.offset = 0;
	createInfo.buffer = uptrBuffer->m_vkBuffer;
	createInfo.deviceAddress = 0; // https://registry.khronos.org/vulkan/specs/latest/man/html/VkAccelerationStructureCreateInfoKHR.html
	createInfo.createFlags = 0;

	VK_CHECK(vkCreateAccelerationStructureKHR(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &vkAccelerationStructure), "Failed to create TLAS!");

	addrInfo.accelerationStructure = vkAccelerationStructure;
	
	vkDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(MyDevice::GetInstance().vkDevice, &addrInfo);
}

void RayTracingAccelerationStructure::TLAS::Uninit()
{
	if (uptrBuffer.get() != nullptr)
	{
		uptrBuffer->Uninit();
		uptrBuffer.reset();
	}
	if (vkAccelerationStructure != VK_NULL_HANDLE)
	{
		vkDestroyAccelerationStructureKHR(MyDevice::GetInstance().vkDevice, vkAccelerationStructure, nullptr);
		vkAccelerationStructure = VK_NULL_HANDLE;
	}
}

void RayTracingAccelerationStructure::TLASInput::Reset()
{
	if (uptrInstanceBuffer.get() != nullptr)
	{
		uptrInstanceBuffer->Uninit();
		uptrInstanceBuffer.reset();
	}
	*this = TLASInput{};
}

void TopLevelAccelStruct::Init(const ITopLevelAccelStructInitializer* inInitializer)
{
	Uninit();

	inInitializer->InitAccelStruct(this);

	CHECK_TRUE(m_vkAccelStruct != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrBuffer != nullptr);
}

auto TopLevelAccelStruct::GetVkAccelerationStructure() const -> VkAccelerationStructureKHR
{
	return m_vkAccelStruct;
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

void TopLevelAccelStruct::Initializer::_LoadDataFromInputToInstanceBuffer()
{
	std::vector<VkAccelerationStructureInstanceKHR> accelStructInstances;
	uint32_t gl_InstanceCustomIndex = 0u;

	accelStructInstances.reserve(m_instanceData.size());
	for (const auto& instance : m_instanceData)
	{
		VkAccelerationStructureInstanceKHR accelStructInstance{};
		accelStructInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		accelStructInstance.mask = 0xFF;
		accelStructInstance.instanceCustomIndex = gl_InstanceCustomIndex;
		accelStructInstance.accelerationStructureReference = instance.accelStructAddress;
		accelStructInstance.instanceShaderBindingTableRecordOffset = instance.shaderGroupOffset;
		accelStructInstance.transform = instance.

		gl_InstanceCustomIndex++;
		accelStructInstances.emplace_back(accelStructInstance);
	}
}

void TopLevelAccelStruct::Initializer::_PrepareGeometry(std::vector<VkAccelerationStructureGeometryKHR>& outGeometries) const
{
	outGeometries.clear();
	VkAccelerationStructureGeometryKHR	accelStructGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	accelStructGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelStructGeometry.geometry.instances = 
}

void TopLevelAccelStruct::Initializer::InitAccelStruct(TopLevelAccelStruct* outTLAS) const
{
	VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	VkAccelerationStructureGeometryKHR	geometryInfo{};
	VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
	VkAccelerationStructureBuildGeometryInfoKHR buildGeomInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	auto mutableThis = const_cast<Initializer*>(this);
	auto uptrTLASBuffer = std::make_unique<Buffer>();
	Buffer::Initializer bufferInitializer{};
	auto& device = MyDevice::GetInstance();

	mutableThis->_PrepareInitialization();
	bufferInitializer.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
	bufferInitializer.SetBufferSize(buildSizeInfo.accelerationStructureSize);
	m_pCmd->CmdBuildAccelerationStructuresKHR({buildGeomInfo}, {&buildRangeInfo});

	// create VkAcceleartionStructure
	Buffer::CreateInformation bufferInfo{};

	if (uptrBuffer.get() != nullptr)
	{
		uptrBuffer->Uninit();
		uptrBuffer.reset();
	}
	uptrBuffer = std::make_unique<Buffer>();
	bufferInfo.optMemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.size = ASSize;
	bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	uptrBuffer->PresetCreateInformation(bufferInfo);
	uptrBuffer->Init();

	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	createInfo.size = ASSize;
	createInfo.pNext = nullptr;
	createInfo.offset = 0;
	createInfo.buffer = uptrBuffer->m_vkBuffer;
	createInfo.deviceAddress = 0; // https://registry.khronos.org/vulkan/specs/latest/man/html/VkAccelerationStructureCreateInfoKHR.html
	createInfo.createFlags = 0;

	VK_CHECK(vkCreateAccelerationStructureKHR(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &vkAccelerationStructure), "Failed to create TLAS!");

	addrInfo.accelerationStructure = vkAccelerationStructure;

	vkDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(MyDevice::GetInstance().vkDevice, &addrInfo);
}
