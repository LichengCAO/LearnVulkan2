#pragma once
#include "acceleration_structure_types.h"
#include "buffer.h"
#include "../ref_counted.h"
#include "private/cluster_acceleration_structure_build.h"
#include <span>

class CommandBuffer;
class MyDevice;
class BottomLevelAccelStruct;

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

class ClusterLevelAccelStructCreateInfo final
{
public:
	void SetMaxTriangleCount(uint32_t inCount);
	void SetMaxVertexCount(uint32_t inCount);
	void CustomizeVertexFormat(VkFormat inFormat);
	void CustomizeIndexType(VkIndexType inType);
	void CustomizeTemplateUsage(bool inUseTemplate);
};

class ClusterLevelAccelStruct final : public RefCounted
{
public:
	void Create(const ClusterLevelAccelStructCreateInfo&);
	auto GetReference() const noexcept->VkDeviceAddress;
};

class ClusterLevelAccelStructImplicitBatchCreateInfo final
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

class ClusterLevelAccelStructExplicitBatchCreateInfo final
{
public:
	// Borrows a packed VkDeviceAddress table until Batch Create completes.
	// Successful creation retains a strong reference to this Buffer for the Batch lifetime.
	// The Buffer must use intrusive reference-counted ownership.
	void SetReferenceBuffer(Buffer& inReferenceBuffer, VkDeviceSize inOffset);
	// Copies the pointer list; source objects remain borrowed until Batch Create completes.
	// Sources must use intrusive reference-counted ownership; the created Batch retains each source.
	void SetSourceAccelStructs(std::span<const ClusterLevelAccelStruct* const> inAccelStructs);
};

class ClusterLevelAccelStructBatch final : public RefCounted
{
	friend struct ClusterLevelAccelStructBuildIndirectInfo;
	friend struct ClusterLevelAccelStructInstantiateBuildIndirectInfo;
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
	bool m_useTemplate = false;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;

public:
	// Synchronous size query and output Buffer allocation for an empty target.
	// Independent batches can create and release concurrently with stable Device and allocator
	// dependencies. Exclude conflicting access to one batch and finish GPU uses before release.
	void Create(const ClusterLevelAccelStructImplicitBatchCreateInfo& inCreateInfo);
	// Same synchronous empty-target contract; retains strong references to the reference Buffer
	// and every source AS until Batch destruction. Retain the Batch through dependent GPU uses.
	void Create(const ClusterLevelAccelStructExplicitBatchCreateInfo& inCreateInfo);
	// Borrowed address table retained by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept->const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCapacity() const noexcept -> uint32_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;
	auto GetVertexFormat() const noexcept -> VkFormat { return m_triangleInput.vertexFormat; }
	auto GetIndexType() const noexcept -> VkIndexType { return m_indexType; }
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureTriangleClusterInputNV { return m_triangleInput; }
};

class BottomLevelAccelStructImplicitBatchCreateInfo final
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

class BottomLevelAccelStructExplicitBatchCreateInfo final
{
public:
	// Borrows a packed VkDeviceAddress table until Batch Create completes.
	// Successful creation retains a strong reference to this Buffer for the Batch lifetime.
	// The Buffer must use intrusive reference-counted ownership.
	void SetReferenceBuffer(Buffer& inReferenceBuffer, VkDeviceSize inOffset);
	// Copies the pointer list; source objects remain borrowed until Batch Create completes.
	// Sources must use intrusive reference-counted ownership; the created Batch retains each source.
	void SetSourceAccelStructs(std::span<const BottomLevelAccelStruct* const> inAccelStructs);
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
	void Create(const BottomLevelAccelStructImplicitBatchCreateInfo& inCreateInfo);
	// Same synchronous empty-target contract; retains strong references to the reference Buffer
	// and every source AS until Batch destruction. Retain the Batch through dependent GPU uses.
	void Create(const BottomLevelAccelStructExplicitBatchCreateInfo& inCreateInfo);
	// Borrowed address table retained by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept -> const Buffer*;
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
	auto GetCapacity() const noexcept -> uint32_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureClustersBottomLevelInputNV { return m_clustersInput; }
};

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

	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{
		return AccelStructGpuLayout::Instantiate::Layout;
	}

	// Encodes and uploads Host tasks using each task's vertex format.
	// The caller guarantees matching templates and eventual build formats/budgets.
	static void CommitToDeviceInline(
		std::span<const ClusterLevelAccelStructInstantiateBuildInfo> inHostInfo,
		Buffer& inoutTargetBuffer,
		VkDeviceSize inOffset = 0);
};

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

	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{
		return AccelStructGpuLayout::Direct::Layout;
	}

	// Encodes and uploads Host tasks using each task's vertex format and index type.
	// The caller guarantees the tasks match the eventual build's formats/budgets.
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructDirectBuildInfo> inHostInfo,
		Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

