#include "acceleration_structure.h"
#include "device.h"
#include "../allocator/private/memory_allocator.h"
#include "../render_context/device_context.h"
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
    constexpr VkDeviceSize ReferenceStride = sizeof(VkDeviceAddress);
    constexpr auto MaxCount = (std::numeric_limits<uint32_t>::max)();
    constexpr uint32_t MaxTriangleTaskCount = (1u << 9) - 1; // Native triangleCount and vertexCount bit widths.
    constexpr VkBufferUsageFlags StorageUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VkBufferUsageFlags ReferenceUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

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

    void CheckCreationDependencies(MyDevice& inDevice, bool inFeatureEnabled)
    {
        CHECK_TRUE(inFeatureEnabled && inDevice.vkDevice != VK_NULL_HANDLE &&
            inDevice.GetMemoryAllocator() && inDevice.GetMemoryAllocator()->IsValid(),
            "Cluster acceleration structure creation requires an initialized Device with the NV feature enabled!");
        CHECK_TRUE(vkGetClusterAccelerationStructureBuildSizesNV && vkGetBufferDeviceAddress &&
            vkCreateBuffer && vkDestroyBuffer && vkGetBufferMemoryRequirements,
            "Cluster acceleration structure creation requires size, address and buffer entrypoints!");
    }

    auto CheckAddress(VkDeviceAddress inBase, VkDeviceSize inOffset, VkDeviceSize inSize,
        VkDeviceSize inAlignment) -> VkDeviceAddress
    {
        constexpr auto maxAddress = (std::numeric_limits<VkDeviceAddress>::max)();
        CHECK_TRUE(inBase != 0 && inOffset <= maxAddress - inBase && inSize != 0,
            "Cluster buffer address range is invalid!");
        const auto address = inBase + inOffset;
        CHECK_TRUE(inSize - 1 <= maxAddress - address && address % inAlignment == 0,
            "Cluster buffer address range overflows or is misaligned!");
        return address;
    }

    struct VertexLayout
    {
        VkDeviceSize size;
        VkDeviceSize alignment;
    };

    auto QueryVertexLayout(VkPhysicalDevice inDevice, VkFormat inFormat) -> VertexLayout
    {
        CHECK_TRUE(vkGetPhysicalDeviceFormatProperties && inFormat != VK_FORMAT_UNDEFINED && inFormat != VK_FORMAT_MAX_ENUM,
            "A cluster vertex format and format query entrypoint are required!");
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(inDevice, inFormat, &properties);
        CHECK_TRUE(CONTAIN_BITS(properties.bufferFeatures, VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR),
            "Vertex format does not support acceleration structure input!");
        const auto format = static_cast<vk::Format>(inFormat);
        const auto size = vk::blockSize(format);
        const auto packedBits = vk::packed(format);
        const VkDeviceSize alignment = (packedBits == 0 ? vk::componentBits(format, 0) : packedBits) / 8;
        CHECK_TRUE(size > 0 && alignment > 0, "Invalid cluster vertex format layout!");
        return { size, alignment };
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
        const VkClusterAccelerationStructureTriangleClusterInputNV& inLimits, const VertexLayout& inVertex,
        const IndexLayout& inIndex) -> TriangleTask
    {
        CHECK_TRUE(inTriangle.clusterID != MaxCount, "Cluster ID is reserved!");
        CHECK_TRUE(inTriangle.primitiveCount > 0 && inTriangle.primitiveCount <= inLimits.maxClusterTriangleCount,
            "Invalid triangle count for a cluster!");
        CHECK_TRUE(inTriangle.vertexCount > 0 && inTriangle.vertexCount <= inLimits.maxClusterVertexCount,
            "Invalid vertex count for a cluster!");
        CHECK_TRUE(inTriangle.vertexStride <= (std::numeric_limits<uint16_t>::max)() &&
            inTriangle.vertexStride % inVertex.size == 0, "Invalid cluster vertex stride!");
        const auto vertexStride = inTriangle.vertexStride == 0 ? inVertex.size : inTriangle.vertexStride;
        const auto vertexSize = (inTriangle.vertexCount - 1) * vertexStride + inVertex.size;
        auto task = EncodeTopology<TriangleTask>(inTriangle.primitiveCount, inTriangle.vertexCount,
            inTriangle.indexAddress, inTriangle.indexStride, inIndex);
        task.clusterID = inTriangle.clusterID;
        task.vertexBufferStride = static_cast<uint16_t>(inTriangle.vertexStride);
        task.vertexBuffer = CheckAddress(inTriangle.vertexAddress, 0, vertexSize, inVertex.alignment);
        return task;
    }

    auto CheckClusterReferenceAddress(VkDeviceAddress inBase, uint32_t inOffset, uint32_t inCount) -> VkDeviceAddress
    {
        constexpr auto maximum = (std::numeric_limits<VkDeviceAddress>::max)();
        const auto offset = inOffset * ReferenceStride;
        const auto bytes = inCount * ReferenceStride;
        CHECK_TRUE(inBase != 0 && offset <= maximum - inBase, "Cluster BLAS reference address is invalid!");
        const auto address = inBase + offset;
        CHECK_TRUE(address % ReferenceStride == 0 && bytes <= maximum - address,
            "Cluster BLAS reference address range overflows or is misaligned!");
        return address;
    }

    auto QuerySizes(const MyDevice& inDevice, const VkClusterAccelerationStructureInputInfoNV& inInput,
        VkDeviceSize inTaskStride, const ClusterProperties& inProperties) -> AccelStructSizesInfo
    {
        CHECK_TRUE(std::has_single_bit(inProperties.cluster.clusterScratchByteAlignment),
            "Device returned an invalid cluster scratch alignment!");
        VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetClusterAccelerationStructureBuildSizesNV(inDevice.vkDevice, &inInput, &sizes);
        const auto buildInfoSize = inInput.maxAccelerationStructureCount * inTaskStride;
        const auto referenceSize = inInput.maxAccelerationStructureCount * ReferenceStride;
        const auto limit = inProperties.maxBufferAllocation;
        CHECK_TRUE(sizes.accelerationStructureSize != 0, "Device returned an empty cluster acceleration structure size!");
        CHECK_TRUE(sizes.accelerationStructureSize <= limit && sizes.buildScratchSize <= limit &&
            buildInfoSize <= limit && referenceSize <= limit, "Cluster batch exceeds device buffer limits; split the batch!");
        return { 0, sizes.buildScratchSize, inProperties.cluster.clusterScratchByteAlignment,
            buildInfoSize, sizes.accelerationStructureSize };
    }

    auto CreateClusterBuffer(VkDeviceSize inSize, VkBufferUsageFlags inUsage, VkDeviceSize inAlignment,
        VkMemoryPropertyFlags inMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        -> std::unique_ptr<Buffer>
    {
        CHECK_TRUE(std::has_single_bit(inAlignment), "Device returned an invalid cluster output alignment!");
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

    auto SnapshotAddresses(const Buffer& storage, const Buffer& references, const AccelStructSizesInfo& sizes,
        uint32_t capacity) -> VulkanDetail::ClusterBuildAddresses
    {
        CHECK_TRUE(storage.IsValid() && references.IsValid() && sizes.accelerationStructureSize <= storage.GetBufferInformation().size,
            "Invalid cluster output buffers!");
        const auto referenceSize = references.GetBufferInformation().size;
        CHECK_TRUE(capacity * ReferenceStride <= referenceSize, "Insufficient cluster address table size!");
        return { sizes, CheckAddress(storage.GetDeviceAddress(), 0, sizes.accelerationStructureSize, 1),
            CheckAddress(references.GetDeviceAddress(), 0, referenceSize, ReferenceStride), referenceSize, capacity };
    }
}

void ClusterLevelAccelStructDirectBuildIndirectInfo::SetTarget(const ClusterLevelAccelStructBatch& inBatch)
{
    CHECK_TRUE(inBatch.GetCapacity() && !inBatch.m_useTemplate, "Direct CLAS requires a matching created target!");
    m_addresses = SnapshotAddresses(*inBatch.m_storageBuffer, *inBatch.m_referenceBuffer, inBatch.GetSizesInfo(), inBatch.GetCapacity());
    m_limits = inBatch.GetBuildLimits();
}

void ClusterLevelAccelStructInstantiateBuildIndirectInfo::SetTarget(const ClusterLevelAccelStructBatch& inBatch)
{
    CHECK_TRUE(inBatch.GetCapacity() && inBatch.m_useTemplate, "Instantiate CLAS requires a matching created target!");
    m_addresses = SnapshotAddresses(*inBatch.m_storageBuffer, *inBatch.m_referenceBuffer, inBatch.GetSizesInfo(), inBatch.GetCapacity());
    m_limits = inBatch.GetBuildLimits();
}

void BottomLevelAccelStructBuildIndirectInfo::SetTarget(const BottomLevelAccelStructBatch& inBatch)
{
    CHECK_TRUE(inBatch.GetCapacity(), "Cluster BLAS requires a matching created target!");
    m_addresses = SnapshotAddresses(*inBatch.m_storageBuffer, *inBatch.m_referenceBuffer, inBatch.GetSizesInfo(), inBatch.GetCapacity());
    m_limits = inBatch.GetBuildLimits();
}

void ClusterLevelAccelStructTemplateInfo::SetIndexBuffer(VkDeviceAddress inAddress, uint32_t inIndexCount)
{
    CHECK_TRUE(inIndexCount % 3 == 0, "Cluster index count must be a multiple of three!");
    m_indices = inAddress;
    m_triangles = inIndexCount / 3;
}

void MyDevice::_RequireClusterBuildQueue(QueueFamilyType inQueue) const
{
    CHECK_TRUE(vkPhysicalDevice != VK_NULL_HANDLE && vkGetPhysicalDeviceQueueFamilyProperties != nullptr,
        "Cluster builds require initialized physical device queue properties!");
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &count, properties.data());
    const auto family = GetQueueFamilyIndexOfType(inQueue);
    CHECK_TRUE(family < count && (properties[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0,
        "Cluster builds require a compute-capable queue family!");
}

void MyDevice::CreateClusterLevelAccelStructTemplatesAndWait(std::span<const ClusterLevelAccelStructTemplateInfo> inInfos,
    ClusterLevelAccelStructTemplateBatch& outBatch)
{
    _CreateClusterLevelAccelStructTemplatesAndWait(nullptr, QueueFamilyType::COMPUTE, inInfos, outBatch);
}

void MyDevice::CreateClusterLevelAccelStructTemplatesAndWait(DeviceContext& inContext, QueueFamilyType inQueue,
    std::span<const ClusterLevelAccelStructTemplateInfo> inInfos, ClusterLevelAccelStructTemplateBatch& outBatch)
{
    _CreateClusterLevelAccelStructTemplatesAndWait(&inContext, inQueue, inInfos, outBatch);
}

void MyDevice::_CreateClusterLevelAccelStructTemplatesAndWait(DeviceContext* inContext, QueueFamilyType inQueue,
    std::span<const ClusterLevelAccelStructTemplateInfo> inInfos, ClusterLevelAccelStructTemplateBatch& outBatch)
{
    CHECK_TRUE(outBatch.GetCount() == 0 && !inInfos.empty() && inInfos.size() <= MaxCount,
        "Template creation requires a nonempty input and an empty output!");
    CheckCreationDependencies(*this, m_clusterAccelerationStructureEnabled);
    CHECK_TRUE(vkCmdBuildClusterAccelerationStructureIndirectNV && vkCmdPipelineBarrier2,
        "Template creation requires cluster build and synchronization entrypoints!");
    _RequireClusterBuildQueue(inQueue);
    const auto properties = QueryProperties(vkPhysicalDevice);
    CHECK_TRUE(properties.cluster.maxClusterGeometryIndex > 0, "Device must support template geometry index zero!");
    const auto format = inInfos.front().m_vertexFormat;
    const auto indexType = inInfos.front().m_indexType;
    QueryVertexLayout(vkPhysicalDevice, format);
    const auto index = GetIndexLayout(indexType);
    VkClusterAccelerationStructureTriangleClusterInputNV limits{
        VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
    limits.vertexFormat = format;
    limits.maxClusterUniqueGeometryCount = 1;
    std::vector<TemplateTask> tasks;
    std::vector<ClusterLevelAccelStructTemplateBatch::Metadata> metadata;
    tasks.reserve(inInfos.size());
    metadata.reserve(inInfos.size());
    for (const auto& source : inInfos)
    {
        CHECK_TRUE(source.m_vertexFormat == format && source.m_indexType == indexType, "Template batch formats must match!");
        CHECK_TRUE(source.m_triangles <= properties.cluster.maxTrianglesPerCluster &&
            source.m_vertices <= properties.cluster.maxVerticesPerCluster, "Template topology exceeds device limits!");
        CHECK_TRUE(source.m_triangles <= MaxCount - limits.maxTotalTriangleCount &&
            source.m_vertices <= MaxCount - limits.maxTotalVertexCount, "Template batch totals exceed native limits!");
        tasks.push_back(EncodeTopology<TemplateTask>(source.m_triangles, source.m_vertices, source.m_indices,
            source.m_indexStride, index));
        metadata.push_back({source.m_triangles, source.m_vertices});
        limits.maxClusterTriangleCount = (std::max)(limits.maxClusterTriangleCount, source.m_triangles);
        limits.maxClusterVertexCount = (std::max)(limits.maxClusterVertexCount, source.m_vertices);
        limits.maxTotalTriangleCount += source.m_triangles;
        limits.maxTotalVertexCount += source.m_vertices;
    }

    VkClusterAccelerationStructureCommandsInfoNV build{VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV};
    build.input = {VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV};
    build.input.maxAccelerationStructureCount = static_cast<uint32_t>(tasks.size());
    build.input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
    build.input.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    build.input.opInput.pTriangleClusters = &limits;
    const auto sizes = QuerySizes(*this, build.input, sizeof(TemplateTask), properties);
    const auto alignment = properties.cluster.clusterTemplateByteAlignment;
    auto storage = CreateClusterBuffer(sizes.accelerationStructureSize, StorageUsage, alignment);
    constexpr auto hostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const auto referenceBytes = tasks.size() * ReferenceStride;
    auto references = CreateClusterBuffer(referenceBytes, ReferenceUsage, ReferenceStride, hostMemory);
    auto arguments = CreateClusterBuffer(sizes.buildInfoBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, alignof(TemplateTask), hostMemory);
    Buffer::UploadToDeviceInline(*arguments, 0, tasks.data(), tasks.size() * sizeof(TemplateTask));
    std::unique_ptr<Buffer> scratch;
    if (sizes.buildScratchBufferSize)
        scratch = CreateClusterBuffer(sizes.buildScratchBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizes.scratchAlignment);
    build.dstImplicitData = storage->GetDeviceAddress();
    build.dstAddressesArray = {references->GetDeviceAddress(), ReferenceStride, referenceBytes};
    build.dstSizesArray.stride = sizeof(uint32_t);
    build.scratchData = scratch ? scratch->GetDeviceAddress() : 0;
    build.srcInfosArray = {arguments->GetDeviceAddress(), sizeof(TemplateTask), sizes.buildInfoBufferSize};

    const auto record = [&build](VkCommandBuffer cmd) {
        // Submission makes coherent Host writes available. The caller makes earlier device index producers ready.
        vkCmdBuildClusterAccelerationStructureIndirectNV(cmd, &build);
        VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        memory.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        memory.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        memory.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        memory.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &memory;
        vkCmdPipelineBarrier2(cmd, &dependency);
    };
    if (inContext)
    {
        std::vector<CommandBuffer> commands(1);
        commands.front().AppendCommand(record);
        inContext->ExecuteCommandsAndWait(inQueue, std::move(commands));
    }
    else
        _ExecuteInlineAndWait(record);

    std::vector<VkDeviceAddress> addresses(tasks.size());
    Buffer::DownloadFromDeviceInline(*references, 0, addresses.data(), static_cast<size_t>(referenceBytes));
    for (auto address : addresses)
        CHECK_TRUE(address >= build.dstImplicitData && address - build.dstImplicitData < sizes.accelerationStructureSize &&
            address % alignment == 0, "Device returned an invalid cluster template address!");
    outBatch.m_limits = limits;
    outBatch.m_indexType = indexType;
    outBatch.m_storage = std::move(storage);
    outBatch.m_references = std::move(references);
    outBatch.m_addresses = std::move(addresses);
    outBatch.m_metadata = std::move(metadata);
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

void ClusterLevelAccelStructBatch::Create(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(m_count == 0, "Cluster batch Create requires an empty target!");
    const auto count = inCreateInfo.m_maxClusterCount;
    CHECK_TRUE(count > 0 && inCreateInfo.m_maxClusterTriangleCount > 0 && inCreateInfo.m_maxClusterVertexCount > 0 &&
        inCreateInfo.m_maxTotalTriangleCount > 0 && inCreateInfo.m_maxTotalVertexCount > 0,
        "All cluster batch capacities must be nonzero!");
    auto& device = MyDevice::GetInstance();
    CheckCreationDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    CHECK_TRUE(properties.cluster.maxClusterGeometryIndex > 0, "Device must support cluster geometry index zero!");
    CHECK_TRUE(inCreateInfo.m_maxClusterTriangleCount <= (std::min)(properties.cluster.maxTrianglesPerCluster, MaxTriangleTaskCount),
        "Cluster batch triangle capacity exceeds the per-cluster limit!");
    CHECK_TRUE(inCreateInfo.m_maxClusterVertexCount <= (std::min)(properties.cluster.maxVerticesPerCluster, MaxTriangleTaskCount),
        "Cluster batch vertex capacity exceeds the per-cluster limit!");
    const auto vertexLayout = QueryVertexLayout(device.vkPhysicalDevice, inCreateInfo.m_vertexFormat);
    const auto indexType = inCreateInfo.m_indexType;
    GetIndexLayout(indexType);

    VkClusterAccelerationStructureTriangleClusterInputNV triangleInput{
        VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
    triangleInput.vertexFormat = inCreateInfo.m_vertexFormat;
    triangleInput.maxClusterUniqueGeometryCount = 1;
    triangleInput.maxClusterTriangleCount = inCreateInfo.m_maxClusterTriangleCount;
    triangleInput.maxClusterVertexCount = inCreateInfo.m_maxClusterVertexCount;
    triangleInput.maxTotalTriangleCount = inCreateInfo.m_maxTotalTriangleCount;
    triangleInput.maxTotalVertexCount = inCreateInfo.m_maxTotalVertexCount;

    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = inCreateInfo.m_useTemplate ? VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV :
        VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    input.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    input.opInput.pTriangleClusters = &triangleInput;
    if (inCreateInfo.m_useTemplate)
        CHECK_TRUE(std::has_single_bit(properties.cluster.clusterTemplateByteAlignment), "Invalid template alignment!");
    const auto sizes = QuerySizes(device, input, inCreateInfo.m_useTemplate ? sizeof(InstanceTask) : sizeof(TriangleTask), properties);
    auto storage = CreateClusterBuffer(sizes.accelerationStructureSize, StorageUsage, properties.cluster.clusterByteAlignment);
    auto references = CreateClusterBuffer(count * ReferenceStride,
        ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReferenceStride);

    m_triangleInput = triangleInput;
    m_vertexElementSize = vertexLayout.size;
    m_vertexAlignment = vertexLayout.alignment;
    m_useTemplate = inCreateInfo.m_useTemplate;
    m_templateAlignment = properties.cluster.clusterTemplateByteAlignment;
    m_indexType = indexType;
    m_sizesInfo = sizes;
    m_storageBuffer = std::move(storage);
    m_referenceBuffer = std::move(references);
    m_count = count;
}

auto ClusterLevelAccelStructBatch::GetReferenceBuffer() const noexcept -> const Buffer*
{ return m_referenceBuffer.get(); }
auto ClusterLevelAccelStructBatch::GetSizesInfo() const noexcept -> const AccelStructSizesInfo&
{ return m_sizesInfo; }
auto ClusterLevelAccelStructBatch::GetCapacity() const noexcept -> uint32_t
{ return m_count; }
auto ClusterLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_sizesInfo.scratchAlignment; }

void ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(std::span<const ClusterLevelAccelStructDirectBuildInfo> inBuildInfos,
    const ClusterLevelAccelStructBatch& target, Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    CHECK_TRUE(target.GetCapacity() > 0 && !target.m_useTemplate, "Direct tasks require a created direct CLAS batch!");
    if (inBuildInfos.empty()) return;
    CHECK_TRUE(inBuildInfos.size() <= target.GetCapacity(), "Cluster task count exceeds batch capacity!");
    const auto bytes = VkDeviceSize{ inBuildInfos.size() } * sizeof(TriangleTask);
    CHECK_TRUE(inOffset % alignof(TriangleTask) == 0, "Direct task offset is misaligned!");
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    const VertexLayout vertex{ target.m_vertexElementSize, target.m_vertexAlignment };
    const auto index = GetIndexLayout(target.GetIndexType());
    std::vector<TriangleTask> tasks;
    tasks.reserve(inBuildInfos.size());
    const auto limits = target.GetBuildLimits();
    uint32_t remainingTriangles = limits.maxTotalTriangleCount;
    uint32_t remainingVertices = limits.maxTotalVertexCount;
    for (const auto& triangle : inBuildInfos)
    {
        CHECK_TRUE(triangle.primitiveCount <= remainingTriangles && triangle.vertexCount <= remainingVertices,
            "Cluster tasks exceed the batch's total triangle or vertex capacity!");
        CHECK_TRUE(triangle.vertexFormat == target.GetVertexFormat() && triangle.indexType == target.GetIndexType(),
            "Direct task formats must match the target batch!");
        tasks.push_back(EncodeTriangle(triangle, limits, vertex, index));
        remainingTriangles -= triangle.primitiveCount;
        remainingVertices -= triangle.vertexCount;
    }
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}

void ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(
    std::span<const ClusterLevelAccelStructInstantiateBuildInfo> inInfos, const ClusterLevelAccelStructBatch& target,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    CHECK_TRUE(target.GetCapacity() && target.m_useTemplate, "Instantiate tasks require a created template-mode CLAS batch!");
    if (inInfos.empty()) return;
    CHECK_TRUE(inInfos.size() <= target.GetCapacity() && inInfos.size() <= target.m_triangleInput.maxTotalTriangleCount &&
        inInfos.size() <= target.m_triangleInput.maxTotalVertexCount, "Instance tasks exceed the target budget!");
    const auto bytes = VkDeviceSize{inInfos.size()} * sizeof(InstanceTask);
    CHECK_TRUE(inOffset % alignof(InstanceTask) == 0, "Instantiate task offset is misaligned!");
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    std::vector<InstanceTask> tasks;
    tasks.reserve(inInfos.size());
    for (const auto& info : inInfos)
    {
        CHECK_TRUE(info.m_vertexFormat == target.GetVertexFormat() && info.m_id != MaxCount &&
            info.m_stride <= UINT32_MAX && info.m_stride % target.m_vertexAlignment == 0, "Invalid instance format, ID or stride!");
        InstanceTask task{};
        task.clusterIdOffset = info.m_id;
        task.clusterTemplateAddress = CheckAddress(info.m_template, 0, 1, target.m_templateAlignment);
        task.vertexBuffer.startAddress = CheckAddress(info.m_vertices, 0, target.m_vertexElementSize, target.m_vertexAlignment);
        task.vertexBuffer.strideInBytes = info.m_stride ? info.m_stride : target.m_vertexElementSize;
        tasks.push_back(task);
    }
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}

void BottomLevelAccelStructBuildInfo::SetClusterReferences(const ClusterLevelAccelStructBatch& inSource,
    uint32_t inOffset, uint32_t inCount)
{
    const auto* references = inSource.GetReferenceBuffer();
    CHECK_TRUE(references, "Cluster BLAS requires a created source cluster batch!");
    const auto capacity = inSource.GetCapacity();
    CHECK_TRUE(inCount > 0 && inOffset <= capacity && inCount <= capacity - inOffset,
        "Cluster BLAS reference range is out of bounds!");
    CHECK_TRUE(vkGetBufferDeviceAddress, "Cluster BLAS reference setup requires the address entrypoint!");
    const auto address = CheckClusterReferenceAddress(references->GetDeviceAddress(), inOffset, inCount);
    m_clusterReferencesAddress = address;
    m_clusterCount = inCount;
}

void BottomLevelAccelStructBatch::Create(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(m_count == 0, "Cluster BLAS batch Create requires an empty target!");
    const auto count = inCreateInfo.m_maxAccelStructCount;
    CHECK_TRUE(count > 0 && inCreateInfo.m_maxClusterCountPerAccelStruct > 0 && inCreateInfo.m_maxTotalClusterCount > 0,
        "All Cluster BLAS batch capacities must be nonzero!");
    auto& device = MyDevice::GetInstance();
    CheckCreationDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    VkClusterAccelerationStructureClustersBottomLevelInputNV clustersInput{
        VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
    clustersInput.maxClusterCountPerAccelerationStructure = inCreateInfo.m_maxClusterCountPerAccelStruct;
    clustersInput.maxTotalClusterCount = inCreateInfo.m_maxTotalClusterCount;
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    input.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    input.opInput.pClustersBottomLevel = &clustersInput;
    const auto sizes = QuerySizes(device, input, sizeof(ClusterBlasTask), properties);
    auto storage = CreateClusterBuffer(sizes.accelerationStructureSize, StorageUsage,
        properties.cluster.clusterBottomLevelByteAlignment);
    auto references = CreateClusterBuffer(count * ReferenceStride,
        ReferenceUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReferenceStride);

    m_clustersInput = clustersInput;
    m_sizesInfo = sizes;
    m_storageBuffer = std::move(storage);
    m_referenceBuffer = std::move(references);
    m_count = count;
}

auto BottomLevelAccelStructBatch::GetReferenceBuffer() const noexcept -> const Buffer*
{ return m_referenceBuffer.get(); }
auto BottomLevelAccelStructBatch::GetSizesInfo() const noexcept -> const AccelStructSizesInfo&
{ return m_sizesInfo; }
auto BottomLevelAccelStructBatch::GetCapacity() const noexcept -> uint32_t
{ return m_count; }
auto BottomLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_sizesInfo.scratchAlignment; }

void BottomLevelAccelStructBatch::WriteBuildInfoBufferInline(std::span<const BottomLevelAccelStructBuildInfo> inBuildInfos,
    Buffer& inDstBuffer, VkDeviceSize inOffset) const
{
    CHECK_TRUE(m_count > 0, "Cluster BLAS build info upload requires a created batch!");
    if (inBuildInfos.empty()) return;
    CHECK_TRUE(inBuildInfos.size() <= m_count, "Cluster BLAS task count exceeds batch capacity!");
    const auto bytes = VkDeviceSize{ inBuildInfos.size() } * sizeof(ClusterBlasTask);
    CHECK_TRUE(CONTAIN_BITS(inDstBuffer.GetBufferInformation().memoryProperty,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        "Cluster BLAS task upload requires host-visible, host-coherent memory!");
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    std::vector<ClusterBlasTask> tasks;
    tasks.reserve(inBuildInfos.size());
    uint32_t remainingClusters = m_clustersInput.maxTotalClusterCount;
    for (const auto& info : inBuildInfos)
    {
        const auto count = info.m_clusterCount;
        CHECK_TRUE(count > 0 && count <= m_clustersInput.maxClusterCountPerAccelerationStructure,
            "Invalid cluster count for a Cluster BLAS task!");
        CHECK_TRUE(count <= remainingClusters, "Cluster BLAS tasks exceed the batch's total cluster capacity!");
        const auto address = CheckClusterReferenceAddress(info.m_clusterReferencesAddress, 0, count);
        tasks.push_back({ count, static_cast<uint32_t>(ReferenceStride), address });
        remainingClusters -= count;
    }
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, tasks.data(), static_cast<size_t>(bytes));
}
