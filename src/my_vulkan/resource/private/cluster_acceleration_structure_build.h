#pragma once
#include "../acceleration_structure_types.h"
class Buffer;

// Internal value snapshot shared by the cluster descriptions and command recording.
namespace VulkanDetail
{
	struct ClusterBuildAddresses
	{
		AccelStructSizesInfo sizes{};
		VkDeviceAddress storage = 0;
		VkDeviceAddress references = 0;
		uint32_t capacity = 0;
		VkClusterAccelerationStructureOpModeNV mode{};
		VkClusterAccelerationStructureOpTypeNV operation{};
		// Borrowed destination ranges known from an explicit Batch, without owning its Buffers.
		std::vector<std::pair<VkDeviceAddress, VkDeviceSize>> destinations;
	};
	auto CreateClusterDestination(const AccelStructSizesInfo& inSizes) -> std::unique_ptr<Buffer>;
}
