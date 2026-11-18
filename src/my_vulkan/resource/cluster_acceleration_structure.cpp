#include "acceleration_structure.h"
#include "device.h"
#include "../allocator/private/memory_allocator.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <vulkan/vulkan_format_traits.hpp>

namespace
{
    using TriangleTask = VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
    using ClusterBlasTask = VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV;
    constexpr VkDeviceSize ReferenceStride = sizeof(VkDeviceAddress);
    constexpr auto MaxCount = (std::numeric_limits<uint32_t>::max)();
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

    auto BatchCount(size_t inCount) -> uint32_t
    {
        CHECK_TRUE(inCount > 0 && inCount <= MaxCount, "Cluster batch count must fit in a nonzero uint32_t!");
        return static_cast<uint32_t>(inCount);
    }

    void AddCount(uint32_t& inOutTotal, uint32_t inCount)
    {
        CHECK_TRUE(inCount <= MaxCount - inOutTotal, "Cluster batch input count overflow; split the batch!");
        inOutTotal += inCount;
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
        CHECK_TRUE(vkGetPhysicalDeviceFormatProperties && inFormat != VK_FORMAT_UNDEFINED,
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

    auto EncodeTriangle(const ClusterLevelAccelStructCreateInfo& inTriangle,
        const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV& inLimits, const VertexLayout& inVertex,
        const IndexLayout& inIndex)
        -> TriangleTask
    {
        constexpr uint32_t maxTaskCount = (1u << 9) - 1;
        CHECK_TRUE(inTriangle.clusterID != MaxCount, "Cluster ID is reserved!");
        CHECK_TRUE(inTriangle.primitiveCount > 0 &&
            inTriangle.primitiveCount <= (std::min)(inLimits.maxTrianglesPerCluster, maxTaskCount),
            "Invalid triangle count for a cluster!");
        CHECK_TRUE(inTriangle.vertexCount > 0 &&
            inTriangle.vertexCount <= (std::min)(inLimits.maxVerticesPerCluster, maxTaskCount),
            "Invalid vertex count for a cluster!");
        CHECK_TRUE(inTriangle.vertexStride <= (std::numeric_limits<uint16_t>::max)() &&
            inTriangle.vertexStride % inVertex.size == 0, "Invalid cluster vertex stride!");
        const auto vertexStride = inTriangle.vertexStride == 0 ? inVertex.size : inTriangle.vertexStride;
        const auto vertexSize = (inTriangle.vertexCount - 1) * vertexStride + inVertex.size;
        const auto indexSize = inTriangle.primitiveCount * 3 * inIndex.size;

        TriangleTask task{};
        task.clusterID = inTriangle.clusterID;
        task.triangleCount = inTriangle.primitiveCount;
        task.vertexCount = inTriangle.vertexCount;
        task.indexType = inIndex.format;
        // The native field requires one bit even when no opacity micromap is supplied.
        task.opacityMicromapIndexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV;
        task.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;
        task.vertexBufferStride = static_cast<uint16_t>(inTriangle.vertexStride);
        task.vertexBuffer = CheckAddress(inTriangle.vertexAddress, 0, vertexSize, inVertex.alignment);
        task.indexBuffer = CheckAddress(inTriangle.indexAddress, 0, indexSize, inIndex.size);
        return task;
    }

    auto DescribeTriangles(std::span<const ClusterLevelAccelStructCreateInfo> inTriangles, VkFormat inFormat, VkIndexType inIndexType,
        VkPhysicalDevice inDevice, const VkPhysicalDeviceClusterAccelerationStructurePropertiesNV& inLimits,
        std::vector<TriangleTask>* outTasks = nullptr) -> VkClusterAccelerationStructureTriangleClusterInputNV
    {
        CHECK_TRUE(inLimits.maxClusterGeometryIndex > 0, "Device must support cluster geometry index zero!");
        const auto vertex = QueryVertexLayout(inDevice, inFormat);
        const auto index = GetIndexLayout(inIndexType);
        VkClusterAccelerationStructureTriangleClusterInputNV result{
            VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
        result.vertexFormat = inFormat;
        result.maxClusterUniqueGeometryCount = 1;
        for (const auto& triangle : inTriangles)
        {
            const auto task = EncodeTriangle(triangle, inLimits, vertex, index);
            result.maxClusterTriangleCount = (std::max)(result.maxClusterTriangleCount, triangle.primitiveCount);
            result.maxClusterVertexCount = (std::max)(result.maxClusterVertexCount, triangle.vertexCount);
            AddCount(result.maxTotalTriangleCount, triangle.primitiveCount);
            AddCount(result.maxTotalVertexCount, triangle.vertexCount);
            if (outTasks) outTasks->push_back(task);
        }
        return result;
    }

    auto DescribeBlasRanges(std::span<const ClusterBasedBottomLevelAccelStructClusterCreateInfo> inRanges,
        std::vector<ClusterBlasTask>* outTasks = nullptr) -> VkClusterAccelerationStructureClustersBottomLevelInputNV
    {
        VkClusterAccelerationStructureClustersBottomLevelInputNV result{
            VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
        for (const auto& range : inRanges)
        {
            CHECK_TRUE(range.sourceClusterBatch && range.sourceClusterBatch->GetReferenceBuffer(),
                "Cluster BLAS requires a created source cluster batch!");
            const auto count = range.sourceClusterBatch->GetCount();
            CHECK_TRUE(range.clusterCount > 0 && range.clusterOffset <= count &&
                range.clusterCount <= count - range.clusterOffset, "Cluster BLAS reference range is out of bounds!");
            result.maxClusterCountPerAccelerationStructure =
                (std::max)(result.maxClusterCountPerAccelerationStructure, range.clusterCount);
            AddCount(result.maxTotalClusterCount, range.clusterCount);
            if (outTasks)
            {
                const auto& references = *range.sourceClusterBatch->GetReferenceBuffer();
                const auto address = CheckAddress(references.GetDeviceAddress(), range.clusterOffset * ReferenceStride,
                    range.clusterCount * ReferenceStride, ReferenceStride);
                outTasks->push_back({ range.clusterCount, static_cast<uint32_t>(ReferenceStride), address });
            }
        }
        return result;
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

    auto CreateOutputBuffer(VkDeviceSize inSize, VkBufferUsageFlags inUsage, VkDeviceSize inAlignment)
        -> std::unique_ptr<Buffer>
    {
        CHECK_TRUE(std::has_single_bit(inAlignment), "Device returned an invalid cluster output alignment!");
        auto buffer = std::make_unique<Buffer>();
        BufferCreateInfo info;
        info.SetBufferSize(inSize).SetBufferUsage(inUsage).CustomizeAlignment(inAlignment);
        buffer->Create(info);
        CheckAddress(buffer->GetDeviceAddress(), 0, inSize, inAlignment);
        return buffer;
    }

    void CheckUploadBuffer(const Buffer& inDst, VkDeviceSize inOffset, VkDeviceSize inBytes)
    {
        const auto& buffer = inDst.GetBufferInformation();
        CHECK_TRUE(inDst.IsValid() && CONTAIN_BITS(buffer.memoryProperty,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            "Cluster build info upload requires a created host-visible, host-coherent Buffer!");
        CHECK_TRUE(inOffset <= buffer.size && inBytes <= buffer.size - inOffset,
            "Cluster build info upload range is out of bounds!");
        constexpr auto hostLimit = (std::numeric_limits<size_t>::max)();
        CHECK_TRUE(inOffset <= hostLimit && inBytes <= hostLimit - static_cast<size_t>(inOffset),
            "Cluster build info upload range exceeds host address space!");
    }
}

void ClusterLevelAccelStructBatchCreateInfo::SetClusterLevelAccelStructCreateInfos(
    std::span<const ClusterLevelAccelStructCreateInfo> inClusterTrianglesBatch)
{
    m_clusters.assign(inClusterTrianglesBatch.begin(), inClusterTrianglesBatch.end());
}

void ClusterLevelAccelStructBatch::Create(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(m_count == 0, "Cluster batch Create requires an empty target!");
    const auto count = BatchCount(inCreateInfo.m_clusters.size());
    auto& device = MyDevice::GetInstance();
    CheckCreationDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
        auto triangleInput = DescribeTriangles(inCreateInfo.m_clusters, inCreateInfo.m_vertexFormat, inCreateInfo.m_indexType,
        device.vkPhysicalDevice, properties.cluster);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
    input.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    input.opInput.pTriangleClusters = &triangleInput;
    const auto sizes = QuerySizes(device, input, sizeof(TriangleTask), properties);
    auto storage = CreateOutputBuffer(sizes.accelerationStructureSize, StorageUsage, properties.cluster.clusterByteAlignment);
    auto references = CreateOutputBuffer(count * ReferenceStride, ReferenceUsage, ReferenceStride);

    m_triangleInput = triangleInput;
    m_sizesInfo = sizes;
    m_storageBuffer = std::move(storage);
    m_referenceBuffer = std::move(references);
    m_count = count;
}

auto ClusterLevelAccelStructBatch::GetReferenceBuffer() const noexcept -> const Buffer*
{ return m_referenceBuffer.get(); }
auto ClusterLevelAccelStructBatch::GetSizesInfo() const noexcept -> const AccelStructSizesInfo&
{ return m_sizesInfo; }
auto ClusterLevelAccelStructBatch::GetCount() const noexcept -> size_t
{ return m_count; }
auto ClusterLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_sizesInfo.scratchAlignment; }

void ClusterLevelAccelStructBatch::WriteBuildInfoBufferInline(const ClusterLevelAccelStructBatchCreateInfo& inCreateInfo,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inCreateInfo.m_clusters.empty()) return;
    const auto count = BatchCount(inCreateInfo.m_clusters.size());
    const auto bytes = VkDeviceSize{ count } * sizeof(TriangleTask);
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    const auto& device = MyDevice::GetInstance();
    CHECK_TRUE(device.m_clusterAccelerationStructureEnabled, "Cluster acceleration structure feature is disabled!");
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    std::vector<TriangleTask> tasks;
    tasks.reserve(count);
    DescribeTriangles(inCreateInfo.m_clusters, inCreateInfo.m_vertexFormat, inCreateInfo.m_indexType,
        device.vkPhysicalDevice, properties.cluster, &tasks);
    inDstBuffer.CopyFromHost(tasks.data(), static_cast<size_t>(inOffset), static_cast<size_t>(bytes));
}

void BottomLevelAccelStructBatchCreateInfo::SetBottomLevelAccelStructCreateInfos(
    std::span<const ClusterBasedBottomLevelAccelStructClusterCreateInfo> inCreateInfos)
{
    m_ranges.assign(inCreateInfos.begin(), inCreateInfos.end());
}

void BottomLevelAccelStructBatch::Create(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo)
{
    CHECK_TRUE(m_count == 0, "Cluster BLAS batch Create requires an empty target!");
    const auto count = BatchCount(inCreateInfo.m_ranges.size());
    auto& device = MyDevice::GetInstance();
    CheckCreationDependencies(device, device.m_clusterAccelerationStructureEnabled);
    const auto properties = QueryProperties(device.vkPhysicalDevice);
    auto clustersInput = DescribeBlasRanges(inCreateInfo.m_ranges);
    VkClusterAccelerationStructureInputInfoNV input{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    input.maxAccelerationStructureCount = count;
    input.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
    input.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
    input.opInput.pClustersBottomLevel = &clustersInput;
    const auto sizes = QuerySizes(device, input, sizeof(ClusterBlasTask), properties);
    auto storage = CreateOutputBuffer(sizes.accelerationStructureSize, StorageUsage,
        properties.cluster.clusterBottomLevelByteAlignment);
    auto references = CreateOutputBuffer(count * ReferenceStride, ReferenceUsage, ReferenceStride);

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
auto BottomLevelAccelStructBatch::GetCount() const noexcept -> size_t
{ return m_count; }
auto BottomLevelAccelStructBatch::GetScratchAlignment() const noexcept -> VkDeviceSize
{ return m_sizesInfo.scratchAlignment; }

void BottomLevelAccelStructBatch::WriteBuildInfoBufferInline(const BottomLevelAccelStructBatchCreateInfo& inCreateInfo,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inCreateInfo.m_ranges.empty()) return;
    const auto count = BatchCount(inCreateInfo.m_ranges.size());
    const auto bytes = VkDeviceSize{ count } * sizeof(ClusterBlasTask);
    CheckUploadBuffer(inDstBuffer, inOffset, bytes);
    CHECK_TRUE(vkGetBufferDeviceAddress, "Cluster BLAS build info upload requires the address entrypoint!");
    std::vector<ClusterBlasTask> tasks;
    tasks.reserve(count);
    DescribeBlasRanges(inCreateInfo.m_ranges, &tasks);
    inDstBuffer.CopyFromHost(tasks.data(), static_cast<size_t>(inOffset), static_cast<size_t>(bytes));
}
