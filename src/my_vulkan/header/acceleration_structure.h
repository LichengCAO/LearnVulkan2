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

class TopLevelAccelStruct;
class BottomLevelAccelStruct;
struct VkAccelStructBuildInfo
{
	VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo;

	// This array should be the size of geometry count.
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesInfos;

	// The result that we store here is that VkAccelerationStructureBuildGeometryInfoKHR will
	// refer to an array of VkAccelerationStructureGeometryKHR, we want to make sure it's valid.
	// This array should be the size of geometry count according to the specification.
	std::vector<VkAccelerationStructureGeometryKHR> geometries;
};

#pragma region BLAS
/// <summary>
/// Bottom-Level Acceleration Structure (BLAS) implementation
/// Encapsulates Vulkan BLAS creation, management, and update logic
/// Complies with Vulkan spec requirements (all geometries in a BLAS must share the same type)
/// </summary>
class BottomLevelAccelStruct final
{
public:
    /// <summary>
    /// Describes a single geometry element (triangles/AABBs) for BLAS construction
    /// Maps to Vulkan's VkAccelerationStructureGeometryKHR with simplified, type-safe fields
    /// </summary>
    struct GeometryDescription
    {
    public:
        /// <summary>
        /// Triangle-specific geometry parameters
        /// Used when geometry type is VK_GEOMETRY_TYPE_TRIANGLES_KHR
        /// </summary>
        struct Triangles
        {
            VkDeviceAddress vertexBufferAddress;  // GPU device address of vertex buffer (ignored in build size calculation)
            VkDeviceAddress indexBufferAddress;   // GPU device address of index buffer (ignored in build size calculation)
            uint32_t vertexStride;                // Byte stride between consecutive vertices in vertex buffer
            uint32_t vertexCount;                 // Total number of vertices in the vertex buffer
            std::optional<VkIndexType> indexType; // Type of index data (e.g., VK_INDEX_TYPE_UINT32) - optional
            std::optional<VkFormat> vertexFormat; // Format of vertex data (e.g., VK_FORMAT_R32G32B32_SFLOAT) - optional
            std::optional<VkDeviceAddress> transform; // Device address of transform buffer (optional per-geometry transform)
        };

        /// <summary>
        /// AABB (Axis-Aligned Bounding Box) specific geometry parameters
        /// Used when geometry type is VK_GEOMETRY_TYPE_AABBS_KHR
        /// </summary>
        struct AABBs
        {
            VkDeviceAddress aabbBufferAddress; // GPU device address of AABB buffer (ignored in build size calculation)
            uint32_t stride;                   // Byte stride between consecutive AABBs in buffer
        };

    public:
        uint32_t primitiveCount; // Total number of primitives (triangles/AABBs) in this geometry
        std::optional<VkGeometryFlagsKHR> flags; // Geometry flags (e.g., VK_GEOMETRY_OPAQUE_BIT_KHR) - optional
        /// <summary>
        /// Union to store either triangle or AABB data (mutually exclusive based on geometry type)
        /// Ensures memory efficiency - only one geometry type is active at a time
        /// </summary>
        union
        {
            GeometryDescription::Triangles triangles; // Triangle geometry data
            GeometryDescription::AABBs aabbs;         // AABB geometry data
        };
    };

    /// <summary>
    /// Describes mutable geometry attributes for BLAS updates (post-construction)
    /// Only contains modifiable fields (buffer addresses/transforms - no format/stride changes)
    /// </summary>
    struct GeometryUpdateDescription
    {
    public:
        /// <summary>
        /// Mutable triangle geometry parameters for BLAS updates
        /// </summary>
        struct Triangles
        {
            VkDeviceAddress vertexBufferAddress; // Updated vertex buffer device address
            VkDeviceAddress indexBufferAddress;  // Updated index buffer device address
            std::optional<VkDeviceAddress> transform; // Updated transform buffer device address (optional)
        };

