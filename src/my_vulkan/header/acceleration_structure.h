#pragma once
#include "common.h";
#include "buffer.h"

class CommandSubmission;

class RayTracingAccelerationStructure final
{
	// A TLAS has multiple Instances,
	// each Instance refers to ONE BLAS,
	// different instances can refer to the same BLAS,
	// we can use gl_InstanceCustomIndexEXT to refer them in shaders.
	// Normally each BLAS only holds one Model,
	// and we can use gl_PrimitiveID to get triangle hit of that model,
	// but one BLAS holding multiple models is also allowed, 
	// in that case, gl_PrimitiveID will be the offset plus the triangle ID in the hit model
	// ===================
	// UPDATE RULES£º
	// ===================
	// An update operation imposes certain constraints on the input, in exchange for considerably faster execution.
	// When performing an update, the application is required to provide a full description of the acceleration structure, 
	// but is prohibited from changing anything other than instance definitions, transform matrices, 
	// and vertex or AABB positions.All other aspects of the description must exactly match the one from the original build.
	// 
	// More precisely, the application must not use an update operation to do any of the following :
	//  Change primitives or instances from active to inactive, or vice versa(as defined in Inactive Primitives and Instances).
	//	Change the index or vertex formats of triangle geometry.
	//	Change triangle geometry transform pointers from null to non - null or vice versa.
	//	Change the number of geometries or instances in the structure.
	//	Change the geometry flags for any geometry in the structure.
	//	Change the number of vertices or primitives for any geometry in the structure.
private:
	struct TLASInput
	{
		VkAccelerationStructureGeometryKHR				vkASGeometry;
		VkAccelerationStructureBuildRangeInfoKHR		vkASBuildRangeInfo;
		VkBuildAccelerationStructureFlagsKHR			vkBuildFlags = 0;
		std::unique_ptr<Buffer>							uptrInstanceBuffer;

		void Reset();
	};
	struct BLASInput
	{
		std::vector<VkAccelerationStructureGeometryKHR>			vkASGeometries;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR>	vkASBuildRangeInfos;
		VkBuildAccelerationStructureFlagsKHR					vkBuildFlags = 0;
	};
	struct BLAS
	{
		std::unique_ptr<Buffer> uptrBuffer;
		VkAccelerationStructureKHR vkAccelerationStructure = VK_NULL_HANDLE;
		VkDeviceAddress vkDeviceAddress = 0;
		VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0; // used later in update, it must be the same as it when the BLAS is built

		BLAS() {};
		BLAS(BLAS&& other) { 
			uptrBuffer = std::move(other.uptrBuffer);
			vkAccelerationStructure = other.vkAccelerationStructure;
			vkDeviceAddress = other.vkDeviceAddress;
			vkBuildFlags = other.vkBuildFlags;
		};
		~BLAS() { assert(vkAccelerationStructure == VK_NULL_HANDLE); };

		// create BLAS not build it
		void Init(VkDeviceSize size);
		void Uninit();
	};
	struct TLAS {
		std::unique_ptr<Buffer> uptrBuffer;
		VkAccelerationStructureKHR vkAccelerationStructure = VK_NULL_HANDLE;
		VkDeviceAddress vkDeviceAddress = 0;
		VkBuildAccelerationStructureFlagsKHR vkBuildFlags = 0; // used later in update, it must be the same as it when the BLAS is built

		~TLAS() { assert(vkAccelerationStructure == VK_NULL_HANDLE); };
		
		// create TLAS not build it
		void Init(VkDeviceSize size);
		void Uninit();
	};

public:
	struct TriangleData
	{
		VkDeviceAddress vkDeviceAddressVertex;
		uint32_t		uVertexStride; // first 3 floats must be position
		uint32_t		uVertexCount;
		VkDeviceAddress vkDeviceAddressIndex;
		VkIndexType		vkIndexType;
		uint32_t		uIndexCount;
	};
	struct AABBData
	{
		VkDeviceAddress vkDeviceAddressAABB;
		uint32_t		uAABBStride; // first must be VkAabbPositionsKHR
		uint32_t		uAABBCount;
	};
	struct InstanceData
	{
		uint32_t	uBLASIndex;
		// offset apply to the shader hit group in shader,
		// in case when AABB BLASes and Triangle BLASes are all in the scene, 
		// and different shader group need to be used,
		// but in rgen we can only set 1 shader hit group when traceRayEXT
		uint32_t	uHitShaderGroupIndexOffset; 
		glm::mat4   transformMatrix = glm::mat4(1.0f);
		// By default, I just use the index of input array for gl_InstanceCustomIndex;
		std::optional<uint32_t> optCustomIndex;
	};

private:
	std::vector<BLASInput>								m_BLASInputs; // hold info temporarily, will be invalid after Init
	TLASInput													m_TLASInput; // hold info temporarily, will be invalid after Init
	std::vector<std::unique_ptr<BLAS>>		m_uptrBLASes;
	std::unique_ptr<TLAS>								m_uptrTLAS;
	bool																m_compactBLAS = false; // if one of BLASes allows compact, we build all BLASes compacted

public:
	VkAccelerationStructureKHR vkAccelerationStructure = VK_NULL_HANDLE;

private:
	// Helper function to initialize scratch buffer, 
	// maxBudget is the maximum size buffer can be used to build AS concurrently,
	// bForBuild is used to check whether this scratch buffer is used for build or update,
	// scratch buffer will be init inside,
	// scratch addresses are the slot addresses for each of the BLASes to build,
	// the number of slots may be smaller than the BLAS count, then several loops are required
	static void _InitScratchBuffer(
		VkDeviceSize maxBudget,
		const std::vector<VkAccelerationStructureBuildSizesInfoKHR>& buildSizeInfo,
		bool bForBuild,
		Buffer& scratchBufferToInit,
		std::vector<VkDeviceAddress>& slotAddresses);

