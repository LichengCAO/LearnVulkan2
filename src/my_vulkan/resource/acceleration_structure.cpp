#include "acceleration_structure.h"
#include "device.h"
#include "private/acceleration_structure_storage.h"
#include "../allocator/private/memory_allocator.h"
#include <limits>

namespace
{
    constexpr auto InputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

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

    auto BufferAddress(const Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize,
        VkBufferUsageFlags inUsage, VkDeviceSize inAlignment) -> VkDeviceAddress
    {
        const auto& bufferInfo = inBuffer.GetBufferInformation();
        CHECK_TRUE(inBuffer.GetVkBuffer() != VK_NULL_HANDLE, "Acceleration structure buffer is not created!");
        CHECK_TRUE(CONTAIN_BITS(bufferInfo.usage, inUsage), "Acceleration structure buffer usage is invalid!");
        CHECK_TRUE(inOffset < bufferInfo.size && inSize <= bufferInfo.size - inOffset,
            "Acceleration structure buffer range is out of bounds!");
        const auto baseAddress = inBuffer.GetDeviceAddress();
        CHECK_TRUE(baseAddress != 0, "Acceleration structure buffer address is invalid!");
        const auto address = CheckedAdd(baseAddress, inOffset);
        CheckedAdd(address, inSize);
        CHECK_TRUE(inAlignment != 0 && address % inAlignment == 0,
            "Acceleration structure buffer address is misaligned!");
        return address;
    }

    auto ScratchAddress(Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize,
        VkDeviceSize inAlignment) -> VkDeviceAddress
    {
        if (inSize == 0) return 0;
        return BufferAddress(inBuffer, inOffset, inSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, inAlignment);
    }