        /// <summary>
        /// Mutable AABB geometry parameters for BLAS updates
        /// </summary>
        struct AABBs
        {
            VkDeviceAddress aabbBufferAddress; // Updated AABB buffer device address
        };

    public:
        /// <summary>
        /// Union for mutable triangle/AABB data (matches GeometryDescription's type)
        /// </summary>
        union
        {
            GeometryUpdateDescription::Triangles triangles; // Mutable triangle data
            GeometryUpdateDescription::AABBs aabbs;         // Mutable AABB data
        };
    };

    // Forward declaration for BuildHelper (friend class access)
    class BuildHelper;

    /// <summary>
    /// Descriptor class to hold BLAS construction/update data
    /// Manages Vulkan geometry/range structures and enforces BLAS type consistency
    /// </summary>
    class Descriptor
    {
    private:
        /// <summary>
        /// Vulkan build range info for each geometry (defines primitive ranges to build)
        /// Maps 1:1 with m_dataSources (each geometry has one range info)
        /// </summary>
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_dataRanges;

        /// <summary>
        /// Vulkan geometry info for each geometry element (defines data format/sources)
        /// Maps 1:1 with m_dataRanges (same size requirement)
        /// </summary>
        std::vector<VkAccelerationStructureGeometryKHR> m_dataSources;

        /// <summary>
        /// Enforces Vulkan spec requirement: all geometries in a BLAS must share the same type
        /// See Vulkan VUIDs:
        /// - VUID-VkAccelerationStructureBuildGeometryInfoKHR-type-03791
        /// - VUID-VkAccelerationStructureBuildGeometryInfoKHR-type-03792
        /// </summary>
        VkGeometryTypeKHR m_geometryType = VK_GEOMETRY_TYPE_MAX_ENUM_KHR;

    public:
        /// <summary>
        /// Sets the geometry type for all elements in this BLAS (enforces type consistency)
        /// Must be called before adding geometries
        /// </summary>
        /// <param name="inType">Geometry type (VK_GEOMETRY_TYPE_TRIANGLES_KHR/AABBS_KHR)</param>
        void SetGeometryType(VkGeometryTypeKHR inType) noexcept { m_geometryType = inType; };

        /// <summary>
        /// Adds multiple geometries to the BLAS descriptor
        /// Validates type consistency with m_geometryType
        /// </summary>
        /// <param name="inDescriptions">Array of geometry descriptions to add</param>
        /// <param name="inCount">Number of geometries in the input array</param>
        void AddGeometries(const BottomLevelAccelStruct::GeometryDescription* inDescriptions, uint32_t inCount);

        /// <summary>
        /// Calculates required build sizes (acceleration structure/scratch buffer) for BLAS construction
        /// Uses Vulkan's vkGetAccelerationStructureBuildSizesKHR under the hood
        /// </summary>
        /// <param name="inFlags">Build flags (e.g., VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR)</param>
        /// <returns>Vulkan build sizes info struct</returns>
        auto GetBuildSizesInfo(VkBuildAccelerationStructureFlagsKHR inFlags) const noexcept->VkAccelerationStructureBuildSizesInfoKHR;

        /// <summary>
        /// Updates a range of existing geometries in the BLAS descriptor
        /// Only modifies mutable fields (buffer addresses/transforms)
        /// </summary>
        /// <param name="inUpdateDescriptions">Array of update descriptions</param>
        /// <param name="inBeginIndex">Start index of geometries to update</param>
        /// <param name="inCount">Number of geometries to update</param>
        void SetGeometries(const BottomLevelAccelStruct::GeometryUpdateDescription* inUpdateDescriptions, uint32_t inBeginIndex, uint32_t inCount);

        /// <summary>
        /// Gets the total number of geometries in the descriptor
        /// </summary>
        /// <returns>Count of geometries (matches m_dataSources/m_dataRanges size)</returns>
        auto GetGeometryCount() const noexcept -> uint32_t { return static_cast<uint32_t>(m_dataRanges.size()); };