	// Build all BLASs, after all BLASInputs are added
	void _BuildBLASs();

	// Build BLASes if the _BLASes is empty, otherwise, update it.
	// If this function is used for build, then _pCmd will not be used, 
	// an internal command buffer will be created and this function will wait till the output BLASes is ready to use.
	// If this function is used for update, then _pCmd must not be nullptr
	static void _BuildOrUpdateBLASes(
		const std::vector<BLASInput>& _inputs, 
		std::vector<std::unique_ptr<BLAS>>& _BLASes,
		CommandSubmission* _pCmd = nullptr,
		VkDeviceSize maxBudget = 256'000'000);

	// Build TLAS
	void _BuildTLAS();

	// Fill TLAS input with the instance data, PS. cannot be static
	void _FillTLASInput(const std::vector<InstanceData>& instData, TLASInput& inputToFill) const;

	// Build TLAS if the vkAccelerationStructure of the input TLAS is VK_NULL_HANDLE, else, update it.
	// The function will only record the commands to the command buffer and manage scratch buffers,
	// so TLASInput should be reset manually after the commands done.
	static void _BuildOrUpdateTLAS(const TLASInput& _input, TLAS* _pTLAS, CommandSubmission* _pCmd);

	// Put triangle data into the BLASInput. 
	// The function only adds geometry to the BLASInput, and will not clear it even if the BLASInput already has some geometries inside.
	static void _FillBLASInput(const std::vector<TriangleData>& _trigData, BLASInput& _inputToFill);

	// Put AABB data into the BLASInput. 
	// The function only adds geometry to the BLASInput, and will not clear it even if the BLASInput already has some geometries inside.
	static void _FillBLASInput(const std::vector<AABBData>& _AABBData, BLASInput& _inputToFill);

public:
	RayTracingAccelerationStructure();
	RayTracingAccelerationStructure(RayTracingAccelerationStructure&& _other);
	RayTracingAccelerationStructure(const RayTracingAccelerationStructure& _other) = delete;
	RayTracingAccelerationStructure& operator=(const RayTracingAccelerationStructure& _other) = delete;
	RayTracingAccelerationStructure& operator=(RayTracingAccelerationStructure&& _other) noexcept;
	~RayTracingAccelerationStructure();

	// Add a BLAS to this acceleration structure, 
	// return the index of this BLAS in the vector of BLASs this acceleration structure holds
	uint32_t PreAddBLAS(
		const std::vector<TriangleData>& geomData,
		VkBuildAccelerationStructureFlagsKHR flags = 
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR 
		| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR 
		| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

	uint32_t PreAddBLAS(
		const std::vector<AABBData>& geomData,
		VkBuildAccelerationStructureFlagsKHR flags =
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
		| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
		| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

	// Set up TLAS after all BLASs are added, TLAS will not be created or be built, we do it in Init
	void PresetTLAS(
		const std::vector<InstanceData>& instData, 
		VkBuildAccelerationStructureFlagsKHR flags = 
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR 
		| VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
	
	// Build AS after TLAS's setup, wait till done
	void Init();

	// Update BLAS in this acceleration structure, 
	// user needs to update TLAS after update BLASes,
	// no need to synchronize TLAS update and BLASes update,
	// it is done inside
	void UpdateBLAS(const std::vector<TriangleData>& _geomData, uint32_t _BLASIndex, CommandSubmission* _pCmd = nullptr);

	// Update TLAS in this acceleration structure, 
	// size of instData vector must be the same as the previous instData that builds this acceleration structure,
	// if pCmd is provided, the user needs to synchronize commands
	void UpdateTLAS(const std::vector<InstanceData>& instData, CommandSubmission* pCmd = nullptr);

	void Uninit();

	// Record pipeline barrier to wait build/update AS done
	static void RecordPipelineBarrier(CommandSubmission* pCmd);
};