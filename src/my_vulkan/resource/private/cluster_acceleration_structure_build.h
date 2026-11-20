#pragma once
#include "../acceleration_structure_types.h"
#include <utility>
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

		ClusterBuildAddresses() = default;
		ClusterBuildAddresses(const ClusterBuildAddresses&) = default;
		ClusterBuildAddresses(ClusterBuildAddresses&& other) noexcept { _Swap(other); }

		auto operator=(ClusterBuildAddresses other) noexcept -> ClusterBuildAddresses&
		{
			_Swap(other);
			return *this;
		}

	private:
		// Transfer the bound target and its overlap checks together. Move construction
		// exchanges the source with an empty target, which also covers move assignment.
		void _Swap(ClusterBuildAddresses& other) noexcept
		{
			std::swap(sizes, other.sizes);
			std::swap(storage, other.storage);
			std::swap(references, other.references);
			std::swap(capacity, other.capacity);
			std::swap(mode, other.mode);
			std::swap(operation, other.operation);
			destinations.swap(other.destinations);
		}
	};
	auto CreateClusterDestination(const AccelStructSizesInfo& inSizes) -> std::unique_ptr<Buffer>;
}
