#include "../cluster_acceleration_structure.h"
#include "../../device.h"
#include "../../allocator/private/memory_allocator.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <unordered_set>
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
    constexpr VkBufferUsageFlags ReferenceUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    constexpr auto Explicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
    constexpr auto Implicit = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    constexpr auto Direct = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    constexpr auto Instantiate = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
    constexpr auto ClusterBlas = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    constexpr auto ComputeSizes = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
    constexpr auto Template = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;

    struct ClusterProperties
    {
        VkPhysicalDeviceClusterAccelerationStructurePropertiesNV cluster;
        VkDeviceSize maxBufferAllocation;
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

    auto EncodeTriangle(const ClusterLevelAccelStructDirectBuildInfo& inTriangle,
        const VertexLayout& inVertex, const IndexLayout& inIndex) -> TriangleTask
    {
        CHECK_TRUE(inTriangle.clusterID != MaxCount, "Cluster ID is reserved!");
        CHECK_TRUE(inTriangle.vertexStride <= (std::numeric_limits<uint16_t>::max)() &&
            inTriangle.vertexStride % inVertex.size == 0, "Invalid cluster vertex stride!");
        auto task = EncodeTopology<TriangleTask>(inTriangle.primitiveCount, inTriangle.vertexCount,
            inTriangle.indexAddress, inTriangle.indexStride, inIndex);
        const auto vertexStride = inTriangle.vertexStride == 0 ? inVertex.size : inTriangle.vertexStride;
        const auto vertexSize = (inTriangle.vertexCount - 1) * vertexStride + inVertex.size;
        task.clusterID = inTriangle.clusterID;
        task.vertexBufferStride = static_cast<uint16_t>(inTriangle.vertexStride);
        task.vertexBuffer = CheckAddress(inTriangle.vertexAddress, 0, vertexSize, inVertex.alignment);
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

    auto QuerySizes(const MyDevice& inDevice, const VkClusterAccelerationStructureInputInfoNV& inInput,
        VkDeviceSize inTaskStride, const ClusterProperties& inProperties) -> AccelStructMemoryRequirements
    {
        CHECK_TRUE(std::has_single_bit(inProperties.cluster.clusterScratchByteAlignment),
            "Device returned an invalid cluster scratch alignment!");
        VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetClusterAccelerationStructureBuildSizesNV(inDevice.vkDevice, &inInput, &sizes);
        const auto buildInfoSize = inInput.maxAccelerationStructureCount * inTaskStride;
        const bool sizing = inInput.opMode == ComputeSizes;
        const auto arraySize = inInput.maxAccelerationStructureCount * (sizing ? sizeof(uint32_t) : ReferenceStride);
        const auto limit = inProperties.maxBufferAllocation;
        CHECK_TRUE(sizes.buildScratchSize <= limit && buildInfoSize <= limit && arraySize <= limit,
            "Cluster batch exceeds device buffer limits; split the batch!");
        AccelStructMemoryRequirements requirements;
        requirements.buildScratchBufferSize = sizes.buildScratchSize;
        requirements.scratchAlignment = inProperties.cluster.clusterScratchByteAlignment;
        if (sizing) return requirements;
        CHECK_TRUE(sizes.accelerationStructureSize != 0 && sizes.accelerationStructureSize <= limit,
            "Device returned an invalid cluster destination size!");
        const auto& cluster = inProperties.cluster;
        const auto alignment = inInput.opType == ClusterBlas ? cluster.clusterBottomLevelByteAlignment :
            inInput.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV ?
            cluster.clusterTemplateByteAlignment : cluster.clusterByteAlignment;
        CHECK_TRUE(std::has_single_bit(alignment), "Invalid cluster output alignment!");
        requirements.accelerationStructureSize = sizes.accelerationStructureSize;
        requirements.accelerationStructureAlignment = alignment;
        return requirements;
    }

    auto CreateClusterBuffer(VkDeviceSize inSize, VkBufferUsageFlags inUsage, VkDeviceSize inAlignment,
        VkMemoryPropertyFlags inMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        -> std::unique_ptr<Buffer>
    {
        CHECK_TRUE(std::has_single_bit(inAlignment), "Device returned an invalid cluster output alignment!");
        const auto allocator = MyDevice::GetInstance().GetMemoryAllocator();
        CHECK_TRUE(allocator && allocator->IsValid() && vkGetBufferDeviceAddress && vkCreateBuffer &&
            vkDestroyBuffer && vkGetBufferMemoryRequirements, "Cluster allocation dependencies are unavailable!");
        auto buffer = std::make_unique<Buffer>();
        BufferCreateInfo info;
        info.SetBufferSize(inSize).SetBufferUsage(inUsage).CustomizeAlignment(inAlignment).CustomizeMemoryProperty(inMemory);
        buffer->Create(info);
        CheckAddress(buffer->GetDeviceAddress(), 0, inSize, inAlignment);
        return buffer;
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

    auto TableAddress(const Buffer* table, VkDeviceSize offset, uint32_t count) -> VkDeviceAddress
    {
        CHECK_TRUE(table && table->IsValid() && CONTAIN_BITS(table->GetBufferInformation().usage,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT), "Invalid cluster address table usage!");
        const auto bytes = count * ReferenceStride;
        const auto size = table->GetBufferInformation().size;
        CHECK_TRUE(offset % ReferenceStride == 0 && offset <= size && bytes <= size - offset, "Invalid packed address table range!");
        return CheckAddress(table->GetDeviceAddress(), offset, bytes, ReferenceStride);
    }

    auto BuildInfoBufferSize(VkClusterAccelerationStructureOpTypeNV operation, uint32_t count) -> VkDeviceSize
    {
        CHECK_TRUE(count, "Cluster maximum task count must be positive!");
        return VkDeviceSize{count} * VulkanDetail::ClusterTaskLayout(operation).recordStride;
    }

    auto MakeTarget(uint32_t count, VkClusterAccelerationStructureOpModeNV mode,
        VkClusterAccelerationStructureOpTypeNV operation, VkDeviceAddress storage,
        VkDeviceAddress references, VkDeviceAddress outputSizes) -> VulkanDetail::ClusterBuildTarget
    {
        BuildInfoBufferSize(operation, count);
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
        target.outputSizes = outputSizes;
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
    default: throw std::runtime_error("Unsupported cluster command operation!");
    }
}

auto VulkanDetail::CreateClusterDestination(const AccelStructMemoryRequirements& sizes) -> std::unique_ptr<Buffer>
{
    return CreateClusterBuffer(sizes.accelerationStructureSize, StorageUsage, sizes.accelerationStructureAlignment);
}

void ClusterLevelAccelStructTemplateBuildInfo::SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount)
{
    CHECK_TRUE(inIndexCount % 3 == 0, "Cluster index count must be a multiple of three!");
    m_indices = inAddress;
    m_triangles = inIndexCount / 3;
}

void ClusterLevelAccelStructTemplateBatch::Create(
    const ClusterLevelAccelStructTemplateBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!m_storage && inCreateInfo.m_maxTemplateCount &&
        inCreateInfo.m_maxTriangleCountPerTemplate && inCreateInfo.m_maxVertexCountPerTemplate &&
        inCreateInfo.m_maxTotalTriangleCount && inCreateInfo.m_maxTotalVertexCount,
        "Template batch requires positive capacities and an empty target!");
    ClusterLevelAccelStructBuildLimits limits;
    limits.SetMaxTriangleCount(inCreateInfo.m_maxTriangleCountPerTemplate,
        inCreateInfo.m_maxTotalTriangleCount);
    limits.SetMaxVertexCount(inCreateInfo.m_maxVertexCountPerTemplate,
        inCreateInfo.m_maxTotalVertexCount);
    limits.CustomizeVertexFormat(inCreateInfo.m_vertexFormat);
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(limits, Template, Implicit,
        inCreateInfo.m_maxTemplateCount);
    auto storage = VulkanDetail::CreateClusterDestination(sizes);
    auto references = CreateClusterBuffer(
        VkDeviceSize(inCreateInfo.m_maxTemplateCount) * ReferenceStride,
        ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReferenceStride);
    const auto storageAddress = storage->GetDeviceAddress();
    const auto referenceAddress = references->GetDeviceAddress();
    m_limits = limits.m_limits;
    m_memoryRequirements = sizes;
    m_capacity = inCreateInfo.m_maxTemplateCount;
    m_storageAddress = storageAddress;
    m_referenceAddress = referenceAddress;
    m_storage = std::move(storage);
    m_references = std::move(references);
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
        GetVertexLayout(info.m_vertexFormat);
        tasks.push_back(EncodeTopology<TemplateTask>(info.m_triangles, info.m_vertices, info.m_indices,
            info.m_indexStride, GetIndexLayout(info.m_indexType)));
    }
    Buffer::UploadToDeviceInline(destination, offset, tasks.data(), static_cast<size_t>(bytes));
}