class BottomLevelAccelStructBuildInfo final
{
private:
	VkDeviceAddress m_clusterReferencesAddress = 0;
	uint32_t m_clusterCount = 0;

public:
	// Borrows a packed VkDeviceAddress array. The caller keeps the table alive through
	// the BLAS build and the referenced CLAS alive through dependent GPU uses.
	void SetClusterReferences(VkDeviceAddress inReferenceBufferAddress, uint32_t inCount);

	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{
		return AccelStructGpuLayout::ClusterBlas::Layout;
	}

	// Encodes and uploads Host tasks to a host-visible, host-coherent Buffer.
	// The caller guarantees the tasks fit the eventual build's batch budgets.
	static void CommitToDeviceInline(std::span<const BottomLevelAccelStructBuildInfo> inHostInfo, Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

class ClusterLevelAccelStructBuildLayout final
{
public:
	enum class BuildType
	{
		EXPLICIT_DIRECT,
		EXPLICIT_INSTANTIATE,
		IMPLICIT_DIRECT,
		IMPLICIT_INSTANTIATE,
	};
	void SetMaxTriangleCount(uint32_t inMaxPerCluster, uint32_t inMaxInTotal);
	void SetMaxVertexCount(uint32_t inMaxPerCluster, uint32_t inMaxInTotal);
	void CustomizeVertexFormat(VkFormat inFormat);
	// Synchronous Host query with no allocation or submission; no GPU addresses are required.
	// Requires an initialized Device with CLAS support and a positive maximum target count.
	// Use matching layout budgets/format, operation, destination mode and capacity when binding
	// the build target. Returned scratch and task-array requirements cover the whole batch.
	auto QuerySizesInfo(BuildType inType, uint32_t inMaxTargetCount) const -> AccelStructSizesInfo;
};

struct ClusterLevelAccelStructBuildIndirectInfo final
{
	friend class CommandBuffer;
private:
	VulkanDetail::ClusterBuildAddresses m_addresses{};
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{};
	VkDeviceAddress m_tasks = 0;
	VkDeviceAddress m_scratch = 0;
	VkDeviceAddress m_count = 0;
public:
	// Snapshots target parameters and budget; GPU addresses remain borrowed.
	// Retain target storage through dependent GPU uses and arrays through the build.
	// Every target setter replaces the operation, destination mode, capacity and addresses.
	// Address arrays contain packed VkDeviceAddress values: explicit reads, implicit writes.
	void SetBuildTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetExplicitTarget(const ClusterLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressArray, uint32_t inMaxTargetCount);
	void SetExplicitInstantiateTarget(const ClusterLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressArray, uint32_t inMaxTargetCount);
	void SetImplicitTarget(const ClusterLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressOutputArray, uint32_t inMaxTargetCount, VkDeviceAddress inStorageBuffer);
	void SetImplicitInstantiateTarget(const ClusterLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressOutputArray, uint32_t inMaxTargetCount, VkDeviceAddress inStorageBuffer);
	void SetBuildInfoAddress(VkDeviceAddress inAddress) { m_tasks = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_count = inAddress; }
};

class BottomLevelAccelStructBuildLayout final
{
public:
	enum class BuildType
	{
		EXPLICIT,
		IMPLICIT,
	};
	void SetMaxClusterCount(uint32_t inMaxPerAccelStruct, uint32_t inMaxInTotal);
	// Same Host query contract as the CLAS layout: no addresses, allocation or submission.
	// Bind targets with matching budgets, destination mode and positive maximum target count.
	auto QuerySizesInfo(BuildType inType, uint32_t inMaxTargetCount) const -> AccelStructSizesInfo;
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
	// Same target snapshot and borrowed-resource contract as direct CLAS builds.
	// Every target setter replaces the destination mode, capacity and addresses.
	// Address arrays contain packed VkDeviceAddress values: explicit reads, implicit writes.
	void SetBuildTarget(const BottomLevelAccelStructBatch& inBatch);
	void SetExplicitTarget(const BottomLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressArray, uint32_t inMaxTargetCount);
	void SetImplicitTarget(const BottomLevelAccelStructBuildLayout& inLayout, VkDeviceAddress inTargetAddressOutputArray, uint32_t inMaxTargetCount, VkDeviceAddress inStorageBuffer);
	void SetBuildInfoAddress(VkDeviceAddress inAddress) { m_tasks = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_scratch = inAddress; }
	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_count = inAddress; }
};
