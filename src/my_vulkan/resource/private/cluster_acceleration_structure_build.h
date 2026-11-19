#pragma once
#include "../acceleration_structure_types.h"

// Internal value snapshot shared by the cluster descriptions and command recording.
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