void ClusterLevelAccelStructDirectBuildInfo::SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount)
{
    CHECK_TRUE(inIndexCount % 3 == 0, "Cluster index count must be a multiple of three!");
    indexAddress = inAddress;
    primitiveCount = inIndexCount / 3;
}

void ClusterLevelAccelStructDirectBuildInfo::SetVertexBuffer(VkDeviceAddress inAddress, uint32_t inVertexCount)
{
    vertexAddress = inAddress;
    vertexCount = inVertexCount;
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
    const auto& limits = layout.m_limits;
    CHECK_TRUE(count && limits.maxClusterTriangleCount && limits.maxClusterVertexCount &&
        limits.maxTotalTriangleCount && limits.maxTotalVertexCount,
        "All cluster batch capacities must be nonzero!");
    auto& device = MyDevice::GetInstance();
    CheckQueryDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    CHECK_TRUE(properties.cluster.maxClusterGeometryIndex > 0, "Device must support cluster geometry index zero!");
    CHECK_TRUE(limits.maxClusterTriangleCount <= (std::min)(properties.cluster.maxTrianglesPerCluster, MaxTriangleTaskCount),
        "Cluster batch triangle capacity exceeds the per-cluster limit!");
    CHECK_TRUE(limits.maxClusterVertexCount <= (std::min)(properties.cluster.maxVerticesPerCluster, MaxTriangleTaskCount),
        "Cluster batch vertex capacity exceeds the per-cluster limit!");
    QueryVertexLayout(device.vkPhysicalDevice, limits.vertexFormat);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = operation;
    input.opMode = mode;
    auto nativeLimits = limits;
    input.opInput.pTriangleClusters = &nativeLimits;
    if (operation == Instantiate)
        CHECK_TRUE(std::has_single_bit(properties.cluster.clusterTemplateByteAlignment), "Invalid template alignment!");
    return QuerySizes(device, input, ClusterTaskLayout(operation).recordStride, properties);
}

