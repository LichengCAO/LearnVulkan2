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

class ClusterLevelAccelStructTemplateInfo
{
	friend class MyDevice;
	VkDeviceAddress m_indices = 0;
	uint32_t m_triangles = 0;
	uint32_t m_vertices = 0;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	VkDeviceSize m_indexStride = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
public:
	void SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount);
	void SetVertexCount(uint32_t inVertexCount) { m_vertices = inVertexCount; }
	void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
	void CustomizeIndexStride(VkDeviceSize inStride) { m_indexStride = inStride; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }
};

class ClusterLevelAccelStructTemplateBatch final : public RefCounted
{
	friend class MyDevice;
	struct Metadata { uint32_t triangles; uint32_t vertices; };
	std::unique_ptr<Buffer> m_storage;
	std::unique_ptr<Buffer> m_references;
	std::vector<VkDeviceAddress> m_addresses;
	std::vector<Metadata> m_metadata;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
public:
	auto GetReferenceBuffer() const noexcept -> const Buffer* { return m_references.get(); }
	auto GetReference(size_t inIndex) const -> VkDeviceAddress { return m_addresses.at(inIndex); }
	auto GetCount() const noexcept -> uint32_t { return static_cast<uint32_t>(m_addresses.size()); }
	auto GetTriangleCount(size_t inIndex) const -> uint32_t { return m_metadata.at(inIndex).triangles; }
	auto GetVertexCount(size_t inIndex) const -> uint32_t { return m_metadata.at(inIndex).vertices; }
	auto GetVertexFormat() const noexcept -> VkFormat { return m_limits.vertexFormat; }
	auto GetIndexType() const noexcept -> VkIndexType { return m_indexType; }
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureTriangleClusterInputNV { return m_limits; }
};

class ClusterLevelAccelStructBatch;

class ClusterLevelAccelStructBatchCreateInfo final
{
	friend class ClusterLevelAccelStructBatch;
private:
	uint32_t m_maxClusterCount = 0;
	uint32_t m_maxClusterTriangleCount = 0;
	uint32_t m_maxClusterVertexCount = 0;
	uint32_t m_maxTotalTriangleCount = 0;
	uint32_t m_maxTotalVertexCount = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	bool m_useTemplate = false;

public:
	// All five capacities are required. Totals are independent bounds across the build.
	void SetMaxClusterCount(uint32_t inCount) { m_maxClusterCount = inCount; }
	void SetMaxClusterTriangleCount(uint32_t inCount) { m_maxClusterTriangleCount = inCount; }
	void SetMaxClusterVertexCount(uint32_t inCount) { m_maxClusterVertexCount = inCount; }
	void SetMaxTotalTriangleCount(uint32_t inCount) { m_maxTotalTriangleCount = inCount; }
	void SetMaxTotalVertexCount(uint32_t inCount) { m_maxTotalVertexCount = inCount; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }
	void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
	void CustomizeTemplateUsage(bool inUseTemplate) { m_useTemplate = inUseTemplate; }
};

// Build from a cluster template
struct ClusterLevelAccelStructInstantiateBuildInfo
{
private:
	VkDeviceAddress m_vertices = 0;
	VkDeviceAddress m_template = 0;
	VkDeviceSize m_stride = 0;
	uint32_t m_id = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
public:
	void SetVertexBuffer(VkDeviceAddress inAddress) { m_vertices = inAddress; }
	// Borrowed template with base ID zero and topology within the target's budgets.
	void SetTemplateReference(VkDeviceAddress inAddress) { m_template = inAddress; }
	void CustomizeVertexStride(VkDeviceSize inStride) { m_stride = inStride; }
	void CustomizeClusterID(uint32_t inID) { m_id = inID; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }

	// Translate and write host data to argument buffer, this can be GPU-Driven.
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructInstantiateBuildInfo> inHostInfo,
		const ClusterLevelAccelStructBatch& inTarget, Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

// Build from a vertex buffer
struct ClusterLevelAccelStructDirectBuildInfo
{
	uint32_t clusterID = 0;
	uint32_t primitiveCount = 0; // Triangles.
	VkDeviceAddress vertexAddress = 0;
	VkDeviceSize vertexStride = 0; // Zero selects tightly packed vertices.
	uint32_t vertexCount = 0; // Unique vertices referenced by the indices.
	VkDeviceAddress indexAddress = 0;
	VkIndexType indexType = VK_INDEX_TYPE_UINT32;
	VkDeviceSize indexStride = 0;
	VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

public:
	void SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount);
	void SetVertexBuffer(VkDeviceAddress inAddress, uint32_t inVertexCount);
	void CustomizeVertexStride(VkDeviceSize inStride) { vertexStride = inStride; }
	void CustomizeIndexType(VkIndexType inType) { indexType = inType; }
	void CustomizeIndexStride(VkDeviceSize inStride) { indexStride = inStride; }
	void CustomizeVertexFormat(VkFormat inFormat) { vertexFormat = inFormat; }
	void CustomizeClusterID(uint32_t inID) { clusterID = inID; }

