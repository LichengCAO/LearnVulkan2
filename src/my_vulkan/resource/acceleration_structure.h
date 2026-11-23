#pragma once
#include "acceleration_structure_types.h"
#include "../ref_counted.h"
#include <span>

class Buffer;
class CommandBuffer;
class BottomLevelAccelStruct;
class TopLevelAccelStruct;
namespace VulkanDetail { struct AccelerationStructureStorage; }
class BuildAccelStructCommandInput;

class AccelStructTriangleGeometryLimits final
{
    friend class BottomLevelAccelStruct;
    uint32_t m_triangles = 0;
    uint32_t m_vertices = 0;
    VkFormat m_format = VK_FORMAT_R32G32B32_SFLOAT;
    VkIndexType m_indexType = VK_INDEX_TYPE_NONE_KHR;
    bool m_allowTransform = false;
    VkGeometryFlagsKHR m_flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
public:
    void SetMaxTriangleCount(uint32_t inCount) { m_triangles = inCount; }
    // Vertex address range capacity, not the number of uniquely referenced vertices.
    void SetMaxVertexCount(uint32_t inCount) { m_vertices = inCount; }
    void CustomizeVertexFormat(VkFormat inFormat) { m_format = inFormat; }
    void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
    void CustomizeAllowTransform(bool inAllow) { m_allowTransform = inAllow; }
    void CustomizeGeometryFlags(VkGeometryFlagsKHR inFlags) { m_flags = inFlags; }
};

class AccelStructAabbGeometryLimits final
{
    friend class BottomLevelAccelStruct;
    uint32_t m_count = 0;
    VkGeometryFlagsKHR m_flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
public:
    void SetMaxAabbCount(uint32_t inCount) { m_count = inCount; }
    void CustomizeGeometryFlags(VkGeometryFlagsKHR inFlags) { m_flags = inFlags; }
};

class AccelStructTriangleBuildInfo final
{
    friend class BuildAccelStructCommandInput;
    VkDeviceAddress m_vertices = 0, m_indices = 0, m_transform = 0;
    uint32_t m_vertexCount = 0, m_triangleCount = 0;
    VkDeviceSize m_stride = 0;
public:
    void SetVertexBuffer(VkDeviceAddress inAddress, uint32_t inVertexCount)
    { m_vertices = inAddress; m_vertexCount = inVertexCount; }
    void SetTriangleCount(uint32_t inCount) { m_triangleCount = inCount; }
    // Required only for indexed targets; the target supplies the index type.
    void SetIndexBuffer(VkDeviceAddress inAddress) { m_indices = inAddress; }
    void CustomizeTransformBuffer(VkDeviceAddress inAddress) { m_transform = inAddress; }
    // Zero selects tightly packed vertices using the target's format.
    void CustomizeVertexStride(VkDeviceSize inStride) { m_stride = inStride; }
};

class AccelStructAabbBuildInfo final
{
    friend class BuildAccelStructCommandInput;
    VkDeviceAddress m_address = 0;
    uint32_t m_count = 0;
    VkDeviceSize m_stride = sizeof(VkAabbPositionsKHR);
public:
    void SetAabbBuffer(VkDeviceAddress inAddress, uint32_t inCount)
    { m_address = inAddress; m_count = inCount; }
    void CustomizeStride(VkDeviceSize inStride) { m_stride = inStride; }
};

// Host description, not GPU ABI. Encode explicitly before a TLAS build.
struct TopLevelAccelStructBuildInfo final
{
    glm::mat4 transform{ 1.0f };
    VkDeviceAddress accelStructReference = 0; // Zero encodes an inactive instance.
    uint32_t shaderGroupIndexOffset = 0;
    uint32_t instanceCustomIndex = 0;
    uint32_t mask = 0xFF;
    VkGeometryInstanceFlagsKHR flags = 0;