    auto SameLayout(const VkAccelStructBuildInfo& inLeft, const VkAccelStructBuildInfo& inRight) -> bool
    {
        if (inLeft.buildGeometryInfo.type != inRight.buildGeometryInfo.type ||
            inLeft.buildGeometryInfo.flags != inRight.buildGeometryInfo.flags ||
            inLeft.geometries.size() != inRight.geometries.size() ||
            inLeft.buildRangesInfos.size() != inRight.buildRangesInfos.size()) return false;
        for (size_t i = 0; i < inLeft.geometries.size(); ++i)
        {
            const auto& left = inLeft.geometries[i];
            const auto& right = inRight.geometries[i];
            if (left.geometryType != right.geometryType || left.flags != right.flags ||
                inLeft.buildRangesInfos[i].primitiveCount != inRight.buildRangesInfos[i].primitiveCount) return false;
            if (left.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR)
            {
                const auto& a = left.geometry.triangles;
                const auto& b = right.geometry.triangles;
                if (a.vertexFormat != b.vertexFormat || a.vertexStride != b.vertexStride ||
                    a.maxVertex != b.maxVertex || a.indexType != b.indexType ||
                    (a.transformData.deviceAddress != 0) != (b.transformData.deviceAddress != 0)) return false;
            }
            else if (left.geometryType == VK_GEOMETRY_TYPE_AABBS_KHR &&
                left.geometry.aabbs.stride != right.geometry.aabbs.stride) return false;
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

void BottomLevelAccelStructCreateInfo::SetTriangles(std::span<const AccelStructTriangleGeometry> inGeometries)
{
    std::vector<AccelStructTriangleGeometry> geometries(inGeometries.begin(), inGeometries.end());
    m_triangles = std::move(geometries);
    m_aabbs.clear();
}

void BottomLevelAccelStructCreateInfo::SetAabbs(std::span<const AccelStructAabbGeometry> inGeometries)
{
    std::vector<AccelStructAabbGeometry> geometries(inGeometries.begin(), inGeometries.end());
    m_aabbs = std::move(geometries);
    m_triangles.clear();
}

void BottomLevelAccelStructCreateInfo::CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept
{
    m_flags = inFlags;
}

void BottomLevelAccelStructCreateInfo::Reset() noexcept
{
    m_triangles.clear();
    m_aabbs.clear();
    m_flags = 0;
}

void TopLevelAccelStructCreateInfo::SetInstances(std::span<const AccelStructInstance> inInstances)
{
    CHECK_TRUE(inInstances.size() <= UINT32_MAX, "Too many acceleration structure instances!");
    std::vector<AccelStructInstance> instances(inInstances.begin(), inInstances.end());
    m_instances = std::move(instances);
}

void TopLevelAccelStructCreateInfo::CustomizeBuildFlags(VkBuildAccelerationStructureFlagsKHR inFlags) noexcept
{
    m_flags = inFlags;
}

void TopLevelAccelStructCreateInfo::Reset() noexcept
{
    m_instances.clear();
    m_flags = 0;
}

auto TopLevelAccelStructCreateInfo::GetInstanceCount() const noexcept -> uint32_t
{
    return static_cast<uint32_t>(m_instances.size());
}

auto TopLevelAccelStructCreateInfo::GetInstanceBufferSize() const noexcept -> VkDeviceSize
{
    return static_cast<VkDeviceSize>(m_instances.size()) * sizeof(VkAccelerationStructureInstanceKHR);
}

auto BottomLevelAccelStruct::_Describe(const BottomLevelAccelStructCreateInfo& inInfo,
    bool inResolveAddresses) -> VkAccelStructBuildInfo
{
    CHECK_TRUE(!inInfo.m_triangles.empty() || !inInfo.m_aabbs.empty(), "BLAS geometry list is empty!");
    const auto count = inInfo.m_triangles.empty() ? inInfo.m_aabbs.size() : inInfo.m_triangles.size();
    CHECK_TRUE(count <= UINT32_MAX, "Too many BLAS geometries!");
    VkAccelStructBuildInfo result;
    result.buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    result.buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    result.buildGeometryInfo.flags = inInfo.m_flags;
    result.buildGeometryInfo.geometryCount = static_cast<uint32_t>(count);
    result.geometries.reserve(count);
    result.buildRangesInfos.reserve(count);
    for (const auto& source : inInfo.m_triangles)
    {
        const auto format = VertexLayout(source.vertexFormat);
        CHECK_TRUE(source.vertexBuffer != nullptr, "BLAS vertex buffer is missing!");
        CHECK_TRUE(source.vertexStride >= format.size && source.vertexStride <= UINT32_MAX &&
            source.vertexStride % format.alignment == 0, "BLAS vertex stride is invalid!");
        CHECK_TRUE(source.primitiveCount == 0 || source.vertexCount != 0, "BLAS vertex count is invalid!");
        const auto vertexBytes = StridedSize(source.vertexCount, source.vertexStride, format.size);
        CheckedAdd(source.vertexOffset, vertexBytes);
        CHECK_TRUE(source.indexBuffer != nullptr ||
            static_cast<uint64_t>(source.primitiveCount) * 3 <= source.vertexCount,
            "Non-indexed BLAS has too few vertices!");
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = source.flags;
        auto& triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = source.vertexFormat;
        triangles.vertexStride = source.vertexStride;
        triangles.maxVertex = source.vertexCount == 0 ? 0 : source.vertexCount - 1;
        triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
        if (inResolveAddresses)
            triangles.vertexData.deviceAddress = BufferAddress(*source.vertexBuffer, source.vertexOffset,
                vertexBytes, InputUsage, format.alignment);
        if (source.indexBuffer != nullptr)
        {
            CHECK_TRUE(source.indexType == VK_INDEX_TYPE_UINT16 || source.indexType == VK_INDEX_TYPE_UINT32,
                "BLAS index type is invalid!");
            const VkDeviceSize indexWidth = source.indexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
            const auto indexBytes = CheckedMultiply(CheckedMultiply(source.primitiveCount, 3), indexWidth);
            CheckedAdd(source.indexOffset, indexBytes);
            triangles.indexType = source.indexType;
            if (inResolveAddresses)
                triangles.indexData.deviceAddress = BufferAddress(*source.indexBuffer, source.indexOffset,
                    indexBytes, InputUsage, indexWidth);
        }
        if (source.transformBuffer != nullptr)
        {
            CheckedAdd(source.transformOffset, sizeof(VkTransformMatrixKHR));
            // Size queries ignore input addresses, but transform presence affects the layout.
            triangles.transformData.deviceAddress = inResolveAddresses
                ? BufferAddress(*source.transformBuffer, source.transformOffset, sizeof(VkTransformMatrixKHR), InputUsage, 16)
                : 1;
        }
        result.geometries.push_back(geometry);
        result.buildRangesInfos.push_back({ source.primitiveCount, 0, 0, 0 });
    }
    for (const auto& source : inInfo.m_aabbs)
    {
        CHECK_TRUE(source.buffer != nullptr, "BLAS AABB buffer is missing!");
        CHECK_TRUE(source.stride >= sizeof(VkAabbPositionsKHR) && source.stride <= UINT32_MAX && source.stride % 8 == 0,
            "BLAS AABB stride is invalid!");
        const auto bytes = StridedSize(source.primitiveCount, source.stride, sizeof(VkAabbPositionsKHR));
        CheckedAdd(source.offset, bytes);
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.flags = source.flags;
        geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.stride = source.stride;
        if (inResolveAddresses)
            geometry.geometry.aabbs.data.deviceAddress = BufferAddress(*source.buffer, source.offset, bytes, InputUsage, 8);
        result.geometries.push_back(geometry);
        result.buildRangesInfos.push_back({ source.primitiveCount, 0, 0, 0 });
    }
    return result;
}

auto TopLevelAccelStruct::_Describe(const TopLevelAccelStructCreateInfo& inInfo) -> VkAccelStructBuildInfo
{
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
    result.buildRangesInfos.push_back({ inInfo.GetInstanceCount(), 0, 0, 0 });
    return result;
}

void BottomLevelAccelStruct::Create(const BottomLevelAccelStructCreateInfo& inCreateInfo)
{
    CHECK_TRUE(!IsValid(), "BLAS Create requires an empty target!");
    auto& device = MyDevice::GetInstance();
    CHECK_TRUE(device.vkDevice != VK_NULL_HANDLE && device.vkPhysicalDevice != VK_NULL_HANDLE &&
        device.GetMemoryAllocator() != nullptr && device.GetMemoryAllocator()->IsValid(),
        "BLAS creation requires initialized Device dependencies!");
    CHECK_TRUE(vkGetPhysicalDeviceProperties2 && vkGetPhysicalDeviceFormatProperties &&
        vkGetAccelerationStructureBuildSizesKHR && vkCreateAccelerationStructureKHR &&
        vkDestroyAccelerationStructureKHR && vkGetAccelerationStructureDeviceAddressKHR,
        "BLAS creation requires acceleration structure entrypoints!");
    auto layout = _Describe(inCreateInfo, false);
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
    m_storage = std::move(storage);
    m_accelStructReference = address;
    m_flags = inCreateInfo.m_flags;
    m_buildSizes = sizes;
    m_scratchAlignment = properties.minAccelerationStructureScratchOffsetAlignment;
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
        vkCreateAccelerationStructureKHR && vkDestroyAccelerationStructureKHR,
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
    m_storage = std::move(storage);
    m_flags = inCreateInfo.m_flags;
    m_buildSizes = sizes;
    m_scratchAlignment = properties.minAccelerationStructureScratchOffsetAlignment;
    m_layout = std::move(layout);
}

auto BottomLevelAccelStruct::_BuildInfo(const BottomLevelAccelStructCreateInfo& inInfo,
    const BottomLevelAccelStruct* inSrc, Buffer& inScratchBuffer, VkDeviceSize inScratchOffset) const
    -> VkAccelStructBuildInfo
{
    CHECK_TRUE(IsValid(), "Destination BLAS is not created!");
    const auto layout = _Describe(inInfo, false);
    CHECK_TRUE(SameLayout(layout, m_layout), "BLAS build layout differs from its creation layout!");
    if (inSrc != nullptr)
    {
        CHECK_TRUE(inSrc->IsValid(), "Source BLAS is not created!");
        CHECK_TRUE(CONTAIN_BITS(m_flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) &&
            CONTAIN_BITS(inSrc->m_flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR),
            "BLAS update requires ALLOW_UPDATE!");
        CHECK_TRUE(SameLayout(layout, inSrc->m_layout), "Source BLAS update layout is incompatible!");
    }
    auto result = _Describe(inInfo, true);
    auto& info = result.buildGeometryInfo;
    info.mode = inSrc == nullptr ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    info.srcAccelerationStructure = inSrc == nullptr ? VK_NULL_HANDLE : inSrc->GetVkAccelerationStructure();
    info.dstAccelerationStructure = GetVkAccelerationStructure();
    info.scratchData.deviceAddress = ScratchAddress(inScratchBuffer, inScratchOffset,
        inSrc == nullptr ? m_buildSizes.buildScratchSize : m_buildSizes.updateScratchSize, m_scratchAlignment);
    return result;
}

auto TopLevelAccelStruct::_BuildInfo(const TopLevelAccelStructCreateInfo& inInfo, const TopLevelAccelStruct* inSrc,
    const Buffer& inInstanceBuffer, VkDeviceSize inInstanceOffset, Buffer& inScratchBuffer,
    VkDeviceSize inScratchOffset) const -> VkAccelStructBuildInfo
{
    CHECK_TRUE(IsValid(), "Destination TLAS is not created!");
    auto result = _Describe(inInfo);
    CHECK_TRUE(SameLayout(result, m_layout), "TLAS build layout differs from its creation layout!");
    if (inSrc != nullptr)
    {
        CHECK_TRUE(inSrc->IsValid(), "Source TLAS is not created!");
        CHECK_TRUE(CONTAIN_BITS(m_flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) &&
            CONTAIN_BITS(inSrc->m_flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR),
            "TLAS update requires ALLOW_UPDATE!");
        CHECK_TRUE(SameLayout(result, inSrc->m_layout), "Source TLAS update layout is incompatible!");
    }
    result.geometries[0].geometry.instances.data.deviceAddress = BufferAddress(inInstanceBuffer,
        inInstanceOffset, inInfo.GetInstanceBufferSize(), InputUsage, 16);
    auto& info = result.buildGeometryInfo;
    info.mode = inSrc == nullptr ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    info.srcAccelerationStructure = inSrc == nullptr ? VK_NULL_HANDLE : inSrc->GetVkAccelerationStructure();
    info.dstAccelerationStructure = GetVkAccelerationStructure();
    info.scratchData.deviceAddress = ScratchAddress(inScratchBuffer, inScratchOffset,
        inSrc == nullptr ? m_buildSizes.buildScratchSize : m_buildSizes.updateScratchSize, m_scratchAlignment);
    return result;
}

void TopLevelAccelStruct::WriteInstances(const TopLevelAccelStructCreateInfo& inInfo,
    Buffer& inDstBuffer, VkDeviceSize inOffset)
{
    if (inInfo.m_instances.empty()) return;
    const auto bytes = inInfo.GetInstanceBufferSize();
    const auto& bufferInfo = inDstBuffer.GetBufferInformation();
    CHECK_TRUE(inDstBuffer.GetVkBuffer() != VK_NULL_HANDLE, "Instance upload buffer is not created!");
    CHECK_TRUE(CONTAIN_BITS(bufferInfo.memoryProperty,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        "Instance upload requires host-visible, host-coherent memory!");
    CHECK_TRUE(inOffset <= bufferInfo.size && bytes <= bufferInfo.size - inOffset,
        "Instance upload buffer range is out of bounds!");
    CHECK_TRUE(inOffset <= std::numeric_limits<size_t>::max() &&
        bytes <= std::numeric_limits<size_t>::max() - static_cast<size_t>(inOffset),
        "Instance upload range exceeds host address space!");
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(inInfo.m_instances.size());
    for (const auto& source : inInfo.m_instances)
    {
        CHECK_TRUE(source.instanceCustomIndex <= 0xFFFFFF && source.shaderGroupIndexOffset <= 0xFFFFFF,
            "Instance custom index or SBT offset exceeds 24 bits!");
        CHECK_TRUE(source.mask <= 0xFF && source.flags <= 0xFF, "Instance mask or flags exceeds 8 bits!");
        VkAccelerationStructureInstanceKHR instance{};
        if (source.accelStruct != nullptr)
        {
            instance.accelerationStructureReference = source.accelStruct->GetDeviceAddress();
            CHECK_TRUE(instance.accelerationStructureReference != 0, "Instance BLAS address is invalid!");
        }
        for (uint32_t row = 0; row < 3; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                instance.transform.matrix[row][column] = source.transform[column][row];
        instance.instanceCustomIndex = source.instanceCustomIndex;
        instance.instanceShaderBindingTableRecordOffset = source.shaderGroupIndexOffset;
        instance.mask = source.mask;
        instance.flags = source.flags;
        instances.push_back(instance);
    }
    inDstBuffer.CopyFromHost(instances.data(), static_cast<size_t>(inOffset), static_cast<size_t>(bytes));
}

BottomLevelAccelStruct::BottomLevelAccelStruct() = default;
BottomLevelAccelStruct::~BottomLevelAccelStruct() noexcept = default;
auto BottomLevelAccelStruct::IsValid() const noexcept -> bool
{ return GetVkAccelerationStructure() != VK_NULL_HANDLE; }
auto BottomLevelAccelStruct::GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR
{ return m_storage ? m_storage->handle : VK_NULL_HANDLE; }

TopLevelAccelStruct::TopLevelAccelStruct() = default;
TopLevelAccelStruct::~TopLevelAccelStruct() noexcept = default;
auto TopLevelAccelStruct::IsValid() const noexcept -> bool
{ return GetVkAccelerationStructure() != VK_NULL_HANDLE; }
auto TopLevelAccelStruct::GetVkAccelerationStructure() const noexcept -> VkAccelerationStructureKHR
{ return m_storage ? m_storage->handle : VK_NULL_HANDLE; }
