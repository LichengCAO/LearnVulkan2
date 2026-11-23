#pragma once
#include "acceleration_structure_types.h"
#include "buffer.h"
#include "../ref_counted.h"
#include "private/cluster_acceleration_structure_build.h"
#include <span>

class CommandBuffer;

class ClusterLevelAccelStructTemplateBuildInfo
{
	VkDeviceAddress m_indices = 0;
	uint32_t m_triangles = 0;
	uint32_t m_vertices = 0;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	VkDeviceSize m_indexStride = 0;
public:
	void SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount);
	void SetVertexCount(uint32_t inVertexCount) { m_vertices = inVertexCount; }
	void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
	void CustomizeIndexStride(VkDeviceSize inStride) { m_indexStride = inStride; }
	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{ return AccelStructGpuLayout::Template::Layout; }
	// Optional CPU initialization helper; shader producers write the same published GPU ABI.
	static void CommitToDeviceInline(std::span<const ClusterLevelAccelStructTemplateBuildInfo> inInfos,
		Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

class ClusterLevelAccelStructCreateInfo final
{
	friend class ClusterLevelAccelStruct;
	uint32_t m_triangles = 0;
	uint32_t m_vertices = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	bool m_useTemplate = false;
public:
	void SetMaxTriangleCount(uint32_t inCount);
	void SetMaxVertexCount(uint32_t inCount);
	void CustomizeVertexFormat(VkFormat inFormat);
	void CustomizeTemplateUsage(bool inUseTemplate);
};

class ClusterLevelAccelStruct final : public RefCounted
{
	std::unique_ptr<Buffer> m_storage;
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
private:
	uint32_t m_clusterID = 0;
	uint32_t m_triangleCount = 0;
	VkDeviceAddress m_vertexAddress = 0;
	VkDeviceSize m_vertexStride = 0;
	uint32_t m_uniqueVertexCount = 0;
	VkDeviceAddress m_indexAddress = 0;
	VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
	VkDeviceSize m_indexStride = 0;
	VkFormat m_vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

	auto _Encode() const -> VkClusterAccelerationStructureBuildTriangleClusterInfoNV;

public:
	// Every three indices describe one triangle.
	void SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount);
	void SetVertexBuffer(VkDeviceAddress inAddress, uint32_t inUniqueVertexCount);
	void CustomizeVertexStride(VkDeviceSize inStride) { m_vertexStride = inStride; }
	void CustomizeIndexType(VkIndexType inType) { m_indexType = inType; }
	void CustomizeIndexStride(VkDeviceSize inStride) { m_indexStride = inStride; }
	void CustomizeVertexFormat(VkFormat inFormat) { m_vertexFormat = inFormat; }
	void CustomizeClusterID(uint32_t inID) { m_clusterID = inID; }

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
	VkClusterAccelerationStructureClustersBottomLevelInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
public:
	void SetMaxClusterCount(uint32_t inMaxPerAccelStruct, uint32_t inMaxInTotal);
};

class ClusterAccelStructMoveLimits final
{
	friend struct VulkanDetail::ClusterSizeQuery;
	friend struct VulkanDetail::ClusterCommandState;
	VkClusterAccelerationStructureMoveObjectsInputNV m_limits{
		VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV, nullptr,
		VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_NV };
public:
	void SetObjectType(VkClusterAccelerationStructureTypeNV inType) { m_limits.type = inType; }
	// Total bytes across the active move operations.
	void SetMaxMovedBytes(VkDeviceSize inBytes) { m_limits.maxMovedBytes = inBytes; }
	// Disjoint source/destination storage permits copies that preserve source validity.
	void CustomizeNoMoveOverlap(bool inNoOverlap) { m_limits.noMoveOverlap = inNoOverlap ? VK_TRUE : VK_FALSE; }
};