void ClusterLevelAccelStructCreateInfo::SetMaxTriangleCount(uint32_t count) { m_triangles = count; }
void ClusterLevelAccelStructCreateInfo::SetMaxVertexCount(uint32_t count) { m_vertices = count; }
void ClusterLevelAccelStructCreateInfo::CustomizeVertexFormat(VkFormat format) { m_vertexFormat = format; }
void ClusterLevelAccelStructCreateInfo::CustomizeIndexType(VkIndexType type) { m_indexType = type; }
void ClusterLevelAccelStructCreateInfo::CustomizeTemplateUsage(bool enabled) { m_useTemplate = enabled; }

void ClusterLevelAccelStruct::Create(const ClusterLevelAccelStructCreateInfo& info)
{
    CHECK_TRUE(!m_storage, "Cluster Create requires an empty target!");
    GetIndexLayout(info.m_indexType);
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
    m_info = info;
}

void ClusterLevelAccelStructBatch::Create(const ClusterLevelAccelStructImplicitBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!m_count, "Cluster batch Create requires an empty target!");
    const auto count = inCreateInfo.m_maxClusterCount;
    GetIndexLayout(inCreateInfo.m_indexType);
    ClusterLevelAccelStructBuildLimits layout;
    layout.SetMaxTriangleCount(inCreateInfo.m_maxClusterTriangleCount, inCreateInfo.m_maxTotalTriangleCount);
    layout.SetMaxVertexCount(inCreateInfo.m_maxClusterVertexCount, inCreateInfo.m_maxTotalVertexCount);
    layout.CustomizeVertexFormat(inCreateInfo.m_vertexFormat);
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, inCreateInfo.m_useTemplate ? Instantiate : Direct, Implicit, count);
    auto storage = VulkanDetail::CreateClusterDestination(sizes);
    auto references = CreateClusterBuffer(count * ReferenceStride,
        ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReferenceStride);
    const auto storageAddress = storage->GetDeviceAddress();
    const auto referenceAddress = references->GetDeviceAddress();

    m_triangleInput = layout.m_limits;
    m_useTemplate = inCreateInfo.m_useTemplate;
    m_indexType = inCreateInfo.m_indexType;
    m_memoryRequirements = sizes;
    m_storageAddress = storageAddress;
    m_referenceAddress = referenceAddress;
    m_storageBuffer = std::move(storage);
    m_referenceBuffer = std::move(references);
    m_count = count;
}

