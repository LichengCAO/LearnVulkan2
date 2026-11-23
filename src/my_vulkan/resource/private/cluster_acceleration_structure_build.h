#pragma once
#include "../acceleration_structure_types.h"
#include <utility>
class Buffer;
class ClusterLevelAccelStructBuildLimits;
class ClusterBottomLevelAccelStructBuildLimits;
class ClusterAccelStructMoveLimits;

namespace VulkanDetail
{
    struct ClusterSizeQuery
    {
        static auto Get(const ClusterLevelAccelStructBuildLimits&, VkClusterAccelerationStructureOpTypeNV,
            VkClusterAccelerationStructureOpModeNV, uint32_t inCapacity) -> AccelStructMemoryRequirements;
        static auto Get(const ClusterBottomLevelAccelStructBuildLimits&, VkClusterAccelerationStructureOpModeNV,
            uint32_t inCapacity) -> AccelStructMemoryRequirements;
        static auto GetMoveScratchRequirements(const ClusterAccelStructMoveLimits&, VkClusterAccelerationStructureOpModeNV,
            uint32_t inCapacity) -> ClusterScratchRequirements;
        static auto GetTemplateIndicesSizes(const ClusterLevelAccelStructBuildLimits&, VkClusterAccelerationStructureOpModeNV,
            uint32_t inCapacity) -> VkAccelerationStructureBuildSizesInfoKHR;
        static auto GetMoveDestinationAlignment(VkClusterAccelerationStructureTypeNV inType) -> VkDeviceSize;
        static auto GetScratchAlignment() -> VkDeviceSize;
    };

    auto ClusterTaskLayout(VkClusterAccelerationStructureOpTypeNV inOperation) -> AccelStructBuildInfoLayout;

    struct ClusterBuildTarget
    {
        VkClusterAccelerationStructureTriangleClusterInputNV triangles{};
        VkClusterAccelerationStructureClustersBottomLevelInputNV clusters{};
        VkClusterAccelerationStructureMoveObjectsInputNV move{};
        VkDeviceAddress sizes = 0;
        VkDeviceAddress storage = 0;
        VkDeviceAddress references = 0;
        uint32_t capacity = 0;
        VkClusterAccelerationStructureOpModeNV mode{};
        VkClusterAccelerationStructureOpTypeNV operation{};

        ClusterBuildTarget() = default;
        ClusterBuildTarget(const ClusterBuildTarget&) = default;
        ClusterBuildTarget(ClusterBuildTarget&& other) noexcept { _Swap(other); }

        auto operator=(ClusterBuildTarget other) noexcept -> ClusterBuildTarget&
        {
            _Swap(other);
            return *this;
        }

    private:
        // Transfer the bound target together. Move construction
        // exchanges the source with an empty target, which also covers move assignment.
        void _Swap(ClusterBuildTarget& other) noexcept
        {
            std::swap(triangles, other.triangles);
            std::swap(clusters, other.clusters);
            std::swap(move, other.move);
            std::swap(sizes, other.sizes);
            std::swap(storage, other.storage);
            std::swap(references, other.references);
            std::swap(capacity, other.capacity);
            std::swap(mode, other.mode);
            std::swap(operation, other.operation);
        }
    };
    struct ClusterCommandState
    {
        ClusterBuildTarget target;
        VkDeviceAddress arguments = 0;
        VkDeviceAddress scratch = 0;
        VkDeviceAddress count = 0;

        void Select(const ClusterLevelAccelStructBuildLimits&, uint32_t inCapacity,
            VkClusterAccelerationStructureOpModeNV, VkClusterAccelerationStructureOpTypeNV,
            VkDeviceAddress inStorage, VkDeviceAddress inReferences, VkDeviceAddress inOutputSizes);
        void Select(const ClusterBottomLevelAccelStructBuildLimits&, uint32_t inCapacity, VkClusterAccelerationStructureOpModeNV,
            VkDeviceAddress inStorage, VkDeviceAddress inReferences, VkDeviceAddress inOutputSizes);
        void Select(const ClusterAccelStructMoveLimits&, uint32_t inCapacity, VkClusterAccelerationStructureOpModeNV,
            VkDeviceAddress inStorage, VkDeviceAddress inReferences, VkDeviceAddress inOutputSizes);
    };
    auto CreateClusterDestination(const AccelStructMemoryRequirements& inSizes) -> std::unique_ptr<Buffer>;
}
