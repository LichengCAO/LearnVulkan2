#include "../acceleration_structure.h"
#include "../cluster_acceleration_structure.h"
#include "../../device.h"
#include "acceleration_structure_storage.h"
#include "../../allocator/private/memory_allocator.h"
#include <limits>

namespace
{
    struct VertexFormatLayout
    {
        VkDeviceSize size;
        VkDeviceSize alignment;
    };

    auto VertexLayout(VkFormat inFormat) -> VertexFormatLayout
    {
        switch (inFormat)
        {
        case VK_FORMAT_R32G32_SFLOAT: return { 8, 4 };
        case VK_FORMAT_R32G32B32_SFLOAT: return { 12, 4 };
        case VK_FORMAT_R32G32B32A32_SFLOAT: return { 16, 4 };
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_UNORM: return { 4, 2 };
        case VK_FORMAT_R16G16B16_SFLOAT:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_UNORM: return { 6, 2 };
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_UNORM: return { 8, 2 };
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UNORM: return { 2, 1 };
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_UNORM: return { 3, 1 };
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UNORM: return { 4, 1 };
        default: throw std::runtime_error("Unsupported acceleration structure vertex format!");
        }
    }

    auto CheckedMultiply(VkDeviceSize inLeft, VkDeviceSize inRight) -> VkDeviceSize
    {
        CHECK_TRUE(inRight == 0 || inLeft <= std::numeric_limits<VkDeviceSize>::max() / inRight,
            "Acceleration structure byte size overflow!");
        return inLeft * inRight;
    }

    auto CheckedAdd(VkDeviceSize inLeft, VkDeviceSize inRight) -> VkDeviceSize
    {
        CHECK_TRUE(inLeft <= std::numeric_limits<VkDeviceSize>::max() - inRight,
            "Acceleration structure address overflow!");
        return inLeft + inRight;
    }

    auto StridedSize(uint32_t inCount, VkDeviceSize inStride, VkDeviceSize inElementSize) -> VkDeviceSize
    {
        return inCount == 0 ? 0 : CheckedAdd(CheckedMultiply(inCount - 1, inStride), inElementSize);
    }

    void CheckAddress(VkDeviceAddress address, VkDeviceSize bytes, VkDeviceSize alignment)
    {
        CHECK_TRUE(alignment && address % alignment == 0, "AS input address is misaligned!");
        CHECK_TRUE(bytes == 0 || address != 0, "AS input address is missing!");
        CheckedAdd(address, bytes);
    }

    // Capacity compatibility for the actual build, not equality of creation maxima.
    auto Fits(const VkAccelStructBuildInfo& build, const VkAccelStructBuildInfo& limits) -> bool
    {
        if (build.buildGeometryInfo.type != limits.buildGeometryInfo.type ||
            build.buildGeometryInfo.flags != limits.buildGeometryInfo.flags ||
            build.geometries.size() > limits.geometries.size()) return false;
        for (size_t i = 0; i < build.geometries.size(); ++i)
        {
            const auto& geometry = build.geometries[i];
            const auto& limit = limits.geometries[i];
            if (geometry.geometryType != limit.geometryType || geometry.flags != limit.flags ||
                build.buildRangesInfos[i].primitiveCount > limits.buildRangesInfos[i].primitiveCount) return false;
            if (geometry.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR)
            {
                const auto& value = geometry.geometry.triangles;
                const auto& maximum = limit.geometry.triangles;
                if (value.vertexFormat != maximum.vertexFormat || value.indexType != maximum.indexType ||
                    value.maxVertex > maximum.maxVertex ||
                    (value.transformData.deviceAddress && !maximum.transformData.deviceAddress)) return false;
            }
        }
        return true;
    }