auto ClusterLevelAccelStructBatch::GetReferenceBuffer() const noexcept -> const Buffer*
{ return m_referenceBuffer ? m_referenceBuffer.get() : m_explicitReferences.get(); }
auto ClusterLevelAccelStructBatch::GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements&
{ return m_memoryRequirements; }
auto ClusterLevelAccelStructBatch::GetCapacity() const noexcept -> uint32_t
{ return m_count; }
auto ClusterLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_memoryRequirements.scratchAlignment; }

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
    {
        const auto vertex = GetVertexLayout(triangle.vertexFormat);
        const auto index = GetIndexLayout(triangle.indexType);
        tasks.push_back(EncodeTriangle(triangle, vertex, index));
    }
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

void ClusterBottomLevelAccelStructBatch::Create(const ClusterBottomLevelAccelStructImplicitBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!m_count, "Cluster BLAS batch Create requires an empty target!");
    const auto count = inCreateInfo.m_maxAccelStructCount;
    ClusterBottomLevelAccelStructBuildLimits layout;
    layout.SetMaxClusterCount(inCreateInfo.m_maxClusterCountPerAccelStruct, inCreateInfo.m_maxTotalClusterCount);
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, Implicit, count);
    auto storage = VulkanDetail::CreateClusterDestination(sizes);
    auto references = CreateClusterBuffer(count * ReferenceStride,
        ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReferenceStride);
    const auto storageAddress = storage->GetDeviceAddress();
    const auto referenceAddress = references->GetDeviceAddress();

    m_clustersInput = layout.m_limits;
    m_memoryRequirements = sizes;
    m_storageAddress = storageAddress;
    m_referenceAddress = referenceAddress;
    m_storageBuffer = std::move(storage);
    m_referenceBuffer = std::move(references);
    m_count = count;
}

auto ClusterBottomLevelAccelStructBatch::GetReferenceBuffer() const noexcept -> const Buffer*
{ return m_referenceBuffer ? m_referenceBuffer.get() : m_explicitReferences.get(); }
auto ClusterBottomLevelAccelStructBatch::GetMemoryRequirements() const noexcept -> const AccelStructMemoryRequirements&
{ return m_memoryRequirements; }
auto ClusterBottomLevelAccelStructBatch::GetCapacity() const noexcept -> uint32_t
{ return m_count; }
auto ClusterBottomLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_memoryRequirements.scratchAlignment; }

