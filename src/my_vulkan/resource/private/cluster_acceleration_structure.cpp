#include "../cluster_acceleration_structure.h"
#include "../../device.h"
#include "../../allocator/private/memory_allocator.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <vulkan/vulkan_format_traits.hpp>

namespace
{
    using TriangleTask = VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
    using TemplateTask = VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV;
    using InstanceTask = VkClusterAccelerationStructureInstantiateClusterInfoNV;
    using ClusterBlasTask = VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV;
    constexpr auto TriangleTaskLayout = ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout();
    constexpr auto InstanceTaskLayout = ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout();
    constexpr auto ClusterBlasTaskLayout = ClusterBottomLevelAccelStructBuildInfo::GetBuildInfoLayout();
    constexpr VkDeviceSize ReferenceStride = AccelStructGpuLayout::ClusterBlas::PackedReferenceStride;
    constexpr auto MaxCount = (std::numeric_limits<uint32_t>::max)();
    constexpr uint32_t MaxTriangleTaskCount = AccelStructGpuLayout::Direct::CountMask;
    constexpr VkBufferUsageFlags StorageUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr auto Explicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
    constexpr auto Implicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    constexpr auto Direct = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    constexpr auto Instantiate = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
    constexpr auto ClusterBlas = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    constexpr auto ComputeSizes = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
    constexpr auto Template = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
    constexpr auto Move = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
    constexpr auto QueryTemplateIndices = VulkanDetail::ClusterTemplateIndicesOperation;

    auto MoveAlignmentProperty(VkClusterAccelerationStructureTypeNV type)
        -> uint32_t VkPhysicalDeviceClusterAccelerationStructurePropertiesNV::*
    {
        using Properties = VkPhysicalDeviceClusterAccelerationStructurePropertiesNV;
        switch (type)
        {
        case VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV: return &Properties::clusterByteAlignment;
        case VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV: return &Properties::clusterTemplateByteAlignment;
        case VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV: return &Properties::clusterBottomLevelByteAlignment;
        default: throw std::runtime_error("A valid cluster move object type is required!");
        }
    }

    void CheckMoveLimits(const VkClusterAccelerationStructureMoveObjectsInputNV& limits)
    {
        MoveAlignmentProperty(limits.type);
        CHECK_TRUE(limits.maxMovedBytes, "Cluster maximum moved bytes must be positive!");
    }

    struct ClusterProperties
    {
        VkPhysicalDeviceClusterAccelerationStructurePropertiesNV cluster;
        VkDeviceSize maxBufferAllocation;

        auto GetScratchAlignment() const -> VkDeviceSize
        {
            const auto alignment = cluster.clusterScratchByteAlignment;
            CHECK_TRUE(std::has_single_bit(alignment), "Device returned an invalid cluster scratch alignment!");
            return alignment;
        }
    };

