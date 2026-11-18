#include "clas_buffer_test_fixture.h"
#include "my_vulkan/device.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

struct ClusterAccelerationStructureTestProbe
{
    static auto ExchangeEnabled(bool inEnabled) -> bool
    {
        return std::exchange(MyDevice::GetInstance().m_clusterAccelerationStructureEnabled, inEnabled);
    }
};

namespace
{
    struct DeviceCaps
    {
        VkDeviceSize maxBufferSize = 8192;
        VkDeviceSize maxAllocationSize = 8192;
        uint32_t maxTriangles = 16;
        uint32_t maxVertices = 32;
        bool supportsVertexFormat = true;
    };
    DeviceCaps caps;
    VkClusterAccelerationStructureTriangleClusterInputNV lastTriangleInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV lastClustersInput{};
    uint32_t queriedOutputCount = 0;
    uint32_t sizeQueries = 0;
    VkDeviceSize scratchSize = 512;

    void VKAPI_PTR Properties(VkPhysicalDevice, VkPhysicalDeviceProperties2* inOut)
    {
        for (auto* property = static_cast<VkBaseOutStructure*>(inOut->pNext); property; property = property->pNext)
        {
            switch (property->sType)
            {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV:
            {
                auto& result = *reinterpret_cast<VkPhysicalDeviceClusterAccelerationStructurePropertiesNV*>(property);
                result.maxTrianglesPerCluster = caps.maxTriangles;
                result.maxVerticesPerCluster = caps.maxVertices;
                result.clusterByteAlignment = 128;
                result.clusterScratchByteAlignment = 256;
                result.clusterBottomLevelByteAlignment = 512;
                result.maxClusterGeometryIndex = 0xFFFFFF;
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
                reinterpret_cast<VkPhysicalDeviceMaintenance3Properties*>(property)->maxMemoryAllocationSize = caps.maxAllocationSize;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES:
                reinterpret_cast<VkPhysicalDeviceMaintenance4Properties*>(property)->maxBufferSize = caps.maxBufferSize;
                break;
            default:
                CHECK_TRUE(false, "Unexpected cluster acceleration structure property query!");
            }
        }
    }

    void VKAPI_PTR Formats(VkPhysicalDevice, VkFormat format, VkFormatProperties* out)
    {
        *out = {};
        if (caps.supportsVertexFormat && format == VK_FORMAT_R32G32B32_SFLOAT)
            out->bufferFeatures = VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR;
    }

    void VKAPI_PTR Sizes(VkDevice device, const VkClusterAccelerationStructureInputInfoNV* in,
        VkAccelerationStructureBuildSizesInfoKHR* out)
    {
        ++sizeQueries;
        CHECK_TRUE(device == MyDevice::GetInstance().vkDevice &&
            in->sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV &&
            in->opMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV &&
            in->flags == 0 && in->pNext == nullptr);
        CHECK_TRUE(out->sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        queriedOutputCount = in->maxAccelerationStructureCount;
        if (in->opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV)
            lastTriangleInput = *in->opInput.pTriangleClusters;
        else
        {
            CHECK_TRUE(in->opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV);
            lastClustersInput = *in->opInput.pClustersBottomLevel;
        }
        out->accelerationStructureSize = 1024;
        out->buildScratchSize = scratchSize;
    }

    struct Hooks final
    {
        decltype(vkGetPhysicalDeviceProperties2) oldProperties = std::exchange(vkGetPhysicalDeviceProperties2, Properties);
        decltype(vkGetPhysicalDeviceFormatProperties) oldFormats = std::exchange(vkGetPhysicalDeviceFormatProperties, Formats);
        decltype(vkGetClusterAccelerationStructureBuildSizesNV) oldSizes =
            std::exchange(vkGetClusterAccelerationStructureBuildSizesNV, Sizes);
        bool oldEnabled = ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);

        Hooks()
        {
            caps = {};
            lastTriangleInput = {};
            lastClustersInput = {};
            queriedOutputCount = 0;
            sizeQueries = 0;
            scratchSize = 512;
        }

        ~Hooks()
        {
            vkGetPhysicalDeviceProperties2 = oldProperties;
            vkGetPhysicalDeviceFormatProperties = oldFormats;
            vkGetClusterAccelerationStructureBuildSizesNV = oldSizes;
            ClusterAccelerationStructureTestProbe::ExchangeEnabled(oldEnabled);
        }
    };

    template<class F>
    void ExpectFailure(F&& action)
    {
        bool failed = false;
        try { action(); }
        catch (const std::exception&) { failed = true; }
        CHECK_TRUE(failed, "Expected cluster acceleration structure operation to fail!");
    }

    auto MakeTriangles() -> std::array<ClusterLevelAccelStructCreateInfo, 3>
    {
        return {{
            { 7, 1, 0x4000, 0, 3, 0x5004 },
            { 8, 3, 0x6000, 12, 5, 0x7004 },
            { 9, 2, 0x8000, 24, 4, 0x9004 }
        }};
    }

    auto UploadBuffer(VkMemoryPropertyFlags inProperties) -> Buffer
    {
        Buffer buffer;
        BufferCreateInfo info;
        info.SetBufferSize(256).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).CustomizeMemoryProperty(inProperties);
        buffer.Create(info);
        return buffer;
    }