void ClusterLevelAccelStructExplicitBatchCreateInfo::SetReferenceBuffer(Buffer& table, VkDeviceSize offset)
{
    m_references = &table;
    m_offset = offset;
}

void ClusterBottomLevelAccelStructExplicitBatchCreateInfo::SetReferenceBuffer(Buffer& table, VkDeviceSize offset)
{
    m_references = &table;
    m_offset = offset;
}

void ClusterLevelAccelStructExplicitBatchCreateInfo::SetDestinationAccelStructs(std::span<const ClusterLevelAccelStruct* const> destinations)
{ m_destinations.assign(destinations.begin(), destinations.end()); }

void ClusterBottomLevelAccelStructExplicitBatchCreateInfo::SetDestinationAccelStructs(std::span<const BottomLevelAccelStruct* const> destinations)
{ m_destinations.assign(destinations.begin(), destinations.end()); }

namespace
{
    template<class T>
    auto RetainDestinations(const std::vector<const T*>& destinations) -> std::vector<IntrusivePtr<const T>>
    {
        CHECK_TRUE(!destinations.empty() && destinations.size() <= MaxCount, "Explicit batch requires a representable, nonempty owner list!");
        std::unordered_set<const T*> unique;
        std::vector<IntrusivePtr<const T>> owners;
        owners.reserve(destinations.size());
        for (const auto* destination : destinations)
        {
            CHECK_TRUE(destination && unique.insert(destination).second, "Explicit destinations require distinct, non-null owners!");
            owners.emplace_back(destination);
        }
        return owners;
    }

    void AddBudget(uint32_t value, uint32_t& maximum, uint32_t& total)
    {
        CHECK_TRUE(value <= MaxCount - total, "Explicit batch aggregate capacity exceeds the native range!");
        maximum = (std::max)(value, maximum);
        total += value;
    }
}

void ClusterLevelAccelStructBatch::Create(const ClusterLevelAccelStructExplicitBatchCreateInfo& info)
{
    CHECK_TRUE(!m_count, "Cluster batch Create requires an empty target!");
    auto destinations = RetainDestinations(info.m_destinations);
    const auto count = static_cast<uint32_t>(destinations.size());
    const auto referenceAddress = TableAddress(info.m_references, info.m_offset, count);
    const auto& first = destinations.front()->m_info;
    ClusterLevelAccelStructBuildLimits layout;
    layout.CustomizeVertexFormat(first.m_vertexFormat);
    for (const auto& owner : destinations)
    {
        const auto& source = owner->m_info;
        CHECK_TRUE(owner->IsValid() && source.m_vertexFormat == first.m_vertexFormat &&
            source.m_indexType == first.m_indexType && source.m_useTemplate == first.m_useTemplate,
            "Explicit CLAS owners must be created with matching vertex/index formats and build operation!");
        AddBudget(source.m_triangles, layout.m_limits.maxClusterTriangleCount, layout.m_limits.maxTotalTriangleCount);
        AddBudget(source.m_vertices, layout.m_limits.maxClusterVertexCount, layout.m_limits.maxTotalVertexCount);
    }
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, first.m_useTemplate ? Instantiate : Direct, Explicit, count);
    m_memoryRequirements = sizes;
    m_triangleInput = layout.m_limits;
    m_useTemplate = first.m_useTemplate;
    m_indexType = first.m_indexType;
    m_explicitReferences.reset(info.m_references);
    m_referenceOffset = info.m_offset;
    m_referenceAddress = referenceAddress;
    m_destinations = std::move(destinations);
    m_count = count;
}