    auto QueryProperties(VkPhysicalDevice inDevice) -> ClusterProperties
    {
        CHECK_TRUE(inDevice != VK_NULL_HANDLE && vkGetPhysicalDeviceProperties2,
            "Cluster acceleration structure properties require an initialized physical device!");
        VkPhysicalDeviceMaintenance4Properties bufferLimits{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES };
        VkPhysicalDeviceMaintenance3Properties allocationLimits{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES, &bufferLimits };
        VkPhysicalDeviceClusterAccelerationStructurePropertiesNV cluster{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV, &allocationLimits };
        VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &cluster };
        vkGetPhysicalDeviceProperties2(inDevice, &properties);
        cluster.pNext = nullptr;
        return { cluster, (std::min)(bufferLimits.maxBufferSize, allocationLimits.maxMemoryAllocationSize) };
    }

    void CheckQueryDependencies(const MyDevice& inDevice, bool inFeatureEnabled)
    {
        CHECK_TRUE(inFeatureEnabled && inDevice.vkDevice && vkGetClusterAccelerationStructureBuildSizesNV,
            "Cluster size queries require an initialized Device with the NV feature and size entrypoint!");
    }

    auto CheckAddress(VkDeviceAddress inBase, VkDeviceSize inOffset, VkDeviceSize inSize,
        VkDeviceSize inAlignment) -> VkDeviceAddress
    {
        constexpr auto maxAddress = (std::numeric_limits<VkDeviceAddress>::max)();
        CHECK_TRUE(inBase != 0 && inOffset <= maxAddress - inBase && inSize != 0,
            "Cluster buffer address range is invalid!");
        const auto address = inBase + inOffset;
        CHECK_TRUE(inSize <= maxAddress - address && address % inAlignment == 0,
            "Cluster buffer address range overflows or is misaligned!");
        return address;
    }

    struct VertexLayout
    {
        VkDeviceSize size;
        VkDeviceSize alignment;
    };

    auto GetVertexLayout(VkFormat inFormat) -> VertexLayout
    {
        CHECK_TRUE(inFormat != VK_FORMAT_UNDEFINED && inFormat != VK_FORMAT_MAX_ENUM,
            "A cluster vertex format is required!");
        const auto format = static_cast<vk::Format>(inFormat);
        const auto size = vk::blockSize(format);
        CHECK_TRUE(size > 0, "Invalid cluster vertex format size!");
        const auto packedBits = vk::packed(format);
        const VkDeviceSize alignment = (packedBits == 0 ? vk::componentBits(format, 0) : packedBits) / 8;
        CHECK_TRUE(alignment > 0, "Invalid cluster vertex format alignment!");
        return { size, alignment };
    }

    auto QueryVertexLayout(VkPhysicalDevice inDevice, VkFormat inFormat) -> VertexLayout
    {
        const auto layout = GetVertexLayout(inFormat);
        CHECK_TRUE(vkGetPhysicalDeviceFormatProperties, "A cluster vertex format query entrypoint is required!");
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(inDevice, inFormat, &properties);
        CHECK_TRUE(CONTAIN_BITS(properties.bufferFeatures, VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR),
            "Vertex format does not support acceleration structure input!");
        return layout;
    }

    struct IndexLayout
    {
        VkClusterAccelerationStructureIndexFormatFlagBitsNV format;
        VkDeviceSize size;
    };

    auto GetIndexLayout(VkIndexType inType) -> IndexLayout
    {
        switch (inType)
        {
        case VK_INDEX_TYPE_UINT8: return { VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV, 1 };
        case VK_INDEX_TYPE_UINT16: return { VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV, 2 };
        case VK_INDEX_TYPE_UINT32: return { VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_32BIT_NV, 4 };
        default: throw std::runtime_error("Unsupported cluster index type!");
        }
    }

    template<typename Task>
    auto EncodeTopology(uint32_t triangles, uint32_t vertices, VkDeviceAddress indices, VkDeviceSize stride,
        const IndexLayout& index) -> Task
    {
        CHECK_TRUE(triangles > 0 && vertices > 0 && triangles <= MaxTriangleTaskCount && vertices <= MaxTriangleTaskCount,
            "Cluster topology counts must fit the native task!");
        const auto count = VkDeviceSize{ triangles } * 3;
        CHECK_TRUE(vertices <= count && vertices <= (uint64_t{1} << (8 * index.size)),
            "Unique vertex count exceeds the number or range of indices!");
        CHECK_TRUE(stride <= UINT16_MAX && stride % index.size == 0, "Invalid cluster index stride!");
        const auto bytes = (count - 1) * (stride ? stride : index.size) + index.size;
        Task task{};
        task.triangleCount = triangles;
        task.vertexCount = vertices;
        task.indexType = index.format;
        task.opacityMicromapIndexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV;
        task.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;
        task.indexBufferStride = static_cast<uint16_t>(stride);
        task.indexBuffer = CheckAddress(indices, 0, bytes, index.size);
        return task;
    }

    auto CheckClusterReferenceAddress(VkDeviceAddress inAddress, uint32_t inCount) -> VkDeviceAddress
    {
        constexpr auto maximum = (std::numeric_limits<VkDeviceAddress>::max)();
        const auto bytes = inCount * ReferenceStride;
        CHECK_TRUE(inAddress != 0 && inAddress % ReferenceStride == 0 && bytes <= maximum - inAddress,
            "Cluster BLAS reference address is invalid, overflows or is misaligned!");
        return inAddress;
    }

    auto QueryNativeSizes(const MyDevice& device, const VkClusterAccelerationStructureInputInfoNV& input)
        -> VkAccelerationStructureBuildSizesInfoKHR
    {
        VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetClusterAccelerationStructureBuildSizesNV(device.vkDevice, &input, &sizes);
        sizes.pNext = nullptr;
        return sizes;
    }

    void CheckTriangleLimits(const VkClusterAccelerationStructureTriangleClusterInputNV& limits)
    {
        CHECK_TRUE(limits.maxClusterTriangleCount && limits.maxClusterVertexCount &&
            limits.maxTotalTriangleCount && limits.maxTotalVertexCount,
            "All cluster batch capacities must be nonzero!");
        CHECK_TRUE(limits.vertexFormat != VK_FORMAT_UNDEFINED && limits.vertexFormat != VK_FORMAT_MAX_ENUM,
            "A cluster vertex format is required!");
    }

    void CheckTriangleDeviceLimits(const VkClusterAccelerationStructureTriangleClusterInputNV& limits,
        const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV& properties, VkPhysicalDevice device)
    {
        CHECK_TRUE(limits.maxClusterTriangleCount <= (std::min)(properties.maxTrianglesPerCluster, MaxTriangleTaskCount),
            "Cluster batch triangle capacity exceeds the per-cluster limit!");
        CHECK_TRUE(limits.maxClusterVertexCount <= (std::min)(properties.maxVerticesPerCluster, MaxTriangleTaskCount),
            "Cluster batch vertex capacity exceeds the per-cluster limit!");
        QueryVertexLayout(device, limits.vertexFormat);
    }

    auto QuerySizes(const MyDevice& inDevice, const VkClusterAccelerationStructureInputInfoNV& inInput,
        VkDeviceSize inTaskStride, const ClusterProperties& inProperties) -> AccelStructMemoryRequirements
    {
        const auto scratchAlignment = inProperties.GetScratchAlignment();
        const auto sizes = QueryNativeSizes(inDevice, inInput);
        const auto buildInfoSize = inInput.maxAccelerationStructureCount * inTaskStride;
        const bool sizing = inInput.opMode == ComputeSizes;
        const auto arraySize = inInput.maxAccelerationStructureCount * (sizing ? sizeof(uint32_t) : ReferenceStride);
        const auto limit = inProperties.maxBufferAllocation;
        CHECK_TRUE(sizes.buildScratchSize <= limit && buildInfoSize <= limit && arraySize <= limit,
            "Cluster batch exceeds device buffer limits; split the batch!");
        AccelStructMemoryRequirements requirements;
        requirements.buildScratchBufferSize = sizes.buildScratchSize;
        requirements.scratchAlignment = scratchAlignment;
        if (sizing) return requirements;
        CHECK_TRUE(sizes.accelerationStructureSize != 0 && sizes.accelerationStructureSize <= limit,
            "Device returned an invalid cluster destination size!");
        const auto& cluster = inProperties.cluster;
        const auto alignment = inInput.opType == ClusterBlas ? cluster.clusterBottomLevelByteAlignment :
            inInput.opType == Template ? cluster.clusterTemplateByteAlignment : cluster.clusterByteAlignment;
        CHECK_TRUE(std::has_single_bit(alignment), "Invalid cluster output alignment!");
        requirements.accelerationStructureSize = sizes.accelerationStructureSize;
        requirements.accelerationStructureAlignment = alignment;
        return requirements;
    }

    void CheckUploadBuffer(const Buffer& inDst, VkDeviceSize inOffset, VkDeviceSize inBytes)
    {
        const auto& buffer = inDst.GetBufferInformation();
        CHECK_TRUE(inDst.IsValid(), "Cluster build info requires a created Buffer!");
        CHECK_TRUE(inOffset <= buffer.size && inBytes <= buffer.size - inOffset,
            "Cluster build info upload range is out of bounds!");
        constexpr auto hostLimit = (std::numeric_limits<size_t>::max)();
        CHECK_TRUE(inOffset <= hostLimit && inBytes <= hostLimit - static_cast<size_t>(inOffset),
            "Cluster build info upload range exceeds host address space!");
    }

    template<class NativeRecord, class Info, class GetReference>
    void UploadReferenceTasks(std::span<const Info> infos, Buffer& destination,
        VkDeviceSize offset, GetReference getReference)
    {
        if (infos.empty()) return;
        constexpr auto layout = Info::GetBuildInfoLayout();
        static_assert(sizeof(NativeRecord) == layout.recordStride);
        CHECK_TRUE(infos.size() <= (std::numeric_limits<size_t>::max)() / layout.recordStride,
            "Cluster reference task upload size overflows!");
        const auto bytes = VkDeviceSize{infos.size()} * layout.recordStride;
        CHECK_TRUE(offset % layout.addressAlignment == 0, "Cluster reference task offset is misaligned!");
        CheckUploadBuffer(destination, offset, bytes);
        std::vector<NativeRecord> tasks;
        tasks.reserve(infos.size());
        for (const auto& info : infos)
        {
            const auto reference = getReference(info);
            CHECK_TRUE(reference, "Cluster reference task requires a nonzero reference!");
            tasks.push_back({reference});
        }
        Buffer::UploadToDeviceInline(destination, offset, tasks.data(), static_cast<size_t>(bytes));
    }

    auto MakeTarget(uint32_t count, VkClusterAccelerationStructureOpModeNV mode,
        VkClusterAccelerationStructureOpTypeNV operation, VkDeviceAddress storage,
        VkDeviceAddress references, VkDeviceAddress outputSizes) -> VulkanDetail::ClusterBuildTarget
    {
        VulkanDetail::ClusterTaskLayout(operation).GetBufferSize(count);
        CHECK_TRUE(mode == Implicit || mode == Explicit || mode == ComputeSizes, "Invalid cluster operation mode!");
        if (mode == Implicit) CheckAddress(storage, 0, 1, 1);
        else CHECK_TRUE(!storage, "Only implicit cluster targets have contiguous storage!");
        if (mode != ComputeSizes) CheckAddress(references, 0, VkDeviceSize{count} * ReferenceStride, ReferenceStride);
        else CHECK_TRUE(!references, "Compute Sizes has no destination reference table!");
        if (mode == ComputeSizes) CheckAddress(outputSizes, 0, VkDeviceSize{count} * sizeof(uint32_t), sizeof(uint32_t));
        else CHECK_TRUE(!outputSizes, "Select optional cluster size output after selecting the build target!");
        VulkanDetail::ClusterBuildTarget target;
        target.storage = storage;
        target.references = references;
        target.sizes = outputSizes;
        target.capacity = count;
        target.mode = mode;
        target.operation = operation;
        return target;
    }
}

