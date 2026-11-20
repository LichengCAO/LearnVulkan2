#include "clas_buffer_test_fixture.h"
#include "my_vulkan/device.h"
#include "my_vulkan/resource/cluster_acceleration_structure.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/intrusive_ptr.h"
#include "my_vulkan/command/command_buffer.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <source_location>
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
    VkClusterAccelerationStructureOpModeNV queriedMode{};
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
            in->flags == 0 && in->pNext == nullptr);
        CHECK_TRUE(out->sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
        queriedOutputCount = in->maxAccelerationStructureCount;
        queriedOp = in->opType;
        queriedMode = in->opMode;
        if (in->opType != VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV)
            lastTriangleInput = *in->opInput.pTriangleClusters;
        else
        {
            CHECK_TRUE(in->opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV);
            lastClustersInput = *in->opInput.pClustersBottomLevel;
        }
        out->accelerationStructureSize = 1024;
        if (queriedMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV)
            out->accelerationStructureSize = VkDeviceSize{256} * (queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV ?
                lastClustersInput.maxClusterCountPerAccelerationStructure : lastTriangleInput.maxClusterTriangleCount);
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
    void ExpectFailure(F&& action, std::source_location where = std::source_location::current())
    {
        bool failed = false;
        try { action(); }
        catch (const std::exception&) { failed = true; }
        CHECK_TRUE(failed, std::format("Expected cluster acceleration structure operation to fail at line {}!", where.line()));
    }

    auto MakeTriangles() -> std::array<ClusterLevelAccelStructDirectBuildInfo, 3>
    {
        return {{
            { 7, 1, 0x4000, 0, 3, 0x5004 },
            { 8, 3, 0x6000, 12, 5, 0x7004 },
            { 9, 2, 0x8000, 24, 4, 0x9004 }
        }};
    }

    auto MakeTriangleCapacity() -> ClusterLevelAccelStructImplicitBatchCreateInfo
    {
        ClusterLevelAccelStructImplicitBatchCreateInfo info;
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

    struct HostQueryDeviceCalls
    {
        static inline uint32_t allocations = 0;
        static inline uint32_t submissions = 0;
        static auto VKAPI_PTR Create(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*) -> VkResult
        {
            ++allocations;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static auto VKAPI_PTR Submit(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence) -> VkResult
        {
            ++submissions;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        decltype(vkCreateBuffer) oldCreate = std::exchange(vkCreateBuffer, Create);
        decltype(vkQueueSubmit2) oldSubmit = std::exchange(vkQueueSubmit2, Submit);
        HostQueryDeviceCalls() { allocations = submissions = 0; }
        ~HostQueryDeviceCalls() { vkCreateBuffer = oldCreate; vkQueueSubmit2 = oldSubmit; }
    };

    void TestCommandInputQueries()
    {
        Hooks hooks;
        HostQueryDeviceCalls calls;
        ClusterLevelAccelStructBuildLayout layout;
        BuildExplicitClusterAccelStructCommandInput explicitBuild;
        BuildImplicitClusterAccelStructCommandInput implicitBuild;
        ComputeClusterAccelStructSizesCommandInput compute;
        ExpectFailure([&] { compute.QueryCurrentTargetSizesInfo(); });
        layout.SetMaxTriangleCount(3, 6);
        ExpectFailure([&] { explicitBuild.SetClusterTarget(layout, 3); });
        layout.SetMaxVertexCount(5, 12);
        ExpectFailure([&] { explicitBuild.SetClusterTarget(layout, 0); });
        caps.supportsVertexFormat = false;
        ExpectFailure([&] { explicitBuild.SetClusterTarget(layout, 3); });
        caps.supportsVertexFormat = true;
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(false);
        ExpectFailure([&] { implicitBuild.SetClusterTarget(layout, 3); });
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);
        CHECK_TRUE(sizeQueries == 0);
        scratchSize = 0;
        const auto check = [&](const auto& input, auto operation, auto mode, size_t stride, bool blas = false)
        {
            const auto sizes = input.QueryCurrentTargetSizesInfo();
            const bool sizing = mode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
            const bool implicit = mode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
            CHECK_TRUE(queriedMode == mode && queriedOp == operation);
            CHECK_TRUE(sizes.accelerationStructureSize == (sizing ? 0 : implicit ? 1024 : blas ? 1280 : 768) &&
                sizes.accelerationStructureAlignment == (sizing ? 0 : blas ? 512 : 128) &&
                sizes.scratchAlignment == 256 && sizes.buildScratchBufferSize == 0 && sizes.updateScratchBufferSize == 0 &&
                sizes.buildInfoBufferSize == 3 * stride);
            CHECK_TRUE(queriedOutputCount == 3);
            if (blas) CHECK_TRUE(lastClustersInput.maxTotalClusterCount == 7);
            else CHECK_TRUE(lastTriangleInput.maxTotalTriangleCount == 6 && lastTriangleInput.maxTotalVertexCount == 12);
        };
        BottomLevelAccelStructBuildLayout blas;
        ExpectFailure([&] { compute.SetBottomLevelAccelStructTarget(blas, 3); });
        blas.SetMaxClusterCount(5, 7);
        const auto checkOperations = [&](auto& input, auto mode)
        {
            input.SetClusterTarget(layout, 3);
            check(input, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV, mode,
                sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV));
            input.SetInstantiateClusterTarget(layout, 3);
            check(input, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV, mode,
                sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV));
            input.SetBottomLevelAccelStructTarget(blas, 3);
            check(input, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV, mode,
                sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV), true);
        };
        checkOperations(explicitBuild, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV);
        checkOperations(implicitBuild, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV);
        checkOperations(compute, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV);
        compute.SetClusterTemplateTarget(layout, 3);
        check(compute, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV,
            VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV,
            sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV));
        CHECK_TRUE(sizeQueries == 10 && !calls.allocations && !calls.submissions && !ClasBufferTestFixture::LiveBufferCount());
    }

    void TestExplicitOwnership()
    {
        Hooks hooks;
        auto table = MakeIntrusive<Buffer>();
        BufferCreateInfo bufferInfo;
        bufferInfo.SetBufferSize(32).SetBufferUsage(ReferenceUsage);
        ClusterLevelAccelStructCreateInfo info;
        info.SetMaxTriangleCount(2);
        info.SetMaxVertexCount(5);
        auto first = MakeIntrusive<ClusterLevelAccelStruct>();
        ClasBufferTestFixture::SetAllocationFailure(true);
        ExpectFailure([&] { first->Create(info); });
        ClasBufferTestFixture::SetAllocationFailure(false);
        CHECK_TRUE(first->GetReference() == 0);
        table->Create(bufferInfo);
        first->Create(info);
        ExpectFailure([&] { first->Create(info); });
        info.SetMaxTriangleCount(3);
        auto second = MakeIntrusive<ClusterLevelAccelStruct>();
        second->Create(info);
        ClusterLevelAccelStructExplicitBatchCreateInfo explicitInfo;
        std::array<const ClusterLevelAccelStruct*, 2> owners{first.get(), second.get()};
        explicitInfo.SetSourceAccelStructs(owners);
        owners.fill(nullptr);
        {
            ClusterLevelAccelStructBatch batch;
            for (const auto offset : {1ull, 24ull, UINT64_MAX})
            {
                ExpectFailure([&] { explicitInfo.SetReferenceBuffer(*table, offset); batch.Create(explicitInfo); });
                CHECK_TRUE(batch.GetCapacity() == 0);
            }
            auto wrongTable = MakeIntrusive<Buffer>();
            bufferInfo.SetBufferUsage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            wrongTable->Create(bufferInfo);
            ExpectFailure([&] { explicitInfo.SetReferenceBuffer(*wrongTable, 0); batch.Create(explicitInfo); });
            wrongTable.reset();
            explicitInfo.SetReferenceBuffer(*table, 8);
            auto invalid = explicitInfo;
            for (const auto mismatch : {0, 1, 2})
            {
                auto different = info;
                if (mismatch == 0) different.CustomizeTemplateUsage(true);
                if (mismatch == 1) different.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
                if (mismatch == 2) different.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
                auto other = MakeIntrusive<ClusterLevelAccelStruct>();
                other->Create(different);
                const std::array mixed{first.get(), other.get()};
                invalid.SetSourceAccelStructs(mixed);
                ExpectFailure([&] { batch.Create(invalid); });
            }
            invalid.SetSourceAccelStructs({});
            ExpectFailure([&] { batch.Create(invalid); });
            const std::array duplicate{first.get(), first.get()};
            invalid.SetSourceAccelStructs(duplicate);
            ExpectFailure([&] { batch.Create(invalid); });
            batch.Create(explicitInfo);
            CHECK_TRUE(batch.GetCapacity() == 2 && batch.GetReferenceBuffer() == table.get() && batch.GetReferenceBufferOffset() == 8 &&
                batch.GetBuildLimits().maxClusterTriangleCount == 3 && batch.GetBuildLimits().maxTotalTriangleCount == 5 &&
                batch.GetSizesInfo().accelerationStructureSize == 768 && queriedOutputCount == 2);
            ExpectFailure([&] { batch.Create(explicitInfo); });
            first.reset();
            second.reset();
            table.reset();
            CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 3);
        }
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
    }

    void TestClusterBlasOwners()
    {
        Hooks hooks;
        BottomLevelAccelStructCreateInfo info;
        info.SetClusters(3);
        ExpectFailure([&] { info.SetClusters(0); });
        ExpectFailure([&] { info.SetClusters(UINT64_MAX); });
        auto owner = MakeIntrusive<BottomLevelAccelStruct>();
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        ExpectFailure([&] { owner->Create(info); });
        info.CustomizeBuildFlags(0);
        ClasBufferTestFixture::SetAllocationFailure(true);
        ExpectFailure([&] { owner->Create(info); });
        CHECK_TRUE(!owner->IsValid() && owner->GetDeviceAddress() == 0);
        ClasBufferTestFixture::SetAllocationFailure(false);
        owner->Create(info);
        CHECK_TRUE(owner->IsValid() && owner->GetDeviceAddress() != 0 && owner->GetVkAccelerationStructure() == VK_NULL_HANDLE &&
            queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV && lastClustersInput.maxTotalClusterCount == 3);
        ExpectFailure([&] { owner->Create(info); });
        CommandBuffer commands;
        Buffer scratch;
        ExpectFailure([&] { commands.BuildAccelerationStructureKHR(*owner, info, scratch, 0); });
        ExpectFailure([&] { commands.UpdateAccelerationStructureKHR(*owner, *owner, info, scratch, 0); });
        auto table = MakeIntrusive<Buffer>();
        BufferCreateInfo bufferInfo;
        bufferInfo.SetBufferSize(32).SetBufferUsage(ReferenceUsage);
        table->Create(bufferInfo);
        BottomLevelAccelStructExplicitBatchCreateInfo explicitInfo;
        explicitInfo.SetReferenceBuffer(*table, 16);
        const BottomLevelAccelStruct* source = owner.get();
        explicitInfo.SetSourceAccelStructs(std::span(&source, 1));
        {
            BottomLevelAccelStructBatch batch;
            ExpectFailure([&] { batch.Create(BottomLevelAccelStructExplicitBatchCreateInfo{}); });
            auto empty = MakeIntrusive<BottomLevelAccelStruct>();
            const auto* emptySource = empty.get();
            auto invalid = explicitInfo;
            invalid.SetSourceAccelStructs(std::span(&emptySource, 1));
            ExpectFailure([&] { batch.Create(invalid); });
            batch.Create(explicitInfo);
            ExpectFailure([&] { batch.Create(explicitInfo); });
            CHECK_TRUE(batch.GetReferenceBufferOffset() == 16 && batch.GetCapacity() == 1 &&
                batch.GetBuildLimits().maxTotalClusterCount == 3 && batch.GetSizesInfo().accelerationStructureAlignment == 512);
            owner.reset();
            table.reset();
            CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 2);
        }
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
    }

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
        ClusterLevelAccelStructImplicitBatchCreateInfo info;
        ClusterLevelAccelStructBatch batch;
        ExpectFailure([&] { batch.Create(info); });
        using Setter = void (ClusterLevelAccelStructImplicitBatchCreateInfo::*)(uint32_t);
        constexpr std::array<Setter, 5> required{
            &ClusterLevelAccelStructImplicitBatchCreateInfo::SetMaxClusterCount,
            &ClusterLevelAccelStructImplicitBatchCreateInfo::SetMaxClusterTriangleCount,
            &ClusterLevelAccelStructImplicitBatchCreateInfo::SetMaxClusterVertexCount,
            &ClusterLevelAccelStructImplicitBatchCreateInfo::SetMaxTotalTriangleCount,
            &ClusterLevelAccelStructImplicitBatchCreateInfo::SetMaxTotalVertexCount,
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
    }

    void TestInstantiatePackingAndAtomicUpload()
    {
        Hooks hooks;
        Buffer empty;
        ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline({}, empty, std::numeric_limits<VkDeviceSize>::max());
        auto upload = UploadBuffer(HostMemory);
        ClusterLevelAccelStructInstantiateBuildInfo instance;
        instance.SetTemplateReference(0x4000);
        instance.SetVertexBuffer(0x8000);
        instance.CustomizeClusterID(41);
        std::array instances{ instance, instance, instance };
        instances[1].CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        instances[2].CustomizeVertexStride(32);
        ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, upload, 8);
        VkClusterAccelerationStructureInstantiateClusterInfoNV record{};
        auto bytes = ClasBufferTestFixture::Bytes(upload);
        constexpr std::array expectedStrides{ 12u, 8u, 32u };
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
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad.back().CustomizeVertexStride(VkDeviceSize{1} << 32);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad = instances;
        bad.back().CustomizeVertexFormat(VK_FORMAT_UNDEFINED);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad = instances;
        bad.back().SetTemplateReference(0);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad.back().SetTemplateReference(0x4010);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad = instances;
        bad.back().SetVertexBuffer(0x8001);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad.back().SetVertexBuffer(0);
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        bad = instances;
        bad.back().CustomizeClusterID(std::numeric_limits<uint32_t>::max());
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(bad, upload); });
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, empty); });
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, upload, 255); });
        ExpectFailure([&] { ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, upload, std::numeric_limits<VkDeviceSize>::max()); });
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
        Buffer empty;
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline({}, empty, std::numeric_limits<VkDeviceSize>::max());
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, empty); });
        caps.supportsVertexFormat = false;
        caps.maxTriangles = 0;
        Buffer upload = UploadBuffer(HostMemory);
        constexpr size_t offset = 8;
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, upload, offset);
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
        ClusterLevelAccelStructDirectBuildInfo defaults;
        defaults.SetVertexBuffer(0x4002, 3);
        defaults.SetIndexBuffer(0x5004, 3);
        ExpectFailure([&] { defaults.SetIndexBuffer(0xFFFF, 4); });
        CHECK_TRUE(defaults.indexAddress == 0x5004 && defaults.primitiveCount == 1);
        for (size_t format = 0; format < indexTypes.size(); ++format)
        {
            defaults.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
            defaults.CustomizeIndexType(indexTypes[format]);
            defaults.CustomizeIndexStride(8);
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(std::span(&defaults, 1), upload, offset);
            Record task{};
            std::memcpy(&task, bytes.data() + offset, sizeof(task));
            CHECK_TRUE(task.indexType == nativeTypes[format] && task.clusterID == 0 && task.vertexBufferStride == 0 &&
                task.indexBufferStride == 8 && task.opacityMicromapIndexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV,
                "Packed native CLAS task must use its own index type and value defaults!");
        }
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        triangles[0].clusterID = 42;
        const auto original = triangles[2];
        auto expectBadTriangle = [&](const ClusterLevelAccelStructDirectBuildInfo& invalid)
        {
            triangles[2] = invalid;
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, upload, offset); });
            CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved),
                "A rejected late task must preserve all caller bytes!");
        };
        auto bad = original; bad.clusterID = std::numeric_limits<uint32_t>::max(); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR); expectBadTriangle(bad);
        bad = original; bad.CustomizeVertexFormat(VK_FORMAT_UNDEFINED); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexStride(65536); expectBadTriangle(bad);
        bad = original; bad.CustomizeIndexStride(5); expectBadTriangle(bad);
        bad = original; bad.indexAddress = std::numeric_limits<VkDeviceAddress>::max() - 31;
        bad.CustomizeIndexStride(8); expectBadTriangle(bad);
        bad = original; bad.primitiveCount = 0; expectBadTriangle(bad);
        bad = original; bad.vertexCount = 0; expectBadTriangle(bad);
        bad = original; bad.primitiveCount = 512; expectBadTriangle(bad);
        bad = original; bad.vertexCount = 512; expectBadTriangle(bad);
        bad = original; bad.vertexAddress = 0; expectBadTriangle(bad);
        bad = original; bad.vertexAddress += 1; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 65536; expectBadTriangle(bad);
        bad = original; bad.vertexStride = 14; expectBadTriangle(bad);
        bad = original; bad.indexAddress += 1; expectBadTriangle(bad);
        bad = original; bad.vertexAddress = std::numeric_limits<VkDeviceAddress>::max() - 3; expectBadTriangle(bad);
        bad = original; bad.indexAddress = std::numeric_limits<VkDeviceAddress>::max() - 3; expectBadTriangle(bad);
        triangles[2] = original;
        const std::array tooMany{ triangles[0], triangles[0], triangles[0], triangles[0], triangles[0] };
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tooMany, upload); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, upload, 255); });
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, upload, std::numeric_limits<VkDeviceSize>::max()); });
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline({}, upload);
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, deviceLocal); });
        CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved));
    }

    void TestDirectUploadUsesTaskFormatsAndCounts()
    {
        Hooks hooks;
        Buffer upload = UploadBuffer(HostMemory);
        std::array<ClusterLevelAccelStructDirectBuildInfo, 3> tasks{{
            { 0, 100, 0x4000, 0, 120, 0x5000, VK_INDEX_TYPE_UINT8 },
            { 1, 101, 0x6002, 0, 121, 0x7002, VK_INDEX_TYPE_UINT16, 0, VK_FORMAT_R16G16B16A16_SFLOAT },
            { 2, 102, 0x8000, 0, 122, 0x9004, VK_INDEX_TYPE_UINT32 }
        }};
        ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        using Record = VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
        std::array<Record, 3> records{};
        std::memcpy(records.data(), bytes.data(), sizeof(records));
        CHECK_TRUE(records[0].indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV &&
            records[1].indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV &&
            records[2].indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV &&
            records[0].triangleCount == 100 && records[1].triangleCount == 101 && records[2].triangleCount == 102 &&
            records[0].vertexCount == 120 && records[1].vertexCount == 121 && records[2].vertexCount == 122 &&
            records[1].vertexBuffer == 0x6002,
            "Direct upload must encode each task's own formats and counts without a target batch!");
    }

    void TestUniqueVertexCountBounds()
    {
        Hooks hooks;
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
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, upload);
            Record record{};
            std::memcpy(&record, bytes.data() + sizeof(record), sizeof(record));
            CHECK_TRUE(record.vertexCount == maxUniqueVertices, "The unique vertex count bound must be inclusive!");
            const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

            tasks[0].clusterID = 9;
            ++tasks[1].vertexCount;
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, upload); });
            CHECK_TRUE(std::ranges::equal(bytes, saved), "A late impossible unique vertex count must preserve caller bytes!");
        }
    }

    auto MakeClusterBlasCapacity() -> BottomLevelAccelStructImplicitBatchCreateInfo
    {
        BottomLevelAccelStructImplicitBatchCreateInfo info;
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
        ExpectFailure([&] { batch.Create(BottomLevelAccelStructImplicitBatchCreateInfo{}); });
        constexpr std::array required{
            &BottomLevelAccelStructImplicitBatchCreateInfo::SetMaxAccelStructCount,
            &BottomLevelAccelStructImplicitBatchCreateInfo::SetMaxClusterCountPerAccelStruct,
            &BottomLevelAccelStructImplicitBatchCreateInfo::SetMaxTotalClusterCount,
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

    void TestClusterBlasRawReferencesAndPacking()
    {
        Hooks hooks;
        constexpr VkDeviceAddress firstReferences = 0x100008;
        constexpr VkDeviceAddress secondReferences = 0x200000;
        std::array<BottomLevelAccelStructBuildInfo, 2> tasks{};
        tasks[0].SetClusterReferences(firstReferences, 2);
        tasks[1].SetClusterReferences(secondReferences, 1);

        ExpectFailure([&] { tasks[0].SetClusterReferences(0, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(firstReferences, 0); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(std::numeric_limits<VkDeviceAddress>::max() - 7, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(std::numeric_limits<VkDeviceAddress>::max() - 15, 2); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(0x100001, 1); });

        Buffer empty;
        BottomLevelAccelStructBuildInfo::CommitToDeviceInline({}, empty, std::numeric_limits<VkDeviceSize>::max());
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, empty); });
        Buffer upload = UploadBuffer(HostMemory);
        constexpr VkDeviceSize offset = 8;
        BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, offset);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, 2> records{};
        std::memcpy(records.data(), bytes.data() + offset, sizeof(records));
        CHECK_TRUE(records[0].clusterReferences == firstReferences &&
            records[0].clusterReferencesCount == 2 && records[0].clusterReferencesStride == 8 &&
            records[1].clusterReferences == secondReferences &&
            records[1].clusterReferencesCount == 1 && records[1].clusterReferencesStride == 8,
            "Each BLAS task must pack its borrowed address with fixed stride and preserve previously valid inputs on setter failure!");
    }

    void TestClusterBlasUploadValidatesBeforeWriting()
    {
        Hooks hooks;
        constexpr VkDeviceAddress references = 0x100000;
        Buffer upload = UploadBuffer(HostMemory);
        std::array<BottomLevelAccelStructBuildInfo, 3> tasks{};
        tasks[0].SetClusterReferences(references + sizeof(VkDeviceAddress), 2);
        tasks[1].SetClusterReferences(references, 3);
        tasks[2].SetClusterReferences(references, 4);
        BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, 3> records{};
        std::memcpy(records.data(), bytes.data(), sizeof(records));
        CHECK_TRUE(records[0].clusterReferencesCount == 2 && records[1].clusterReferencesCount == 3 &&
            records[2].clusterReferencesCount == 4,
            "Cluster BLAS upload must encode caller-supplied counts without a target batch!");
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

        tasks.back() = {};
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload); });
        CHECK_TRUE(std::ranges::equal(bytes, saved), "A late rejected BLAS task must preserve the entire upload buffer!");

        tasks.back().SetClusterReferences(references, 4);
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, 255); });
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, std::numeric_limits<VkDeviceSize>::max()); });
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, deviceLocal); });
        Buffer nonCoherent = UploadBuffer(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        ExpectFailure([&] { BottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, nonCoherent); });
        CHECK_TRUE(std::ranges::equal(bytes, saved));
    }
}