    auto QueryProperties(const VkAccelStructBuildInfo& inLayout) -> VkPhysicalDeviceAccelerationStructurePropertiesKHR
    {
        auto& device = MyDevice::GetInstance();
        VkPhysicalDeviceAccelerationStructurePropertiesKHR result{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        properties.pNext = &result;
        vkGetPhysicalDeviceProperties2(device.vkPhysicalDevice, &properties);
        CHECK_TRUE(result.minAccelerationStructureScratchOffsetAlignment != 0,
            "Acceleration structure scratch alignment is invalid!");
        if (inLayout.buildGeometryInfo.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
        {
            CHECK_TRUE(inLayout.geometries.size() <= result.maxGeometryCount, "Too many BLAS geometries!");
            VkDeviceSize primitiveCount = 0;
            for (size_t i = 0; i < inLayout.geometries.size(); ++i)
            {
                primitiveCount = CheckedAdd(primitiveCount, inLayout.buildRangesInfos[i].primitiveCount);
                if (inLayout.geometries[i].geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR)
                {
                    VkFormatProperties formatProperties{};
                    vkGetPhysicalDeviceFormatProperties(device.vkPhysicalDevice,
                        inLayout.geometries[i].geometry.triangles.vertexFormat, &formatProperties);
                    CHECK_TRUE(CONTAIN_BITS(formatProperties.bufferFeatures,
                        VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR),
                        "Vertex format does not support acceleration structure input!");
                }
            }
            CHECK_TRUE(primitiveCount <= result.maxPrimitiveCount, "Too many BLAS primitives!");
        }
        else
        {
            CHECK_TRUE(inLayout.buildRangesInfos[0].primitiveCount <= result.maxInstanceCount,
                "Too many TLAS instances!");
        }
        return result;
    }

    auto QuerySizes(const VkAccelStructBuildInfo& inLayout) -> VkAccelerationStructureBuildSizesInfoKHR
    {
        auto buildInfo = inLayout.buildGeometryInfo;
        buildInfo.pGeometries = inLayout.geometries.data();
        std::vector<uint32_t> primitiveCounts;
        primitiveCounts.reserve(inLayout.buildRangesInfos.size());
        for (const auto& range : inLayout.buildRangesInfos) primitiveCounts.push_back(range.primitiveCount);
        VkAccelerationStructureBuildSizesInfoKHR result{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        MyDevice::GetInstance().GetAccelerationStructureBuildSizes(
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, buildInfo, primitiveCounts, result);
        CHECK_TRUE(result.accelerationStructureSize != 0, "Acceleration structure size query returned zero!");
        return result;
    }

    auto CreateBackingBuffer(VkDeviceSize inSize) -> std::unique_ptr<Buffer>
    {
        BufferCreateInfo info{};
        info.SetBufferSize(inSize);
        info.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        info.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto buffer = std::make_unique<Buffer>();
        buffer->Create(info);
        CHECK_TRUE(buffer->GetVkBuffer() != VK_NULL_HANDLE, "Acceleration structure backing buffer creation failed!");
        return buffer;
    }

}

void BottomLevelAccelStructCreateInfo::SetTriangles(std::span<const AccelStructTriangleGeometryLimits> inGeometries)
{
    std::vector<AccelStructTriangleGeometryLimits> geometries(inGeometries.begin(), inGeometries.end());
    m_triangles = std::move(geometries);
    m_aabbs.clear();
    m_clusterCount = 0;
}

void BottomLevelAccelStructCreateInfo::SetAabbs(std::span<const AccelStructAabbGeometryLimits> inGeometries)
{
    std::vector<AccelStructAabbGeometryLimits> geometries(inGeometries.begin(), inGeometries.end());
    m_aabbs = std::move(geometries);
    m_triangles.clear();
    m_clusterCount = 0;
}

void BottomLevelAccelStructCreateInfo::SetClusters(size_t count)
{
    CHECK_TRUE(count && count <= UINT32_MAX, "Cluster BLAS requires a representable, nonzero cluster count!");
    m_clusterCount = static_cast<uint32_t>(count);
    m_triangles.clear();
    m_aabbs.clear();
}

void BottomLevelAccelStructCreateInfo::CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept
{
    m_flags = inFlags;
}

void TopLevelAccelStructCreateInfo::SetMaxInstanceCount(uint32_t inCount)
{
    CHECK_TRUE(inCount > 0, "TLAS instance capacity must be positive!");
    m_maxInstanceCount = inCount;
}

void TopLevelAccelStructCreateInfo::CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept
{ m_flags = inFlags; }

auto BottomLevelAccelStruct::_Describe(const BottomLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo
{
    const auto count = inInfo.m_triangles.empty() ? inInfo.m_aabbs.size() : inInfo.m_triangles.size();
    CHECK_TRUE(count > 0 && count <= UINT32_MAX, "BLAS geometry capacity list is empty or too large!");
    VkAccelStructBuildInfo result;
    result.buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    result.buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    result.buildGeometryInfo.flags = inInfo.m_flags;
    result.buildGeometryInfo.geometryCount = static_cast<uint32_t>(count);
    result.geometries.reserve(count);
    result.buildRangesInfos.reserve(count);
    for (const auto& source : inInfo.m_triangles)
    {
        const auto format = VertexLayout(source.m_format);
        CHECK_TRUE(source.m_triangles && source.m_vertices, "BLAS triangle/vertex capacities must be positive!");
        CHECK_TRUE(source.m_indexType == VK_INDEX_TYPE_NONE_KHR || source.m_indexType == VK_INDEX_TYPE_UINT16 ||
            source.m_indexType == VK_INDEX_TYPE_UINT32, "Invalid BLAS index type!");
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = source.m_flags;
        auto& triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = source.m_format;
        triangles.vertexStride = format.size; // Legal query placeholder; stride is not a size constraint.
        triangles.maxVertex = source.m_vertices - 1;
        triangles.indexType = source.m_indexType;
        // The size query tests transformData.hostAddress for nullness only.
        triangles.transformData.hostAddress = source.m_allowTransform ? reinterpret_cast<const void*>(uintptr_t{1}) : nullptr;
        result.geometries.push_back(geometry);
        result.buildRangesInfos.push_back({source.m_triangles, 0, 0, 0});
    }
    for (const auto& source : inInfo.m_aabbs)
    {
        CHECK_TRUE(source.m_count > 0, "BLAS AABB capacity must be positive!");
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.flags = source.m_flags;
        geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);
        result.geometries.push_back(geometry);
        result.buildRangesInfos.push_back({source.m_count, 0, 0, 0});
    }
    return result;
}

auto TopLevelAccelStruct::_Describe(const TopLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo
{
    CHECK_TRUE(inInfo.m_maxInstanceCount > 0, "TLAS instance capacity must be set!");
    VkAccelStructBuildInfo result;
    result.buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    result.buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    result.buildGeometryInfo.flags = inInfo.m_flags;
    result.buildGeometryInfo.geometryCount = 1;
    VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    result.geometries.push_back(geometry);
    result.buildRangesInfos.push_back({ inInfo.m_maxInstanceCount, 0, 0, 0 });
    return result;
}

void BottomLevelAccelStruct::Create(const BottomLevelAccelStructCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!IsValid(), "BLAS Create requires an empty target!");
    if (inCreateInfo.m_clusterCount)
    {
        CHECK_TRUE(inCreateInfo.m_flags == 0, "Cluster BLAS layouts use default build flags!");
        ClusterBottomLevelAccelStructBuildLimits layout;
        layout.SetMaxClusterCount(inCreateInfo.m_clusterCount, inCreateInfo.m_clusterCount);
        const auto sizes = VulkanDetail::ClusterSizeQuery::Get(layout, VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV, 1);
        auto storage = std::make_unique<VulkanDetail::AccelerationStructureStorage>();
        storage->buffer = VulkanDetail::CreateClusterDestination(sizes);
        const auto reference = storage->buffer->GetDeviceAddress();
        m_storage = std::move(storage);
        m_accelStructReference = reference;
        m_clusterCount = inCreateInfo.m_clusterCount;
        m_sizesInfo = sizes;
        m_storageAddress = reference;
        return;
    }
    auto& device = MyDevice::GetInstance();
    CHECK_TRUE(device.vkDevice != VK_NULL_HANDLE && device.vkPhysicalDevice != VK_NULL_HANDLE &&
        device.GetMemoryAllocator() != nullptr && device.GetMemoryAllocator()->IsValid(),
        "BLAS creation requires initialized Device dependencies!");
    CHECK_TRUE(vkGetPhysicalDeviceProperties2 && vkGetPhysicalDeviceFormatProperties &&
        vkGetAccelerationStructureBuildSizesKHR && vkCreateAccelerationStructureKHR &&
        vkDestroyAccelerationStructureKHR && vkGetAccelerationStructureDeviceAddressKHR && vkGetBufferDeviceAddress,
        "BLAS creation requires acceleration structure entrypoints!");
    auto layout = _Describe(inCreateInfo);
    const auto properties = QueryProperties(layout);
    const auto sizes = QuerySizes(layout);
    auto storage = std::make_unique<VulkanDetail::AccelerationStructureStorage>();
    storage->device = device.vkDevice;
    storage->buffer = CreateBackingBuffer(sizes.accelerationStructureSize);
    VkAccelerationStructureCreateInfoKHR nativeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    nativeInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    nativeInfo.size = sizes.accelerationStructureSize;
    nativeInfo.buffer = storage->buffer->GetVkBuffer();
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VK_CHECK(vkCreateAccelerationStructureKHR(storage->device, &nativeInfo, nullptr, &handle));
    CHECK_TRUE(handle != VK_NULL_HANDLE, "Acceleration structure creation returned a null handle!");
    storage->handle = handle;
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = handle;
    const auto address = vkGetAccelerationStructureDeviceAddressKHR(storage->device, &addressInfo);
    CHECK_TRUE(address != 0, "BLAS device address is invalid!");
    const auto storageAddress = storage->buffer->GetDeviceAddress();
    CHECK_TRUE(storageAddress != 0, "AS storage address is invalid!");
    m_storageAddress = storageAddress;
    m_storage = std::move(storage);
    m_accelStructReference = address;
    m_flags = inCreateInfo.m_flags;
    m_sizesInfo = { sizes.updateScratchSize, sizes.buildScratchSize,
        properties.minAccelerationStructureScratchOffsetAlignment,
        0,
        sizes.accelerationStructureSize, 256 };
    m_layout = std::move(layout);
}

void TopLevelAccelStruct::Create(const TopLevelAccelStructCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!IsValid(), "TLAS Create requires an empty target!");
    auto& device = MyDevice::GetInstance();
    CHECK_TRUE(device.vkDevice != VK_NULL_HANDLE && device.vkPhysicalDevice != VK_NULL_HANDLE &&
        device.GetMemoryAllocator() != nullptr && device.GetMemoryAllocator()->IsValid(),
        "TLAS creation requires initialized Device dependencies!");
    CHECK_TRUE(vkGetPhysicalDeviceProperties2 && vkGetAccelerationStructureBuildSizesKHR &&
        vkCreateAccelerationStructureKHR && vkDestroyAccelerationStructureKHR && vkGetBufferDeviceAddress,
        "TLAS creation requires acceleration structure entrypoints!");
    auto layout = _Describe(inCreateInfo);
    const auto properties = QueryProperties(layout);
    const auto sizes = QuerySizes(layout);
    auto storage = std::make_unique<VulkanDetail::AccelerationStructureStorage>();
    storage->device = device.vkDevice;
    storage->buffer = CreateBackingBuffer(sizes.accelerationStructureSize);
    VkAccelerationStructureCreateInfoKHR nativeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    nativeInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    nativeInfo.size = sizes.accelerationStructureSize;
    nativeInfo.buffer = storage->buffer->GetVkBuffer();
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VK_CHECK(vkCreateAccelerationStructureKHR(storage->device, &nativeInfo, nullptr, &handle));
    CHECK_TRUE(handle != VK_NULL_HANDLE, "Acceleration structure creation returned a null handle!");
    storage->handle = handle;
    const auto storageAddress = storage->buffer->GetDeviceAddress();
    CHECK_TRUE(storageAddress != 0, "AS storage address is invalid!");
    m_storageAddress = storageAddress;
    m_storage = std::move(storage);
    m_flags = inCreateInfo.m_flags;
    m_sizesInfo = { sizes.updateScratchSize, sizes.buildScratchSize,
        properties.minAccelerationStructureScratchOffsetAlignment,
        VkDeviceSize(layout.buildRangesInfos[0].primitiveCount) * TopLevelAccelStructBuildInfo::GetBuildInfoLayout().recordStride,
        sizes.accelerationStructureSize, 256 };
    m_layout = std::move(layout);
}

void TopLevelAccelStructBuildInfo::CommitToDeviceInline(std::span<const TopLevelAccelStructBuildInfo> inInfos,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inInfos.empty()) return;
    CHECK_TRUE(inInfos.size() <= UINT32_MAX, "Too many instance records!");
    const auto bytes = CheckedMultiply(inInfos.size(), GetBuildInfoLayout().recordStride);
    CHECK_TRUE(bytes <= std::numeric_limits<size_t>::max(), "Instance upload exceeds host address space!");
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(inInfos.size());
    for (const auto& source : inInfos)
    {
        CHECK_TRUE(source.instanceCustomIndex <= 0xFFFFFF && source.shaderGroupIndexOffset <= 0xFFFFFF,
            "Instance custom index or SBT offset exceeds 24 bits!");
        CHECK_TRUE(source.mask <= 0xFF && source.flags <= 0xFF, "Instance mask or flags exceeds 8 bits!");
        VkAccelerationStructureInstanceKHR instance{};
        instance.accelerationStructureReference = source.accelStructReference;
        for (uint32_t row = 0; row < 3; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                instance.transform.matrix[row][column] = source.transform[column][row];
        instance.instanceCustomIndex = source.instanceCustomIndex;
        instance.instanceShaderBindingTableRecordOffset = source.shaderGroupIndexOffset;
        instance.mask = source.mask;
        instance.flags = source.flags;
        instances.push_back(instance);
    }
    Buffer::UploadToDeviceInline(inDstBuffer, inOffset, instances.data(), static_cast<size_t>(bytes));
}

BottomLevelAccelStruct::BottomLevelAccelStruct() = default;
BottomLevelAccelStruct::~BottomLevelAccelStruct() noexcept = default;
auto BottomLevelAccelStruct::IsValid() const noexcept -> bool
{ return GetVkAccelerationStructure() != VK_NULL_HANDLE || m_clusterCount != 0; }
auto BottomLevelAccelStruct::GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR
{ return m_storage ? m_storage->handle : VK_NULL_HANDLE; }

TopLevelAccelStruct::TopLevelAccelStruct() = default;
TopLevelAccelStruct::~TopLevelAccelStruct() noexcept = default;
auto TopLevelAccelStruct::IsValid() const noexcept -> bool
{ return GetVkAccelerationStructure() != VK_NULL_HANDLE; }
auto TopLevelAccelStruct::GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR
{ return m_storage ? m_storage->handle : VK_NULL_HANDLE; }

void BuildAccelStructCommandInput::SetTrianglesTarget(const BottomLevelAccelStruct& target,
    std::span<const AccelStructTriangleBuildInfo> infos)
{
    CHECK_TRUE(target.GetVkAccelerationStructure() && !infos.empty() &&
        infos.size() <= target.m_layout.geometries.size(), "Invalid triangle BLAS target or geometry count!");
    BuildAccelStructCommandInput next;
    next.m_build = target.m_layout;
    next.m_build.geometries.resize(infos.size());
    next.m_build.buildRangesInfos.resize(infos.size());
    next.m_build.buildGeometryInfo.geometryCount = static_cast<uint32_t>(infos.size());
    next.m_build.buildGeometryInfo.dstAccelerationStructure = target.GetVkAccelerationStructure();
    next.m_build.buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        auto& geometry = next.m_build.geometries[i];
        CHECK_TRUE(geometry.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR, "Target is not a triangle BLAS!");
        auto& triangle = geometry.geometry.triangles;
        const auto& input = infos[i];
        CHECK_TRUE(input.m_triangleCount <= target.m_layout.buildRangesInfos[i].primitiveCount &&
            uint64_t(input.m_vertexCount) <= uint64_t(triangle.maxVertex) + 1,
            "Triangle input exceeds target capacity!");
        CHECK_TRUE(!input.m_triangleCount || input.m_vertexCount, "Triangles require a vertex range!");
        const auto format = VertexLayout(triangle.vertexFormat);
        const auto stride = input.m_stride ? input.m_stride : format.size;
        CHECK_TRUE(stride <= UINT32_MAX && stride % format.alignment == 0, "Invalid vertex stride!");
        CheckAddress(input.m_vertices, StridedSize(input.m_vertexCount, stride, format.size), format.alignment);
        if (triangle.indexType == VK_INDEX_TYPE_NONE_KHR)
        {
            CHECK_TRUE(!input.m_indices && uint64_t(input.m_triangleCount) * 3 <= input.m_vertexCount,
                "Nonindexed input has indices or too few vertices!");
        }
        else
            CheckAddress(input.m_indices, CheckedMultiply(input.m_triangleCount,
                triangle.indexType == VK_INDEX_TYPE_UINT16 ? 6 : 12),
                triangle.indexType == VK_INDEX_TYPE_UINT16 ? 2 : 4);
        CHECK_TRUE(!input.m_transform || triangle.transformData.hostAddress, "Transform capacity was not reserved!");
        if (input.m_transform) CheckAddress(input.m_transform, sizeof(VkTransformMatrixKHR), 16);
        triangle.vertexStride = stride;
        triangle.maxVertex = input.m_vertexCount ? input.m_vertexCount - 1 : 0;
        triangle.vertexData.deviceAddress = input.m_vertices;
        triangle.indexData.deviceAddress = input.m_indices;
        triangle.transformData.deviceAddress = input.m_transform;
        next.m_build.buildRangesInfos[i] = {input.m_triangleCount, 0, 0, 0};
    }
    next.m_sizes = target.m_sizesInfo;
    next.m_targetRange = {target.m_storageAddress, target.m_sizesInfo.accelerationStructureSize};
    next.m_scratch = m_scratch;
    *this = std::move(next);
}

void BuildAccelStructCommandInput::SetAabbsTarget(const BottomLevelAccelStruct& target,
    std::span<const AccelStructAabbBuildInfo> infos)
{
    CHECK_TRUE(target.GetVkAccelerationStructure() && !infos.empty() &&
        infos.size() <= target.m_layout.geometries.size(), "Invalid AABB BLAS target or geometry count!");
    BuildAccelStructCommandInput next;
    next.m_build = target.m_layout;
    next.m_build.geometries.resize(infos.size());
    next.m_build.buildRangesInfos.resize(infos.size());
    next.m_build.buildGeometryInfo.geometryCount = static_cast<uint32_t>(infos.size());
    next.m_build.buildGeometryInfo.dstAccelerationStructure = target.GetVkAccelerationStructure();
    next.m_build.buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        auto& geometry = next.m_build.geometries[i];
        const auto& input = infos[i];
        CHECK_TRUE(geometry.geometryType == VK_GEOMETRY_TYPE_AABBS_KHR, "Target is not an AABB BLAS!");
        CHECK_TRUE(input.m_count <= target.m_layout.buildRangesInfos[i].primitiveCount, "AABB input exceeds capacity!");
        CHECK_TRUE(input.m_stride <= UINT32_MAX && input.m_stride % 8 == 0, "Invalid AABB stride!");
        CheckAddress(input.m_address, StridedSize(input.m_count, input.m_stride, sizeof(VkAabbPositionsKHR)), 8);
        geometry.geometry.aabbs.stride = input.m_stride;
        geometry.geometry.aabbs.data.deviceAddress = input.m_address;
        next.m_build.buildRangesInfos[i] = {input.m_count, 0, 0, 0};
    }
    next.m_sizes = target.m_sizesInfo;
    next.m_targetRange = {target.m_storageAddress, target.m_sizesInfo.accelerationStructureSize};
    next.m_scratch = m_scratch;
    *this = std::move(next);
}