auto VulkanDetail::ClusterTaskLayout(VkClusterAccelerationStructureOpTypeNV operation) -> AccelStructBuildInfoLayout
{
    switch (operation)
    {
    case Direct: return TriangleTaskLayout;
    case Instantiate: return InstanceTaskLayout;
    case ClusterBlas: return ClusterBlasTaskLayout;
    case Template: return ClusterLevelAccelStructTemplateBuildInfo::GetBuildInfoLayout();
    case Move: return ClusterAccelStructMoveInfo::GetBuildInfoLayout();
    case QueryTemplateIndices: return ClusterTemplateIndicesQueryInfo::GetBuildInfoLayout();
    default: throw std::runtime_error("Unsupported cluster command operation!");
    }
}

auto VulkanDetail::CreateClusterDestination(const AccelStructMemoryRequirements& sizes) -> std::unique_ptr<Buffer>
{
    const auto alignment = sizes.accelerationStructureAlignment;
    CHECK_TRUE(std::has_single_bit(alignment), "Device returned an invalid cluster output alignment!");
    const auto allocator = MyDevice::GetInstance().GetMemoryAllocator();
    CHECK_TRUE(allocator && allocator->IsValid() && vkGetBufferDeviceAddress && vkCreateBuffer &&
        vkDestroyBuffer && vkGetBufferMemoryRequirements, "Cluster allocation dependencies are unavailable!");
    BufferCreateInfo info;
    info.SetBufferSize(sizes.accelerationStructureSize).SetBufferUsage(StorageUsage)
        .CustomizeAlignment(alignment).CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto buffer = std::make_unique<Buffer>();
    buffer->Create(info);
    CheckAddress(buffer->GetDeviceAddress(), 0, sizes.accelerationStructureSize, alignment);
    return buffer;
}

