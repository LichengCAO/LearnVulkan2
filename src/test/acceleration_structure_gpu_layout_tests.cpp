#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/resource/cluster_acceleration_structure.h"
#include "my_vulkan/resource/acceleration_structure_gpu_layout.h"
#include <array>
#include <cstring>

namespace
{
    template<class T>
    auto ReadWord(const T& inRecord, VkDeviceSize inOffset) -> uint32_t
    {
        CHECK_TRUE(inOffset <= sizeof(T) && sizeof(uint32_t) <= sizeof(T) - inOffset,
            "GPU layout word is outside its native record!");
        uint32_t word = 0;
        std::memcpy(&word, reinterpret_cast<const uint8_t*>(&inRecord) + inOffset, sizeof(word));
        return word;
    }

    template<class T>
    auto ReadAddress(const T& inRecord, VkDeviceSize inOffset) -> VkDeviceAddress
    {
        CHECK_TRUE(inOffset <= sizeof(T) && sizeof(VkDeviceAddress) <= sizeof(T) - inOffset,
            "GPU layout address is outside its native record!");
        VkDeviceAddress address = 0;
        std::memcpy(&address, reinterpret_cast<const uint8_t*>(&inRecord) + inOffset, sizeof(address));
        return address;
    }

    void TestDirectTaskBinaryLayout()
    {
        namespace Layout = AccelStructGpuLayout::Direct;
        constexpr auto layout = ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout();
        static_assert(layout.recordStride == 64 && layout.addressAlignment == 8);
        VkClusterAccelerationStructureBuildTriangleClusterInfoNV record{};
        record.clusterID = 0x10203040;
        record.clusterFlags = 0x89ABCDEF;
        record.triangleCount = 0x101;
        record.vertexCount = 0x155;
        record.positionTruncateBitCount = 0x12;
        record.indexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV;
        record.opacityMicromapIndexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV;
        record.baseGeometryIndexAndGeometryFlags.geometryIndex = 0x123456;
        record.baseGeometryIndexAndGeometryFlags.geometryFlags = 3;
        record.indexBufferStride = 0x1122;
        record.vertexBufferStride = 0x3344;
        record.geometryIndexAndFlagsBufferStride = 0x5566;
        record.opacityMicromapIndexBufferStride = 0x7788;
        record.indexBuffer = 0x1122334455667788ULL;
        record.vertexBuffer = 0x8877665544332211ULL;
        record.geometryIndexAndFlagsBuffer = 0x1234567812345678ULL;
        record.opacityMicromapArray = 0x2345678923456789ULL;
        record.opacityMicromapIndexBuffer = 0x3456789A3456789AULL;
        CHECK_TRUE(ReadWord(record, Layout::ClusterIdOffset) == 0x10203040 &&
            ReadWord(record, Layout::ClusterFlagsOffset) == 0x89ABCDEF &&
            ReadWord(record, Layout::CountsAndFormatsOffset) == 0x244AAB01 &&
            ReadWord(record, Layout::GeometryIndexAndFlagsOffset) == 0x60123456 &&
            ReadWord(record, Layout::IndexAndVertexStridesOffset) == 0x33441122 &&
            ReadWord(record, Layout::GeometryAndOpacityStridesOffset) == 0x77885566,
            "Public CLAS word offsets must match native bitfields and 16-bit stride pairs!");
        const auto packed = ReadWord(record, Layout::CountsAndFormatsOffset);
        CHECK_TRUE(((packed >> Layout::TriangleCountShift) & Layout::CountMask) == 0x101 &&
            ((packed >> Layout::VertexCountShift) & Layout::CountMask) == 0x155 &&
            ((packed >> Layout::PositionTruncateShift) & Layout::PositionTruncateMask) == 0x12 &&
            ((packed >> Layout::IndexFormatShift) & Layout::IndexFormatMask) == 4 &&
            ((packed >> Layout::OpacityIndexFormatShift) & Layout::IndexFormatMask) == 2 &&
            ((ReadWord(record, Layout::GeometryIndexAndFlagsOffset) >> Layout::GeometryFlagsShift) & Layout::GeometryFlagsMask) == 3,
            "Public CLAS bit masks and shifts must decode native packed fields!");
        CHECK_TRUE(ReadAddress(record, Layout::IndexBufferOffset) == 0x1122334455667788ULL &&
            ReadAddress(record, Layout::VertexBufferOffset) == 0x8877665544332211ULL &&
            ReadAddress(record, Layout::GeometryBufferOffset) == 0x1234567812345678ULL &&
            ReadAddress(record, Layout::OpacityArrayOffset) == 0x2345678923456789ULL &&
            ReadAddress(record, Layout::OpacityIndexBufferOffset) == 0x3456789A3456789AULL,
            "Public CLAS address offsets must locate each full 64-bit native address!");
    }

