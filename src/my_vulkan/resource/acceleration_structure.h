#pragma once
#include "common.h"
#include "buffer.h"
#include "../ref_counted.h"
#include <span>

class CommandBuffer;
class BottomLevelAccelStruct;
class TopLevelAccelStruct;

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
    VkAccelerationStructureKHR m_vkAccelStruct = VK_NULL_HANDLE;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    std::unique_ptr<Buffer> m_uptrBuffer;
    VkDeviceAddress m_accelStructReference = 0;
    VkAccelerationStructureBuildSizesInfoKHR m_buildSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    VkDeviceSize m_scratchAlignment = 1;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const BottomLevelAccelStructCreateInfo& inInfo, bool inResolveAddresses) -> VkAccelStructBuildInfo;
    auto _BuildInfo(const BottomLevelAccelStructCreateInfo& inInfo,
        const BottomLevelAccelStruct* inSrc, Buffer& inScratchBuffer, VkDeviceSize inScratchOffset) const -> VkAccelStructBuildInfo;
public:
    ~BottomLevelAccelStruct() override;
    // Host only: queries sizes and creates the handle/backing Buffer. Caller synchronizes recreation.
    void Create(const BottomLevelAccelStructCreateInfo& inCreateInfo);
    void Destroy();
    auto GetDeviceAddress() const noexcept -> VkDeviceAddress { return m_accelStructReference; }
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR { return m_vkAccelStruct; }
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetBuildSizes() const noexcept -> const VkAccelerationStructureBuildSizesInfoKHR& { return m_buildSizes; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_scratchAlignment; }
};

class TopLevelAccelStruct final : public RefCounted
{
    friend class CommandBuffer;
    friend struct CommandBufferTestProbe;
private:
    VkAccelerationStructureKHR m_vkAccelStruct = VK_NULL_HANDLE;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
    std::unique_ptr<Buffer> m_uptrBuffer;
    VkAccelerationStructureBuildSizesInfoKHR m_buildSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    VkDeviceSize m_scratchAlignment = 1;
    VkAccelStructBuildInfo m_layout;

    static auto _Describe(const TopLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo;
    auto _BuildInfo(const TopLevelAccelStructCreateInfo& inInfo, const TopLevelAccelStruct* inSrc,
        const Buffer& inInstanceBuffer, VkDeviceSize inInstanceOffset,
        Buffer& inScratchBuffer, VkDeviceSize inScratchOffset) const -> VkAccelStructBuildInfo;
public:
    ~TopLevelAccelStruct() override;
    void Create(const TopLevelAccelStructCreateInfo& inCreateInfo);
    void Destroy();
    // Host only. No TLAS object is required; destination may be a staging Buffer.
    // Non-null BLAS references must already have a valid device address.
    static void WriteInstances(const TopLevelAccelStructCreateInfo& inInfo,
        Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR { return m_vkAccelStruct; }
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetBuildSizes() const noexcept -> const VkAccelerationStructureBuildSizesInfoKHR& { return m_buildSizes; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_scratchAlignment; }
};