    constexpr VkMemoryPropertyFlags HostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    constexpr VkBufferUsageFlags ReferenceUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    void TestTriangleSnapshotsAndNativeSizes()
    {
        Hooks hooks;
        auto triangles = MakeTriangles();
        ClusterLevelAccelStructBatchCreateInfo info;
        info.SetClusterLevelAccelStructCreateInfos(triangles);
        triangles.fill({});
        ClusterLevelAccelStructBatch batch;
        CHECK_TRUE(batch.GetCount() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetSizesInfo().accelerationStructureSize == 0 && batch.GetScratchAlignment() == 0);
        batch.Create(info);
        CHECK_TRUE(queriedOutputCount == 3 && sizeQueries == 1 && batch.GetCount() == 3);
        CHECK_TRUE(lastTriangleInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV &&
            lastTriangleInput.vertexFormat == VK_FORMAT_R32G32B32_SFLOAT && lastTriangleInput.maxGeometryIndexValue == 0 &&
            lastTriangleInput.maxClusterUniqueGeometryCount == 1 && lastTriangleInput.minPositionTruncateBitCount == 0 &&
            lastTriangleInput.maxClusterTriangleCount == 3 && lastTriangleInput.maxClusterVertexCount == 5 &&
            lastTriangleInput.maxTotalTriangleCount == 6 && lastTriangleInput.maxTotalVertexCount == 12);
        const auto& sizes = batch.GetSizesInfo();
        CHECK_TRUE(sizes.accelerationStructureSize == 1024 && sizes.buildScratchBufferSize == 512 &&
            sizes.updateScratchBufferSize == 0 && sizes.scratchAlignment == 256 && batch.GetScratchAlignment() == 256 &&
            sizes.buildInfoBufferSize == 3 * sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV));
        const auto* reference = batch.GetReferenceBuffer();
        CHECK_TRUE(reference && reference->GetBufferInformation().size == 24 &&
            CONTAIN_BITS(reference->GetBufferInformation().usage, ReferenceUsage));
        ExpectFailure([&] { batch.Create(info); });
        CHECK_TRUE(batch.GetReferenceBuffer() == reference && sizeQueries == 1 &&
            ClasBufferTestFixture::LiveBufferCount() == 2);
    }

    void TestTriangleCreateRejectionAndRetry()
    {
        Hooks hooks;
        ClusterLevelAccelStructBatchCreateInfo info;
        ClusterLevelAccelStructBatch batch;
        ExpectFailure([&] { batch.Create(info); });
        const auto triangles = MakeTriangles();
        info.SetClusterLevelAccelStructCreateInfos(triangles);
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(false);
        ExpectFailure([&] { batch.Create(info); });
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);
        caps.supportsVertexFormat = false;
        ExpectFailure([&] { batch.Create(info); });
        caps.supportsVertexFormat = true;
        caps.maxVertices = 4;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxVertices = 32;
        info.CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR);
        ExpectFailure([&] { batch.Create(info); });
        info.CustomizeIndexType(VK_INDEX_TYPE_UINT32);
        CHECK_TRUE(sizeQueries == 0 && ClasBufferTestFixture::LiveBufferCount() == 0);

        caps.maxBufferSize = 1023;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxBufferSize = 8192;
        caps.maxAllocationSize = 1023;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxAllocationSize = 8192;
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
        ClasBufferTestFixture::FailNextReferenceBufferCreation();
        ExpectFailure([&] { batch.Create(info); });
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetCount() == 0 && batch.GetSizesInfo().buildInfoBufferSize == 0);
        scratchSize = 0;
        batch.Create(info);
        CHECK_TRUE(batch.GetCount() == 3 && batch.GetSizesInfo().buildScratchBufferSize == 0);
    }

    void TestTrianglePackingAndAtomicUpload()
    {
        Hooks hooks;
        auto triangles = MakeTriangles();
        ClusterLevelAccelStructBatchCreateInfo info;
        Buffer empty;
        ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, empty);
        info.SetClusterLevelAccelStructCreateInfos(triangles);
        Buffer upload = UploadBuffer(HostMemory);
        constexpr size_t offset = 8;
        ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload, offset);
        using Record = VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            Record task{};
            std::memcpy(&task, bytes.data() + offset + i * sizeof(task), sizeof(task));
            CHECK_TRUE(task.clusterID == triangles[i].clusterID && task.triangleCount == triangles[i].primitiveCount &&
                task.vertexCount == triangles[i].vertexCount && task.vertexBuffer == triangles[i].vertexAddress &&
                task.vertexBufferStride == triangles[i].vertexStride && task.indexBuffer == triangles[i].indexAddress &&
                task.indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV &&
                task.positionTruncateBitCount == 0 && task.clusterFlags == 0 &&
                task.baseGeometryIndexAndGeometryFlags.geometryIndex == 0 &&
                task.baseGeometryIndexAndGeometryFlags.geometryFlags == VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV);
        }
        constexpr std::array indexTypes{ VK_INDEX_TYPE_UINT8, VK_INDEX_TYPE_UINT16, VK_INDEX_TYPE_UINT32 };
        constexpr std::array nativeTypes{
            VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV,
            VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV,
            VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV
        };
        for (size_t format = 0; format < indexTypes.size(); ++format)
        {
            info.CustomizeIndexType(indexTypes[format]);
            ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload, offset);
            for (size_t i = 0; i < triangles.size(); ++i)
            {
                Record task{};
                std::memcpy(&task, bytes.data() + offset + i * sizeof(task), sizeof(task));
                CHECK_TRUE(task.indexType == nativeTypes[format],
                    "Every native CLAS task must use the batch's common index type!");
            }
        }
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        info.CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR);
        ExpectFailure([&] { ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload, offset); });
        CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved),
            "A rejected batch index type must preserve all caller bytes!");
        info.CustomizeIndexType(VK_INDEX_TYPE_UINT32);
        triangles[0].clusterID = 42;
        const auto original = triangles[2];
        auto expectBadTriangle = [&](const ClusterLevelAccelStructCreateInfo& invalid)
        {
            triangles[2] = invalid;
            info.SetClusterLevelAccelStructCreateInfos(triangles);
            ExpectFailure([&] { ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload, offset); });
            CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved),
                "A rejected late task must preserve all caller bytes!");
        };
        auto bad = original; bad.clusterID = std::numeric_limits<uint32_t>::max(); expectBadTriangle(bad);
        bad = original; bad.primitiveCount = 0; expectBadTriangle(bad);
        bad = original; bad.vertexCount = 0; expectBadTriangle(bad);
        caps.maxTriangles = 512;
        bad = original; bad.primitiveCount = 512; expectBadTriangle(bad);
        bad = original; bad.vertexAddress = 0; expectBadTriangle(bad);
        bad = original; bad.vertexAddress += 1; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 65536; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 14; expectBadTriangle(bad);
        bad = original; bad.indexAddress += 1; expectBadTriangle(bad);
        triangles[2] = original;
        info.SetClusterLevelAccelStructCreateInfos(triangles);
        ExpectFailure([&] { ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload, 255); });
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(info, deviceLocal); });
        CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved));
    }

    void TestClusterBlasRangesAndPacking()
    {
        Hooks hooks;
        ClusterLevelAccelStructBatchCreateInfo triangleInfo;
        const auto triangles = MakeTriangles();
        triangleInfo.SetClusterLevelAccelStructCreateInfos(triangles);
        ClusterLevelAccelStructBatch clusters;
        clusters.Create(triangleInfo);

        BottomLevelAccelStructBatch batch;
        BottomLevelAccelStructBatchCreateInfo info;
        CHECK_TRUE(batch.GetCount() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetSizesInfo().buildScratchBufferSize == 0 && batch.GetScratchAlignment() == 0);
        ExpectFailure([&] { batch.Create(info); });
        ClusterLevelAccelStructBatch uncreated;
        const std::array invalidRanges{
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ nullptr, 0, 1 },
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &uncreated, 0, 1 },
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &clusters, 0, 0 },
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &clusters, 2, 2 },
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &clusters, std::numeric_limits<uint32_t>::max(), 2 }
        };
        for (const auto& range : invalidRanges)
        {
            info.SetBottomLevelAccelStructCreateInfos(std::span(&range, 1));
            ExpectFailure([&] { batch.Create(info); });
        }
        CHECK_TRUE(sizeQueries == 1, "Invalid reference ranges must reject before querying BLAS sizes!");
        std::array<ClusterBasedBottomLevelAccelStructClusterCreateInfo, 2> ranges{{ { &clusters, 1, 2 }, { &clusters, 0, 1 } }};
        info.SetBottomLevelAccelStructCreateInfos(ranges);
        ranges.fill({});
        batch.Create(info);
        CHECK_TRUE(queriedOutputCount == 2 && batch.GetCount() == 2 &&
            lastClustersInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV &&
            lastClustersInput.maxTotalClusterCount == 3 && lastClustersInput.maxClusterCountPerAccelerationStructure == 2);
        using Record = VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV;
        CHECK_TRUE(batch.GetSizesInfo().buildInfoBufferSize == 2 * sizeof(Record) &&
            batch.GetReferenceBuffer()->GetBufferInformation().size == 16 && batch.GetScratchAlignment() == 256);
        Buffer upload = UploadBuffer(HostMemory);
        BottomLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<Record, 2> tasks{};
        std::memcpy(tasks.data(), bytes.data(), sizeof(tasks));
        const auto references = clusters.GetReferenceBuffer()->GetDeviceAddress();
        CHECK_TRUE(tasks[0].clusterReferences == references + 8 && tasks[0].clusterReferencesCount == 2 &&
            tasks[0].clusterReferencesStride == 8 && tasks[1].clusterReferences == references &&
            tasks[1].clusterReferencesCount == 1 && tasks[1].clusterReferencesStride == 8);
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        ranges = {{ { &clusters, 0, 1 }, { &clusters, 1, 3 } }};
        info.SetBottomLevelAccelStructCreateInfos(ranges);
        ExpectFailure([&] { BottomLevelAccelStructBatch::WriteBuildInfoBufferInline(info, upload); });
        CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved));
    }
}

void RunClasResourceTests()
{
    ClasBufferTestFixture::Run(TestTriangleSnapshotsAndNativeSizes);
    ClasBufferTestFixture::Run(TestTriangleCreateRejectionAndRetry);
    ClasBufferTestFixture::Run(TestTrianglePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestClusterBlasRangesAndPacking);
}