    static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
    { return AccelStructGpuLayout::Tlas::Layout; }
    // Validates/encodes the whole span before one synchronous Buffer upload.
    // Empty uploads do not access Device or Buffer. Noncoherent/device-local destinations
    // use Buffer's staged transfer contract; callers synchronize all conflicting uses.
    static void CommitToDeviceInline(std::span<const TopLevelAccelStructBuildInfo> inInfos,
        Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

class BottomLevelAccelStructCreateInfo final
{
    friend class BottomLevelAccelStruct;
    friend struct CommandBufferTestProbe;
private:
    std::vector<AccelStructTriangleGeometryLimits> m_triangles;
    std::vector<AccelStructAabbGeometryLimits> m_aabbs;
    uint32_t m_clusterCount = 0;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
public:
    // Replaces the ordered capacity list and selects its type. No GPU bindings.
    void SetTriangles(std::span<const AccelStructTriangleGeometryLimits> inLimits);
    void SetAabbs(std::span<const AccelStructAabbGeometryLimits> inLimits);
    // Replaces triangle/AABB input with cluster-reference mode and its maximum reference count.
    // The count must be in [1, UINT32_MAX] and is checked before native conversion.
    // Cluster Create uses default build flags (zero). The destination has an address and no KHR handle.
    void SetClusters(size_t inMaxClusterCount);
    void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept;
};

class TopLevelAccelStructCreateInfo final
{
    friend class TopLevelAccelStruct;
    friend struct CommandBufferTestProbe;
private:
    uint32_t m_maxInstanceCount = 0;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
public:
    // Required positive capacity; zero/default construction is unconfigured.
    void SetMaxInstanceCount(uint32_t inCount);
    void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept;
};

class BottomLevelAccelStruct final : public RefCounted
{
    friend class BuildAccelStructCommandInput;
    friend class ClusterBottomLevelAccelStructBatch;
    friend class CommandBuffer;
    friend struct CommandBufferTestProbe;
private:
    std::unique_ptr<VulkanDetail::AccelerationStructureStorage> m_storage;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    VkDeviceAddress m_accelStructReference = 0;
    uint32_t m_clusterCount = 0;
    AccelStructMemoryRequirements m_memoryRequirements{ .scratchAlignment = 1 };
    VkDeviceAddress m_storageAddress = 0;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const BottomLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo;
public:
    BottomLevelAccelStruct();
    ~BottomLevelAccelStruct() noexcept override;
    BottomLevelAccelStruct(const BottomLevelAccelStruct&) = delete;
    auto operator=(const BottomLevelAccelStruct&) -> BottomLevelAccelStruct& = delete;
    BottomLevelAccelStruct(BottomLevelAccelStruct&&) = delete;
    auto operator=(BottomLevelAccelStruct&&) -> BottomLevelAccelStruct& = delete;
    // Synchronous creation requires an empty target. Independent instances may create/release
    // concurrently; callers exclude conflicting access to one instance and keep Device,
    // allocator and native dispatch alive and stable through final release. Complete GPU use
    // before release. Geometry buffers and command inputs remain borrowed.
    void Create(const BottomLevelAccelStructCreateInfo& inCreateInfo);
    auto IsValid() const noexcept -> bool;
    auto GetDeviceAddress() const noexcept -> VkDeviceAddress { return m_accelStructReference; }
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR;
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements& { return m_memoryRequirements; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_memoryRequirements.scratchAlignment; }
};

class TopLevelAccelStruct final : public RefCounted
{
    friend class BuildAccelStructCommandInput;
    friend class CommandBuffer;
    friend struct CommandBufferTestProbe;
private:
    std::unique_ptr<VulkanDetail::AccelerationStructureStorage> m_storage;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    AccelStructMemoryRequirements m_memoryRequirements{ .scratchAlignment = 1 };
    VkDeviceAddress m_storageAddress = 0;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const TopLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo;
public:
    TopLevelAccelStruct();
    ~TopLevelAccelStruct() noexcept override;
    TopLevelAccelStruct(const TopLevelAccelStruct&) = delete;
    auto operator=(const TopLevelAccelStruct&) -> TopLevelAccelStruct& = delete;
    TopLevelAccelStruct(TopLevelAccelStruct&&) = delete;
    auto operator=(TopLevelAccelStruct&&) -> TopLevelAccelStruct& = delete;
    // Synchronous creation requires an empty target. Independent instances may create/release
    // concurrently with stable Device, allocator and dispatch. Exclude same-instance conflicts
    // and finish GPU use before release; instance/BLAS/build inputs remain borrowed.
    void Create(const TopLevelAccelStructCreateInfo& inCreateInfo);
    auto IsValid() const noexcept -> bool;
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR;
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements& { return m_memoryRequirements; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_memoryRequirements.scratchAlignment; }
    auto GetCapacity() const noexcept -> uint32_t
    { return IsValid() ? m_layout.buildRangesInfos[0].primitiveCount : 0; }
};

// One independent KHR task. Set*Target snapshots target metadata and input arrays,
// clears the source (BUILD), and preserves scratch. Failed selection preserves state.
// Copy/move owns Host data only. Callers exclude conflicting access to one description,
// keep all raw-address resources/owners alive through GPU use, and synchronize dependencies.
// Assign {} to clear the description, including scratch. No GPU resource is released.
class BuildAccelStructCommandInput final
{
    friend class CommandBuffer;
    VkAccelStructBuildInfo m_build;
    AccelStructMemoryRequirements m_memoryRequirements{};
    VkDeviceAddress m_scratch = 0;
    std::pair<VkDeviceAddress, VkDeviceSize> m_targetRange{}, m_sourceRange{};

    void _SetSource(const VkAccelStructBuildInfo& inLimits, VkAccelerationStructureKHR inHandle,
        VkDeviceAddress inStorageAddress, VkDeviceSize inStorageSize);
    auto _GetScratchBufferSize() const -> VkDeviceSize;
    auto _GetBuild() const -> VkAccelStructBuildInfo;
public:
    void SetTrianglesTarget(const BottomLevelAccelStruct& inTarget,
        std::span<const AccelStructTriangleBuildInfo> inInfos);
    void SetAabbsTarget(const BottomLevelAccelStruct& inTarget,
        std::span<const AccelStructAabbBuildInfo> inInfos);
    // Packed instances only; a zero actual count accepts address zero.
    void SetInstancesTarget(const TopLevelAccelStruct& inTarget,
        VkDeviceAddress inInstanceAddress, uint32_t inInstanceCount);
    void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
    // Source must have a compatible previous ALLOW_UPDATE build. GPU history,
    // unchanged counts/topology/maxVertex/active state/transform presence remain caller contracts.
    void CustomizeSourceAccelStruct(const BottomLevelAccelStruct& inSource);
    void CustomizeSourceAccelStruct(const TopLevelAccelStruct& inSource);
    auto GetMemoryRequirements() const -> AccelStructMemoryRequirements;
};
