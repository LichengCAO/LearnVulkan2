#pragma once
#include "common.h"
#include "acceleration_structure_gpu_layout.h"
#include <optional>

// Low-level batch input. The command snapshots both arrays when appended.
struct VkAccelStructBuildInfo
{
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesInfos;
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
};

struct AccelStructMemoryRequirements
{
    // Output capacity bound, not the actual GPU-built size. For CLAS and Cluster BLAS:
    // implicit destinations require this many bytes for the whole contiguous batch;
    // an explicit object using the layout maxima fits this many bytes. In a managed
    // heterogeneous batch, task i must fit owner i's separately queried capacities.
    VkDeviceSize accelerationStructureSize = 0;
    // Required alignment of the implicit storage base or each explicit destination address.
    // For fixed explicit slots, align the slot stride up from accelerationStructureSize.
    VkDeviceSize accelerationStructureAlignment = 0;

    // Scratch requirements cover the whole queried batch.
    VkDeviceSize buildScratchBufferSize = 0;
    // Present only for ordinary BLAS/TLAS created with ALLOW_UPDATE; a present zero
    // still permits KHR UPDATE. CLAS and PTLAS do not use KHR UPDATE scratch.
    std::optional<VkDeviceSize> updateScratchBufferSize;
    // BUILD and KHR UPDATE share this address alignment.
    VkDeviceSize scratchAlignment = 0;
};