        // Grant BuildHelper access to private Vulkan structure data
        friend class BottomLevelAccelStruct::BuildHelper;
    };

    /// <summary>
    /// Creator class for BottomLevelAccelStruct initialization
    /// Configures build flags and descriptor before creating the Vulkan BLAS object
    /// </summary>
    class Creator
    {
    private:
        VkBuildAccelerationStructureFlagsKHR m_flags = 0; // BLAS build flags
        const BottomLevelAccelStruct::Descriptor* m_descriptor = nullptr; // BLAS descriptor with geometry data

    public:
        /// <summary>
        /// Sets custom build flags for BLAS construction
        /// Overrides default flags (0) with user-specified values
        /// </summary>
        /// <param name="inFlags">Vulkan build flags (e.g., prefer fast trace/build)</param>
        void CustomizeFeatureFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept { m_flags = inFlags; };

        /// <summary>
        /// Associates a descriptor with the creator (provides geometry data for BLAS)
        /// Must be called before Create()
        /// </summary>
        /// <param name="inDescriptor">Pointer to valid BLAS descriptor</param>
        void SetDescriptor(const BottomLevelAccelStruct::Descriptor* inDescriptor) noexcept { m_descriptor = inDescriptor; };

        /// <summary>
        /// Creates and initializes a Vulkan BLAS object from the configured creator
        /// Populates the input BottomLevelAccelStruct with Vulkan handles/buffers
        /// </summary>
        /// <param name="inoutAccelStruct">Pointer to BLAS object to initialize</param>
        void Create(BottomLevelAccelStruct* inoutAccelStruct) const;
    };

    /// <summary>
    /// Build helper class to populate Vulkan build/update command structures
    /// Abstracts Vulkan API details for BLAS build/update operations
    /// </summary>
    class BuildHelper
    {
    private:
        const BottomLevelAccelStruct::Descriptor* m_descriptor = nullptr; // BLAS descriptor with geometry data
        VkDeviceAddress m_scratchBufferAddress = 0; // GPU device address of scratch buffer (required for BLAS build)

    public:
        /// <summary>
        /// Resets the build helper to default state (clears all set parameters)
        /// </summary>
        void Reset() noexcept { *this = BuildHelper{}; };

        /// <summary>
        /// Sets the BLAS descriptor for build/update operations
        /// Provides geometry/range data to populate Vulkan command structures
        /// </summary>
        /// <param name="inDescriptor">Pointer to valid BLAS descriptor</param>
        void SetDescriptor(const BottomLevelAccelStruct::Descriptor* inDescriptor) noexcept { m_descriptor = inDescriptor; };

        /// <summary>
        /// Sets the GPU device address of the scratch buffer
        /// Scratch buffer is required for temporary BLAS build operations
        /// </summary>
        /// <param name="inAddress">Device address of scratch buffer</param>
        void SetScratchBufferAddress(VkDeviceAddress inAddress) noexcept { m_scratchBufferAddress = inAddress; };

        /// <summary>
        /// Populates Vulkan build info structure for BLAS creation
        /// Used to record vkCmdBuildAccelerationStructuresKHR commands
        /// </summary>
        /// <param name="inDstAccelStruct">Destination BLAS object to build</param>
        /// <param name="outBuildInfo">Output Vulkan build info struct to populate</param>
        void GetBuildInfo(
            const BottomLevelAccelStruct* inDstAccelStruct,
            VkAccelStructBuildInfo& outBuildInfo) const noexcept;