void ClusterBottomLevelAccelStructBatch::Create(const ClusterBottomLevelAccelStructExplicitBatchCreateInfo& info)
{
    CHECK_TRUE(!m_count, "Cluster BLAS batch Create requires an empty target!");
    auto destinations = RetainDestinations(info.m_destinations);
    const auto count = static_cast<uint32_t>(destinations.size());
    const auto referenceAddress = TableAddress(info.m_references, info.m_offset, count);
    ClusterBottomLevelAccelStructBuildLimits layout;
    for (const auto& source : destinations)
    {
        CHECK_TRUE(source->m_clusterCount && source->GetDeviceAddress(), "Explicit Cluster BLAS requires cluster-backed owners!");
        AddBudget(source->m_clusterCount, layout.m_limits.maxClusterCountPerAccelerationStructure, layout.m_limits.maxTotalClusterCount);
    }
    const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, Explicit, count);
    m_memoryRequirements = sizes;
    m_clustersInput = layout.m_limits;
    m_explicitReferences.reset(info.m_references);
    m_referenceOffset = info.m_offset;
    m_referenceAddress = referenceAddress;
    m_destinations = std::move(destinations);
    m_count = count;
}

void VulkanDetail::ClusterCommandState::Select(const ClusterLevelAccelStructBatch& batch,
    VkClusterAccelerationStructureOpModeNV mode, VkClusterAccelerationStructureOpTypeNV operation)
{
    CHECK_TRUE(operation == (batch.m_useTemplate ? Instantiate : Direct), "Cluster Batch operation does not match the command!");
    CHECK_TRUE((mode == Implicit) == bool(batch.m_storageBuffer), "Batch destination mode does not match the command!");
    auto next = MakeTarget(batch.m_count, mode, operation, batch.m_storageAddress, batch.m_referenceAddress, 0);
    next.triangles = batch.m_triangleInput;
    target = std::move(next);
}

void VulkanDetail::ClusterCommandState::Select(const ClusterBottomLevelAccelStructBatch& batch, VkClusterAccelerationStructureOpModeNV mode)
{
    CHECK_TRUE((mode == Implicit) == bool(batch.m_storageBuffer), "Batch destination mode does not match the command!");
    auto next = MakeTarget(batch.m_count, mode, ClusterBlas, batch.m_storageAddress, batch.m_referenceAddress, 0);
    next.clusters = batch.m_clustersInput;
    target = std::move(next);
}

void VulkanDetail::ClusterCommandState::Select(
    const ClusterLevelAccelStructTemplateBatch& batch, VkClusterAccelerationStructureOpModeNV mode)
{
    CHECK_TRUE(mode == Implicit && batch.IsValid() && batch.GetCapacity() != 0,
        "Template batch destination mode is invalid!");
    auto next = MakeTarget(batch.m_capacity, mode, Template, batch.m_storageAddress, batch.m_referenceAddress, 0);
    next.triangles = batch.m_limits;
    target = std::move(next);
}

void VulkanDetail::ClusterCommandState::Select(const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity,
    VkClusterAccelerationStructureOpModeNV mode, VkClusterAccelerationStructureOpTypeNV operation,
    VkDeviceAddress storage, VkDeviceAddress references, VkDeviceAddress outputSizes)
{
    CHECK_TRUE(layout.m_limits.maxClusterTriangleCount && layout.m_limits.maxClusterVertexCount &&
        layout.m_limits.maxTotalTriangleCount && layout.m_limits.maxTotalVertexCount,
        "All cluster batch capacities must be nonzero!");
    CHECK_TRUE(layout.m_limits.vertexFormat != VK_FORMAT_UNDEFINED && layout.m_limits.vertexFormat != VK_FORMAT_MAX_ENUM,
        "A cluster vertex format is required!");
    auto next = MakeTarget(capacity, mode, operation, storage, references, outputSizes);
    next.triangles = layout.m_limits;
    target = std::move(next);
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
    const auto alignment = QueryProperties(device.vkPhysicalDevice).cluster.clusterScratchByteAlignment;
    CHECK_TRUE(std::has_single_bit(alignment), "Device returned an invalid cluster scratch alignment!");
    return alignment;
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

void BuildExplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, 0, references, 0);
}

void BuildExplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Explicit);
}

void BuildExplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, Direct, 0, references, 0);
}

void BuildExplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Explicit, Direct);
}

void BuildExplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Explicit, Instantiate, 0, references, 0);
}

void BuildExplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Explicit, Instantiate);
}

void BuildImplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, storage, references, 0);
}

void BuildImplicitClusterAccelStructCommandInput::SetBottomLevelAccelStructTarget(
    const ClusterBottomLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Implicit);
}

void BuildImplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, Direct, storage, references, 0);
}

void BuildImplicitClusterAccelStructCommandInput::SetClusterTarget(
    const ClusterLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Implicit, Direct);
}

void BuildImplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBuildLimits& layout, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(layout, capacity, Implicit, Instantiate, storage, references, 0);
}

void BuildImplicitClusterAccelStructCommandInput::SetInstantiateClusterTarget(
    const ClusterLevelAccelStructBatch& batch)
{
    m_state.Select(batch, Implicit, Instantiate);
}

void BuildImplicitClusterAccelStructCommandInput::SetClusterTemplateTarget(
    const ClusterLevelAccelStructTemplateBatch& batch)
{
    m_state.Select(batch, Implicit);
}

void BuildImplicitClusterAccelStructCommandInput::SetClusterTemplateTarget(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity, VkDeviceAddress storage, VkDeviceAddress references)
{
    m_state.Select(limits, capacity, Implicit, Template, storage, references, 0);
}

auto ComputeClusterAccelStructSizesCommandInput::GetBottomLevelAccelStructBuildScratchBufferSize(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, ComputeSizes, capacity).buildScratchBufferSize;
}

auto ComputeClusterAccelStructSizesCommandInput::GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(ClusterBlas, capacity);
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterBuildScratchBufferSize(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Direct, ComputeSizes, capacity).buildScratchBufferSize;
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Direct, capacity);
}

auto ComputeClusterAccelStructSizesCommandInput::GetInstantiateClusterBuildScratchBufferSize(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, ComputeSizes, capacity).buildScratchBufferSize;
}

auto ComputeClusterAccelStructSizesCommandInput::GetInstantiateClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Instantiate, capacity);
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterTemplateBuildScratchBufferSize(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Template, ComputeSizes, capacity).buildScratchBufferSize;
}

auto ComputeClusterAccelStructSizesCommandInput::GetClusterTemplateBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Template, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructMemoryRequirements(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Explicit, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(ClusterBlas, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Direct, Explicit, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Direct, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetInstantiateClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, Explicit, capacity);
}

auto BuildExplicitClusterAccelStructCommandInput::GetInstantiateClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Instantiate, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructMemoryRequirements(
    const ClusterBottomLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Implicit, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetBottomLevelAccelStructBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(ClusterBlas, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Direct, Implicit, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Direct, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetInstantiateClusterMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Instantiate, Implicit, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetInstantiateClusterBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Instantiate, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetClusterTemplateMemoryRequirements(
    const ClusterLevelAccelStructBuildLimits& limits, uint32_t capacity) -> AccelStructMemoryRequirements
{
    return VulkanDetail::ClusterSizeQuery::Get(limits, Template, Implicit, capacity);
}

auto BuildImplicitClusterAccelStructCommandInput::GetClusterTemplateBuildInfoBufferSize(uint32_t capacity) -> VkDeviceSize
{
    return BuildInfoBufferSize(Template, capacity);
}

auto ComputeClusterAccelStructSizesCommandInput::GetScratchAlignment() -> VkDeviceSize
{
    return VulkanDetail::ClusterSizeQuery::GetScratchAlignment();
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
