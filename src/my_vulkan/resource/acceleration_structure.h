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

struct AccelStructSizesInfo
{
	VkDeviceSize updateScratchBufferSize = 0;
	VkDeviceSize buildScratchBufferSize = 0;
	VkDeviceSize scratchAlignment = 0;
	VkDeviceSize buildInfoBufferSize = 0;
	VkDeviceSize accelerationStructureSize = 0;
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
    static void WriteBuildInfoBufferInline(const TopLevelAccelStructCreateInfo& inInfo, Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
    auto GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR;
    auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_flags; }
    auto GetBuildSizes() const noexcept -> const VkAccelerationStructureBuildSizesInfoKHR& { return m_buildSizes; }
    auto GetScratchAlignment() const noexcept -> VkDeviceSize { return m_scratchAlignment; }
};

struct ClusterLevelAccelStructCreateInfo
{
	uint32_t clusterID = 0;
	uint32_t primitiveCount = 0;
	VkDeviceAddress vertexAddress = 0;
	VkDeviceSize vertexStride = 0;
	uint32_t vertexCount = 0;
	VkDeviceAddress indexAddress = 0;
};

class ClusterLevelAccelStructBatchCreateInfo final
{
	friend class ClusterLevelAccelStructBatch;
private:
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	std::vector<ClusterLevelAccelStructCreateInfo> m_clusters;

public:
	// Copies the task descriptions. The vertex and index addresses remain borrowed.
	void SetClusterLevelAccelStructCreateInfos(std::span<const ClusterLevelAccelStructCreateInfo> inClusterTrianglesBatch);
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; };
    // All clusters in one batch use this index type. Defaults to 32-bit indices.
    void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
};

class ClusterLevelAccelStructBatch final : public RefCounted
{
	friend class CommandBuffer;
	friend struct CommandBufferTestProbe;
	friend struct ClusterAccelerationStructureTestProbe;
	friend struct ClusterAccelerationStructureGpuTestProbe;
private:
	std::unique_ptr<Buffer> m_storageBuffer;
	std::unique_ptr<Buffer> m_referenceBuffer;
	AccelStructSizesInfo m_sizesInfo{};
	uint32_t m_count = 0;
	VkClusterAccelerationStructureTriangleClusterInputNV m_triangleInput{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };

public:
	// Synchronous size query and output Buffer allocation for an empty target.
	// Independent batches can create and release concurrently with stable Device and allocator
	// dependencies. Exclude conflicting access to one batch and finish GPU uses before release.
	void Create(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo);
	// Borrowed address table owned by this batch. The build writes the table on the GPU.
	auto GetReferenceBuffer() const noexcept->const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCount() const noexcept-> size_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;

	// Writes packed native records to a caller-owned host-visible, host-coherent Buffer.
	// The caller keeps the GPU address ranges valid and matches the target batch's build limits.
	static void WriteBuildInfoBufferInline(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo, Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

struct ClusterBasedBottomLevelAccelStructClusterCreateInfo
{
	const ClusterLevelAccelStructBatch* sourceClusterBatch = nullptr;
	uint32_t clusterOffset = 0;
	uint32_t clusterCount = 0;
};

class BottomLevelAccelStructBatchCreateInfo
{
	friend class BottomLevelAccelStructBatch;
private:
	std::vector<ClusterBasedBottomLevelAccelStructClusterCreateInfo> m_ranges;

public:
	// Copies the ranges. Their source cluster batches remain borrowed.
	void SetBottomLevelAccelStructCreateInfos(std::span<const ClusterBasedBottomLevelAccelStructClusterCreateInfo> inCreateInfos);
};

class BottomLevelAccelStructBatch final : public RefCounted
{
	friend class CommandBuffer;
	friend struct CommandBufferTestProbe;
	friend struct ClusterAccelerationStructureTestProbe;
	friend struct ClusterAccelerationStructureGpuTestProbe;
private:
	std::unique_ptr<Buffer> m_storageBuffer;
	std::unique_ptr<Buffer> m_referenceBuffer;
	AccelStructSizesInfo m_sizesInfo{};
	uint32_t m_count = 0;
	VkClusterAccelerationStructureClustersBottomLevelInputNV m_clustersInput{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };

public:
	// Same empty-target creation and lifetime contract as ClusterLevelAccelStructBatch.
	// Keep source cluster batches through all GPU uses of their references and cluster storage.
	void Create(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo);
	// Borrowed address table owned by this batch. The build writes the table on the GPU.
	auto GetReferenceBuffer() const noexcept->const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCount() const noexcept-> size_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;

	// Writes addresses into source reference tables to a caller-owned host-visible,
	// host-coherent Buffer. The caller matches the records to the target batch's build limits.
	static void WriteBuildInfoBufferInline(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo, Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

struct ClusterLevelAccelStructBuildIndirectInfo
{
	VkDeviceAddress scratchAddress = 0;
	VkDeviceAddress buildInfoAddress = 0;
	ClusterLevelAccelStructBatch* targetAccelStructs = nullptr;
};

struct BottomLevelAccelStructBuildIndirectInfo
{
	VkDeviceAddress scratchAddress = 0;
	VkDeviceAddress buildInfoAddress = 0;
	BottomLevelAccelStructBatch* targetAccelStructs = nullptr;
};
