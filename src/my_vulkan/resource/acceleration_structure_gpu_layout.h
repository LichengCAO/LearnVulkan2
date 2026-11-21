#pragma once
#include "common.h"
#include <cstddef>

// Layout of one GPU-readable build record. Alignment applies to its GPU address,
// including a suballocation offset; it is independent of C++ alignof and scratch alignment.
struct AccelStructBuildInfoLayout
{
    VkDeviceSize recordStride;
    VkDeviceSize addressAlignment;
};

// Byte offsets and unshifted bit masks for the native Vulkan input records.
// These constants describe serialization only; producers satisfy format, budget,
// resource-lifetime and synchronization requirements before the native build.
namespace AccelStructGpuLayout
{
    inline constexpr VkDeviceSize AddressSize = 8;
    inline constexpr VkDeviceSize CountSize = 4;

    namespace Direct
    {
        inline constexpr AccelStructBuildInfoLayout Layout{64, 8};
        inline constexpr uint32_t ClusterIdOffset = 0;
        inline constexpr uint32_t ClusterFlagsOffset = 4;
        inline constexpr uint32_t CountsAndFormatsOffset = 8;
        inline constexpr uint32_t GeometryIndexAndFlagsOffset = 12;
        inline constexpr uint32_t IndexAndVertexStridesOffset = 16;
        inline constexpr uint32_t GeometryAndOpacityStridesOffset = 20;
        inline constexpr uint32_t IndexBufferOffset = 24;
        inline constexpr uint32_t VertexBufferOffset = 32;
        inline constexpr uint32_t GeometryBufferOffset = 40;
        inline constexpr uint32_t OpacityArrayOffset = 48;
        inline constexpr uint32_t OpacityIndexBufferOffset = 56;

        inline constexpr uint32_t TriangleCountShift = 0;
        inline constexpr uint32_t VertexCountShift = 9;
        inline constexpr uint32_t CountMask = 0x1FF;
        inline constexpr uint32_t PositionTruncateShift = 18;
        inline constexpr uint32_t PositionTruncateMask = 0x3F;
        inline constexpr uint32_t IndexFormatShift = 24;
        inline constexpr uint32_t OpacityIndexFormatShift = 28;
        inline constexpr uint32_t IndexFormatMask = 0xF;
        // These are native NV format bits, not the VkIndexType enumeration values.
        inline constexpr uint32_t IndexFormat8Bit = 1;
        inline constexpr uint32_t IndexFormat16Bit = 2;
        inline constexpr uint32_t IndexFormat32Bit = 4;
        inline constexpr uint32_t GeometryIndexMask = 0xFFFFFF;
        inline constexpr uint32_t GeometryFlagsShift = 29;
        inline constexpr uint32_t GeometryFlagsMask = 0x7;
        inline constexpr uint32_t GeometryReservedMask = 0x1F000000;
        inline constexpr uint32_t FirstStrideShift = 0;
        inline constexpr uint32_t SecondStrideShift = 16;
        inline constexpr uint32_t StrideMask = 0xFFFF;
    }

    namespace Template
    {
        // Same first 64 bytes and packing as Direct, followed by one optional bounds address.
        inline constexpr AccelStructBuildInfoLayout Layout{72, 8};
        inline constexpr uint32_t InstantiationBoundingBoxLimitOffset = 64;
    }

    namespace Instantiate
    {
        inline constexpr AccelStructBuildInfoLayout Layout{32, 8};
        inline constexpr uint32_t ClusterIdOffset = 0;
        inline constexpr uint32_t GeometryOffsetAndReservedOffset = 4;
        inline constexpr uint32_t TemplateAddressOffset = 8;
        inline constexpr uint32_t VertexAddressOffset = 16;
        inline constexpr uint32_t VertexStrideOffset = 24;
        // The signed geometry offset is stored as a 24-bit two's-complement value.
        inline constexpr uint32_t GeometryOffsetMask = 0xFFFFFF;
        inline constexpr uint32_t ReservedMask = 0xFF000000;
        // The native stride slot is 64 bits; only the low 32 bits are used.
        inline constexpr uint64_t VertexStrideMask = 0xFFFFFFFFULL;
    }

    namespace ClusterBlas
    {
        inline constexpr AccelStructBuildInfoLayout Layout{16, 8};
        inline constexpr uint32_t ReferenceCountOffset = 0;
        inline constexpr uint32_t ReferenceStrideOffset = 4;
        inline constexpr uint32_t ReferencesOffset = 8;
        inline constexpr uint32_t PackedReferenceStride = 8;
    }