void ClusterAccelStructMoveInfo::CommitToDeviceInline(
    std::span<const ClusterAccelStructMoveInfo> infos, Buffer& destination, VkDeviceSize offset)
{
    UploadReferenceTasks<VkClusterAccelerationStructureMoveObjectsInfoNV>(infos, destination, offset,
        [](const ClusterAccelStructMoveInfo& info) -> VkDeviceAddress { return info.m_source; });
}

void ClusterTemplateIndicesQueryInfo::CommitToDeviceInline(
    std::span<const ClusterTemplateIndicesQueryInfo> infos, Buffer& destination, VkDeviceSize offset)
{
    UploadReferenceTasks<VulkanDetail::ClusterTemplateIndicesTask>(infos, destination, offset,
        [](const ClusterTemplateIndicesQueryInfo& info) -> VkDeviceAddress { return info.m_template; });
}

void ClusterLevelAccelStructTemplateBuildInfo::SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount)
{
    CHECK_TRUE(inIndexCount % 3 == 0, "Cluster index count must be a multiple of three!");
    m_indices = inAddress;
    m_triangles = inIndexCount / 3;
}

void ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(
    std::span<const ClusterLevelAccelStructTemplateBuildInfo> infos, Buffer& destination, VkDeviceSize offset)
{
    if (infos.empty()) return;
    constexpr auto layout = GetBuildInfoLayout();
    CHECK_TRUE(infos.size() <= (std::numeric_limits<size_t>::max)() / layout.recordStride,
        "Template task upload size overflows!");
    const auto bytes = VkDeviceSize(infos.size()) * layout.recordStride;
    CHECK_TRUE(offset % layout.addressAlignment == 0, "Template task offset is misaligned!");
    CheckUploadBuffer(destination, offset, bytes);
    std::vector<TemplateTask> tasks;
    tasks.reserve(infos.size());
    for (const auto& info : infos)
    {
        tasks.push_back(EncodeTopology<TemplateTask>(info.m_triangles, info.m_vertices, info.m_indices,
            info.m_indexStride, GetIndexLayout(info.m_indexType)));
    }
    Buffer::UploadToDeviceInline(destination, offset, tasks.data(), static_cast<size_t>(bytes));
}

void ClusterLevelAccelStructDirectBuildInfo::SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount)
{
    CHECK_TRUE(inIndexCount % 3 == 0, "Cluster index count must be a multiple of three!");
    m_indexAddress = inAddress;
    m_triangleCount = inIndexCount / 3;
}

void ClusterLevelAccelStructDirectBuildInfo::SetVertexBuffer(VkDeviceAddress inAddress, uint32_t inUniqueVertexCount)
{
    m_vertexAddress = inAddress;
    m_uniqueVertexCount = inUniqueVertexCount;
}

void ClusterLevelAccelStructBuildLimits::SetMaxTriangleCount(uint32_t perCluster, uint32_t total)
{
    m_limits.maxClusterTriangleCount = perCluster;
    m_limits.maxTotalTriangleCount = total;
}

void ClusterLevelAccelStructBuildLimits::SetMaxVertexCount(uint32_t perCluster, uint32_t total)
{
    m_limits.maxClusterVertexCount = perCluster;
    m_limits.maxTotalVertexCount = total;
}

void ClusterLevelAccelStructBuildLimits::CustomizeVertexFormat(VkFormat format)
{ m_limits.vertexFormat = format; }

