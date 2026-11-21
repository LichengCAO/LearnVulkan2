#pragma once
#include "common.h"
#include "acceleration_structure_gpu_layout.h"

// Low-level batch input. The command snapshots both arrays when appended.
struct VkAccelStructBuildInfo
{
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesInfos;
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
};

struct AccelStructSizesInfo
{
	// Scratch requirements cover the whole queried batch; unsupported updates require zero bytes.
	VkDeviceSize updateScratchBufferSize = 0;
	VkDeviceSize buildScratchBufferSize = 0;
	VkDeviceSize scratchAlignment = 0;
	// Task array capacity for the queried maximum target count, not the GPU actual count.
	// Ordinary BLAS uses zero; TLAS uses maximum instance count times packed record stride.
	// For PTLAS, this is a fixed 72-byte budget for three outer operation groups,
	// independent of the native AS/scratch size query; inner record arrays are separate.
	VkDeviceSize buildInfoBufferSize = 0;
	// Output capacity bound, not the actual GPU-built size. For CLAS and Cluster BLAS:
	// implicit destinations require this many bytes for the whole contiguous batch;
	// an explicit object using the layout maxima fits this many bytes. In a managed
	// heterogeneous batch, task i must fit owner i's separately queried capacities.
	// Compute-sizes commands return zero for both AS fields and write per-task byte sizes on the GPU.
	VkDeviceSize accelerationStructureSize = 0;
	// Required alignment of the implicit storage base or each explicit destination address.
	// For fixed explicit slots, align the slot stride up from accelerationStructureSize.
	VkDeviceSize accelerationStructureAlignment = 0;
};
