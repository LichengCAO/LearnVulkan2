#pragma once
#include "acceleration_structure_types.h"
#include "../ref_counted.h"
#include "private/partitioned_acceleration_structure_build.h"
#include <span>

class Buffer;

class PartitionedTopLevelAccelStructCreateInfo final
{
	friend class PartitionedTopLevelAccelStruct;
	VulkanDetail::PartitionedBuildConfiguration m_configuration;
public:
	// Fixed PTLAS slot/partition capacities, not the number of records changed by a command.
	void SetMaxInstanceCount(uint32_t inCount) noexcept { m_configuration.instanceCount = inCount; }
	void SetPartitionCount(uint32_t inCount) noexcept { m_configuration.partitionCount = inCount; }
	void SetMaxInstanceCountPerPartition(uint32_t inCount) noexcept { m_configuration.maxInstancesPerPartition = inCount; }
	void CustomizeMaxInstanceCountInGlobalPartition(uint32_t inCount) noexcept
	{ m_configuration.maxInstancesInGlobalPartition = inCount; }
	// Defaults to zero.
	void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept { m_configuration.flags = inFlags; }
	// Defaults to false. Enabling translation requires additional PTLAS storage.
	// Creation size queries and subsequent builds use the same saved
	// VkPartitionedAccelerationStructureFlagsNV::enablePartitionTranslation value.
	void CustomizePartitionTranslation(bool inEnable) noexcept { m_configuration.enablePartitionTranslation = inEnable; }
};

// Address-based PTLAS storage, distinct from a KHR TopLevelAccelStruct/handle.
class PartitionedTopLevelAccelStruct final : public RefCounted
{
	friend class BuildPartitionedTopLevelAccelStructCommandInput;
	std::unique_ptr<Buffer> m_storage;
	VulkanDetail::PartitionedBuildConfiguration m_configuration;
	AccelStructMemoryRequirements m_memoryRequirements;
	VkDeviceAddress m_address = 0;
public:
	PartitionedTopLevelAccelStruct();
	~PartitionedTopLevelAccelStruct() noexcept override;
	PartitionedTopLevelAccelStruct(const PartitionedTopLevelAccelStruct&) = delete;
	auto operator=(const PartitionedTopLevelAccelStruct&) -> PartitionedTopLevelAccelStruct& = delete;
	PartitionedTopLevelAccelStruct(PartitionedTopLevelAccelStruct&&) = delete;
	auto operator=(PartitionedTopLevelAccelStruct&&) -> PartitionedTopLevelAccelStruct& = delete;

	// Synchronous size query and storage allocation for an empty target; no GPU build/submission.
	// Snapshots the partition-translation setting for both the size query and later builds.
	// Independent instances may create/release concurrently with stable Device/allocator/dispatch.
	// Exclude conflicting access to one instance and finish GPU uses before final RAII release.
	void Create(const PartitionedTopLevelAccelStructCreateInfo& inCreateInfo);
	// Valid storage does not imply that a GPU build has completed.
	auto IsValid() const noexcept -> bool { return m_address != 0; }
	auto GetDeviceAddress() const noexcept -> VkDeviceAddress { return m_address; }
	auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR { return m_configuration.flags; }
	// Saved AS/scratch requirements. The outer operation table uses the separate
	// AccelStructGpuLayout::Partitioned::OperationBufferSize allocation budget;
	// inner record arrays use their record type's GetBuildInfoLayout().
	auto GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements& { return m_memoryRequirements; }
};

// One operation group selects one operation type and its inner GPU record array.
// The outer array supports at most three groups, one per operation type.
// Host records use GLM types and must be encoded; their sizeof/layout is not the GPU ABI.
class PartitionedTopLevelAccelStructOperationGroup final
{
	VkBuildPartitionedAccelerationStructureIndirectCommandNV m_operation{
		VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_MAX_ENUM_NV, 0, {0, 0}};
public:
	struct WriteInstanceRecord
	{
		glm::mat4 transform{ 1.0f };
		// Used only with ENABLE_EXPLICIT_BOUNDING_BOX_NV. Must contain transformed geometry,
		// before partition translation, including replacements made through UpdateInstanceRecord.
		VkAabbPositionsKHR explicitAABB{};
		uint32_t shaderGroupIndexOffset = 0; // Low 24 bits only.
		uint32_t instanceCustomIndex = 0; // Full 32-bit PTLAS instance ID.
		uint32_t mask = 0xFF; // Low 8 bits only.
		VkPartitionedAccelerationStructureInstanceFlagsNV flags = 0;
		VkDeviceAddress accelStructReference = 0; // BLAS/Cluster BLAS address; zero disables the slot.