void BuildAccelStructCommandInput::SetInstancesTarget(const TopLevelAccelStruct& target,
    VkDeviceAddress address, uint32_t count)
{
    CHECK_TRUE(target.IsValid() && count <= target.GetCapacity(), "Invalid TLAS target or instance count!");
    CheckAddress(address, CheckedMultiply(count, TopLevelAccelStructBuildInfo::GetBuildInfoLayout().recordStride), 16);
    BuildAccelStructCommandInput next;
    next.m_build = target.m_layout;
    next.m_build.buildGeometryInfo.dstAccelerationStructure = target.GetVkAccelerationStructure();
    next.m_build.buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    next.m_build.geometries[0].geometry.instances.data.deviceAddress = address;
    next.m_build.buildRangesInfos[0] = {count, 0, 0, 0};
    next.m_sizes = target.m_sizesInfo;
    next.m_targetRange = {target.m_storageAddress, target.m_sizesInfo.accelerationStructureSize};
    next.m_scratch = m_scratch;
    *this = std::move(next);
}

void BuildAccelStructCommandInput::_SetSource(const VkAccelStructBuildInfo& limits,
    VkAccelerationStructureKHR handle, VkDeviceAddress storageAddress, VkDeviceSize storageSize)
{
    CHECK_TRUE(m_build.buildGeometryInfo.dstAccelerationStructure && handle && Fits(m_build, limits),
        "Select a target before a compatible update source!");
    CHECK_TRUE(CONTAIN_BITS(m_build.buildGeometryInfo.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR),
        "Update requires ALLOW_UPDATE!");
    m_build.buildGeometryInfo.srcAccelerationStructure = handle;
    m_build.buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    m_sourceRange = {storageAddress, storageSize};
}