    namespace Partitioned
    {
        inline constexpr uint32_t MaxOperationGroupCount = 3;
        inline constexpr VkDeviceSize OperationBufferSize = 72;
        inline constexpr VkDeviceSize StorageAlignment = 256;
        namespace Operation
        {
            inline constexpr AccelStructBuildInfoLayout Layout{24, 8};
            inline constexpr uint32_t TypeOffset = 0, CountOffset = 4, AddressOffset = 8, StrideOffset = 16;
        }
        namespace WriteInstance
        {
            inline constexpr AccelStructBuildInfoLayout Layout{104, 8};
            inline constexpr uint32_t TransformOffset = 0, TransformRowStride = 16;
            inline constexpr uint32_t BoundingBoxOffset = 48, InstanceIdOffset = 72, MaskOffset = 76;
            inline constexpr uint32_t SbtOffset = 80, FlagsOffset = 84, InstanceIndexOffset = 88;
            inline constexpr uint32_t PartitionIndexOffset = 92, ReferenceOffset = 96;
        }
        namespace UpdateInstance
        {
            inline constexpr AccelStructBuildInfoLayout Layout{16, 8};
            inline constexpr uint32_t InstanceIndexOffset = 0, SbtOffset = 4, ReferenceOffset = 8;
        }
        namespace Translation
        {
            inline constexpr AccelStructBuildInfoLayout Layout{16, 4};
            inline constexpr uint32_t PartitionIndexOffset = 0, TranslationOffset = 4;
        }
    }

    namespace Tlas
    {
        // A packed VkAccelerationStructureInstanceKHR array (arrayOfPointers = VK_FALSE).
        inline constexpr AccelStructBuildInfoLayout Layout{64, 16};
        inline constexpr uint32_t TransformOffset = 0;
        inline constexpr uint32_t TransformRowCount = 3;
        inline constexpr uint32_t TransformRowStride = 16;
        inline constexpr uint32_t CustomIndexAndMaskOffset = 48;
        inline constexpr uint32_t SbtOffsetAndFlagsOffset = 52;
        inline constexpr uint32_t ReferenceOffset = 56;
        inline constexpr uint32_t IndexMask = 0xFFFFFF;
        inline constexpr uint32_t MaskShift = 24;
        inline constexpr uint32_t MaskMask = 0xFF;
        inline constexpr uint32_t FlagsShift = 24;
        inline constexpr uint32_t FlagsMask = 0xFF;
    }

