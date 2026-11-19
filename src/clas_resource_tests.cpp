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
    VkClusterAccelerationStructureOpTypeNV queriedOp{};
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
                result.clusterTemplateByteAlignment = 32;
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
        if (caps.supportsVertexFormat &&
            (format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R16G16B16A16_SFLOAT))
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
        queriedOp = in->opType;
        if (in->opType != VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV)
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

    auto MakeTriangles() -> std::array<ClusterLevelAccelStructDirectBuildInfo, 3>
    {
        return {{
            { 7, 1, 0x4000, 0, 3, 0x5004 },
            { 8, 3, 0x6000, 12, 5, 0x7004 },
            { 9, 2, 0x8000, 24, 4, 0x9004 }
        }};
    }

    auto MakeTriangleCapacity() -> ClusterLevelAccelStructBatchCreateInfo
    {
        ClusterLevelAccelStructBatchCreateInfo info;
        info.SetMaxClusterCount(3);
        info.SetMaxClusterTriangleCount(3);
        info.SetMaxClusterVertexCount(5);
        info.SetMaxTotalTriangleCount(6);
        info.SetMaxTotalVertexCount(12);
        return info;
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
        auto info = MakeTriangleCapacity();
        ClusterLevelAccelStructBatch batch;
        CHECK_TRUE(batch.GetCapacity() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetSizesInfo().accelerationStructureSize == 0 && batch.GetScratchAlignment() == 0);
        batch.Create(info);
        CHECK_TRUE(queriedOutputCount == 3 && sizeQueries == 1 && batch.GetCapacity() == 3);
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
            CONTAIN_BITS(reference->GetBufferInformation().usage, ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
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
        using Setter = void (ClusterLevelAccelStructBatchCreateInfo::*)(uint32_t);
        constexpr std::array<Setter, 5> required{
            &ClusterLevelAccelStructBatchCreateInfo::SetMaxClusterCount,
            &ClusterLevelAccelStructBatchCreateInfo::SetMaxClusterTriangleCount,
            &ClusterLevelAccelStructBatchCreateInfo::SetMaxClusterVertexCount,
            &ClusterLevelAccelStructBatchCreateInfo::SetMaxTotalTriangleCount,
            &ClusterLevelAccelStructBatchCreateInfo::SetMaxTotalVertexCount,
        };
        for (auto set : required)
        {
            auto missing = MakeTriangleCapacity();
            (missing.*set)(0);
            ExpectFailure([&] { batch.Create(missing); });
        }
        info = MakeTriangleCapacity();
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(false);
        ExpectFailure([&] { batch.Create(info); });
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);
        caps.supportsVertexFormat = false;
        ExpectFailure([&] { batch.Create(info); });
        caps.supportsVertexFormat = true;
        caps.maxTriangles = 2;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxTriangles = 512;
        info.SetMaxClusterTriangleCount(512);
        ExpectFailure([&] { batch.Create(info); });
        info.SetMaxClusterTriangleCount(3);
        caps.maxTriangles = 16;
        caps.maxVertices = 4;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxVertices = 512;
        info.SetMaxClusterVertexCount(512);
        ExpectFailure([&] { batch.Create(info); });
        info.SetMaxClusterVertexCount(5);
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
        auto oversized = info;
        oversized.SetMaxClusterCount(std::numeric_limits<uint32_t>::max());
        ExpectFailure([&] { batch.Create(oversized); });
        scratchSize = std::numeric_limits<VkDeviceSize>::max();
        ExpectFailure([&] { batch.Create(info); });
        scratchSize = 512;
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
        ClasBufferTestFixture::FailNextReferenceBufferCreation();
        ExpectFailure([&] { batch.Create(info); });
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetCapacity() == 0 && batch.GetSizesInfo().buildInfoBufferSize == 0);
        scratchSize = 0;
        batch.Create(info);
        CHECK_TRUE(batch.GetCapacity() == 3 && batch.GetSizesInfo().buildScratchBufferSize == 0);
    }

    void TestInstantiateUsesWholeCapacity()
    {
        Hooks hooks;
        auto info = MakeTriangleCapacity();
        info.CustomizeTemplateUsage(true);
        ClusterLevelAccelStructBatch batch;
        batch.Create(info);
        CHECK_TRUE(queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV &&
            queriedOutputCount == 3 && lastTriangleInput.maxTotalTriangleCount == 6 &&
            batch.GetSizesInfo().buildInfoBufferSize == 3 * sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV),
            "Template usage must size a full-capacity native instantiation.");
        CHECK_TRUE(batch.GetVertexFormat() == VK_FORMAT_R32G32B32_SFLOAT && batch.GetIndexType() == VK_INDEX_TYPE_UINT32 &&
            batch.GetBuildLimits().pNext == nullptr && batch.GetBuildLimits().maxTotalVertexCount == 12);
        auto upload = UploadBuffer(HostMemory);
        ClusterLevelAccelStructInstantiateBuildInfo instance;
        instance.SetTemplateReference(0x4000);
        instance.SetVertexBuffer(0x8000);
        instance.CustomizeClusterID(41);
        std::array instances{ instance, instance, instance };
        instances[1].CustomizeVertexStride(16);
        instances[2].CustomizeVertexStride(32);
        ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, batch, upload, 8);
        VkClusterAccelerationStructureInstantiateClusterInfoNV record{};
        auto bytes = ClasBufferTestFixture::Bytes(upload);
        constexpr std::array expectedStrides{ 12u, 16u, 32u };
        for (size_t i = 0; i < instances.size(); ++i)
        {
            std::memcpy(&record, bytes.data() + 8 + i * sizeof(record), sizeof(record));
            CHECK_TRUE(record.clusterIdOffset == 41 && record.clusterTemplateAddress == 0x4000 &&
                record.vertexBuffer.startAddress == 0x8000 && record.vertexBuffer.strideInBytes == expectedStrides[i] &&
                record.geometryIndexOffset == 0 && record.reserved == 0);
        }
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        auto bad = instances;
        bad.back().CustomizeVertexStride(14);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, batch, upload); });
        bad.back().CustomizeVertexStride(VkDeviceSize{1} << 32);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, batch, upload); });
        bad = instances;
        bad.back().CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, batch, upload); });
        bad = instances;
        bad.back().SetTemplateReference(0);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, batch, upload); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(MakeTriangles(), batch, upload); });
        ClusterLevelAccelStructBatch direct;
        direct.Create(MakeTriangleCapacity());
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, direct, upload); });
        CHECK_TRUE(std::ranges::equal(bytes, saved));
    }

    auto VKAPI_PTR TemplateBufferAddress(VkDevice, const VkBufferDeviceAddressInfo* info) -> VkDeviceAddress
    {
        return reinterpret_cast<uintptr_t>(info->buffer) << 16;
    }

    bool validTemplateOutput = false;
    VkQueueFlags templateQueueFlags = VK_QUEUE_COMPUTE_BIT;
    void VKAPI_PTR TemplateQueueProperties(VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* families)
    {
        *count = 8;
        if (families) families[7].queueFlags = templateQueueFlags;
    }

    void VKAPI_PTR CaptureTemplates(VkCommandBuffer, const VkClusterAccelerationStructureCommandsInfoNV* build)
    {
        using Task = VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV;
        CHECK_TRUE(build->input.maxAccelerationStructureCount == 2 && build->input.flags == 0 &&
            build->input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV &&
            build->input.opMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV &&
            build->srcInfosArray.stride == sizeof(Task) && build->srcInfosArray.size == 2 * sizeof(Task));
        const auto* limits = build->input.opInput.pTriangleClusters;
        CHECK_TRUE(limits->maxClusterTriangleCount == 2 && limits->maxTotalTriangleCount == 3 &&
            limits->maxClusterVertexCount == 4 && limits->maxTotalVertexCount == 7 && limits->maxClusterUniqueGeometryCount == 1);
        const auto bytes = ClasBufferTestFixture::Bytes(reinterpret_cast<VkBuffer>(build->srcInfosArray.deviceAddress >> 16));
        std::array<Task, 2> tasks;
        std::memcpy(tasks.data(), bytes.data(), sizeof(tasks));
        CHECK_TRUE(tasks[0].indexBuffer == 0x1000 && tasks[1].indexBuffer == 0x2000 &&
            tasks[0].indexBufferStride == 0 && tasks[1].indexBufferStride == 6 &&
            tasks[0].triangleCount == 1 && tasks[1].triangleCount == 2 && tasks[0].vertexCount == 3 && tasks[1].vertexCount == 4);
        for (const auto& task : tasks)
            CHECK_TRUE(task.clusterID == 0 && task.clusterFlags == 0 && task.vertexBuffer == 0 && task.vertexBufferStride == 0 &&
                task.opacityMicromapArray == 0 && task.opacityMicromapIndexBuffer == 0 && task.instantiationBoundingBoxLimit == 0 &&
                task.opacityMicromapIndexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV &&
                task.indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV &&
                task.baseGeometryIndexAndGeometryFlags.geometryIndex == 0 && task.positionTruncateBitCount == 0 &&
                task.baseGeometryIndexAndGeometryFlags.geometryFlags == VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV);
        const std::array addresses{ build->dstImplicitData, validTemplateOutput ? build->dstImplicitData + 32 : 0 };
        auto dst = ClasBufferTestFixture::Bytes(reinterpret_cast<VkBuffer>(build->dstAddressesArray.deviceAddress >> 16));
        std::memcpy(dst.data(), addresses.data(), sizeof(addresses));
    }

    template<class Fn> struct TemplateHook
    {
        Fn& target;
        Fn saved;
        TemplateHook(Fn& fn, Fn replacement) : target(fn), saved(std::exchange(fn, replacement)) {}
        ~TemplateHook() { target = saved; }
    };
    void VKAPI_PTR TemplateBarrier(VkCommandBuffer, const VkDependencyInfo*) {}

    void TestTemplatePackingAndRollback()
    {
        Hooks hooks;
        const TemplateHook address(vkGetBufferDeviceAddress, &TemplateBufferAddress);
        const TemplateHook queue(vkGetPhysicalDeviceQueueFamilyProperties, &TemplateQueueProperties);
        const TemplateHook build(vkCmdBuildClusterAccelerationStructureIndirectNV, &CaptureTemplates);
        const TemplateHook barrier(vkCmdPipelineBarrier2, &TemplateBarrier);
        auto& device = MyDevice::GetInstance();
        ClusterLevelAccelStructTemplateBatch batch;
        std::array<ClusterLevelAccelStructTemplateInfo, 2> infos;
        for (auto& info : infos) info.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
        infos[0].SetIndexBuffer(0x1000, 3);
        infos[0].SetVertexCount(3);
        infos[1].SetIndexBuffer(0x2000, 6);
        infos[1].SetVertexCount(4);
        infos[1].CustomizeIndexStride(6);
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait({}, batch); });
        auto invalid = infos;
        invalid[1].CustomizeIndexType(VK_INDEX_TYPE_UINT8);
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait(invalid, batch); });
        invalid = infos;
        invalid[1].CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait(invalid, batch); });
        templateQueueFlags = VK_QUEUE_TRANSFER_BIT;
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait(infos, batch); });
        templateQueueFlags = VK_QUEUE_COMPUTE_BIT;
        CHECK_TRUE(sizeQueries == 0);
        validTemplateOutput = false;
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait(infos, batch); });
        CHECK_TRUE(batch.GetCount() == 0 && !batch.GetReferenceBuffer() && ClasBufferTestFixture::LiveBufferCount() == 0);
        validTemplateOutput = true;
        device.CreateClusterLevelAccelStructTemplatesAndWait(infos, batch);
        CHECK_TRUE(batch.GetCount() == 2 && batch.GetReferenceBuffer() && batch.GetReference(1) == batch.GetReference(0) + 32 &&
            batch.GetVertexCount(1) == 4 && batch.GetTriangleCount(0) == 1 && sizeQueries == 2 &&
            queriedOutputCount == 2 && lastTriangleInput.maxTotalVertexCount == 7 && ClasBufferTestFixture::LiveBufferCount() == 2);
        ExpectFailure([&] { device.CreateClusterLevelAccelStructTemplatesAndWait(infos, batch); });
        ExpectFailure([&] { (void)batch.GetReference(2); });
        CHECK_TRUE(sizeQueries == 2 && batch.GetReference(1) == batch.GetReference(0) + 32);
    }

    void TestTrianglePackingAndAtomicUpload()
    {
        Hooks hooks;
        auto triangles = MakeTriangles();
        auto info = MakeTriangleCapacity();
        ClusterLevelAccelStructBatch batch;
        Buffer empty;
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline({}, batch, empty); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, empty); });
        batch.Create(info);
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline({}, batch, empty);
        info.CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR);
        info.CustomizeVertexFormat(VK_FORMAT_UNDEFINED);
        info.SetMaxClusterCount(0);
        info.SetMaxClusterTriangleCount(0);
        info.SetMaxClusterVertexCount(0);
        info.SetMaxTotalTriangleCount(0);
        info.SetMaxTotalVertexCount(0);
        caps.supportsVertexFormat = false;
        caps.maxTriangles = 0;
        Buffer upload = UploadBuffer(HostMemory);
        constexpr size_t offset = 8;
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, upload, offset);
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
        caps.supportsVertexFormat = true;
        caps.maxTriangles = 16;
        info = MakeTriangleCapacity();
        info.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        ClusterLevelAccelStructDirectBuildInfo defaults;
        defaults.SetVertexBuffer(0x4002, 3);
        defaults.SetIndexBuffer(0x5004, 3);
        CHECK_TRUE(defaults.clusterID == 0 && defaults.vertexStride == 0 && defaults.primitiveCount == 1);
        ExpectFailure([&] { defaults.SetIndexBuffer(0xFFFF, 4); });
        CHECK_TRUE(defaults.indexAddress == 0x5004 && defaults.primitiveCount == 1);
        auto customized = defaults;
        customized.CustomizeVertexStride(12);
        customized.CustomizeClusterID(11);
        CHECK_TRUE(customized.vertexStride == 12 && customized.clusterID == 11);
        for (size_t format = 0; format < indexTypes.size(); ++format)
        {
            info.CustomizeIndexType(indexTypes[format]);
            ClusterLevelAccelStructBatch other;
            other.Create(info);
            CHECK_TRUE(lastTriangleInput.vertexFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
            defaults.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
            defaults.CustomizeIndexType(indexTypes[format]);
            defaults.CustomizeIndexStride(8);
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(std::span(&defaults, 1), other, upload, offset);
            Record task{};
            std::memcpy(&task, bytes.data() + offset, sizeof(task));
            CHECK_TRUE(task.indexType == nativeTypes[format] && task.clusterID == 0 && task.vertexBufferStride == 0 &&
                task.indexBufferStride == 8 && task.opacityMicromapIndexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV,
                "Packed native CLAS task must use the batch's common index type and value defaults!");
        }
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        triangles[0].clusterID = 42;
        const auto original = triangles[2];
        auto expectBadTriangle = [&](const ClusterLevelAccelStructDirectBuildInfo& invalid)
        {
            triangles[2] = invalid;
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, upload, offset); });
            CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved),
                "A rejected late task must preserve all caller bytes!");
        };
        auto bad = original; bad.clusterID = std::numeric_limits<uint32_t>::max(); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexType(VK_INDEX_TYPE_UINT16); expectBadTriangle(bad);
        bad = original; bad.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexStride(65536); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexStride(5); expectBadTriangle(bad);
        bad = original; bad.indexAddress = std::numeric_limits<VkDeviceAddress>::max() - 31;
        bad.CustomizeIndexStride(8); expectBadTriangle(bad);
        bad = original; bad.primitiveCount = 0; expectBadTriangle(bad);
        bad = original; bad.vertexCount = 0; expectBadTriangle(bad);
        bad = original; bad.primitiveCount = 4; expectBadTriangle(bad);
        bad = original; bad.vertexCount = 6; expectBadTriangle(bad);
        bad = original; bad.vertexAddress = 0; expectBadTriangle(bad);
        bad = original; bad.vertexAddress += 1; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 65536; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 14; expectBadTriangle(bad);
        bad = original; bad.indexAddress += 1; expectBadTriangle(bad);
        bad = original; bad.vertexAddress = std::numeric_limits<VkDeviceAddress>::max() - 3; expectBadTriangle(bad);
        bad = original; bad.indexAddress = std::numeric_limits<VkDeviceAddress>::max() - 3; expectBadTriangle(bad);
        triangles[2] = original;
        const std::array tooMany{ triangles[0], triangles[0], triangles[0], triangles[0] };
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tooMany, batch, upload); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, upload, 255); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, upload, std::numeric_limits<VkDeviceSize>::max()); });
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline({}, batch, upload);
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, batch, deviceLocal); });
        CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved));
    }

    void TestIndependentTriangleAndVertexBudgets()
    {
        Hooks hooks;
        caps.maxTriangles = 100;
        caps.maxVertices = 120;
        ClusterLevelAccelStructBatchCreateInfo info;
        info.SetMaxClusterCount(2);
        info.SetMaxClusterTriangleCount(100);
        info.SetMaxClusterVertexCount(120);
        info.SetMaxTotalTriangleCount(160);
        info.SetMaxTotalVertexCount(170);
        ClusterLevelAccelStructBatch batch;
        batch.Create(info);
        CHECK_TRUE(queriedOutputCount == 2 && lastTriangleInput.maxTotalTriangleCount == 160 &&
            lastTriangleInput.maxTotalVertexCount == 170);
        info.SetMaxTotalTriangleCount(200);
        info.SetMaxTotalVertexCount(240);

        Buffer upload = UploadBuffer(HostMemory);
        std::array<ClusterLevelAccelStructDirectBuildInfo, 2> tasks{{
            { 0, 80, 0x4000, 0, 85, 0x5000 },
            { 1, 80, 0x6000, 0, 85, 0x7000 }
        }};
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        tasks[0].primitiveCount = tasks[1].primitiveCount = 100;
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload); });
        tasks[0].primitiveCount = tasks[1].primitiveCount = 80;
        tasks[1].vertexCount = 86;
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload); });
        tasks[0].primitiveCount = 59;
        tasks[1].primitiveCount = 101;
        tasks[1].vertexCount = 85;
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload); });
        tasks[0].vertexCount = 49;
        tasks[1].vertexCount = 121;
        tasks[0].primitiveCount = tasks[1].primitiveCount = 80;
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload); });
        CHECK_TRUE(std::ranges::equal(bytes, saved), "A rejected task must preserve caller bytes!");
    }

    void TestUniqueVertexCountBounds()
    {
        Hooks hooks;
        caps.maxTriangles = 100;
        caps.maxVertices = 300;
        ClusterLevelAccelStructBatchCreateInfo info;
        info.SetMaxClusterCount(2);
        info.SetMaxClusterTriangleCount(100);
        info.SetMaxClusterVertexCount(300);
        info.SetMaxTotalTriangleCount(110);
        info.SetMaxTotalVertexCount(310);
        info.CustomizeIndexType(VK_INDEX_TYPE_UINT8);
        ClusterLevelAccelStructBatch batch;
        batch.Create(info);

        using Record = VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
        Buffer upload = UploadBuffer(HostMemory);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        struct VertexCountCase { uint32_t triangles; uint32_t maxUniqueVertices; };
        constexpr std::array cases{ VertexCountCase{ 1, 3 }, VertexCountCase{ 100, 256 } };
        for (const auto& [triangles, maxUniqueVertices] : cases)
        {
            std::array<ClusterLevelAccelStructDirectBuildInfo, 2> tasks{{
                { 7, 1, 0x4000, 0, 3, 0x6000 },
                { 8, triangles, 0x8000, 0, maxUniqueVertices, 0xa000 }
            }};
            for (auto& task : tasks) task.CustomizeIndexType(VK_INDEX_TYPE_UINT8);
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload);
            Record record{};
            std::memcpy(&record, bytes.data() + sizeof(record), sizeof(record));
            CHECK_TRUE(record.vertexCount == maxUniqueVertices, "The unique vertex count bound must be inclusive!");
            const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

            tasks[0].clusterID = 9;
            ++tasks[1].vertexCount;
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, batch, upload); });
            CHECK_TRUE(std::ranges::equal(bytes, saved), "A late impossible unique vertex count must preserve caller bytes!");
        }
    }

    auto MakeClusterBlasCapacity() -> BottomLevelAccelStructBatchCreateInfo
    {
        BottomLevelAccelStructBatchCreateInfo info;
        info.SetMaxAccelStructCount(2);
        info.SetMaxClusterCountPerAccelStruct(2);
        info.SetMaxTotalClusterCount(3);
        return info;
    }

    void TestClusterBlasCreationUsesExplicitCapacity()
    {
        Hooks hooks;
        BottomLevelAccelStructBatch batch;
        auto info = MakeClusterBlasCapacity();
        CHECK_TRUE(batch.GetCapacity() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetSizesInfo().buildScratchBufferSize == 0 && batch.GetScratchAlignment() == 0);
        batch.Create(info);
        CHECK_TRUE(queriedOutputCount == 2 && batch.GetCapacity() == 2 &&
            lastClustersInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV &&
            lastClustersInput.maxTotalClusterCount == 3 && lastClustersInput.maxClusterCountPerAccelerationStructure == 2);
        using Record = VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV;
        const auto* reference = batch.GetReferenceBuffer();
        CHECK_TRUE(batch.GetSizesInfo().buildInfoBufferSize == 2 * sizeof(Record) &&
            reference && reference->GetBufferInformation().size == 16 && batch.GetScratchAlignment() == 256 &&
            CONTAIN_BITS(reference->GetBufferInformation().usage, ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        info.SetMaxAccelStructCount(1);
        info.SetMaxClusterCountPerAccelStruct(1);
        info.SetMaxTotalClusterCount(1);
        ExpectFailure([&] { batch.Create(info); });
        CHECK_TRUE(batch.GetCapacity() == 2 && batch.GetReferenceBuffer() == reference && sizeQueries == 1 &&
            ClasBufferTestFixture::LiveBufferCount() == 2);
    }

    void TestClusterBlasCreateRejectionAndRetry()
    {
        Hooks hooks;
        BottomLevelAccelStructBatch batch;
        const auto info = MakeClusterBlasCapacity();
        ExpectFailure([&] { batch.Create({}); });
        constexpr std::array required{
            &BottomLevelAccelStructBatchCreateInfo::SetMaxAccelStructCount,
            &BottomLevelAccelStructBatchCreateInfo::SetMaxClusterCountPerAccelStruct,
            &BottomLevelAccelStructBatchCreateInfo::SetMaxTotalClusterCount,
        };
        for (const auto set : required)
        {
            auto missing = info;
            (missing.*set)(0);
            ExpectFailure([&] { batch.Create(missing); });
        }
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(false);
        ExpectFailure([&] { batch.Create(info); });
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);
        CHECK_TRUE(sizeQueries == 0 && ClasBufferTestFixture::LiveBufferCount() == 0);

        caps.maxBufferSize = 1023;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxBufferSize = 8192;
        caps.maxAllocationSize = 1023;
        ExpectFailure([&] { batch.Create(info); });
        caps.maxAllocationSize = 8192;
        auto oversized = info;
        oversized.SetMaxAccelStructCount(std::numeric_limits<uint32_t>::max());
        ExpectFailure([&] { batch.Create(oversized); });
        scratchSize = std::numeric_limits<VkDeviceSize>::max();
        ExpectFailure([&] { batch.Create(info); });
        scratchSize = 512;
        ClasBufferTestFixture::SetAllocationFailure(true);
        ExpectFailure([&] { batch.Create(info); });
        ClasBufferTestFixture::SetAllocationFailure(false);
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0 && batch.GetReferenceBuffer() == nullptr);
        ClasBufferTestFixture::FailNextReferenceBufferCreation();
        ExpectFailure([&] { batch.Create(info); });
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0 && batch.GetReferenceBuffer() == nullptr &&
            batch.GetCapacity() == 0 && batch.GetSizesInfo().buildInfoBufferSize == 0);
        scratchSize = 0;
        batch.Create(info);
        CHECK_TRUE(batch.GetCapacity() == 2 && batch.GetSizesInfo().buildScratchBufferSize == 0);
    }

    void TestClusterBlasRangesAndPacking()
    {
        Hooks hooks;
        ClusterLevelAccelStructBatch clusters;
        ClusterLevelAccelStructBatch otherClusters;
        clusters.Create(MakeTriangleCapacity());
        otherClusters.Create(MakeTriangleCapacity());
        std::array<BottomLevelAccelStructBuildInfo, 2> tasks{};
        tasks[0].SetClusterReferences(clusters, 1, 2);
        tasks[1].SetClusterReferences(otherClusters, 0, 1);

        ClusterLevelAccelStructBatch uncreated;
        ExpectFailure([&] { tasks[0].SetClusterReferences(uncreated, 0, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, 0, 0); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, 2, 2); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, std::numeric_limits<uint32_t>::max(), 2); });
        ClasBufferTestFixture::SetBufferAddressOverride(std::numeric_limits<VkDeviceAddress>::max() - 7);
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, 0, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, 1, 1); });
        ClasBufferTestFixture::SetBufferAddressOverride(0x100001);
        ExpectFailure([&] { tasks[0].SetClusterReferences(clusters, 0, 1); });
        ClasBufferTestFixture::SetBufferAddressOverride(0);

        BottomLevelAccelStructBatch batch;
        Buffer empty;
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline({}, empty); });
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, empty); });
        batch.Create(MakeClusterBlasCapacity());
        batch.WriteBuildInfoBufferInline({}, empty, std::numeric_limits<VkDeviceSize>::max());
        Buffer upload = UploadBuffer(HostMemory);
        constexpr VkDeviceSize offset = 8;
        batch.WriteBuildInfoBufferInline(tasks, upload, offset);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, 2> records{};
        std::memcpy(records.data(), bytes.data() + offset, sizeof(records));
        CHECK_TRUE(records[0].clusterReferences == clusters.GetReferenceBuffer()->GetDeviceAddress() + 8 &&
            records[0].clusterReferencesCount == 2 && records[0].clusterReferencesStride == 8 &&
            records[1].clusterReferences == otherClusters.GetReferenceBuffer()->GetDeviceAddress() &&
            records[1].clusterReferencesCount == 1 && records[1].clusterReferencesStride == 8,
            "Each BLAS task must pack its source range with fixed stride and preserve previously valid inputs on setter failure!");
    }

    void TestClusterBlasUploadChecksSavedBudgetsBeforeWriting()
    {
        Hooks hooks;
        ClusterLevelAccelStructBatch clusters;
        clusters.Create(MakeTriangleCapacity());
        BottomLevelAccelStructBatch batch;
        auto info = MakeClusterBlasCapacity();
        batch.Create(info);
        info.SetMaxAccelStructCount(3);
        info.SetMaxClusterCountPerAccelStruct(3);
        info.SetMaxTotalClusterCount(6);
        Buffer upload = UploadBuffer(HostMemory);
        std::array<BottomLevelAccelStructBuildInfo, 2> tasks{};
        tasks[0].SetClusterReferences(clusters, 1, 2);
        tasks[1].SetClusterReferences(clusters, 0, 1);
        batch.WriteBuildInfoBufferInline(tasks, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

        const std::array tooMany{ tasks[1], tasks[1], tasks[1] };
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tooMany, upload); });
        auto expectLateFailure = [&]
        {
            ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, upload); });
            CHECK_TRUE(std::ranges::equal(bytes, saved), "A late rejected BLAS task must preserve the entire upload buffer!");
        };
        tasks[0].SetClusterReferences(clusters, 0, 2);
        tasks[1].SetClusterReferences(clusters, 0, 2);
        expectLateFailure();
        tasks[0].SetClusterReferences(clusters, 0, 1);
        tasks[1].SetClusterReferences(clusters, 0, 3);
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(std::span(&tasks[1], 1), upload); });
        tasks[1] = {};
        expectLateFailure();

        tasks[1].SetClusterReferences(clusters, 0, 2);
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, upload, 255); });
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, upload, std::numeric_limits<VkDeviceSize>::max()); });
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, deviceLocal); });
        Buffer nonCoherent = UploadBuffer(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        ExpectFailure([&] { batch.WriteBuildInfoBufferInline(tasks, nonCoherent); });
        CHECK_TRUE(std::ranges::equal(bytes, saved));
    }
}

void RunClasResourceTests()
{
    ClasBufferTestFixture::Run([] { ClasBufferTestFixture::WithInlineExecutor(TestTemplatePackingAndRollback); });
    ClasBufferTestFixture::Run(TestInstantiateUsesWholeCapacity);
    ClasBufferTestFixture::Run(TestTriangleSnapshotsAndNativeSizes);
    ClasBufferTestFixture::Run(TestTriangleCreateRejectionAndRetry);
    ClasBufferTestFixture::Run(TestTrianglePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestIndependentTriangleAndVertexBudgets);
    ClasBufferTestFixture::Run(TestUniqueVertexCountBounds);
    ClasBufferTestFixture::Run(TestClusterBlasCreationUsesExplicitCapacity);
    ClasBufferTestFixture::Run(TestClusterBlasCreateRejectionAndRetry);
    ClasBufferTestFixture::Run(TestClusterBlasRangesAndPacking);
    ClasBufferTestFixture::Run(TestClusterBlasUploadChecksSavedBudgetsBeforeWriting);
}
