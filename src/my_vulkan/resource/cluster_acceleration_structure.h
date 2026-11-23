#pragma once
#include "acceleration_structure_types.h"
#include "buffer.h"
#include "acceleration_structure.h"
#include "../intrusive_ptr.h"
#include "../ref_counted.h"
#include "private/cluster_acceleration_structure_build.h"
#include <span>

class CommandBuffer;
class MyDevice;
class BottomLevelAccelStruct;

class ClusterLevelAccelStructTemplateBuildInfo
{
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
	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{ return AccelStructGpuLayout::Template::Layout; }
	// Optional CPU initialization helper; shader producers write the same published GPU ABI.
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructTemplateBuildInfo> inInfos,
		Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

class ClusterLevelAccelStructTemplateBatchCreateInfo final
{
	friend class ClusterLevelAccelStructTemplateBatch;
	uint32_t m_maxTemplateCount = 0;
	uint32_t m_maxTriangleCountPerTemplate = 0;
	uint32_t m_maxVertexCountPerTemplate = 0;
	uint32_t m_maxTotalTriangleCount = 0;
	uint32_t m_maxTotalVertexCount = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
public:
	// All five capacities are required; index bindings/types belong to task records.
	void SetMaxTemplateCount(uint32_t inCount) { m_maxTemplateCount = inCount; }
	void SetMaxTriangleCountPerTemplate(uint32_t inCount) { m_maxTriangleCountPerTemplate = inCount; }
	void SetMaxVertexCountPerTemplate(uint32_t inCount) { m_maxVertexCountPerTemplate = inCount; }
	void SetMaxTotalTriangleCount(uint32_t inCount) { m_maxTotalTriangleCount = inCount; }
	void SetMaxTotalVertexCount(uint32_t inCount) { m_maxTotalVertexCount = inCount; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }
};

class ClusterLevelAccelStructTemplateBatch final : public RefCounted
{
	friend struct VulkanDetail::ClusterCommandState;
	std::unique_ptr<Buffer> m_storage;
	std::unique_ptr<Buffer> m_references;
	VkDeviceAddress m_storageAddress = 0;
	VkDeviceAddress m_referenceAddress = 0;
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
	AccelStructMemoryRequirements m_memoryRequirements{};
	uint32_t m_capacity = 0;
public:
	// Host-only creation: queries and allocates template storage and its address table.
	// It does not upload tasks, submit GPU work, wait, or read back addresses.
	// Independent batches may create/release concurrently with stable Device dependencies;
	// exclude same-instance conflicts and retain templates through dependent GPU use.
	void Create(const ClusterLevelAccelStructTemplateBatchCreateInfo& inCreateInfo);
	// Saved Host allocation state; validity does not mean the GPU build has completed.
	auto IsValid() const noexcept -> bool { return m_storage != nullptr; }
	auto GetCapacity() const noexcept -> uint32_t { return m_capacity; }
	auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements& { return m_memoryRequirements; }
	// Borrowed packed GPU address table. Only the built active prefix is valid.
	auto GetReferenceBuffer() const noexcept -> const Buffer* { return m_references.get(); }
	auto GetVertexFormat() const noexcept -> VkFormat { return m_limits.vertexFormat; }
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureTriangleClusterInputNV { return m_limits; }
};

class ClusterLevelAccelStructCreateInfo final
{
	friend class ClusterLevelAccelStruct;
	friend class ClusterLevelAccelStructBatch;
	uint32_t m_triangles = 0;
	uint32_t m_vertices = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	bool m_useTemplate = false;
public:
	void SetMaxTriangleCount(uint32_t inCount);
	void SetMaxVertexCount(uint32_t inCount);
	void CustomizeVertexFormat(VkFormat inFormat);
	void CustomizeIndexType(VkIndexType inType);
	void CustomizeTemplateUsage(bool inUseTemplate);
};

class ClusterLevelAccelStruct final : public RefCounted
{
	friend class ClusterLevelAccelStructBatch;
	friend struct VulkanDetail::ClusterCommandState;
	std::unique_ptr<Buffer> m_storage;
	ClusterLevelAccelStructCreateInfo m_info;
	VkDeviceAddress m_reference = 0;
	AccelStructMemoryRequirements m_memoryRequirements{};
public:
	// Synchronous Host creation for an empty target; no upload, GPU build or wait.
	// Independent owners may create/release concurrently with stable Device dependencies.
	// Exclude same-instance conflicts and complete GPU uses before final release.
	void Create(const ClusterLevelAccelStructCreateInfo&);
	// Validity means allocated storage, not GPU build completion. Queries read saved Host data.
	auto IsValid() const noexcept -> bool { return m_reference != 0; }
	auto GetDeviceAddress() const noexcept -> VkDeviceAddress { return m_reference; }
	auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements& { return m_memoryRequirements; }
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
	void SetMaxTriangleCountPerCluster(uint32_t inCount) { m_maxClusterTriangleCount = inCount; }
	void SetMaxVertexCountPerCluster(uint32_t inCount) { m_maxClusterVertexCount = inCount; }
	void SetMaxTotalTriangleCount(uint32_t inCount) { m_maxTotalTriangleCount = inCount; }
	void SetMaxTotalVertexCount(uint32_t inCount) { m_maxTotalVertexCount = inCount; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }
	void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
	void CustomizeTemplateUsage(bool inUseTemplate) { m_useTemplate = inUseTemplate; }
};

class ClusterLevelAccelStructExplicitBatchCreateInfo final
{
	friend class ClusterLevelAccelStructBatch;
	Buffer* m_references = nullptr;
	VkDeviceSize m_offset = 0;
	std::vector<const ClusterLevelAccelStruct*> m_destinations;
public:
	// Borrows a packed VkDeviceAddress table until Batch Create completes.
	// Successful creation retains a strong reference to this Buffer for the Batch lifetime.
	// The Buffer must use intrusive reference-counted ownership.
	void SetReferenceBuffer(Buffer& inReferenceBuffer, VkDeviceSize inOffset);
	// Copies the pointer list; destination objects remain borrowed until Batch Create completes.
	// Entry i must contain destination i's reference. Task i must fit that owner's capacities.
	// Destinations must use intrusive reference-counted ownership; the created Batch retains each destination.
	void SetDestinationAccelStructs(std::span<const ClusterLevelAccelStruct* const> inAccelStructs);
};

class ClusterLevelAccelStructBatch final : public RefCounted
{
	friend struct VulkanDetail::ClusterCommandState;
	friend struct CommandBufferTestProbe;
	friend struct ClusterAccelerationStructureTestProbe;
	friend struct ClusterAccelerationStructureGpuTestProbe;
private:
	std::unique_ptr<Buffer> m_storageBuffer;
	std::unique_ptr<Buffer> m_referenceBuffer;
	VkDeviceAddress m_storageAddress = 0;
	VkDeviceAddress m_referenceAddress = 0;
	IntrusivePtr<Buffer> m_explicitReferences;
	std::vector<IntrusivePtr<const ClusterLevelAccelStruct>> m_destinations;
	VkDeviceSize m_referenceOffset = 0;
	AccelStructMemoryRequirements m_memoryRequirements{};
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
	// and every destination AS until Batch destruction. Retain the Batch through dependent GPU uses.
	void Create(const ClusterLevelAccelStructExplicitBatchCreateInfo& inCreateInfo);
	// Borrowed address table retained by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept->const Buffer*;
	auto GetReferenceBufferOffset() const noexcept -> VkDeviceSize { return m_referenceOffset; }
	auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements&;
	auto GetCapacity() const noexcept -> uint32_t;
	auto GetScratchAlignment() const noexcept -> VkDeviceSize;
	auto GetVertexFormat() const noexcept -> VkFormat { return m_triangleInput.vertexFormat; }
	auto GetIndexType() const noexcept -> VkIndexType { return m_indexType; }
	auto GetBuildLimits() const noexcept -> VkClusterAccelerationStructureTriangleClusterInputNV { return m_triangleInput; }
};

class ClusterBottomLevelAccelStructImplicitBatchCreateInfo final
{
	friend class ClusterBottomLevelAccelStructBatch;
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

class ClusterBottomLevelAccelStructExplicitBatchCreateInfo final
{
	friend class ClusterBottomLevelAccelStructBatch;
	Buffer* m_references = nullptr;
	VkDeviceSize m_offset = 0;
	std::vector<const BottomLevelAccelStruct*> m_destinations;
public:
	// Borrows a packed VkDeviceAddress table until Batch Create completes.
	// Successful creation retains a strong reference to this Buffer for the Batch lifetime.
	// The Buffer must use intrusive reference-counted ownership.
	void SetReferenceBuffer(Buffer& inReferenceBuffer, VkDeviceSize inOffset);
	// Copies the pointer list; destination objects remain borrowed until Batch Create completes.
	// Entry i must contain destination i's device address. Task i must fit that owner's cluster capacity.
	// Destinations must use intrusive reference-counted ownership; the created Batch retains each destination.
	void SetDestinationAccelStructs(std::span<const BottomLevelAccelStruct* const> inAccelStructs);
};

class ClusterBottomLevelAccelStructBatch final : public RefCounted
{
	friend struct VulkanDetail::ClusterCommandState;
	friend struct CommandBufferTestProbe;
	friend struct ClusterAccelerationStructureTestProbe;
	friend struct ClusterAccelerationStructureGpuTestProbe;
private:
	std::unique_ptr<Buffer> m_storageBuffer;
	std::unique_ptr<Buffer> m_referenceBuffer;
	VkDeviceAddress m_storageAddress = 0;
	VkDeviceAddress m_referenceAddress = 0;
	AccelStructMemoryRequirements m_memoryRequirements{};
	IntrusivePtr<Buffer> m_explicitReferences;
	std::vector<IntrusivePtr<const BottomLevelAccelStruct>> m_destinations;
	VkDeviceSize m_referenceOffset = 0;
	uint32_t m_count = 0;
	VkClusterAccelerationStructureClustersBottomLevelInputNV m_clustersInput{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };

public:
	// Same empty-target creation and lifetime contract as ClusterLevelAccelStructBatch.
	void Create(const ClusterBottomLevelAccelStructImplicitBatchCreateInfo& inCreateInfo);
	// Same synchronous empty-target contract; retains strong references to the reference Buffer
	// and every destination AS until Batch destruction. Retain the Batch through dependent GPU uses.
	void Create(const ClusterBottomLevelAccelStructExplicitBatchCreateInfo& inCreateInfo);
	// Borrowed address table retained by this batch. Only the GPU-built prefix is valid.
	auto GetReferenceBuffer() const noexcept -> const Buffer*;
	auto GetReferenceBufferOffset() const noexcept -> VkDeviceSize { return m_referenceOffset; }
	auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements&;
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
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructDirectBuildInfo> inHostInfo, Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

class ClusterBottomLevelAccelStructBuildInfo final
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
	static void CommitToDeviceInline(std::span<const ClusterBottomLevelAccelStructBuildInfo> inHostInfo, Buffer& inoutTargetBuffer, VkDeviceSize inOffset = 0);
};

class ClusterLevelAccelStructBuildLimits final
{
	friend struct VulkanDetail::ClusterSizeQuery;
	friend struct VulkanDetail::ClusterCommandState;
	friend class ClusterLevelAccelStructBatch;
	friend class ClusterLevelAccelStructTemplateBatch;
	VkClusterAccelerationStructureTriangleClusterInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV, nullptr, VK_FORMAT_R32G32B32_SFLOAT, 0, 1 };
public:
	void SetMaxTriangleCount(uint32_t inMaxPerCluster, uint32_t inMaxInTotal);
	void SetMaxVertexCount(uint32_t inMaxPerCluster, uint32_t inMaxInTotal);
	void CustomizeVertexFormat(VkFormat inFormat);
};

class ClusterBottomLevelAccelStructBuildLimits final
{
	friend struct VulkanDetail::ClusterSizeQuery;
	friend struct VulkanDetail::ClusterCommandState;
	friend class ClusterBottomLevelAccelStructBatch;
	VkClusterAccelerationStructureClustersBottomLevelInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
public:
	void SetMaxClusterCount(uint32_t inMaxPerAccelStruct, uint32_t inMaxInTotal);
};

// Static memory/scratch queries access the Device; static task sizes use the GPU ABI only.
// Target selection snapshots limits, capacity and all required output addresses without queries.
// Reselection clears optional outputs and preserves arguments, scratch and GPU count.
// Callers guarantee borrowed AS/scratch capacity, Device-specific alignment and allocation disjointness.
// All GPU addresses remain borrowed through their last dependent use; scratch is passed unchanged.
class ComputeClusterAccelStructSizesCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	static auto GetBottomLevelAccelStructBuildScratchBufferSize(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterTemplateBuildScratchBufferSize(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterBuildScratchBufferSize(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetInstantiateClusterBuildScratchBufferSize(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetScratchAlignment() -> VkDeviceSize;
	static auto GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterTemplateBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetInstantiateClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;

	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetClusterTemplateTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
};

class BuildExplicitClusterAccelStructCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBatch& inBatch);
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetClusterTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	static auto GetBottomLevelAccelStructMemoryRequirements(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetInstantiateClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetInstantiateClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
};

class BuildImplicitClusterAccelStructCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBatch& inBatch);
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetClusterTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBatch& inBatch);
	void SetClusterTemplateTarget(const ClusterLevelAccelStructTemplateBatch& inBatch);
	void SetClusterTemplateTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	static auto GetBottomLevelAccelStructMemoryRequirements(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterTemplateMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetInstantiateClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterTemplateBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;
	static auto GetInstantiateClusterBuildInfoBufferSize(uint32_t inMaxTargetCount) -> VkDeviceSize;

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeBuildCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
	void CustomizeOutputSizesArray(VkDeviceAddress inOutputAddress) { m_state.target.outputSizes = inOutputAddress; }
};
