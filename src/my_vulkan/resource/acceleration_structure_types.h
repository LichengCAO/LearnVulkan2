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
	VkDeviceSize updateScratchBufferSize = 0;
	VkDeviceSize buildScratchBufferSize = 0;
	VkDeviceSize scratchAlignment = 0;
	VkDeviceSize buildInfoBufferSize = 0;
	VkDeviceSize accelerationStructureSize = 0;
};
