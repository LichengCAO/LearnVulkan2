#include "../partitioned_acceleration_structure.h"
#include "../buffer.h"
#include "../../device.h"
#include "../../allocator/private/memory_allocator.h"
#include "../../command/command_buffer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
    namespace Layout = AccelStructGpuLayout::Partitioned;
    using Operation = VkBuildPartitionedAccelerationStructureIndirectCommandNV;

    void CheckRange(VkDeviceAddress address, VkDeviceSize bytes, VkDeviceSize alignment)
    {
        CHECK_TRUE(address && bytes && address % alignment == 0 &&
            bytes <= (std::numeric_limits<VkDeviceAddress>::max)() - address,
            "PTLAS address range is null, misaligned or overflows!");
    }

    auto Disjoint(VkDeviceAddress a, VkDeviceSize aBytes, VkDeviceAddress b, VkDeviceSize bBytes) -> bool
    {
        return !aBytes || !bBytes || a + aBytes <= b || b + bBytes <= a;
    }

    auto MakeOperation(VkPartitionedAccelerationStructureOpTypeNV type, VkDeviceAddress address,
        uint32_t count, AccelStructBuildInfoLayout layout) -> Operation
    {
        if (count) CheckRange(address, VkDeviceSize{count} * layout.recordStride, layout.addressAlignment);
        else if (address) CheckRange(address, 1, layout.addressAlignment);
        return {type, count, {address, layout.recordStride}};
    }

    void CheckUploadRange(const Buffer& destination, VkDeviceSize offset, VkDeviceSize bytes, VkDeviceSize alignment)
    {
        const auto& info = destination.GetBufferInformation();
        CHECK_TRUE(destination.IsValid() && offset % alignment == 0 && offset <= info.size && bytes <= info.size - offset,
            "PTLAS upload range is invalid!");
        CHECK_TRUE(offset <= SIZE_MAX && bytes <= SIZE_MAX - offset, "PTLAS upload exceeds Host address space!");
    }

    template<typename T>
    void Upload(const std::vector<T>& encoded, Buffer& destination, VkDeviceSize offset, VkDeviceSize alignment)
    {
        const auto bytes = VkDeviceSize{encoded.size()} * sizeof(T);
        CheckUploadRange(destination, offset, bytes, alignment);
        Buffer::UploadToDeviceInline(destination, offset, encoded.data(), bytes);
    }

    void CheckSbt(uint32_t value)
    {
        CHECK_TRUE(value <= 0xFFFFFF, "PTLAS SBT offset must fit 24 bits!");
    }

    template<typename Record>
    void CheckUnique(std::unordered_set<uint32_t>& indices, const Record& record)
    {
        CHECK_TRUE(indices.insert(record.instanceIndex).second, "Duplicate PTLAS instance index!");
    }
}

PartitionedTopLevelAccelStruct::PartitionedTopLevelAccelStruct() = default;
PartitionedTopLevelAccelStruct::~PartitionedTopLevelAccelStruct() noexcept = default;