auto VulkanDetail::ClusterSizeQuery::Get(const ClusterLevelAccelStructBuildLimits& layout,
    VkClusterAccelerationStructureOpTypeNV operation, VkClusterAccelerationStructureOpModeNV mode, uint32_t count) -> AccelStructMemoryRequirements
{
    auto limits = layout.m_limits;
    CHECK_TRUE(count, "Cluster maximum task count must be positive!");
    CheckTriangleLimits(limits);
    auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    CHECK_TRUE(properties.cluster.maxClusterGeometryIndex > 0, "Device must support cluster geometry index zero!");
    CheckTriangleDeviceLimits(limits, properties.cluster, device.vkPhysicalDevice);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = operation;
    input.opMode = mode;
    input.opInput.pTriangleClusters = &limits;
    if (operation == Instantiate)
        CHECK_TRUE(std::has_single_bit(properties.cluster.clusterTemplateByteAlignment), "Invalid template alignment!");
    return QuerySizes(device, input, ClusterTaskLayout(operation).recordStride, properties);
}

void ClusterLevelAccelStructCreateInfo::SetMaxTriangleCount(uint32_t count) { m_triangles = count; }
void ClusterLevelAccelStructCreateInfo::SetMaxVertexCount(uint32_t count) { m_vertices = count; }
void ClusterLevelAccelStructCreateInfo::CustomizeVertexFormat(VkFormat format) { m_vertexFormat = format; }
void ClusterLevelAccelStructCreateInfo::CustomizeTemplateUsage(bool enabled) { m_useTemplate = enabled; }

void ClusterLevelAccelStruct::Create(const ClusterLevelAccelStructCreateInfo& info)
{
    CHECK_TRUE(!m_storage, "Cluster Create requires an empty target!");
    ClusterLevelAccelStructBuildLimits layout;
    layout.SetMaxTriangleCount(info.m_triangles, info.m_triangles);
    layout.SetMaxVertexCount(info.m_vertices, info.m_vertices);
    layout.CustomizeVertexFormat(info.m_vertexFormat);
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, info.m_useTemplate ? Instantiate : Direct, Explicit, 1);
    auto storage = VulkanDetail::CreateClusterDestination(sizes);
    const auto reference = storage->GetDeviceAddress();
    CHECK_TRUE(reference != 0, "Cluster device address is invalid!");
    m_storage = std::move(storage);
    m_reference = reference;
    m_memoryRequirements = sizes;
}

auto ClusterLevelAccelStructDirectBuildInfo::_Encode() const -> VkClusterAccelerationStructureBuildTriangleClusterInfoNV
{
    const auto vertex = GetVertexLayout(m_vertexFormat);
    const auto index = GetIndexLayout(m_indexType);
    CHECK_TRUE(m_clusterID != MaxCount, "Cluster ID is reserved!");
    CHECK_TRUE(m_vertexStride <= (std::numeric_limits<uint16_t>::max)() &&
        m_vertexStride % vertex.size == 0, "Invalid cluster vertex stride!");
    auto task = EncodeTopology<TriangleTask>(m_triangleCount, m_uniqueVertexCount, m_indexAddress, m_indexStride, index);
    const auto effectiveStride = m_vertexStride == 0 ? vertex.size : m_vertexStride;
    const auto vertexBytes = (m_uniqueVertexCount - 1) * effectiveStride + vertex.size;
    task.clusterID = m_clusterID;
    task.vertexBufferStride = static_cast<uint16_t>(m_vertexStride);
    task.vertexBuffer = CheckAddress(m_vertexAddress, 0, vertexBytes, vertex.alignment);
    return task;
}

void ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(std::span<const ClusterLevelAccelStructDirectBuildInfo> inBuildInfos,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inBuildInfos.empty()) return;
    CHECK_TRUE(inBuildInfos.size() <= (std::numeric_limits<VkDeviceSize>::max)() / TriangleTaskLayout.recordStride,
        "Direct task upload size overflows!");
    const auto bytes = VkDeviceSize{ inBuildInfos.size() } * TriangleTaskLayout.recordStride;
    CHECK_TRUE(inOffset % TriangleTaskLayout.addressAlignment == 0, "Direct task offset is misaligned!");
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    std::vector<TriangleTask> tasks;
    tasks.reserve(inBuildInfos.size());
    for (const auto& triangle : inBuildInfos)
        tasks.push_back(triangle._Encode());
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}

void ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(
    std::span<const ClusterLevelAccelStructInstantiateBuildInfo> inInfos, Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inInfos.empty()) return;
    CHECK_TRUE(inInfos.size() <= (std::numeric_limits<VkDeviceSize>::max)() / InstanceTaskLayout.recordStride,
        "Instantiate task upload size overflows!");
    const auto bytes = VkDeviceSize{inInfos.size()} * InstanceTaskLayout.recordStride;
    CHECK_TRUE(inOffset % InstanceTaskLayout.addressAlignment == 0, "Instantiate task offset is misaligned!");
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    const auto templateAlignment = QueryProperties(MyDevice::GetInstance().vkPhysicalDevice).cluster.clusterTemplateByteAlignment;
    CHECK_TRUE(std::has_single_bit(templateAlignment), "Invalid template alignment!");
    std::vector<InstanceTask> tasks;
    tasks.reserve(inInfos.size());
    for (const auto& info : inInfos)
    {
        const auto vertex = GetVertexLayout(info.m_vertexFormat);
        CHECK_TRUE(info.m_id != MaxCount && info.m_stride <= UINT32_MAX && info.m_stride % vertex.alignment == 0,
            "Invalid instance ID or stride!");
        InstanceTask task{};
        task.clusterIdOffset = info.m_id;
        task.clusterTemplateAddress = CheckAddress(info.m_template, 0, 1, templateAlignment);
        task.vertexBuffer.startAddress = CheckAddress(info.m_vertices, 0, vertex.size, vertex.alignment);
        task.vertexBuffer.strideInBytes = info.m_stride ? info.m_stride : vertex.size;
        tasks.push_back(task);
    }
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}

void ClusterBottomLevelAccelStructBuildInfo::SetClusterReferences(VkDeviceAddress inAddress, uint32_t inCount)
{
    CHECK_TRUE(inCount > 0, "Cluster BLAS reference count must be nonzero!");
    const auto address = CheckClusterReferenceAddress(inAddress, inCount);
    m_clusterReferencesAddress = address;
    m_clusterCount = inCount;
}

void ClusterBottomLevelAccelStructBuildLimits::SetMaxClusterCount(uint32_t perBlas, uint32_t total)
{
    m_limits.maxClusterCountPerAccelerationStructure = perBlas;
    m_limits.maxTotalClusterCount = total;
}

auto VulkanDetail::ClusterSizeQuery::Get(const ClusterBottomLevelAccelStructBuildLimits& layout,
    VkClusterAccelerationStructureOpModeNV mode, uint32_t count) -> AccelStructMemoryRequirements
{
    auto limits = layout.m_limits;
    CHECK_TRUE(count && limits.maxClusterCountPerAccelerationStructure && limits.maxTotalClusterCount,
        "All Cluster BLAS batch capacities must be nonzero!");
    auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    input.opMode = mode;
    input.opInput.pClustersBottomLevel = &limits;
    return QuerySizes(device, input, ClusterBlasTaskLayout.recordStride, properties);
}

void VulkanDetail::ClusterCommandState::Select(const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity,
    VkClusterAccelerationStructureOpModeNV mode, VkClusterAccelerationStructureOpTypeNV operation,
    VkDeviceAddress storage, VkDeviceAddress references, VkDeviceAddress outputSizes)
{
    CheckTriangleLimits(layout.m_limits);
    auto next = MakeTarget(capacity, mode, operation, storage, references, outputSizes);
    next.triangles = layout.m_limits;
    target = std::move(next);
}

void VulkanDetail::ClusterCommandState::Select(const ClusterAccelStructMoveLimits& layout, uint32_t capacity,
    VkClusterAccelerationStructureOpModeNV mode, VkDeviceAddress storage,
    VkDeviceAddress references, VkDeviceAddress outputSizes)
{
    CheckMoveLimits(layout.m_limits);
    auto next = MakeTarget(capacity, mode, Move, storage, references, outputSizes);
    next.move = layout.m_limits;
    target = std::move(next);
}

auto VulkanDetail::ClusterSizeQuery::GetMoveScratchRequirements(const ClusterAccelStructMoveLimits& layout,
    VkClusterAccelerationStructureOpModeNV mode, uint32_t count) -> ClusterScratchRequirements
{
    auto limits = layout.m_limits;
    CHECK_TRUE(count, "Cluster maximum task count must be positive!");
    CheckMoveLimits(limits);
    const auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = Move;
    input.opMode = mode;
    input.opInput.pMoveObjects = &limits;
    const auto sizes = QueryNativeSizes(device, input);
    // Drivers may report MOVE scratch in updateScratchSize even for a no-overlap copy.
    const auto scratchSize = (std::max)(sizes.buildScratchSize, sizes.updateScratchSize);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    CHECK_TRUE(scratchSize <= properties.maxBufferAllocation,
        "Cluster MOVE scratch exceeds Device allocation limits!");
    return { scratchSize, properties.GetScratchAlignment() };
}

auto VulkanDetail::ClusterSizeQuery::GetTemplateIndicesSizes(const ClusterLevelAccelStructBuildLimits& layout,
    VkClusterAccelerationStructureOpModeNV mode, uint32_t count) -> VkAccelerationStructureBuildSizesInfoKHR
{
    auto limits = layout.m_limits;
    CHECK_TRUE(count, "Cluster maximum task count must be positive!");
    CheckTriangleLimits(limits);
    const auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    CHECK_TRUE(device.m_clusterAccelerationStructureRevision >= ClusterTemplateIndicesMinRevision,
        "Cluster template index queries require NV cluster acceleration structure revision 3 or newer!");
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    CheckTriangleDeviceLimits(limits, properties.cluster, device.vkPhysicalDevice);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = QueryTemplateIndices;
    input.opMode = mode;
    input.opInput.pTriangleClusters = &limits;
    return QueryNativeSizes(device, input);
}

