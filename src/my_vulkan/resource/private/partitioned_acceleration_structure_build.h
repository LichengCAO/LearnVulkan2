#pragma once
#include "../acceleration_structure_types.h"

namespace VulkanDetail
{
struct PartitionedBuildConfiguration
{
    uint32_t instanceCount = 0;
    uint32_t partitionCount = 0;
    uint32_t maxInstancesPerPartition = 0;
    uint32_t maxInstancesInGlobalPartition = 0;
    VkBuildAccelerationStructureFlagsKHR flags = 0;
    bool enablePartitionTranslation = false;
    auto operator==(const PartitionedBuildConfiguration&) const -> bool = default;
    auto MakeInput(VkPartitionedAccelerationStructureFlagsNV& inFlags) const
        -> VkPartitionedAccelerationStructureInstancesInputNV
    {
        inFlags = {VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_FLAGS_NV};
        inFlags.enablePartitionTranslation = enablePartitionTranslation;
        return {VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCES_INPUT_NV,
            &inFlags, flags, instanceCount, maxInstancesPerPartition, partitionCount, maxInstancesInGlobalPartition};
    }
};

// Borrowed GPU ranges and pointer-free Host metadata. Copies never access resource owners.
struct PartitionedBuildSnapshot
{
    PartitionedBuildConfiguration configuration;
    AccelStructSizesInfo sizes;
    VkDeviceAddress target = 0;
    VkDeviceAddress source = 0;
    VkDeviceAddress arguments = 0;
    VkDeviceAddress count = 0;
    VkDeviceAddress scratch = 0;
};
}