	// Translate and write host data to argument buffer, this can be GPU-Driven.
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructDirectBuildInfo> inHostInfo,
		const ClusterLevelAccelStructBatch& inTarget, Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

class ClusterLevelAccelStructBatch final : public RefCounted
{
	friend struct ClusterLevelAccelStructDirectBuildIndirectInfo;
	friend struct ClusterLevelAccelStructInstantiateBuildIndirectInfo;
	friend struct ClusterLevelAccelStructDirectBuildInfo;
	friend struct ClusterLevelAccelStructInstantiateBuildInfo;
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
	VkDeviceSize m_vertexElementSize = 0;
	VkDeviceSize m_vertexAlignment = 0;
	VkDeviceSize m_templateAlignment = 0;
	bool m_useTemplate = false;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;

public:
	// Synchronous size query and output Buffer allocation for an empty target.
	// Independent batches can create and release concurrently with stable Device and allocator
	// dependencies. Exclude conflicting access to one batch and finish GPU uses before release.
	void Create(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo);
	// Borrowed address table owned by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept->const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCapacity() const noexcept -> uint32_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;
	auto GetVertexFormat() const noexcept -> VkFormat { return m_triangleInput.vertexFormat; }
	auto GetIndexType() const noexcept -> VkIndexType { return m_indexType; }
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureTriangleClusterInputNV { return m_triangleInput; }
};

class BottomLevelAccelStructBuildInfo final
{
	friend class BottomLevelAccelStructBatch;
private:
	VkDeviceAddress m_clusterReferencesAddress = 0;
	uint32_t m_clusterCount = 0;

public:
	// Snapshots an address range from the source's reference table. The caller keeps the
	// table alive through the BLAS build and the source CLAS alive through dependent GPU uses.
	void SetClusterReferences(const ClusterLevelAccelStructBatch& inSource, uint32_t inOffset, uint32_t inCount);
};

class BottomLevelAccelStructBatchCreateInfo final
{
	friend class BottomLevelAccelStructBatch;
private:
	uint32_t m_maxAccelStructCount = 0;
	uint32_t m_maxClusterCountPerAccelStruct = 0;
	uint32_t m_maxTotalClusterCount = 0;

public:
	// All three capacities are required. The total is an independent bound across the build.
	void SetMaxAccelStructCount(uint32_t inCount) { m_maxAccelStructCount = inCount; }
	void SetMaxClusterCountPerAccelStruct(uint32_t inCount) { m_maxClusterCountPerAccelStruct = inCount; }
	void SetMaxTotalClusterCount(uint32_t inCount) { m_maxTotalClusterCount = inCount; }
};

class BottomLevelAccelStructBatch final : public RefCounted
{
	friend struct BottomLevelAccelStructBuildIndirectInfo;
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
	void Create(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo);
	// Borrowed address table owned by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept -> const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCapacity() const noexcept -> uint32_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;

	// Validates against this batch's creation limits before writing to a caller-owned
	// host-visible, host-coherent Buffer. The source reference tables remain borrowed.
	void WriteBuildInfoBufferInline(std::span<const BottomLevelAccelStructBuildInfo> inBuildInfos, Buffer& inDstBuffer, VkDeviceSize inOffset = 0) const;
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureClustersBottomLevelInputNV { return m_clustersInput; }
};

namespace VulkanDetail
{
	struct ClusterBuildAddresses
	{
		AccelStructSizesInfo sizes{};
		VkDeviceAddress storage = 0;
		VkDeviceAddress references = 0;
		VkDeviceSize referenceSize = 0;
		uint32_t capacity = 0;
	};
}

struct ClusterLevelAccelStructDirectBuildIndirectInfo final
{
	friend class CommandBuffer;
private:
	VulkanDetail::ClusterBuildAddresses m_addresses{};
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{};
	VkDeviceAddress m_tasks = 0;
	VkDeviceAddress m_scratch = 0;
	VkDeviceAddress m_count = 0;
public:
	void SetTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetBuildInfoAddress(VkDeviceAddress inAddress) { m_tasks = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_count = inAddress; }
};

struct ClusterLevelAccelStructInstantiateBuildIndirectInfo final
{
	friend class CommandBuffer;
private:
	VulkanDetail::ClusterBuildAddresses m_addresses{};
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{};
	VkDeviceAddress m_tasks = 0;
	VkDeviceAddress m_scratch = 0;
	VkDeviceAddress m_count = 0;
public:
	void SetTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetBuildInfoAddress(VkDeviceAddress inAddress) { m_tasks = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_count = inAddress; }
};

struct BottomLevelAccelStructBuildIndirectInfo final
{
	friend class CommandBuffer;
private:
	VulkanDetail::ClusterBuildAddresses m_addresses{};
	VkClusterAccelerationStructureClustersBottomLevelInputNV m_limits{};
	VkDeviceAddress m_tasks = 0;
	VkDeviceAddress m_scratch = 0;
	VkDeviceAddress m_count = 0;
public:
	void SetTarget(const BottomLevelAccelStructBatch& inBatch);
	void SetBuildInfoAddress(VkDeviceAddress inAddress) { m_tasks = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_count = inAddress; }
};