        /// <summary>
        /// Populates Vulkan update info structure for BLAS modification
        /// Used to record incremental BLAS updates (avoids full rebuild)
        /// </summary>
        /// <param name="inSrcAccelStruct">Source BLAS (original state)</param>
        /// <param name="inDstAccelStruct">Destination BLAS (updated state)</param>
        /// <param name="outBuildInfo">Output Vulkan build info struct to populate</param>
        void GetUpdateInfo(
            const BottomLevelAccelStruct* inSrcAccelStruct,
            const BottomLevelAccelStruct* inDstAccelStruct,
            VkAccelStructBuildInfo& outBuildInfo) const noexcept;
    };

private:
    VkAccelerationStructureKHR m_vkAccelStruct = VK_NULL_HANDLE; // Vulkan BLAS handle
    VkBuildAccelerationStructureFlagsKHR m_flags = 0; // Flags used to build the BLAS
    std::unique_ptr<Buffer> m_uptrBuffer; // Host/GPU buffer storing BLAS data
    uint64_t m_accelStructReference = 0; // Internal reference counter/identifier

public:
    /// <summary>
    /// Initializes the BLAS object using a Creator (creates Vulkan handle/buffers)
    /// Must be called before using the BLAS for ray tracing
    /// </summary>
    /// <param name="inCreator">Configured Creator object</param>
    void Init(const Creator* inCreator);

    /// <summary>
    /// Gets the internal reference identifier for the BLAS
    /// Used for tracking/referencing BLAS instances (application-specific)
    /// </summary>
    /// <returns>64-bit reference value</returns>
    auto GetReference() const noexcept-> uint64_t { return m_accelStructReference; };

    /// <summary>
    /// Gets the Vulkan BLAS handle for ray tracing operations
    /// </summary>
    /// <returns>VkAccelerationStructureKHR handle (VK_NULL_HANDLE if uninitialized)</returns>
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR { return m_vkAccelStruct; };

    /// <summary>
    /// Gets the build flags used to create the BLAS
    /// </summary>
    /// <returns>Vulkan build flags (e.g., prefer fast trace/build)</returns>
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; };

    /// <summary>
    /// Cleans up Vulkan resources (BLAS handle/buffer)
    /// Must be called to avoid resource leaks
    /// </summary>
    void Uninit();
};

#pragma endregion

#pragma region TLAS
/// <summary>
/// Top-Level Acceleration Structure (TLAS) implementation
/// Encapsulates Vulkan TLAS creation, management, and update logic
/// References BLAS instances to form a complete ray tracing scene hierarchy
/// </summary>
class TopLevelAccelStruct final
{
public:
    /// <summary>
    /// Describes a single BLAS instance in the TLAS
    /// Maps to Vulkan's VkAccelerationStructureInstanceKHR with simplified, type-safe fields
    /// </summary>
    struct InstanceDescription
    {
        glm::mat4 transform; // Model-to-world transform matrix for the BLAS instance
        const BottomLevelAccelStruct* accelStruct; // Pointer to the BLAS to instance
        uint32_t shaderGroupIndexOffset; // Offset for shader binding table (SBT) indexing
        uint32_t instanceCustomIndex; // Custom index (accessible in shaders via gl_InstanceCustomIndexEXT)
        uint32_t mask = 0xFF; // Visibility mask (0xFF = visible to all rays)
        VkGeometryInstanceFlagsKHR flags; // Instance flags (e.g., VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR)
    };

    /// <summary>
    /// Describes mutable instance attributes for TLAS updates (post-construction)
    /// Alias of InstanceDescription (all TLAS instance fields are mutable)
    /// </summary>
    using InstanceUpdateDescription = InstanceDescription;

    /// <summary>
    /// Descriptor class to hold TLAS construction/update data
    /// Manages Vulkan instance structures for TLAS creation
    /// </summary>
    class Descriptor
    {
    private:
        /// <summary>
        /// Vulkan instance structures for all BLAS instances in the TLAS
        /// Directly used for TLAS build operations (mapped to GPU memory)
        /// </summary>
        std::vector<VkAccelerationStructureInstanceKHR> m_accelStructInstances;

    public:
        /// <summary>
        /// Adds multiple BLAS instances to the TLAS descriptor
        /// Converts InstanceDescription to Vulkan's VkAccelerationStructureInstanceKHR
        /// </summary>
        /// <param name="inDescriptions">Array of instance descriptions to add</param>
        /// <param name="inCount">Number of instances in the input array</param>
        /// <returns>Start index of the added instances</returns>
        auto AddInstances(const InstanceDescription* inDescriptions, uint32_t inCount) -> uint32_t;