    static_assert(sizeof(VkDeviceAddress) == AddressSize && sizeof(uint32_t) == CountSize);
    static_assert(sizeof(VkBuildPartitionedAccelerationStructureIndirectCommandNV) == Partitioned::Operation::Layout.recordStride &&
        offsetof(VkBuildPartitionedAccelerationStructureIndirectCommandNV, opType) == Partitioned::Operation::TypeOffset &&
        offsetof(VkBuildPartitionedAccelerationStructureIndirectCommandNV, argCount) == Partitioned::Operation::CountOffset &&
        offsetof(VkBuildPartitionedAccelerationStructureIndirectCommandNV, argData) == Partitioned::Operation::AddressOffset &&
        offsetof(VkStridedDeviceAddressNV, strideInBytes) + Partitioned::Operation::AddressOffset == Partitioned::Operation::StrideOffset);
    static_assert(sizeof(VkPartitionedAccelerationStructureWriteInstanceDataNV) == Partitioned::WriteInstance::Layout.recordStride &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, transform) == Partitioned::WriteInstance::TransformOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, explicitAABB) == Partitioned::WriteInstance::BoundingBoxOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, instanceID) == Partitioned::WriteInstance::InstanceIdOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, instanceMask) == Partitioned::WriteInstance::MaskOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, instanceContributionToHitGroupIndex) == Partitioned::WriteInstance::SbtOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, instanceFlags) == Partitioned::WriteInstance::FlagsOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, instanceIndex) == Partitioned::WriteInstance::InstanceIndexOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, partitionIndex) == Partitioned::WriteInstance::PartitionIndexOffset &&
        offsetof(VkPartitionedAccelerationStructureWriteInstanceDataNV, accelerationStructure) == Partitioned::WriteInstance::ReferenceOffset);
    static_assert(sizeof(VkPartitionedAccelerationStructureUpdateInstanceDataNV) == Partitioned::UpdateInstance::Layout.recordStride &&
        offsetof(VkPartitionedAccelerationStructureUpdateInstanceDataNV, instanceIndex) == Partitioned::UpdateInstance::InstanceIndexOffset &&
        offsetof(VkPartitionedAccelerationStructureUpdateInstanceDataNV, instanceContributionToHitGroupIndex) == Partitioned::UpdateInstance::SbtOffset &&
        offsetof(VkPartitionedAccelerationStructureUpdateInstanceDataNV, accelerationStructure) == Partitioned::UpdateInstance::ReferenceOffset);
    static_assert(sizeof(VkPartitionedAccelerationStructureWritePartitionTranslationDataNV) == Partitioned::Translation::Layout.recordStride &&
        offsetof(VkPartitionedAccelerationStructureWritePartitionTranslationDataNV, partitionIndex) == Partitioned::Translation::PartitionIndexOffset &&
        offsetof(VkPartitionedAccelerationStructureWritePartitionTranslationDataNV, partitionTranslation) == Partitioned::Translation::TranslationOffset);
    static_assert(sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV) == Direct::Layout.recordStride);
    static_assert(offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, clusterID) == Direct::ClusterIdOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, clusterFlags) == Direct::ClusterFlagsOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, baseGeometryIndexAndGeometryFlags) == Direct::GeometryIndexAndFlagsOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, indexBufferStride) == Direct::IndexAndVertexStridesOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, vertexBufferStride) == Direct::IndexAndVertexStridesOffset + 2 &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, geometryIndexAndFlagsBufferStride) == Direct::GeometryAndOpacityStridesOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, opacityMicromapIndexBufferStride) == Direct::GeometryAndOpacityStridesOffset + 2);
    static_assert(offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, indexBuffer) == Direct::IndexBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, vertexBuffer) == Direct::VertexBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, geometryIndexAndFlagsBuffer) == Direct::GeometryBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, opacityMicromapArray) == Direct::OpacityArrayOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV, opacityMicromapIndexBuffer) == Direct::OpacityIndexBufferOffset);
    static_assert(Direct::IndexFormat8Bit == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV &&
        Direct::IndexFormat16Bit == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV &&
        Direct::IndexFormat32Bit == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV);

    static_assert(sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV) == Template::Layout.recordStride);
    static_assert(offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, clusterID) == Direct::ClusterIdOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, clusterFlags) == Direct::ClusterFlagsOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, baseGeometryIndexAndGeometryFlags) == Direct::GeometryIndexAndFlagsOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, indexBufferStride) == Direct::IndexAndVertexStridesOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, vertexBufferStride) == Direct::IndexAndVertexStridesOffset + 2 &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, geometryIndexAndFlagsBufferStride) == Direct::GeometryAndOpacityStridesOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, opacityMicromapIndexBufferStride) == Direct::GeometryAndOpacityStridesOffset + 2 &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, indexBuffer) == Direct::IndexBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, vertexBuffer) == Direct::VertexBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, geometryIndexAndFlagsBuffer) == Direct::GeometryBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, opacityMicromapArray) == Direct::OpacityArrayOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, opacityMicromapIndexBuffer) == Direct::OpacityIndexBufferOffset &&
        offsetof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV, instantiationBoundingBoxLimit) == Template::InstantiationBoundingBoxLimitOffset);

    static_assert(sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV) == Instantiate::Layout.recordStride);
    static_assert(offsetof(VkClusterAccelerationStructureInstantiateClusterInfoNV, clusterIdOffset) == Instantiate::ClusterIdOffset &&
        offsetof(VkClusterAccelerationStructureInstantiateClusterInfoNV, clusterTemplateAddress) == Instantiate::TemplateAddressOffset &&
        offsetof(VkClusterAccelerationStructureInstantiateClusterInfoNV, vertexBuffer) +
            offsetof(VkStridedDeviceAddressNV, startAddress) == Instantiate::VertexAddressOffset &&
        offsetof(VkClusterAccelerationStructureInstantiateClusterInfoNV, vertexBuffer) +
            offsetof(VkStridedDeviceAddressNV, strideInBytes) == Instantiate::VertexStrideOffset);

    static_assert(sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV) == ClusterBlas::Layout.recordStride);
    static_assert(offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferencesCount) == ClusterBlas::ReferenceCountOffset &&
        offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferencesStride) == ClusterBlas::ReferenceStrideOffset &&
        offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferences) == ClusterBlas::ReferencesOffset &&
        ClusterBlas::PackedReferenceStride == AddressSize);

    static_assert(sizeof(VkAccelerationStructureInstanceKHR) == Tlas::Layout.recordStride);
    static_assert(offsetof(VkAccelerationStructureInstanceKHR, transform) == Tlas::TransformOffset &&
        sizeof(VkTransformMatrixKHR) == Tlas::TransformRowCount * Tlas::TransformRowStride &&
        offsetof(VkAccelerationStructureInstanceKHR, accelerationStructureReference) == Tlas::ReferenceOffset);
}
