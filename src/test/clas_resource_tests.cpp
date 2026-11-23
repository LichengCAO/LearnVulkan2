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
    static auto ExchangeRevision(uint32_t inRevision) -> uint32_t
    {
        return std::exchange(MyDevice::GetInstance().m_clusterAccelerationStructureRevision, inRevision);
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
        uint32_t scratchAlignment = 256;
        bool supportsVertexFormat = true;
    };
    DeviceCaps caps;
    VkClusterAccelerationStructureTriangleClusterInputNV lastTriangleInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV lastClustersInput{};
    VkClusterAccelerationStructureMoveObjectsInputNV lastMoveInput{};
    constexpr auto QueryTemplateIndices = static_cast<VkClusterAccelerationStructureOpTypeNV>(5);
    uint32_t queriedOutputCount = 0;
    uint32_t sizeQueries = 0;
    uint32_t propertyQueries = 0;
    VkClusterAccelerationStructureOpTypeNV queriedOp{};
    VkClusterAccelerationStructureOpModeNV queriedMode{};
    VkDeviceSize scratchSize = 512;
    VkDeviceSize moveUpdateScratchSize = 0;

    void VKAPI_PTR Properties(VkPhysicalDevice, VkPhysicalDeviceProperties2* inOut)
    {
        ++propertyQueries;
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
                result.clusterScratchByteAlignment = caps.scratchAlignment;
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
        CHECK_TRUE(out->sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR && !out->pNext);
        queriedOutputCount = in->maxAccelerationStructureCount;
        queriedOp = in->opType;
        queriedMode = in->opMode;
        switch (in->opType)
        {
        case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV:
            lastMoveInput = *in->opInput.pMoveObjects;
            break;
        case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV:
            lastClustersInput = *in->opInput.pClustersBottomLevel;
            break;
        default:
            lastTriangleInput = *in->opInput.pTriangleClusters;
            break;
        }
        if (queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV || queriedOp == QueryTemplateIndices)
        {
            out->accelerationStructureSize = 0;
            out->buildScratchSize = scratchSize;
            out->updateScratchSize = queriedOp == QueryTemplateIndices ? 73 : moveUpdateScratchSize;
            return;
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
        uint32_t oldRevision = ClusterAccelerationStructureTestProbe::ExchangeRevision(3);

        Hooks()
        {
            caps = {};
            lastTriangleInput = {};
            lastClustersInput = {};
            lastMoveInput = {};
            queriedOutputCount = 0;
            sizeQueries = 0;
            propertyQueries = 0;
            scratchSize = 512;
            moveUpdateScratchSize = 0;
        }

        ~Hooks()
        {
            vkGetPhysicalDeviceProperties2 = oldProperties;
            vkGetPhysicalDeviceFormatProperties = oldFormats;
            vkGetClusterAccelerationStructureBuildSizesNV = oldSizes;
            ClusterAccelerationStructureTestProbe::ExchangeEnabled(oldEnabled);
            ClusterAccelerationStructureTestProbe::ExchangeRevision(oldRevision);
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

    auto UploadBuffer(VkMemoryPropertyFlags inProperties) -> Buffer
    {
        Buffer buffer;
        BufferCreateInfo info;
        info.SetBufferSize(256).SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT).CustomizeMemoryProperty(inProperties);
        buffer.Create(info);
        return buffer;
    }

    constexpr VkMemoryPropertyFlags HostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

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
        static void VKAPI_PTR RejectSizes(VkDevice, const VkClusterAccelerationStructureInputInfoNV*, VkAccelerationStructureBuildSizesInfoKHR*)
        { throw std::runtime_error("Unexpected Device size query during target binding!"); }
        static void VKAPI_PTR RejectProperties(VkPhysicalDevice, VkPhysicalDeviceProperties2*)
        { throw std::runtime_error("Unexpected Device property query during target binding!"); }
        static void VKAPI_PTR RejectFormats(VkPhysicalDevice, VkFormat, VkFormatProperties*)
        { throw std::runtime_error("Unexpected Device format query during target binding!"); }
        static auto VKAPI_PTR RejectAddress(VkDevice, const VkBufferDeviceAddressInfo*) -> VkDeviceAddress
        { throw std::runtime_error("Unexpected Device address query during target binding!"); }
        decltype(vkCreateBuffer) oldCreate = std::exchange(vkCreateBuffer, Create);
        decltype(vkQueueSubmit2) oldSubmit = std::exchange(vkQueueSubmit2, Submit);
        decltype(vkGetClusterAccelerationStructureBuildSizesNV) oldSizes = vkGetClusterAccelerationStructureBuildSizesNV;
        decltype(vkGetPhysicalDeviceProperties2) oldProperties = vkGetPhysicalDeviceProperties2;
        decltype(vkGetPhysicalDeviceFormatProperties) oldFormats = vkGetPhysicalDeviceFormatProperties;
        decltype(vkGetBufferDeviceAddress) oldAddress = vkGetBufferDeviceAddress;
        explicit HostQueryDeviceCalls(bool rejectQueries = false)
        {
            allocations = submissions = 0;
            if (rejectQueries)
            {
                vkGetClusterAccelerationStructureBuildSizesNV = RejectSizes;
                vkGetPhysicalDeviceProperties2 = RejectProperties;
                vkGetPhysicalDeviceFormatProperties = RejectFormats;
                vkGetBufferDeviceAddress = RejectAddress;
            }
        }
        ~HostQueryDeviceCalls()
        {
            vkCreateBuffer = oldCreate; vkQueueSubmit2 = oldSubmit;
            vkGetClusterAccelerationStructureBuildSizesNV = oldSizes;
            vkGetPhysicalDeviceProperties2 = oldProperties;
            vkGetPhysicalDeviceFormatProperties = oldFormats;
            vkGetBufferDeviceAddress = oldAddress;
        }
    };

    void TestTargetSelectionDoesNotQueryMemoryRequirements()
    {
        Hooks hooks;
        ClusterLevelAccelStructBuildLimits limits;
        limits.SetMaxTriangleCount(3, 6);
        limits.SetMaxVertexCount(5, 12);
        ClusterBottomLevelAccelStructBuildLimits blas;
        blas.SetMaxClusterCount(5, 7);
        HostQueryDeviceCalls forbidden(true);
        ImplicitClusterAccelStructCommandInput input;
        const auto queries = sizeQueries;
        input.SetClusterTarget(limits, 3, 0x4000, 0x3000);
        input.SetInstantiateClusterTarget(limits, 3, 0x4000, 0x3000);
        input.SetClusterTemplateTarget(limits, 3, 0x4000, 0x3000);
        input.SetBottomLevelAccelStructTarget(blas, 3, 0x4000, 0x3000);
        auto incomplete = limits;
        incomplete.CustomizeVertexFormat(VK_FORMAT_UNDEFINED);
        ExpectFailure([&] { input.SetClusterTarget(incomplete, 3, 0x4000, 0x3000); });
        ExplicitClusterAccelStructCommandInput explicitBuild;
        explicitBuild.SetClusterTarget(limits, 3, 0x3000);
        explicitBuild.SetInstantiateClusterTarget(limits, 3, 0x3000);
        explicitBuild.SetBottomLevelAccelStructTarget(blas, 3, 0x3000);
        ComputeClusterAccelStructSizesCommandInput compute;
        compute.SetClusterTarget(limits, 3, 0x2000);
        compute.SetInstantiateClusterTarget(limits, 3, 0x2000);
        compute.SetClusterTemplateTarget(limits, 3, 0x2000);
        compute.SetBottomLevelAccelStructTarget(blas, 3, 0x2000);
        CHECK_TRUE(sizeQueries == queries, "Cluster target selection must not query Device memory requirements!");
    }

    void TestCommandInputQueries()
    {
        Hooks hooks;
        HostQueryDeviceCalls calls;
        ClusterLevelAccelStructBuildLimits layout;
        using E = ExplicitClusterAccelStructCommandInput;
        using I = ImplicitClusterAccelStructCommandInput;
        using C = ComputeClusterAccelStructSizesCommandInput;
        layout.SetMaxTriangleCount(3, 6);
        ExpectFailure([&] { E::GetClusterMemoryRequirements(layout, 3); });
        layout.SetMaxVertexCount(5, 12);
        ExpectFailure([&] { E::GetClusterMemoryRequirements(layout, 0); });
        caps.supportsVertexFormat = false;
        ExpectFailure([&] { E::GetClusterMemoryRequirements(layout, 3); });
        caps.supportsVertexFormat = true;
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(false);
        ExpectFailure([&] { I::GetClusterMemoryRequirements(layout, 3); });
        ClusterAccelerationStructureTestProbe::ExchangeEnabled(true);
        CHECK_TRUE(sizeQueries == 0);
        scratchSize = 0;
        const auto check = [&](const auto& sizes, auto operation, auto mode, bool blas = false)
        {
            const bool implicit = mode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
            CHECK_TRUE(queriedMode == mode && queriedOp == operation);
            CHECK_TRUE(sizes.accelerationStructureSize == (implicit ? 1024 : blas ? 1280 : 768) &&
                sizes.accelerationStructureAlignment == (operation == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV ? 32 : blas ? 512 : 128) &&
                sizes.scratchAlignment == 256 && sizes.buildScratchBufferSize == 0 && !sizes.updateScratchBufferSize);
            CHECK_TRUE(queriedOutputCount == 3);
            if (blas)
            {
                CHECK_TRUE(lastClustersInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV &&
                    !lastClustersInput.pNext && lastClustersInput.maxClusterCountPerAccelerationStructure == 5 &&
                    lastClustersInput.maxTotalClusterCount == 7);
            }
            else
            {
                CHECK_TRUE(lastTriangleInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV &&
                    !lastTriangleInput.pNext && lastTriangleInput.vertexFormat == VK_FORMAT_R32G32B32_SFLOAT &&
                    lastTriangleInput.maxGeometryIndexValue == 0 && lastTriangleInput.maxClusterUniqueGeometryCount == 1 &&
                    lastTriangleInput.minPositionTruncateBitCount == 0 && lastTriangleInput.maxClusterTriangleCount == 3 &&
                    lastTriangleInput.maxClusterVertexCount == 5 && lastTriangleInput.maxTotalTriangleCount == 6 &&
                    lastTriangleInput.maxTotalVertexCount == 12);
            }
        };
        ClusterBottomLevelAccelStructBuildLimits blas;
        ExpectFailure([&] { C::GetBottomLevelAccelStructBuildScratchRequirements(blas, 3); });
        blas.SetMaxClusterCount(5, 7);
        constexpr auto direct = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
        constexpr auto instantiate = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
        constexpr auto bottom = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
        constexpr auto implicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
        constexpr auto explicitMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
        check(E::GetClusterMemoryRequirements(layout, 3), direct, explicitMode);
        check(E::GetInstantiateClusterMemoryRequirements(layout, 3), instantiate, explicitMode);
        check(E::GetBottomLevelAccelStructMemoryRequirements(blas, 3), bottom, explicitMode, true);
        check(I::GetClusterMemoryRequirements(layout, 3), direct, implicit);
        check(I::GetInstantiateClusterMemoryRequirements(layout, 3), instantiate, implicit);
        check(I::GetBottomLevelAccelStructMemoryRequirements(blas, 3), bottom, implicit, true);
        check(I::GetClusterTemplateMemoryRequirements(layout, 3), VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV, implicit);
        caps.scratchAlignment = 1024;
        auto previousProperties = propertyQueries;
        const auto checkScratch = [&](ClusterScratchRequirements scratch, auto operation)
        {
            CHECK_TRUE(propertyQueries == previousProperties + 1);
            previousProperties = propertyQueries;
            CHECK_TRUE(scratch.size == scratchSize && scratch.alignment == caps.scratchAlignment &&
                queriedOp == operation && queriedOutputCount == 3 &&
                queriedMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV);
        };
        checkScratch(C::GetClusterBuildScratchRequirements(layout, 3), direct);
        scratchSize = 512;
        checkScratch(C::GetInstantiateClusterBuildScratchRequirements(layout, 3), instantiate);
        checkScratch(C::GetBottomLevelAccelStructBuildScratchRequirements(blas, 3), bottom);
        checkScratch(C::GetClusterTemplateBuildScratchRequirements(layout, 3), VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV);
        CHECK_TRUE(sizeQueries == 11 &&
            !calls.allocations && !calls.submissions && !ClasBufferTestFixture::LiveBufferCount());
    }

    void TestClusterMemoryRequirementLimits()
    {
        Hooks hooks;
        HostQueryDeviceCalls calls;
        using Implicit = ImplicitClusterAccelStructCommandInput;
        ClusterLevelAccelStructBuildLimits limits;
        limits.SetMaxTriangleCount(3, 6);
        limits.SetMaxVertexCount(5, 12);
        caps.maxTriangles = 2;
        ExpectFailure([&] { Implicit::GetClusterMemoryRequirements(limits, 3); });
        caps.maxTriangles = 16;
        caps.maxVertices = 4;
        ExpectFailure([&] { Implicit::GetClusterTemplateMemoryRequirements(limits, 3); });
        caps.maxVertices = caps.maxTriangles = 512;
        limits.SetMaxTriangleCount(512, 512);
        ExpectFailure([&] { Implicit::GetClusterMemoryRequirements(limits, 3); });
        limits.SetMaxTriangleCount(3, 6);
        limits.SetMaxVertexCount(512, 512);
        ExpectFailure([&] { Implicit::GetClusterMemoryRequirements(limits, 3); });
        limits.SetMaxVertexCount(5, 12);
        CHECK_TRUE(sizeQueries == 0);

        ClusterBottomLevelAccelStructBuildLimits blas;
        blas.SetMaxClusterCount(2, 3);
        caps.maxBufferSize = 1023;
        ExpectFailure([&] { Implicit::GetClusterMemoryRequirements(limits, 3); });
        caps.maxBufferSize = 8192;
        caps.maxAllocationSize = 1023;
        ExpectFailure([&] { Implicit::GetBottomLevelAccelStructMemoryRequirements(blas, 2); });
        caps.maxAllocationSize = 8192;
        ExpectFailure([&] { Implicit::GetClusterMemoryRequirements(limits, UINT32_MAX); });
        scratchSize = UINT64_MAX;
        ExpectFailure([&] { Implicit::GetBottomLevelAccelStructMemoryRequirements(blas, 2); });
        CHECK_TRUE(!calls.allocations && !calls.submissions);
    }

    void TestMoveAndTemplateIndexSizeQueries()
    {
        Hooks hooks;
        HostQueryDeviceCalls calls;
        using Compute = ComputeClusterAccelStructSizesCommandInput;
        using Explicit = ExplicitClusterAccelStructCommandInput;
        using Implicit = ImplicitClusterAccelStructCommandInput;
        struct MoveCase
        {
            VkClusterAccelerationStructureTypeNV type;
            VkDeviceSize alignment;
        };
        constexpr std::array cases{
            MoveCase{VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV, 128},
            MoveCase{VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV, 32},
            MoveCase{VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV, 512},
        };
        ClusterAccelStructMoveLimits move;
        move.SetMaxMovedBytes(16384);
        ExpectFailure([&] { Compute::GetMoveObjectsScratchRequirements(move, 3); });
        move.SetObjectType(cases.front().type);
        move.SetMaxMovedBytes(0);
        ExpectFailure([&] { Compute::GetMoveObjectsScratchRequirements(move, 3); });
        move.SetMaxMovedBytes(16384);
        ExpectFailure([&] { Explicit::GetMoveObjectsScratchRequirements(move, 0); });
        ExpectFailure([&] { Explicit::GetMoveObjectsDestinationAlignment(VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_NV); });
        CHECK_TRUE(sizeQueries == 0);

        const auto checkMode = [](VkClusterAccelerationStructureOpModeNV mode, VkClusterAccelerationStructureOpTypeNV operation)
        {
            CHECK_TRUE(queriedOutputCount == 3 && queriedOp == operation && queriedMode == mode);
        };
        constexpr auto compute = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
        constexpr auto explicitMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
        constexpr auto implicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
        for (const auto& item : cases)
        {
            for (bool noOverlap : {false, true})
            {
                ClusterAccelStructMoveLimits limits;
                limits.SetObjectType(item.type);
                limits.SetMaxMovedBytes(16384);
                if (noOverlap) limits.CustomizeNoMoveOverlap(true);
                scratchSize = noOverlap ? 128 : 0;
                auto previousProperties = propertyQueries;
                const auto checkMove = [&](ClusterScratchRequirements scratch, VkClusterAccelerationStructureOpModeNV mode)
                {
                    CHECK_TRUE(propertyQueries == previousProperties + 1);
                    previousProperties = propertyQueries;
                    checkMode(mode, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV);
                    CHECK_TRUE(scratch.size == scratchSize && scratch.alignment == caps.scratchAlignment &&
                        lastMoveInput.type == item.type &&
                        lastMoveInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV &&
                        !lastMoveInput.pNext && lastMoveInput.maxMovedBytes == 16384 &&
                        lastMoveInput.noMoveOverlap == (noOverlap ? VK_TRUE : VK_FALSE));
                };
                checkMove(Compute::GetMoveObjectsScratchRequirements(limits, 3), compute);
                checkMove(Explicit::GetMoveObjectsScratchRequirements(limits, 3), explicitMode);
                checkMove(Implicit::GetMoveObjectsScratchRequirements(limits, 3), implicit);
            }
            CHECK_TRUE(Explicit::GetMoveObjectsDestinationAlignment(item.type) == item.alignment &&
                Implicit::GetMoveObjectsDestinationAlignment(item.type) == item.alignment);
        }

        ClusterLevelAccelStructBuildLimits triangles;
        triangles.SetMaxTriangleCount(3, 7);
        triangles.SetMaxVertexCount(5, 11);
        triangles.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        const auto checkQuery = [&](const VkAccelerationStructureBuildSizesInfoKHR& sizes, auto mode)
        {
            checkMode(mode, QueryTemplateIndices);
            CHECK_TRUE(sizes.sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR &&
                !sizes.pNext && sizes.accelerationStructureSize == 0 &&
                sizes.buildScratchSize == 128 && sizes.updateScratchSize == 73);
            CHECK_TRUE(lastTriangleInput.sType == VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV &&
                !lastTriangleInput.pNext && lastTriangleInput.vertexFormat == VK_FORMAT_R16G16B16A16_SFLOAT &&
                lastTriangleInput.maxClusterTriangleCount == 3 && lastTriangleInput.maxTotalTriangleCount == 7 &&
                lastTriangleInput.maxClusterVertexCount == 5 && lastTriangleInput.maxTotalVertexCount == 11);
        };
        checkQuery(Compute::GetQueryTemplateIndicesBuildSizes(triangles, 3), compute);
        checkQuery(Explicit::GetQueryTemplateIndicesBuildSizes(triangles, 3), explicitMode);
        checkQuery(Implicit::GetQueryTemplateIndicesBuildSizes(triangles, 3), implicit);
        CHECK_TRUE(Compute::GetScratchAlignment() == 256 && Explicit::GetScratchAlignment() == 256 &&
            Implicit::GetScratchAlignment() == 256 && sizeQueries == 21);

        ClusterAccelerationStructureTestProbe::ExchangeRevision(2);
        ExpectFailure([&] { Compute::GetQueryTemplateIndicesBuildSizes(triangles, 3); });
        CHECK_TRUE(sizeQueries == 21);
        CHECK_TRUE(Compute::GetMoveObjectsScratchRequirements(move, 3).size == 128 && sizeQueries == 22);
        struct ScratchCase
        {
            VkDeviceSize build;
            VkDeviceSize update;
            VkDeviceSize expected;
        };
        constexpr std::array scratchCases{
            ScratchCase{0, 128, 128}, ScratchCase{128, 0, 128}, ScratchCase{64, 128, 128}, ScratchCase{0, 0, 0}
        };
        for (const auto& [build, update, expected] : scratchCases)
        {
            scratchSize = build;
            moveUpdateScratchSize = update;
            CHECK_TRUE(Compute::GetMoveObjectsScratchRequirements(move, 3).size == expected &&
                Explicit::GetMoveObjectsScratchRequirements(move, 3).size == expected &&
                Implicit::GetMoveObjectsScratchRequirements(move, 3).size == expected,
                "MOVE must provide scratch for both native size fields!");
        }
        moveUpdateScratchSize = caps.maxBufferSize;
        CHECK_TRUE(Explicit::GetMoveObjectsScratchRequirements(move, 3).size == caps.maxBufferSize);
        --caps.maxBufferSize;
        ExpectFailure([&] { Explicit::GetMoveObjectsScratchRequirements(move, 3); });
        ++caps.maxBufferSize;
        --caps.maxAllocationSize;
        ExpectFailure([&] { Implicit::GetMoveObjectsScratchRequirements(move, 3); });
        moveUpdateScratchSize = 0;
        caps.scratchAlignment = 3;
        ExpectFailure([&] { Compute::GetMoveObjectsScratchRequirements(move, 3); });
        CHECK_TRUE(!calls.allocations && !calls.submissions && !ClasBufferTestFixture::LiveBufferCount());
    }

    template<class Info>
    void CheckPackedReferenceUpload(void (Info::*setReference)(VkDeviceAddress))
    {
        constexpr auto layout = Info::GetBuildInfoLayout();
        static_assert(layout.recordStride == 8 && layout.addressAlignment == 8);
        constexpr std::array<VkDeviceAddress, 2> expected{0x0123456789abc008, 0x200001};
        std::array<Info, 2> records{};
        (records[0].*setReference)(expected[0]);
        (records[1].*setReference)(expected[1]);
        Buffer upload = UploadBuffer(HostMemory);
        Info::CommitToDeviceInline(records, upload, 8);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<VkDeviceAddress, 2> actual{};
        std::memcpy(actual.data(), bytes.data() + 8, sizeof(actual));
        CHECK_TRUE(actual == expected, "Reference upload must preserve full GPU addresses in packed records!");
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        ExpectFailure([&] { Info::CommitToDeviceInline(records, upload, 1); });
        ExpectFailure([&] { Info::CommitToDeviceInline(records, upload, 248); });
        records.back() = {};
        ExpectFailure([&] { Info::CommitToDeviceInline(records, upload, 8); });
        CHECK_TRUE(std::ranges::equal(bytes, saved), "Rejected reference uploads must preserve Buffer contents!");
        Buffer empty;
        Info::CommitToDeviceInline({}, empty);
    }

    void TestBuildInfoBufferSizes()
    {
        HostQueryDeviceCalls forbidden(true);
        constexpr std::array layouts{
            ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout(),
            ClusterLevelAccelStructTemplateBuildInfo::GetBuildInfoLayout(),
            ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout(),
            ClusterBottomLevelAccelStructBuildInfo::GetBuildInfoLayout(),
            ClusterAccelStructMoveInfo::GetBuildInfoLayout(),
            ClusterTemplateIndicesQueryInfo::GetBuildInfoLayout()
        };
        constexpr std::array<VkDeviceSize, 6> expectedBytes{192, 216, 96, 48, 24, 24};
        for (size_t i = 0; i < layouts.size(); ++i)
        {
            CHECK_TRUE(layouts[i].GetBufferSize(3) == expectedBytes[i]);
            ExpectFailure([&] { layouts[i].GetBufferSize(0); });
        }
        static_assert(layouts[0].GetBufferSize(1) == 64);
        CHECK_TRUE(layouts[0].GetBufferSize(UINT32_MAX) == 274877906880ULL,
            "Task buffer sizes must not truncate to 32 bits!");
    }

    void TestReferenceUploads()
    {
        CheckPackedReferenceUpload(&ClusterAccelStructMoveInfo::SetSourceReference);
        CheckPackedReferenceUpload(&ClusterTemplateIndicesQueryInfo::SetTemplateReference);
    }

    void TestClusterOwnerCreationAndRetry()
    {
        Hooks hooks;
        ClusterLevelAccelStructCreateInfo info;
        info.SetMaxTriangleCount(0);
        info.SetMaxVertexCount(5);
        auto owner = MakeIntrusive<ClusterLevelAccelStruct>();
        CHECK_TRUE(!owner->IsValid() && owner->GetDeviceAddress() == 0 &&
            owner->GetMemoryRequirements().accelerationStructureSize == 0 &&
            owner->GetMemoryRequirements().buildScratchBufferSize == 0);
        ExpectFailure([&] { owner->Create(info); });
        CHECK_TRUE(sizeQueries == 0 && !owner->IsValid());
        info.SetMaxTriangleCount(2);
        ClasBufferTestFixture::SetAllocationFailure(true);
        ExpectFailure([&] { owner->Create(info); });
        ClasBufferTestFixture::SetAllocationFailure(false);
        CHECK_TRUE(!owner->IsValid() && owner->GetDeviceAddress() == 0 &&
            owner->GetMemoryRequirements().accelerationStructureSize == 0);
        ClasBufferTestFixture::SetBufferAddressOverride(1);
        ExpectFailure([&] { owner->Create(info); });
        ClasBufferTestFixture::SetBufferAddressOverride(0);
        CHECK_TRUE(!owner->IsValid() && owner->GetMemoryRequirements().accelerationStructureSize == 0 &&
            ClasBufferTestFixture::LiveBufferCount() == 0);
        owner->Create(info);
        const auto savedAddress = owner->GetDeviceAddress();
        const auto savedSizes = owner->GetMemoryRequirements();
        const auto savedQueries = sizeQueries;
        CHECK_TRUE(owner->IsValid() && savedAddress != 0 && savedSizes.accelerationStructureSize == 512 &&
            savedSizes.accelerationStructureAlignment == 128 && savedSizes.buildScratchBufferSize == 512 &&
            savedSizes.scratchAlignment == 256 &&
            savedSizes.updateScratchBufferSize == std::nullopt);
        CHECK_TRUE(queriedOutputCount == 1 && queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV &&
            queriedMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV);
        info.CustomizeTemplateUsage(true);
        ExpectFailure([&] { owner->Create(info); });
        CHECK_TRUE(sizeQueries == savedQueries && owner->GetDeviceAddress() == savedAddress &&
            owner->GetMemoryRequirements().accelerationStructureSize == savedSizes.accelerationStructureSize,
            "CLAS queries must use saved Host data and duplicate Create must preserve it!");
        owner.reset();
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
        owner = MakeIntrusive<ClusterLevelAccelStruct>();
        owner->Create(info);
        CHECK_TRUE(queriedOutputCount == 1 && queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV &&
            owner->GetMemoryRequirements().accelerationStructureSize == savedSizes.accelerationStructureSize);
        owner.reset();
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
        CHECK_TRUE(!owner->IsValid() && owner->GetDeviceAddress() == 0 &&
            owner->GetMemoryRequirements().accelerationStructureSize == 0 && ClasBufferTestFixture::LiveBufferCount() == 0);
        ClasBufferTestFixture::SetAllocationFailure(false);
        ClasBufferTestFixture::SetBufferAddressOverride(1);
        ExpectFailure([&] { owner->Create(info); });
        ClasBufferTestFixture::SetBufferAddressOverride(0);
        CHECK_TRUE(!owner->IsValid() && owner->GetDeviceAddress() == 0 && ClasBufferTestFixture::LiveBufferCount() == 0);
        owner->Create(info);
        const auto address = owner->GetDeviceAddress();
        const auto queries = sizeQueries;
        CHECK_TRUE(owner->IsValid() && address != 0 && owner->GetVkAccelerationStructure() == VK_NULL_HANDLE &&
            owner->GetMemoryRequirements().accelerationStructureAlignment == 512 && queriedOutputCount == 1 &&
            queriedOp == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV &&
            lastClustersInput.maxTotalClusterCount == 3);
        ExpectFailure([&] { owner->Create(info); });
        CHECK_TRUE(sizeQueries == queries && owner->GetDeviceAddress() == address);
        BuildAccelStructCommandInput input;
        AccelStructTriangleBuildInfo triangle;
        ExpectFailure([&] { input.SetTrianglesTarget(*owner, std::span(&triangle, 1)); });
        ExpectFailure([&] { input.CustomizeSourceAccelStruct(*owner); });
        owner.reset();
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0);
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

    template<class Fn> struct TemplateHook
    {
        Fn& target;
        Fn saved;
        TemplateHook(Fn& fn, Fn replacement) : target(fn), saved(std::exchange(fn, replacement)) {}
        ~TemplateHook() { target = saved; }
    };

    void TestTemplatePackingAndAtomicUpload()
    {
        Buffer empty;
        ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline({}, empty);
        std::array<ClusterLevelAccelStructTemplateBuildInfo, 2> infos;
        for (auto& info : infos) info.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
        infos[0].SetIndexBuffer(0x1000, 3);
        infos[0].SetVertexCount(3);
        infos[1].SetIndexBuffer(0x2000, 6);
        infos[1].SetVertexCount(4);
        infos[1].CustomizeIndexType(VK_INDEX_TYPE_UINT32);
        infos[1].CustomizeIndexStride(8);
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(infos, empty); });
        Buffer upload = UploadBuffer(HostMemory);
        // Encoding with coherent memory requires no physical device properties or GPU submission.
        const TemplateHook properties(vkGetPhysicalDeviceProperties2,
            static_cast<PFN_vkGetPhysicalDeviceProperties2>(nullptr));
        const TemplateHook submit(vkQueueSubmit2, &HostQueryDeviceCalls::Submit);
        HostQueryDeviceCalls::submissions = 0;
        ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(infos, upload, 8);
        using Task = VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV;
        std::array<Task, 2> tasks;
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::memcpy(tasks.data(), bytes.data() + 8, sizeof(tasks));
        CHECK_TRUE(tasks[0].indexBuffer == 0x1000 && tasks[1].indexBuffer == 0x2000 &&
            tasks[0].indexBufferStride == 0 && tasks[1].indexBufferStride == 8 &&
            tasks[0].indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV &&
            tasks[1].indexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV &&
            tasks[0].triangleCount == 1 && tasks[1].triangleCount == 2 &&
            tasks[0].vertexCount == 3 && tasks[1].vertexCount == 4 && !HostQueryDeviceCalls::submissions);
        for (const auto& task : tasks)
            CHECK_TRUE(task.clusterID == 0 && task.clusterFlags == 0 && task.vertexBuffer == 0 && task.vertexBufferStride == 0 &&
                task.geometryIndexAndFlagsBuffer == 0 && task.opacityMicromapArray == 0 &&
                task.opacityMicromapIndexBuffer == 0 && task.instantiationBoundingBoxLimit == 0 &&
                task.opacityMicromapIndexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV &&
                task.baseGeometryIndexAndGeometryFlags.geometryIndex == 0 && task.positionTruncateBitCount == 0 &&
                task.baseGeometryIndexAndGeometryFlags.geometryFlags == VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV);
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        auto invalid = infos;
        invalid.back().SetVertexCount(512);
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(invalid, upload, 8); });
        invalid = infos;
        invalid.back().SetIndexBuffer(0, 6);
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(invalid, upload, 8); });
        invalid = infos;
        invalid.back().CustomizeIndexStride(3);
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(invalid, upload, 8); });
        invalid = infos;
        invalid.back().CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR);
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(invalid, upload, 8); });
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(infos, upload, 1); });
        ExpectFailure([&] { ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(infos, upload, 200); });
        CHECK_TRUE(std::ranges::equal(bytes, saved), "Rejected template packing must preserve all destination bytes!");
    }

    void TestTrianglePackingAndAtomicUpload()
    {
        Hooks hooks;
        struct TriangleInput
        {
            uint32_t clusterID;
            VkDeviceAddress indexAddress;
            uint32_t indexCount;
            VkDeviceAddress vertexAddress;
            uint32_t uniqueVertexCount;
            VkDeviceSize vertexStride;
        };
        constexpr std::array inputs{
            TriangleInput{ 7, 0x5004, 3, 0x4000, 3, 0 },
            TriangleInput{ 8, 0x7004, 9, 0x6000, 5, 12 },
            TriangleInput{ 9, 0x9004, 6, 0x8000, 4, 24 }
        };
        std::array<ClusterLevelAccelStructDirectBuildInfo, inputs.size()> triangles{};
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            const auto& input = inputs[i];
            triangles[i].SetIndexBuffer(input.indexAddress, input.indexCount);
            triangles[i].SetVertexBuffer(input.vertexAddress, input.uniqueVertexCount);
            triangles[i].CustomizeVertexStride(input.vertexStride);
            triangles[i].CustomizeClusterID(input.clusterID);
        }
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
            const auto& input = inputs[i];
            CHECK_TRUE(task.clusterID == input.clusterID && task.triangleCount == input.indexCount / 3 &&
                task.vertexCount == input.uniqueVertexCount && task.vertexBuffer == input.vertexAddress &&
                task.vertexBufferStride == input.vertexStride && task.indexBuffer == input.indexAddress && task.indexBufferStride == 0 &&
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
        defaults.SetIndexBuffer(0x5004, 6);
        ExpectFailure([&] { defaults.SetIndexBuffer(0xFFFF, 4); });
        defaults.CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        defaults.CustomizeIndexStride(8);
        for (size_t format = 0; format < indexTypes.size(); ++format)
        {
            defaults.CustomizeIndexType(indexTypes[format]);
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(std::span(&defaults, 1), upload, offset);
            Record task{};
            std::memcpy(&task, bytes.data() + offset, sizeof(task));
            CHECK_TRUE(task.indexBuffer == 0x5004 && task.triangleCount == 2,
                "Rejecting four indices must preserve the address and two triangles from the six-index binding!");
            CHECK_TRUE(task.indexType == nativeTypes[format] && task.clusterID == 0 && task.vertexBufferStride == 0 &&
                task.indexBufferStride == 8 && task.opacityMicromapIndexType == VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV,
                "Packed native CLAS task must use its own index type and value defaults!");
        }
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
        triangles.front().CustomizeClusterID(42);
        const auto original = triangles.back();
        const auto& last = inputs.back();
        auto expectBadTriangle = [&](auto configureLast)
        {
            triangles.back() = original;
            configureLast(triangles.back());
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(triangles, upload, offset); });
            CHECK_TRUE(std::ranges::equal(ClasBufferTestFixture::Bytes(upload), saved),
                "A rejected late task must preserve all caller bytes!");
        };
        expectBadTriangle([](auto& task) { task.CustomizeClusterID(std::numeric_limits<uint32_t>::max()); });
        expectBadTriangle([](auto& task) { task.CustomizeIndexType(VK_INDEX_TYPE_NONE_KHR); });
        expectBadTriangle([](auto& task) { task.CustomizeVertexFormat(VK_FORMAT_UNDEFINED); });
        expectBadTriangle([](auto& task) { task.CustomizeIndexStride(65536); });
        expectBadTriangle([](auto& task) { task.CustomizeIndexStride(5); });
        constexpr auto maxAddress = std::numeric_limits<VkDeviceAddress>::max();
        expectBadTriangle([&](auto& task) {
            task.SetIndexBuffer(maxAddress - 31, last.indexCount);
            task.CustomizeIndexStride(8);
        });
        expectBadTriangle([&](auto& task) { task.SetIndexBuffer(last.indexAddress, 0); });
        expectBadTriangle([&](auto& task) { task.SetVertexBuffer(last.vertexAddress, 0); });
        expectBadTriangle([&](auto& task) { task.SetIndexBuffer(last.indexAddress, 512 * 3); });
        expectBadTriangle([&](auto& task) { task.SetVertexBuffer(last.vertexAddress, 512); });
        expectBadTriangle([&](auto& task) { task.SetVertexBuffer(0, last.uniqueVertexCount); });
        expectBadTriangle([&](auto& task) { task.SetVertexBuffer(last.vertexAddress + 1, last.uniqueVertexCount); });
        expectBadTriangle([](auto& task) { task.CustomizeVertexStride(65536); });
        expectBadTriangle([](auto& task) { task.CustomizeVertexStride(14); });
        expectBadTriangle([&](auto& task) { task.SetIndexBuffer(last.indexAddress + 1, last.indexCount); });
        expectBadTriangle([&](auto& task) { task.SetVertexBuffer(maxAddress - 3, last.uniqueVertexCount); });
        expectBadTriangle([&](auto& task) { task.SetIndexBuffer(maxAddress - 3, last.indexCount); });
        triangles.back() = original;
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
        std::array<ClusterLevelAccelStructDirectBuildInfo, 3> tasks{};
        tasks[0].SetIndexBuffer(0x5000, 300);
        tasks[0].SetVertexBuffer(0x4000, 120);
        tasks[0].CustomizeIndexType(VK_INDEX_TYPE_UINT8);
        tasks[1].SetIndexBuffer(0x7002, 303);
        tasks[1].SetVertexBuffer(0x6002, 121);
        tasks[1].CustomizeIndexType(VK_INDEX_TYPE_UINT16);
        tasks[1].CustomizeVertexFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
        tasks[1].CustomizeClusterID(1);
        tasks[2].SetIndexBuffer(0x9004, 306);
        tasks[2].SetVertexBuffer(0x8000, 122);
        tasks[2].CustomizeClusterID(2);
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
            std::array<ClusterLevelAccelStructDirectBuildInfo, 2> tasks{};
            tasks[0].SetIndexBuffer(0x6000, 3);
            tasks[0].SetVertexBuffer(0x4000, 3);
            tasks[0].CustomizeClusterID(7);
            tasks[1].SetIndexBuffer(0xa000, triangles * 3);
            tasks[1].SetVertexBuffer(0x8000, maxUniqueVertices);
            tasks[1].CustomizeClusterID(8);
            for (auto& task : tasks) task.CustomizeIndexType(VK_INDEX_TYPE_UINT8);
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, upload);
            Record record{};
            std::memcpy(&record, bytes.data() + sizeof(record), sizeof(record));
            CHECK_TRUE(record.vertexCount == maxUniqueVertices, "The unique vertex count bound must be inclusive!");
            const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

            tasks[0].CustomizeClusterID(9);
            tasks[1].SetVertexBuffer(0x8000, maxUniqueVertices + 1);
            ExpectFailure([&] { ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(tasks, upload); });
            CHECK_TRUE(std::ranges::equal(bytes, saved), "A late impossible unique vertex count must preserve caller bytes!");
        }
    }

    void TestClusterBlasRawReferencesAndPacking()
    {
        Hooks hooks;
        constexpr VkDeviceAddress firstReferences = 0x100008;
        constexpr VkDeviceAddress secondReferences = 0x200000;
        std::array<ClusterBottomLevelAccelStructBuildInfo, 2> tasks{};
        tasks[0].SetClusterReferences(firstReferences, 2);
        tasks[1].SetClusterReferences(secondReferences, 1);

        ExpectFailure([&] { tasks[0].SetClusterReferences(0, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(firstReferences, 0); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(std::numeric_limits<VkDeviceAddress>::max() - 7, 1); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(std::numeric_limits<VkDeviceAddress>::max() - 15, 2); });
        ExpectFailure([&] { tasks[0].SetClusterReferences(0x100001, 1); });

        Buffer empty;
        ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline({}, empty, std::numeric_limits<VkDeviceSize>::max());
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, empty); });
        Buffer upload = UploadBuffer(HostMemory);
        constexpr VkDeviceSize offset = 8;
        ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, offset);
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
        std::array<ClusterBottomLevelAccelStructBuildInfo, 3> tasks{};
        tasks[0].SetClusterReferences(references + sizeof(VkDeviceAddress), 2);
        tasks[1].SetClusterReferences(references, 3);
        tasks[2].SetClusterReferences(references, 4);
        ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload);
        const auto bytes = ClasBufferTestFixture::Bytes(upload);
        std::array<VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, 3> records{};
        std::memcpy(records.data(), bytes.data(), sizeof(records));
        CHECK_TRUE(records[0].clusterReferencesCount == 2 && records[1].clusterReferencesCount == 3 &&
            records[2].clusterReferencesCount == 4,
            "Cluster BLAS upload must encode each caller-supplied count!");
        const std::vector<uint8_t> saved(bytes.begin(), bytes.end());

        tasks.back() = {};
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload); });
        CHECK_TRUE(std::ranges::equal(bytes, saved), "A late rejected BLAS task must preserve the entire upload buffer!");

        tasks.back().SetClusterReferences(references, 4);
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, 255); });
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, upload, std::numeric_limits<VkDeviceSize>::max()); });
        Buffer deviceLocal = UploadBuffer(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, deviceLocal); });
        Buffer nonCoherent = UploadBuffer(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        ExpectFailure([&] { ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(tasks, nonCoherent); });
        CHECK_TRUE(std::ranges::equal(bytes, saved));
    }
}

void RunClasResourceTests()
{
    ClasBufferTestFixture::Run(TestTargetSelectionDoesNotQueryMemoryRequirements);
    ClasBufferTestFixture::Run(TestCommandInputQueries);
    ClasBufferTestFixture::Run(TestClusterMemoryRequirementLimits);
    ClasBufferTestFixture::Run(TestMoveAndTemplateIndexSizeQueries);
    ClasBufferTestFixture::Run(TestBuildInfoBufferSizes);
    ClasBufferTestFixture::Run(TestReferenceUploads);
    ClasBufferTestFixture::Run(TestClusterOwnerCreationAndRetry);
    ClasBufferTestFixture::Run(TestClusterBlasOwners);
    ClasBufferTestFixture::Run(TestTemplatePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestInstantiatePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestTrianglePackingAndAtomicUpload);
    ClasBufferTestFixture::Run(TestDirectUploadUsesTaskFormatsAndCounts);
    ClasBufferTestFixture::Run(TestUniqueVertexCountBounds);
    ClasBufferTestFixture::Run(TestClusterBlasRawReferencesAndPacking);
    ClasBufferTestFixture::Run(TestClusterBlasUploadValidatesBeforeWriting);
}
