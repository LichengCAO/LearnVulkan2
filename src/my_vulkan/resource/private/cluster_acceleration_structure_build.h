#pragma once
#include "../acceleration_structure_types.h"
#include <utility>
class Buffer;
class ClusterLevelAccelStructBuildLimits;
class ClusterBottomLevelAccelStructBuildLimits;
class ClusterLevelAccelStructBatch;
class ClusterBottomLevelAccelStructBatch;
class ClusterLevelAccelStructTemplateBatch;

namespace VulkanDetail
{
    struct ClusterSizeQuery
    {
        static auto Get(const ClusterLevelAccelStructBuildLimits&, VkClusterAccelerationStructureOpTypeNV,
            VkClusterAccelerationStructureOpModeNV, uint32_t inCapacity) -> AccelStructSizesInfo;
        static auto Get(const ClusterBottomLevelAccelStructBuildLimits&, VkClusterAccelerationStructureOpModeNV,
            uint32_t inCapacity) -> AccelStructSizesInfo;
    };

    auto ClusterTaskLayout(VkClusterAccelerationStructureOpTypeNV inOperation) -> AccelStructBuildInfoLayout;

    struct ClusterBuildTarget
    {
        AccelStructSizesInfo sizes{};
        VkClusterAccelerationStructureTriangleClusterInputNV triangles{};
        VkClusterAccelerationStructureClustersBottomLevelInputNV clusters{};
        VkDeviceAddress outputSizes = 0;
        bool managed = false;
        VkDeviceAddress storage = 0;
        VkDeviceAddress references = 0;
        uint32_t capacity = 0;
        VkClusterAccelerationStructureOpModeNV mode{};
        VkClusterAccelerationStructureOpTypeNV operation{};
        // Borrowed destination ranges known from an explicit Batch, without owning its Buffers.
        std::vector<std::pair<VkDeviceAddress, VkDeviceSize>> destinations;

        ClusterBuildTarget() = default;
        ClusterBuildTarget(const ClusterBuildTarget&) = default;
        ClusterBuildTarget(ClusterBuildTarget&& other) noexcept { _Swap(other); }

        auto operator=(ClusterBuildTarget other) noexcept -> ClusterBuildTarget&
        {
            _Swap(other);
            return *this;
        }

    private:
        // Transfer the bound target and its overlap checks together. Move construction
        // exchanges the source with an empty target, which also covers move assignment.
        void _Swap(ClusterBuildTarget& other) noexcept
        {
            std::swap(sizes, other.sizes);
            std::swap(triangles, other.triangles);
            std::swap(clusters, other.clusters);
            std::swap(outputSizes, other.outputSizes);
            std::swap(managed, other.managed);
            std::swap(storage, other.storage);
            std::swap(references, other.references);
            std::swap(capacity, other.capacity);
            std::swap(mode, other.mode);
            std::swap(operation, other.operation);
            destinations.swap(other.destinations);
        }
    };
    struct ClusterCommandState
    {
        ClusterBuildTarget target;
        VkDeviceAddress arguments = 0;
        VkDeviceAddress scratch = 0;
        VkDeviceAddress count = 0;

        void Select(const ClusterLevelAccelStructBuildLimits&, uint32_t inCapacity,
            VkClusterAccelerationStructureOpModeNV, VkClusterAccelerationStructureOpTypeNV);
        void Select(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inCapacity, VkClusterAccelerationStructureOpModeNV);
        void Select(const ClusterLevelAccelStructBatch&, VkClusterAccelerationStructureOpModeNV,
            VkClusterAccelerationStructureOpTypeNV);
        void Select(const ClusterLevelAccelStructTemplateBatch&, VkClusterAccelerationStructureOpModeNV);
        void Select(const ClusterBottomLevelAccelStructBatch&, VkClusterAccelerationStructureOpModeNV);
        auto GetSizes() const -> AccelStructSizesInfo;
        void SetReferences(VkDeviceAddress inAddress);
        void SetStorage(VkDeviceAddress inAddress);
    };
    auto CreateClusterDestination(const AccelStructSizesInfo& inSizes) -> std::unique_ptr<Buffer>;
}
