#pragma once
#include "common.h"
#include "buffer.h"
#include "../ref_counted.h"
#include <span>

class CommandBuffer;
class BottomLevelAccelStruct;
class TopLevelAccelStruct;
namespace VulkanDetail { struct AccelerationStructureStorage; }

// Low-level batch input. The command snapshots both arrays when appended.
struct VkAccelStructBuildInfo
{
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesInfos;
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
};

struct AccelStructTriangleGeometry
{
    const Buffer* vertexBuffer = nullptr;
    VkDeviceSize vertexOffset = 0;
    VkDeviceSize vertexStride = 0;
    uint32_t vertexCount = 0;
    uint32_t primitiveCount = 0;
    VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    const Buffer* indexBuffer = nullptr; // nullptr selects non-indexed triangles.
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    const Buffer* transformBuffer = nullptr;
    VkDeviceSize transformOffset = 0;
    VkGeometryFlagsKHR flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
};

struct AccelStructAabbGeometry
{
    const Buffer* buffer = nullptr;
    VkDeviceSize offset = 0;
    VkDeviceSize stride = sizeof(VkAabbPositionsKHR);
    uint32_t primitiveCount = 0;
    VkGeometryFlagsKHR flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
};

struct AccelStructInstance
{
    glm::mat4 transform{ 1.0f };
    const BottomLevelAccelStruct* accelStruct = nullptr; // nullptr encodes an inactive instance.
    uint32_t shaderGroupIndexOffset = 0;
    uint32_t instanceCustomIndex = 0;
    uint32_t mask = 0xFF;
    VkGeometryInstanceFlagsKHR flags = 0;
};

class BottomLevelAccelStructCreateInfo final
{
    friend class BottomLevelAccelStruct;
    friend struct CommandBufferTestProbe;
private:
    std::vector<AccelStructTriangleGeometry> m_triangles;
    std::vector<AccelStructAabbGeometry> m_aabbs;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
public:
    // Replaces the geometry list and selects its type. Copies descriptions, not Buffer contents.
    void SetTriangles(std::span<const AccelStructTriangleGeometry> inGeometries);
    void SetAabbs(std::span<const AccelStructAabbGeometry> inGeometries);
    void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept;
    void Reset() noexcept;
};

class TopLevelAccelStructCreateInfo final
{
    friend class TopLevelAccelStruct;
    friend struct CommandBufferTestProbe;
private:
    std::vector<AccelStructInstance> m_instances;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
public:
    void SetInstances(std::span<const AccelStructInstance> inInstances);
    void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept;
    void Reset() noexcept;
    auto GetInstanceCount() const noexcept -> uint32_t;
    auto GetInstanceBufferSize() const noexcept -> VkDeviceSize;
};

class BottomLevelAccelStruct final : public RefCounted
{
    friend class CommandBuffer;
    friend struct CommandBufferTestProbe;
private:
    std::unique_ptr<VulkanDetail::AccelerationStructureStorage> m_storage;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    VkDeviceAddress m_accelStructReference = 0;
    VkAccelerationStructureBuildSizesInfoKHR m_buildSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    VkDeviceSize m_scratchAlignment = 1;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const BottomLevelAccelStructCreateInfo& inInfo, bool inResolveAddresses) -> VkAccelStructBuildInfo;
    auto _BuildInfo(const BottomLevelAccelStructCreateInfo& inInfo,
        const BottomLevelAccelStruct* inSrc, Buffer& inScratchBuffer, VkDeviceSize inScratchOffset) const -> VkAccelStructBuildInfo;
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
    auto GetBuildSizes() const noexcept -> const VkAccelerationStructureBuildSizesInfoKHR& { return m_buildSizes; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_scratchAlignment; }
};

class TopLevelAccelStruct final : public RefCounted
{
    friend class CommandBuffer;
    friend struct CommandBufferTestProbe;
private:
    std::unique_ptr<VulkanDetail::AccelerationStructureStorage> m_storage;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    VkAccelerationStructureBuildSizesInfoKHR m_buildSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    VkDeviceSize m_scratchAlignment = 1;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const TopLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo;
    auto _BuildInfo(const TopLevelAccelStructCreateInfo& inInfo, const TopLevelAccelStruct* inSrc,
        const Buffer& inInstanceBuffer, VkDeviceSize inInstanceOffset,
        Buffer& inScratchBuffer, VkDeviceSize inScratchOffset) const -> VkAccelStructBuildInfo;
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
    // Host only. No TLAS object is required; destination may be a staging Buffer.
    // Non-null BLAS references must already have a valid device address.
    static void WriteInstances(const TopLevelAccelStructCreateInfo& inInfo,
        Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR;
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetBuildSizes() const noexcept -> const VkAccelerationStructureBuildSizesInfoKHR& { return m_buildSizes; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_scratchAlignment; }
};