void PartitionedTopLevelAccelStruct::Create(const PartitionedTopLevelAccelStructCreateInfo& info)
{
    CHECK_TRUE(!IsValid(), "PTLAS is already created!");
    const auto& config = info.m_configuration;
    CHECK_TRUE(config.instanceCount && config.partitionCount && config.maxInstancesPerPartition,
        "PTLAS requires positive instance, partition and per-partition capacities!");
    auto& device = MyDevice::GetInstance();
    CHECK_TRUE(device.m_partitionedAccelerationStructureEnabled && device.vkDevice && device.vkPhysicalDevice &&
        device.GetMemoryAllocator() && device.GetMemoryAllocator()->IsValid() &&
        vkGetPartitionedAccelerationStructuresBuildSizesNV && vkGetPhysicalDeviceProperties2 &&
        vkGetBufferDeviceAddress && vkCreateBuffer && vkDestroyBuffer && vkGetBufferMemoryRequirements,
        "PTLAS requires VK_NV_partitioned_acceleration_structure enabled on the Device!");
    VkPhysicalDeviceMaintenance4Properties buffers{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
    VkPhysicalDeviceMaintenance3Properties allocations{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES, &buffers};
    VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV partitioned{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV, &allocations};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &partitioned};
    vkGetPhysicalDeviceProperties2(device.vkPhysicalDevice, &properties);
    CHECK_TRUE(config.partitionCount <= partitioned.maxPartitionCount &&
        config.maxInstancesInGlobalPartition <= partitioned.maxPartitionCount - config.partitionCount,
        "PTLAS ordinary partitions plus global instance capacity exceed maxPartitionCount!");
    VkPartitionedAccelerationStructureFlagsNV flags{};
    const auto input = config.MakeInput(flags);
    VkAccelerationStructureBuildSizesInfoKHR native{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetPartitionedAccelerationStructuresBuildSizesNV(device.vkDevice, &input, &native);
    const auto limit = (std::min)(buffers.maxBufferSize, allocations.maxMemoryAllocationSize);
    CHECK_TRUE(native.accelerationStructureSize && native.accelerationStructureSize <= limit &&
        native.buildScratchSize <= limit && Layout::OperationBufferSize <= limit,
        "PTLAS size query returned invalid sizes or exceeded Device buffer/allocation limits!");
    AccelStructMemoryRequirements sizes;
    sizes.accelerationStructureSize = native.accelerationStructureSize;
    sizes.accelerationStructureAlignment = Layout::StorageAlignment;
    sizes.buildScratchBufferSize = native.buildScratchSize;
    sizes.scratchAlignment = Layout::StorageAlignment;
    auto storage = std::make_unique<Buffer>();
    BufferCreateInfo bufferInfo;
    bufferInfo.SetBufferSize(sizes.accelerationStructureSize).SetBufferUsage(
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        .CustomizeAlignment(Layout::StorageAlignment).CustomizeMemoryProperty(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    storage->Create(bufferInfo);
    const auto address = storage->GetDeviceAddress();
    CheckRange(address, sizes.accelerationStructureSize, Layout::StorageAlignment);
    m_storage = std::move(storage);
    m_configuration = config;
    m_memoryRequirements = sizes;
    m_address = address;
}

void PartitionedTopLevelAccelStructOperationGroup::SetWriteInstances(VkDeviceAddress address, uint32_t count)
{
    m_operation = MakeOperation(VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_INSTANCE_NV,
        address, count, WriteInstanceRecord::GetBuildInfoLayout());
}
void PartitionedTopLevelAccelStructOperationGroup::SetUpdateInstances(VkDeviceAddress address, uint32_t count)
{
    m_operation = MakeOperation(VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_UPDATE_INSTANCE_NV,
        address, count, UpdateInstanceRecord::GetBuildInfoLayout());
}
void PartitionedTopLevelAccelStructOperationGroup::SetWritePartitionTranslations(VkDeviceAddress address, uint32_t count)
{
    m_operation = MakeOperation(VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_PARTITION_TRANSLATION_NV,
        address, count, WritePartitionTranslationRecord::GetBuildInfoLayout());
}

void PartitionedTopLevelAccelStructOperationGroup::CommitToDeviceInline(
    std::span<const WriteInstanceRecord> records, Buffer& destination, VkDeviceSize offset)
{
    if (records.empty()) return;
    CHECK_TRUE(records.size() <= UINT32_MAX, "Too many PTLAS records!");
    std::vector<VkPartitionedAccelerationStructureWriteInstanceDataNV> encoded;
    encoded.reserve(records.size());
    std::unordered_set<uint32_t> indices;
    constexpr VkPartitionedAccelerationStructureInstanceFlagsNV validFlags =
        VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_TRIANGLE_FACING_CULL_DISABLE_BIT_NV |
        VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_TRIANGLE_FLIP_FACING_BIT_NV |
        VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_OPAQUE_BIT_NV |
        VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_NO_OPAQUE_BIT_NV |
        VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_ENABLE_EXPLICIT_BOUNDING_BOX_NV;
    for (const auto& record : records)
    {
        CheckUnique(indices, record);
        CheckSbt(record.shaderGroupIndexOffset);
        CHECK_TRUE(record.mask <= 0xFF && !(record.flags & ~validFlags), "Invalid PTLAS mask or instance flags!");
        CHECK_TRUE((record.flags & (VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_OPAQUE_BIT_NV |
            VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_NO_OPAQUE_BIT_NV)) !=
            (VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_OPAQUE_BIT_NV |
            VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_NO_OPAQUE_BIT_NV), "Conflicting PTLAS opacity flags!");
        VkPartitionedAccelerationStructureWriteInstanceDataNV native{};
        for (uint32_t row = 0; row < 3; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                native.transform.matrix[row][column] = record.transform[column][row];
        if (record.flags & VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_ENABLE_EXPLICIT_BOUNDING_BOX_NV)
        {
            const auto& box = record.explicitAABB;
            const float values[]{box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ};
            CHECK_TRUE(std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); }) &&
                box.minX <= box.maxX && box.minY <= box.maxY && box.minZ <= box.maxZ, "Invalid PTLAS explicit AABB!");
            std::copy(std::begin(values), std::end(values), native.explicitAABB);
        }
        native.instanceID = record.instanceCustomIndex;
        native.instanceMask = record.mask;
        native.instanceContributionToHitGroupIndex = record.shaderGroupIndexOffset;
        native.instanceFlags = record.flags;
        native.instanceIndex = record.instanceIndex;
        native.partitionIndex = record.partitionIndex;
        native.accelerationStructure = record.accelStructReference;
        encoded.push_back(native);
    }
    Upload(encoded, destination, offset, Layout::WriteInstance::Layout.addressAlignment);
}

void PartitionedTopLevelAccelStructOperationGroup::CommitToDeviceInline(
    std::span<const UpdateInstanceRecord> records, Buffer& destination, VkDeviceSize offset)
{
    if (records.empty()) return;
    CHECK_TRUE(records.size() <= UINT32_MAX, "Too many PTLAS records!");
    std::vector<VkPartitionedAccelerationStructureUpdateInstanceDataNV> encoded;
    encoded.reserve(records.size());
    std::unordered_set<uint32_t> indices;
    for (const auto& record : records)
    {
        CheckUnique(indices, record);
        CheckSbt(record.shaderGroupIndexOffset);
        encoded.push_back({record.instanceIndex, record.shaderGroupIndexOffset, record.accelStructReference});
    }
    Upload(encoded, destination, offset, Layout::UpdateInstance::Layout.addressAlignment);
}

void PartitionedTopLevelAccelStructOperationGroup::CommitToDeviceInline(
    std::span<const WritePartitionTranslationRecord> records, Buffer& destination, VkDeviceSize offset)
{
    if (records.empty()) return;
    CHECK_TRUE(records.size() <= UINT32_MAX, "Too many PTLAS records!");
    std::vector<VkPartitionedAccelerationStructureWritePartitionTranslationDataNV> encoded;
    encoded.reserve(records.size());
    std::unordered_set<uint32_t> indices;
    for (const auto& record : records)
    {
        CHECK_TRUE(indices.insert(record.partitionIndex).second, "Duplicate PTLAS translation partition index!");
        const auto& v = record.partitionTranslation;
        encoded.push_back({record.partitionIndex, {v.x, v.y, v.z}});
    }
    Upload(encoded, destination, offset, Layout::Translation::Layout.addressAlignment);
}

void PartitionedTopLevelAccelStructOperationGroup::CommitToDeviceInline(
    std::span<const PartitionedTopLevelAccelStructOperationGroup> records, Buffer& destination, VkDeviceSize offset)
{
    if (records.empty()) return;
    CHECK_TRUE(records.size() <= Layout::MaxOperationGroupCount, "PTLAS supports at most three operation groups!");
    std::vector<Operation> encoded;
    std::unordered_set<uint32_t> types;
    for (const auto& record : records)
    {
        const auto& operation = record.m_operation;
        CHECK_TRUE(operation.opType >= VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_INSTANCE_NV &&
            operation.opType <= VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_PARTITION_TRANSLATION_NV &&
            types.insert(operation.opType).second, "Unset or duplicate PTLAS operation type!");
        encoded.push_back(operation);
    }
    CheckUploadRange(destination, offset, Layout::OperationBufferSize, Layout::Operation::Layout.addressAlignment);
    Upload(encoded, destination, offset, Layout::Operation::Layout.addressAlignment);
}

void BuildPartitionedTopLevelAccelStructCommandInput::SetTarget(const PartitionedTopLevelAccelStruct& target,
    VkDeviceAddress arguments, VkDeviceAddress count)
{
    CHECK_TRUE(target.IsValid(), "PTLAS target is not created!");
    CheckRange(arguments, Layout::OperationBufferSize, Layout::Operation::Layout.addressAlignment);
    CheckRange(count, sizeof(uint32_t), alignof(uint32_t));
    auto snapshot = m_snapshot;
    snapshot.configuration = target.m_configuration;
    snapshot.sizes = target.m_memoryRequirements;
    snapshot.target = target.m_address;
    snapshot.source = 0;
    snapshot.arguments = arguments;
    snapshot.count = count;
    m_snapshot = snapshot;
}

auto BuildPartitionedTopLevelAccelStructCommandInput::GetMemoryRequirements() const -> AccelStructMemoryRequirements
{
    CHECK_TRUE(m_snapshot.target, "Select a PTLAS target first!");
    return m_snapshot.sizes;
}

void BuildPartitionedTopLevelAccelStructCommandInput::CustomizeSourceAccelStruct(const PartitionedTopLevelAccelStruct& source)
{
    CHECK_TRUE(m_snapshot.target && source.IsValid() && source.m_configuration == m_snapshot.configuration &&
        source.m_memoryRequirements.accelerationStructureSize == m_snapshot.sizes.accelerationStructureSize,
        "PTLAS source requires a selected target with identical capacities, flags and translation setting!");
    if (source.m_address != m_snapshot.target)
        CHECK_TRUE(Disjoint(source.m_address, source.m_memoryRequirements.accelerationStructureSize,
            m_snapshot.target, m_snapshot.sizes.accelerationStructureSize), "PTLAS source overlaps target!");
    m_snapshot.source = source.m_address;
}

void CommandBuffer::BuildPartitionedTopLevelAccelStruct(const BuildPartitionedTopLevelAccelStructCommandInput& input)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(vkCmdBuildPartitionedAccelerationStructuresNV, "PTLAS build entrypoint is unavailable!");
    auto snapshot = input.m_snapshot;
    const auto asBytes = snapshot.sizes.accelerationStructureSize;
    const auto scratchBytes = snapshot.sizes.buildScratchBufferSize;
    CheckRange(snapshot.target, asBytes, Layout::StorageAlignment);
    if (snapshot.source) CheckRange(snapshot.source, asBytes, Layout::StorageAlignment);
    CheckRange(snapshot.arguments, Layout::OperationBufferSize, Layout::Operation::Layout.addressAlignment);
    CheckRange(snapshot.count, sizeof(uint32_t), alignof(uint32_t));
    if (scratchBytes) CheckRange(snapshot.scratch, scratchBytes, Layout::StorageAlignment);
    else snapshot.scratch = 0;
    const std::array ranges{
        std::pair{snapshot.target, asBytes}, std::pair{snapshot.source, snapshot.source ? asBytes : 0},
        std::pair{snapshot.scratch, scratchBytes}, std::pair{snapshot.arguments, Layout::OperationBufferSize},
        std::pair{snapshot.count, VkDeviceSize{sizeof(uint32_t)}}};
    for (size_t a = 0; a < ranges.size(); ++a)
        for (size_t b = a + 1; b < ranges.size(); ++b)
        {
            if (a == 0 && b == 1 && snapshot.source == snapshot.target) continue;
            CHECK_TRUE(Disjoint(ranges[a].first, ranges[a].second, ranges[b].first, ranges[b].second),
                "PTLAS build address ranges overlap!");
        }
    auto command = std::make_unique<BuildPartitionedAccelerationStructuresNVCommand>();
    command->snapshot = snapshot;
    _AppendOwnedCommand(std::move(command));
}