void RunClasResourceTests()
{
    ClasBufferTestFixture::Run(TestCommandInputQueries);
    ClasBufferTestFixture::Run(TestExplicitOwnership);
    ClasBufferTestFixture::Run(TestClusterBlasOwners);
    ClasBufferTestFixture::Run([] { ClasBufferTestFixture::WithInlineExecutor(TestTemplatePackingAndRollback); });
    ClasBufferTestFixture::Run(TestInstantiateUsesWholeCapacity);
    ClasBufferTestFixture::Run(TestInstantiatePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestTriangleSnapshotsAndNativeSizes);
    ClasBufferTestFixture::Run(TestTriangleCreateRejectionAndRetry);
    ClasBufferTestFixture::Run(TestTrianglePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestDirectUploadUsesTaskFormatsAndCounts);
    ClasBufferTestFixture::Run(TestUniqueVertexCountBounds);
    ClasBufferTestFixture::Run(TestClusterBlasCreationUsesExplicitCapacity);
    ClasBufferTestFixture::Run(TestClusterBlasCreateRejectionAndRetry);
    ClasBufferTestFixture::Run(TestClusterBlasRawReferencesAndPacking);
    ClasBufferTestFixture::Run(TestClusterBlasUploadValidatesBeforeWriting);
}

void ClasBufferTestFixture::WithClusterQueries(void (*inTest)(), VkDeviceSize inScratchBytes)
{
    Hooks hooks;
    scratchSize = inScratchBytes;
    inTest();
}