        /// <summary>
        /// Updates a range of existing instances in the TLAS descriptor
        /// Modifies instance attributes (transform/mask/flags etc.)
        /// </summary>
        /// <param name="inUpdateDescriptions">Array of update descriptions</param>
        /// <param name="inBeginIndex">Start index of instances to update</param>
        /// <param name="inCount">Number of instances to update</param>
        void SetInstances(const InstanceUpdateDescription* inUpdateDescriptions, uint32_t inBeginIndex, uint32_t inCount);

        /// <summary>
        /// Calculates required build sizes (acceleration structure/scratch buffer) for TLAS construction
        /// Uses Vulkan's vkGetAccelerationStructureBuildSizesKHR under the hood
        /// </summary>
        /// <param name="inFlags">Build flags (e.g., VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR)</param>
        /// <returns>Vulkan build sizes info struct</returns>
        auto GetBuildSizesInfo(VkBuildAccelerationStructureFlagsKHR inFlags) const->VkAccelerationStructureBuildSizesInfoKHR;

        /// <summary>
        /// Gets the raw Vulkan instance buffer data for GPU mapping
        /// Used to copy instance data to GPU memory for TLAS build
        /// </summary>
        /// <returns>Const pointer to VkAccelerationStructureInstanceKHR array</returns>
        auto GetInstanceBufferContent() const noexcept->const VkAccelerationStructureInstanceKHR* { return m_accelStructInstances.data(); };

        /// <summary>
        /// Gets the total number of instances in the descriptor
        /// </summary>
        /// <returns>Count of BLAS instances in the TLAS</returns>
        auto GetInstanceCount() const noexcept->uint32_t { return static_cast<uint32_t>(m_accelStructInstances.size()); };
    };

    /// <summary>
    /// Creator class for TopLevelAccelStruct initialization
    /// Configures build flags and descriptor before creating the Vulkan TLAS object
    /// </summary>
    class Creator
    {
    private:
        VkBuildAccelerationStructureFlagsKHR m_flags; // TLAS build flags
        const TopLevelAccelStruct::Descriptor* m_descriptor; // TLAS descriptor with instance data

    public:
        /// <summary>
        /// Sets custom build flags for TLAS construction (fluent interface)
        /// Returns reference to self for method chaining
        /// </summary>
        /// <param name="inFlags">Vulkan build flags (e.g., prefer fast trace/build)</param>
        /// <returns>Reference to Creator (for chaining)</returns>
        auto CustomizeFeatureFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept -> TopLevelAccelStruct::Creator&
        {
            m_flags = inFlags;
            return *this;
        };

        /// <summary>
        /// Associates a descriptor with the creator (provides instance data for TLAS)
        /// Returns reference to self for method chaining
        /// </summary>
        /// <param name="inDescriptor">Pointer to valid TLAS descriptor</param>
        /// <returns>Reference to Creator (for chaining)</returns>
        auto SetDescriptor(const TopLevelAccelStruct::Descriptor* inDescriptor) noexcept -> TopLevelAccelStruct::Creator&
        {
            m_descriptor = inDescriptor;
            return *this;
        };

        /// <summary>
        /// Creates and initializes a Vulkan TLAS object from the configured creator
        /// Populates the input TopLevelAccelStruct with Vulkan handles/buffers
        /// </summary>
        /// <param name="inoutAccelStruct">Pointer to TLAS object to initialize</param>
        void Create(TopLevelAccelStruct* inoutAccelStruct) const;
    };