class ClusterAccelStructMoveInfo final
{
	VkDeviceAddress m_source = 0;
public:
	// Borrows a built object with the type selected in the move limits.
	void SetSourceReference(VkDeviceAddress inAddress) { m_source = inAddress; }
	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{ return AccelStructGpuLayout::Move::Layout; }
	static void CommitToDeviceInline(std::span<const ClusterAccelStructMoveInfo> inInfos,
		Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

class ClusterTemplateIndicesQueryInfo final
{
	VkDeviceAddress m_template = 0;
public:
	// Borrows a built triangle cluster template.
	void SetTemplateReference(VkDeviceAddress inAddress) { m_template = inAddress; }
	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{ return AccelStructGpuLayout::TemplateIndicesQuery::Layout; }
	static void CommitToDeviceInline(std::span<const ClusterTemplateIndicesQueryInfo> inInfos,
		Buffer& inDstBuffer, VkDeviceSize inOffset = 0);
};

// Static memory/scratch queries access the Device; task bytes come from GetBuildInfoLayout().GetBufferSize().
// All MOVE scratch queries take the larger of the native build and update scratch fields.
// Target selection snapshots limits, capacity and all required output addresses without queries.
// Reselection clears the optional size table and preserves arguments, scratch and GPU count.
// Callers guarantee borrowed AS/scratch capacity, Device-specific alignment and allocation disjointness.
// Implicit builds write addresses into the caller's output table. Explicit builds read the
// caller's table of destination addresses. Keep the tables alive through the commands using them
// and keep source CLAS storage alive through the last dependent Cluster BLAS use.
// All GPU addresses remain borrowed through their last dependent use; scratch is passed unchanged.
class ComputeClusterAccelStructSizesCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	static auto GetBottomLevelAccelStructBuildScratchRequirements(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetClusterTemplateBuildScratchRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetClusterBuildScratchRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetInstantiateClusterBuildScratchRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetScratchAlignment() -> VkDeviceSize;
	static auto GetMoveObjectsScratchRequirements(const ClusterAccelStructMoveLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetQueryTemplateIndicesBuildSizes(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkAccelerationStructureBuildSizesInfoKHR;

	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetClusterTemplateTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetMoveObjectsTarget(const ClusterAccelStructMoveLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);
	void SetQueryTemplateIndicesTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputSizes);

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeOperationCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
};

class ExplicitClusterAccelStructCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetMoveObjectsTarget(const ClusterAccelStructMoveLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationReferences);
	void SetQueryTemplateIndicesTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inDestinationDataAddresses);
	static auto GetBottomLevelAccelStructMemoryRequirements(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetInstantiateClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetMoveObjectsScratchRequirements(const ClusterAccelStructMoveLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetQueryTemplateIndicesBuildSizes(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkAccelerationStructureBuildSizesInfoKHR;
	static auto GetScratchAlignment() -> VkDeviceSize;
	static auto GetMoveObjectsDestinationAlignment(VkClusterAccelerationStructureTypeNV inType) -> VkDeviceSize;

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeOperationCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
	// Optional packed uint32_t sizes read by the GPU.
	void CustomizeInputSizesArray(VkDeviceAddress inAddress) { m_state.target.sizes = inAddress; }
};

class ImplicitClusterAccelStructCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::ClusterCommandState m_state;
public:
	void SetBottomLevelAccelStructTarget(const ClusterBottomLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetClusterTemplateTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetInstantiateClusterTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetMoveObjectsTarget(const ClusterAccelStructMoveLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inStorage, VkDeviceAddress inOutputReferences);
	void SetQueryTemplateIndicesTarget(const ClusterLevelAccelStructBuildLimits& inLimits, uint32_t inMaxTargetCount, VkDeviceAddress inOutputData, VkDeviceAddress inOutputDataAddresses);
	static auto GetBottomLevelAccelStructMemoryRequirements(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterTemplateMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetInstantiateClusterMemoryRequirements(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> AccelStructMemoryRequirements;
	static auto GetMoveObjectsScratchRequirements(const ClusterAccelStructMoveLimits&, uint32_t inMaxTargetCount) -> ClusterScratchRequirements;
	static auto GetQueryTemplateIndicesBuildSizes(const ClusterLevelAccelStructBuildLimits&, uint32_t inMaxTargetCount) -> VkAccelerationStructureBuildSizesInfoKHR;
	static auto GetScratchAlignment() -> VkDeviceSize;
	static auto GetMoveObjectsDestinationAlignment(VkClusterAccelerationStructureTypeNV inType) -> VkDeviceSize;

	void SetArgumentAddress(VkDeviceAddress inAddress) { m_state.arguments = inAddress; }
	void SetScratchAddress(VkDeviceAddress inAddress) { m_state.scratch = inAddress; }

	// Optional GPU actual count; defaults to the target's maximum capacity.
	void CustomizeOperationCountAddress(VkDeviceAddress inAddress) { m_state.count = inAddress; }
	// Optional packed uint32_t sizes written by the GPU.
	void CustomizeOutputSizesArray(VkDeviceAddress inOutputAddress) { m_state.target.sizes = inOutputAddress; }
};