void BuildAccelStructCommandInput::CustomizeSourceAccelStruct(const BottomLevelAccelStruct& source)
{
    _SetSource(source.m_layout, source.GetVkAccelerationStructure(),
        source.m_storageAddress, source.m_sizesInfo.accelerationStructureSize);
}

void BuildAccelStructCommandInput::CustomizeSourceAccelStruct(const TopLevelAccelStruct& source)
{
    _SetSource(source.m_layout, source.GetVkAccelerationStructure(),
        source.m_storageAddress, source.m_sizesInfo.accelerationStructureSize);
}

auto BuildAccelStructCommandInput::QueryCurrentTargetSizesInfo() const -> AccelStructSizesInfo
{
    CHECK_TRUE(m_build.buildGeometryInfo.dstAccelerationStructure && !m_build.geometries.empty(),
        "No AS target selected or moved-from input!");
    return m_sizes;
}

auto BuildAccelStructCommandInput::_GetBuild() const -> VkAccelStructBuildInfo
{
    CHECK_TRUE(m_build.buildGeometryInfo.dstAccelerationStructure && !m_build.geometries.empty() &&
        m_build.geometries.size() == m_build.buildRangesInfos.size(), "No AS target selected or moved-from input!");
    const auto bytes = m_build.buildGeometryInfo.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
        ? m_sizes.updateScratchBufferSize : m_sizes.buildScratchBufferSize;
    CheckAddress(m_scratch, bytes, m_sizes.scratchAlignment);
    auto result = m_build;
    result.buildGeometryInfo.scratchData.deviceAddress = bytes ? m_scratch : 0;
    return result;
}