		// Persistent PTLAS slot, not an index into the dirty-record array.
		// Must be in range and unique within the submitted WRITE_INSTANCE group.
		uint32_t instanceIndex = 0;
		uint32_t partitionIndex = 0;

		static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
		{ return AccelStructGpuLayout::Partitioned::WriteInstance::Layout; }
	};

	// Changes only the AS reference and SBT offset; transform/partition changes require WRITE.
	// A previously WRITE-assigned AS requires ENABLE_EXPLICIT_BOUNDING_BOX_NV for UPDATE.
	// A slot disabled by WRITE with a zero AS reference must be enabled again through WRITE.
	struct UpdateInstanceRecord
	{
		uint32_t shaderGroupIndexOffset = 0; // Low 24 bits only.
		VkDeviceAddress accelStructReference = 0; // Zero disables the slot.
		// Persistent PTLAS slot; in range and unique within the submitted UPDATE_INSTANCE group.
		uint32_t instanceIndex = 0;

		static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
		{ return AccelStructGpuLayout::Partitioned::UpdateInstance::Layout; }
	};

	// The eventual target must have been created with partition translation enabled.
	// Encoding/uploading records does not enable translation or execute the operation.
	struct WritePartitionTranslationRecord
	{
		// In range and unique within the submitted WRITE_PARTITION_TRANSLATION group.
		uint32_t partitionIndex = 0;
		glm::vec3 partitionTranslation{};

		static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
		{ return AccelStructGpuLayout::Partitioned::Translation::Layout; }
	};

	// Each setter replaces the selected operation group. inCount counts inner records.
	// Addresses refer to arrays encoded with the corresponding record type's GPU layout.
	void SetWriteInstances(VkDeviceAddress inArgumentAddress, uint32_t inCount);
	void SetUpdateInstances(VkDeviceAddress inArgumentAddress, uint32_t inCount);
	// Requires an eventual target created with CustomizePartitionTranslation(true).
	void SetWritePartitionTranslations(VkDeviceAddress inArgumentAddress, uint32_t inCount);

	// GPU layout of the outer operation-group array, not any of its inner record arrays.
	static constexpr auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout
	{ return AccelStructGpuLayout::Partitioned::Operation::Layout; }

	// With a source PTLAS, submit only changed slots; each WRITE record still describes a full slot.
	// Encodes Host descriptions to native GPU layouts and uploads synchronously; never memcpy
	// the Host structures. Caller excludes conflicting Host/GPU access to the destination range.
	static void CommitToDeviceInline(std::span<const WriteInstanceRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const UpdateInstanceRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const WritePartitionTranslationRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const PartitionedTopLevelAccelStructOperationGroup> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
};

// Value description: setters snapshot addresses, capacities, flags (including
// partition-translation enablement) and requirements. Rebuild native pNext from saved values.
// GPU storage, argument/count/scratch buffers and referenced BLAS remain borrowed.
// Exclude conflicting Host access to one description; retain resources through dependent GPU use.
class BuildPartitionedTopLevelAccelStructCommandInput final
{
	friend class CommandBuffer;
	VulkanDetail::PartitionedBuildSnapshot m_snapshot;
public:
	// Transactional selection; clears source, preserves scratch. Requires a 72-byte outer table.
	// GPU uint32_t actual outer group count, bounded by three by the producer.
	// Operation types must be unique. Host code does not read back or clamp the count.
	void SetTarget(const PartitionedTopLevelAccelStruct& inTarget,
		VkDeviceAddress inOperationGroupsAddress, VkDeviceAddress inOperationGroupCountAddress);
	void SetScratchAddress(VkDeviceAddress inAddress) noexcept { m_snapshot.scratch = inAddress; }
	auto GetMemoryRequirements() const -> AccelStructMemoryRequirements;

	// Defaults to no source (address zero): build from empty contents.
	// A supplied source must already be GPU-built with compatible capacities/flags,
	// including the partition-translation setting.
	// Source == target updates in place; a different source is preserved and its contents carried forward.
	void CustomizeSourceAccelStruct(const PartitionedTopLevelAccelStruct& inAccelStruct);
};