    void TestInstantiateAndClusterBlasBinaryLayout()
    {
        namespace Instantiate = AccelStructGpuLayout::Instantiate;
        namespace Blas = AccelStructGpuLayout::ClusterBlas;
        constexpr auto instanceLayout = ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout();
        constexpr auto blasLayout = BottomLevelAccelStructBuildInfo::GetBuildInfoLayout();
        static_assert(instanceLayout.recordStride == 32 && instanceLayout.addressAlignment == 8);
        static_assert(blasLayout.recordStride == 16 && blasLayout.addressAlignment == 8);
        VkClusterAccelerationStructureInstantiateClusterInfoNV instance{};
        instance.clusterIdOffset = 0x10203040;
        instance.geometryIndexOffset = 0xFEDCBA;
        instance.clusterTemplateAddress = 0x1122334455667788ULL;
        instance.vertexBuffer = {0x8877665544332211ULL, 0x12345678};
        CHECK_TRUE(ReadWord(instance, Instantiate::ClusterIdOffset) == 0x10203040 &&
            ReadWord(instance, Instantiate::GeometryOffsetAndReservedOffset) == 0x00FEDCBA &&
            ReadAddress(instance, Instantiate::TemplateAddressOffset) == 0x1122334455667788ULL &&
            ReadAddress(instance, Instantiate::VertexAddressOffset) == 0x8877665544332211ULL &&
            ReadAddress(instance, Instantiate::VertexStrideOffset) == 0x12345678,
            "Public instantiation offsets must include the 64-bit stride slot and zero reserved bits!");
        VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV blas{0x1234, 8, 0x1122334455667788ULL};
        CHECK_TRUE(ReadWord(blas, Blas::ReferenceCountOffset) == 0x1234 &&
            ReadWord(blas, Blas::ReferenceStrideOffset) == 8 &&
            ReadAddress(blas, Blas::ReferencesOffset) == 0x1122334455667788ULL,
            "Public Cluster BLAS offsets must match native reference count, byte stride and address!");
    }

    void TestTlasInstanceBinaryLayout()
    {
        namespace Layout = AccelStructGpuLayout::Tlas;
        constexpr auto layout = TopLevelAccelStruct::GetBuildInfoLayout();
        static_assert(layout.recordStride == 64 && layout.addressAlignment == 16);
        VkAccelerationStructureInstanceKHR record{};
        constexpr std::array<float, 12> rows{2, 0, 0, 5, 0, 3, 0, 6, 0, 0, 4, 7};
        std::memcpy(record.transform.matrix, rows.data(), sizeof(rows));
        record.instanceCustomIndex = 0xABCDEF;
        record.mask = 0x12;
        record.instanceShaderBindingTableRecordOffset = 0x123456;
        record.flags = 3;
        record.accelerationStructureReference = 0x1122334455667788ULL;
        std::array<float, 12> decoded{};
        std::memcpy(decoded.data(), reinterpret_cast<const uint8_t*>(&record) + Layout::TransformOffset, sizeof(decoded));
        CHECK_TRUE(decoded == rows && Layout::TransformRowStride == 4 * sizeof(float) &&
            ReadWord(record, Layout::CustomIndexAndMaskOffset) == 0x12ABCDEF &&
            ReadWord(record, Layout::SbtOffsetAndFlagsOffset) == 0x03123456 &&
            ReadAddress(record, Layout::ReferenceOffset) == 0x1122334455667788ULL,
            "Public TLAS layout must preserve three transform rows, both 24/8-bit words and the BLAS address!");
        CHECK_TRUE((ReadWord(record, Layout::CustomIndexAndMaskOffset) & Layout::IndexMask) == 0xABCDEF &&
            (ReadWord(record, Layout::CustomIndexAndMaskOffset) >> Layout::MaskShift) == 0x12 &&
            (ReadWord(record, Layout::SbtOffsetAndFlagsOffset) >> Layout::FlagsShift) == 3,
            "Public TLAS bit conventions must decode the native custom index, mask and flags!");
    }
}

void RunAccelerationStructureGpuLayoutTests()
{
    TestDirectTaskBinaryLayout();
    TestInstantiateAndClusterBlasBinaryLayout();
    TestTlasInstanceBinaryLayout();
}