auto VulkanDetail::ClusterSizeQuery::GetMoveDestinationAlignment(VkClusterAccelerationStructureTypeNV type) -> VkDeviceSize
{
    const auto property = MoveAlignmentProperty(type);
    const auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    const auto alignment = properties.cluster.*property;
    CHECK_TRUE(std::has_single_bit(alignment), "Device returned an invalid cluster move destination alignment!");
    return alignment;
}

void VulkanDetail::ClusterCommandState::Select(const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity,
    VkClusterAccelerationStructureOpModeNV mode,
    VkDeviceAddress storage, VkDeviceAddress references, VkDeviceAddress outputSizes)
{
    CHECK_TRUE(layout.m_limits.maxClusterCountPerAccelerationStructure && layout.m_limits.maxTotalClusterCount,
        "All Cluster BLAS batch capacities must be nonzero!");
    auto next = MakeTarget(capacity, mode, ClusterBlas, storage, references, outputSizes);
    next.clusters = layout.m_limits;
    target = std::move(next);
}

auto VulkanDetail::ClusterSizeQuery::GetScratchAlignment() -> VkDeviceSize
{
    const auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    return QueryProperties(device.vkPhysicalDevice).GetScratchAlignment();
}

void ComputeClusterAccelStructSizesCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress outputSizes)
{
    m_state.Select(layout, capacity, ComputeSizes, 0, 0, outputSizes);
}

void ComputeClusterAccelStructSizesCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress outputSizes)
{
    m_state.Select(layout, capacity, ComputeSizes, Direct, 0, 0, outputSizes);
}

void ComputeClusterAccelStructSizesCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress outputSizes)
{
    m_state.Select(layout, capacity, ComputeSizes, Instantiate, 0, 0, outputSizes);
}

void ComputeClusterAccelStructSizesCommandInput::SetClusterTemplateTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress outputSizes)
{
    m_state.Select(layout, capacity, ComputeSizes, Template, 0, 0, outputSizes);
}

void ExplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, 0, references, 0);
}

void ExplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, Direct, 0, references, 0);
}

void ExplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, Instantiate, 0, references, 0);
}

void ImplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, storage, references, 0);
}

void ImplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, Direct, storage, references, 0);
}

void ImplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, Instantiate, storage, references, 0);
}

void ImplicitClusterAccelStructCommandInput::SetClusterTemplateTarget(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(limits, capacity, Implicit, Template, storage, references, 0);
}

auto ComputeClusterAccelStructSizesCommandInput::GetBottomLevelAccelStructBuildScratchRequirements(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> ClusterScratchRequirements
{
    const auto requirements = VulkanDetail::ClusterSizeQuery::Get(limits, ComputeSizes, capacity);
    return { requirements.buildScratchBufferSize, requirements.scratchAlignment };
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterBuildScratchRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> ClusterScratchRequirements
{
    const auto requirements = VulkanDetail::ClusterSizeQuery::Get(limits, Direct, ComputeSizes, capacity);
    return { requirements.buildScratchBufferSize, requirements.scratchAlignment };
}

auto ComputeClusterAccelStructSizesCommandInput::GetInstantiateClusterBuildScratchRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> ClusterScratchRequirements
{
    const auto requirements = VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, ComputeSizes, capacity);
    return { requirements.buildScratchBufferSize, requirements.scratchAlignment };
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterTemplateBuildScratchRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> ClusterScratchRequirements
{
    const auto requirements = VulkanDetail::ClusterSizeQuery::Get(limits, Template, ComputeSizes, capacity);
    return { requirements.buildScratchBufferSize, requirements.scratchAlignment };
}

auto ExplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructMemoryRequirements(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Explicit, capacity);
}

auto ExplicitClusterAccelStructCommandInput::GetClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Direct, Explicit, capacity);
}

auto ExplicitClusterAccelStructCommandInput::GetInstantiateClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, Explicit, capacity);
}

auto ImplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructMemoryRequirements(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Implicit, capacity);
}

auto ImplicitClusterAccelStructCommandInput::GetClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Direct, Implicit, capacity);
}

auto ImplicitClusterAccelStructCommandInput::GetInstantiateClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, Implicit, capacity);
}

auto ImplicitClusterAccelStructCommandInput::GetClusterTemplateMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Template, Implicit, capacity);
}

auto ComputeClusterAccelStructSizesCommandInput::GetScratchAlignment() -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetScratchAlignment();
}

void ComputeClusterAccelStructSizesCommandInput::SetMoveObjectsTarget(
    const ClusterAccelStructMoveLimits& limits, uint32_t count, VkDeviceAddress outputSizes)
{
    m_state.Select(limits, count, ComputeSizes, 0, 0, outputSizes);
}

void ComputeClusterAccelStructSizesCommandInput::SetQueryTemplateIndicesTarget(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count, VkDeviceAddress outputSizes)
{
    m_state.Select(limits, count, ComputeSizes, QueryTemplateIndices, 0, 0, outputSizes);
}

