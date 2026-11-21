#pragma once
#include "acceleration_structure_types.h"
#include "../ref_counted.h"
#include <span>

class Buffer;

class PartitionedTopLevelAccelStructCreateInfo final
{
public:
	// Fixed PTLAS slot/partition capacities, not the number of records changed by a command.
	void SetMaxInstanceCount(uint32_t inMaxInstanceCount) noexcept;
	void SetPartitionCount(uint32_t inPartitionCount) noexcept;
	void SetMaxInstanceCountInPartition(uint32_t inMaxInstancePerPartition, uint32_t inMaxInstanceInGlobalPartition) noexcept;
	// Defaults to zero.
	void CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept;

	// Defaults to 3: WRITE_INSTANCE, UPDATE_INSTANCE and WRITE_PARTITION_TRANSLATION.
	// Capacity of the outer operation-group array, in [1, 3]; each type appears at most once.
	// This is a wrapper command budget, not an instance count or native AS size-query input.
	void CustomizeMaxOperationGroupCount(uint32_t inCount);
};

// Address-based PTLAS storage, distinct from a KHR TopLevelAccelStruct/handle.
class PartitionedTopLevelAccelStruct final : public RefCounted
{
public:
	PartitionedTopLevelAccelStruct();
	~PartitionedTopLevelAccelStruct() noexcept override;
	PartitionedTopLevelAccelStruct(const PartitionedTopLevelAccelStruct&) = delete;
	auto operator=(const PartitionedTopLevelAccelStruct&) -> PartitionedTopLevelAccelStruct& = delete;
	PartitionedTopLevelAccelStruct(PartitionedTopLevelAccelStruct&&) = delete;
	auto operator=(PartitionedTopLevelAccelStruct&&) -> PartitionedTopLevelAccelStruct& = delete;

	// Synchronous size query and storage allocation for an empty target; no GPU build/submission.
	// Independent instances may create/release concurrently with stable Device/allocator/dispatch.
	// Exclude conflicting access to one instance and finish GPU uses before final RAII release.
	void Create(const PartitionedTopLevelAccelStructCreateInfo& inCreateInfo);
	// Valid storage does not imply that a GPU build has completed.
	auto IsValid() const noexcept -> bool;
	auto GetDeviceAddress() const noexcept -> VkDeviceAddress;
	auto GetVkBuildAccelerationStructureFlags() const noexcept -> VkBuildAccelerationStructureFlagsKHR;
	// Includes AS/scratch requirements. buildInfoBufferSize covers the outer operation array;
	// callers size each inner record array using its record type's GetBuildInfoLayout().
	auto GetSizesInfo() const noexcept -> const AccelStructSizesInfo&;
};

// One operation group selects one operation type and its inner GPU record array.
// Host records use GLM types and must be encoded; their sizeof/layout is not the GPU ABI.
class PartitionedTopLevelAccelStructOperationGroup final
{
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

		static auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout;
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

		static auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout;
	};

	struct WritePartitionTranslationRecord
	{
		// In range and unique within the submitted WRITE_PARTITION_TRANSLATION group.
		uint32_t partitionIndex = 0;
		glm::vec3 partitionTranslation{};

		static auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout;
	};

	// Each setter replaces the selected operation group. inCount counts inner records.
	// Addresses refer to arrays encoded with the corresponding record type's GPU layout.
	void SetWriteInstances(VkDeviceAddress inArgumentAddress, uint32_t inCount);
	void SetUpdateInstances(VkDeviceAddress inArgumentAddress, uint32_t inCount);
	void SetWritePartitionTranslations(VkDeviceAddress inArgumentAddress, uint32_t inCount);

	// GPU layout of the outer operation-group array, not any of its inner record arrays.
	static auto GetBuildInfoLayout() noexcept -> AccelStructBuildInfoLayout;

	// With a source PTLAS, submit only changed slots; each WRITE record still describes a full slot.
	// Encodes Host descriptions to native GPU layouts and uploads synchronously; never memcpy
	// the Host structures. Caller excludes conflicting Host/GPU access to the destination range.
	static void CommitToDeviceInline(std::span<const WriteInstanceRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const UpdateInstanceRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const WritePartitionTranslationRecord> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
	static void CommitToDeviceInline(std::span<const PartitionedTopLevelAccelStructOperationGroup> inArgumentToCommit, Buffer& inDstBuffer, VkDeviceSize inOffset);
};

// Value description: setters snapshot addresses, capacities, flags and requirements.
// GPU storage, argument/count/scratch buffers and referenced BLAS remain borrowed.
// Exclude conflicting Host access to one description; retain resources through dependent GPU use.
class BuildPartitionedTopLevelAccelStructCommandInput final
{
public:
	void SetAccelStructToBuild(PartitionedTopLevelAccelStruct& inOutputAccelStruct);
	void SetScratchAddress(VkDeviceAddress inScratchAddress);
	// Outer operation-group array; each operation type may appear at most once.
	void SetArgumentAddress(VkDeviceAddress inArgumentAddress);
	// GPU uint32_t outer group count, bounded by CustomizeMaxOperationGroupCount; not an instance count.
	// The producer supplies a valid count. Host code does not read it back.
	void SetArgumentCountAddress(VkDeviceAddress inCountAddress);

	// Defaults to no source (address zero): build from empty contents.
	// A supplied source must already be GPU-built with compatible capacities/flags.
	// Source == target updates in place; a different source is preserved and its contents carried forward.
	void CustomizeSourceAccelStruct(const PartitionedTopLevelAccelStruct& inAccelStruct);
};