    /// <summary>
    /// Build helper class to populate Vulkan build/update command structures
    /// Abstracts Vulkan API details for TLAS build/update operations
    /// </summary>
    class BuildHelper
    {
    private:
        const TopLevelAccelStruct::Descriptor* m_descriptor; // TLAS descriptor with instance data
        VkDeviceAddress m_scratchBuffer; // GPU device address of scratch buffer (required for TLAS build)
        VkDeviceAddress m_instanceBuffer; // GPU device address of instance buffer (holds TLAS instance data)

    public:
        /// <summary>
        /// Resets the build helper to default state (clears all set parameters)
        /// </summary>
        void Reset() { *this = BuildHelper{}; };

        /// <summary>
        /// Sets the TLAS descriptor for build/update operations
        /// Provides instance data to populate Vulkan command structures
        /// </summary>
        /// <param name="inDescriptor">Pointer to valid TLAS descriptor</param>
        void SetDescriptor(const TopLevelAccelStruct::Descriptor* inDescriptor) { m_descriptor = inDescriptor; }

        /// <summary>
        /// Sets the GPU device address of the scratch buffer
        /// Scratch buffer is required for temporary TLAS build operations
        /// </summary>
        /// <param name="inAddress">Device address of scratch buffer</param>
        void SetScratchBufferAddress(VkDeviceAddress inAddress) { m_scratchBuffer = inAddress; };

        /// <summary>
        /// Sets the GPU device address of the instance buffer
        /// Instance buffer holds TLAS instance data (copied from descriptor)
        /// </summary>
        /// <param name="inAddress">Device address of instance buffer</param>
        void SetInstanceBufferAddress(VkDeviceAddress inAddress) { m_instanceBuffer = inAddress; };

        /// <summary>
        /// Populates Vulkan build info structure for TLAS creation
        /// Used to record vkCmdBuildAccelerationStructuresKHR commands
        /// </summary>
        /// <param name="inDstAccelStruct">Destination TLAS object to build</param>
        /// <param name="outBuildInfo">Output Vulkan build info struct to populate</param>
        void GetBuildInfo(
            const TopLevelAccelStruct* inDstAccelStruct,
            VkAccelStructBuildInfo& outBuildInfo) const noexcept;

        /// <summary>
        /// Populates Vulkan update info structure for TLAS modification
        /// Used to record incremental TLAS updates (avoids full rebuild)
        /// </summary>
        /// <param name="inSrcAccelStruct">Source TLAS (original state)</param>
        /// <param name="inDstAccelStruct">Destination TLAS (updated state)</param>
        /// <param name="outBuildInfo">Output Vulkan build info struct to populate</param>
        void GetUpdateInfo(
            const TopLevelAccelStruct* inSrcAccelStruct,
            const TopLevelAccelStruct* inDstAccelStruct,
            VkAccelStructBuildInfo& outBuildInfo) const noexcept;
    };

private:
    VkAccelerationStructureKHR m_vkAccelStruct = VK_NULL_HANDLE; // Vulkan TLAS handle
    VkBuildAccelerationStructureFlagsKHR m_flags = 0; // Flags used to build the TLAS
    std::unique_ptr<Buffer> m_uptrBuffer; // Host/GPU buffer storing TLAS data

public:
    /// <summary>
    /// Initializes the TLAS object using a Creator (creates Vulkan handle/buffers)
    /// Must be called before using the TLAS for ray tracing
    /// </summary>
    /// <param name="inCreator">Configured Creator object</param>
    void Init(const Creator* inCreator);

    /// <summary>
    /// Gets the Vulkan TLAS handle for ray tracing operations
    /// </summary>
    /// <returns>VkAccelerationStructureKHR handle (VK_NULL_HANDLE if uninitialized)</returns>
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR { return m_vkAccelStruct; };

    /// <summary>
    /// Gets the build flags used to create the TLAS
    /// </summary>
    /// <returns>Vulkan build flags (e.g., prefer fast trace/build)</returns>
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; };

    /// <summary>
    /// Cleans up Vulkan resources (TLAS handle/buffer)
    /// Must be called to avoid resource leaks
    /// </summary>
    void Uninit();
};

#pragma endregion