auto ComputeClusterAccelStructSizesCommandInput::GetMoveObjectsScratchRequirements(
    const ClusterAccelStructMoveLimits& limits, uint32_t count) -> ClusterScratchRequirements
{
    return VulkanDetail::ClusterSizeQuery::GetMoveScratchRequirements(limits, ComputeSizes, count);
}

auto ComputeClusterAccelStructSizesCommandInput::GetQueryTemplateIndicesBuildSizes(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count) -> VkAccelerationStructureBuildSizesInfoKHR
{
    return VulkanDetail::ClusterSizeQuery::GetTemplateIndicesSizes(limits, ComputeSizes, count);
}

void ExplicitClusterAccelStructCommandInput::SetMoveObjectsTarget(
    const ClusterAccelStructMoveLimits& limits, uint32_t count, VkDeviceAddress destinationReferences)
{
    m_state.Select(limits, count, Explicit, 0, destinationReferences, 0);
}

void ExplicitClusterAccelStructCommandInput::SetQueryTemplateIndicesTarget(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count, VkDeviceAddress destinationDataAddresses)
{
    m_state.Select(limits, count, Explicit, QueryTemplateIndices, 0, destinationDataAddresses, 0);
}

auto ExplicitClusterAccelStructCommandInput::GetMoveObjectsScratchRequirements(
    const ClusterAccelStructMoveLimits& limits, uint32_t count) -> ClusterScratchRequirements
{
    return VulkanDetail::ClusterSizeQuery::GetMoveScratchRequirements(limits, Explicit, count);
}

auto ExplicitClusterAccelStructCommandInput::GetQueryTemplateIndicesBuildSizes(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count) -> VkAccelerationStructureBuildSizesInfoKHR
{
    return VulkanDetail::ClusterSizeQuery::GetTemplateIndicesSizes(limits, Explicit, count);
}

auto ExplicitClusterAccelStructCommandInput::GetScratchAlignment() -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetScratchAlignment();
}

auto ExplicitClusterAccelStructCommandInput::GetMoveObjectsDestinationAlignment(VkClusterAccelerationStructureTypeNV type) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetMoveDestinationAlignment(type);
}

void ImplicitClusterAccelStructCommandInput::SetMoveObjectsTarget(
    const ClusterAccelStructMoveLimits& limits, uint32_t count, VkDeviceAddress storage, VkDeviceAddress outputReferences)
{
    m_state.Select(limits, count, Implicit, storage, outputReferences, 0);
}

void ImplicitClusterAccelStructCommandInput::SetQueryTemplateIndicesTarget(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count, VkDeviceAddress outputData, VkDeviceAddress outputDataAddresses)
{
    m_state.Select(limits, count, Implicit, QueryTemplateIndices, outputData, outputDataAddresses, 0);
}

auto ImplicitClusterAccelStructCommandInput::GetMoveObjectsScratchRequirements(
    const ClusterAccelStructMoveLimits& limits, uint32_t count) -> ClusterScratchRequirements
{
    return VulkanDetail::ClusterSizeQuery::GetMoveScratchRequirements(limits, Implicit, count);
}

auto ImplicitClusterAccelStructCommandInput::GetQueryTemplateIndicesBuildSizes(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t count) -> VkAccelerationStructureBuildSizesInfoKHR
{
    return VulkanDetail::ClusterSizeQuery::GetTemplateIndicesSizes(limits, Implicit, count);
}

auto ImplicitClusterAccelStructCommandInput::GetScratchAlignment() -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetScratchAlignment();
}

auto ImplicitClusterAccelStructCommandInput::GetMoveObjectsDestinationAlignment(VkClusterAccelerationStructureTypeNV type) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetMoveDestinationAlignment(type);
}

void ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(std::span<const ClusterBottomLevelAccelStructBuildInfo> inHostInfo,
    Buffer& inoutTargetBuffer, VkDeviceSize inOffset)
{
    if (inHostInfo.empty()) return;
    CHECK_TRUE(inHostInfo.size() <= (std::numeric_limits<VkDeviceSize>::max)() / ClusterBlasTaskLayout.recordStride,
        "Cluster BLAS task upload size overflows!");
    const auto bytes = VkDeviceSize{ inHostInfo.size() } * ClusterBlasTaskLayout.recordStride;
    CHECK_TRUE(CONTAIN_BITS(inoutTargetBuffer.GetBufferInformation().memoryProperty,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        "Cluster BLAS task upload requires host-visible, host-coherent memory!");
    CheckUploadBuffer(inoutTargetBuffer, inOffset, bytes);
    std::vector<ClusterBlasTask> tasks;
    tasks.reserve(inHostInfo.size());
    for (const auto& info : inHostInfo)
    {
        const auto count = info.m_clusterCount;
        CHECK_TRUE(count > 0, "Cluster BLAS reference count must be nonzero!");
        const auto address = CheckClusterReferenceAddress(info.m_clusterReferencesAddress, count);
        tasks.push_back({ count, static_cast<uint32_t>(ReferenceStride), address });
    }
    Buffer::UploadToDeviceInline(inoutTargetBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}
