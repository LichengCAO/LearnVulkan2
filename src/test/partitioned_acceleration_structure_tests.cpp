#include "clas_buffer_test_fixture.h"
#include "my_vulkan/device.h"
#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/resource/partitioned_acceleration_structure.h"
#include "my_vulkan/intrusive_ptr.h"
#include <algorithm>
#include <cstring>
#include <source_location>
#include <utility>
#include <thread>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

struct PartitionedAccelerationStructureTestProbe
{
    static auto Enable(bool value) -> bool
    { return std::exchange(MyDevice::GetInstance().m_partitionedAccelerationStructureEnabled, value); }
    static void SetComputeFamily(uint32_t value)
    { MyDevice::GetInstance().queueFamilyIndices.graphicsAndComputeFamily = value; }
    static auto Count(const CommandBuffer& commands) -> size_t { return commands.m_commands.size(); }
    static void Record(const CommandBuffer& commands)
    { for (const auto* command : commands.m_commands) command->Record(reinterpret_cast<VkCommandBuffer>(uintptr_t{1})); }
    static void Validate(const CommandBuffer& commands) { commands._ValidateQueueCapabilities(QueueFamilyType::COMPUTE); }
    static void Merge(CommandBuffer& commands, CommandBuffer&& tail) { commands._Append(std::move(tail)); }
    static auto Splittable(const CommandBuffer& commands) -> bool { return !commands.m_requiresSingleVkCommandBuffer; }
    static void SetRendering(CommandBuffer& commands, bool enabled)
    {
        if (enabled) commands.m_renderingScopeState = CommandBuffer::DynamicRenderingState{};
        else commands.m_renderingScopeState = std::monostate{};
    }
    static void AppendBorrowed(CommandBuffer& commands, const Command* command)
    { commands._AppendCommands(&command, 1); }
    static auto First(const CommandBuffer& commands) -> const Command* { return commands.m_commands.front(); }
};

namespace
{
    using Probe = PartitionedAccelerationStructureTestProbe;
    using Group = PartitionedTopLevelAccelStructOperationGroup;
    namespace Layout = AccelStructGpuLayout::Partitioned;
    struct State
    {
        uint32_t maxPartitions = 32, queries = 0, builds = 0;
        VkDeviceSize limit = 8192, asBytes = 1024, scratchBytes = 512;
        bool translation = false;
        VkPartitionedAccelerationStructureInstancesInputNV queried{};
        VkBuildPartitionedAccelerationStructureInfoNV built{};
        VkQueueFlags queueFlags = VK_QUEUE_COMPUTE_BIT;
    } state;

    template<class F>
    void ExpectFailure(F&& action, std::source_location where = std::source_location::current())
    {
        bool failed = false;
        try { action(); } catch (const std::exception&) { failed = true; }
        CHECK_TRUE(failed, std::format("Expected PTLAS failure at line {}!", where.line()));
    }
    void VKAPI_PTR Properties(VkPhysicalDevice, VkPhysicalDeviceProperties2* out)
    {
        for (auto* next = static_cast<VkBaseOutStructure*>(out->pNext); next; next = next->pNext)
            switch (next->sType)
            {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_PROPERTIES_NV:
                reinterpret_cast<VkPhysicalDevicePartitionedAccelerationStructurePropertiesNV*>(next)->maxPartitionCount = state.maxPartitions;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
                reinterpret_cast<VkPhysicalDeviceMaintenance3Properties*>(next)->maxMemoryAllocationSize = state.limit;
                break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES:
                reinterpret_cast<VkPhysicalDeviceMaintenance4Properties*>(next)->maxBufferSize = state.limit;
                break;
            }
    }
    auto CheckFlags(const VkPartitionedAccelerationStructureInstancesInputNV& input) -> bool
    {
        CHECK_TRUE(input.sType == VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCES_INPUT_NV && input.pNext);
        const auto& flags = *static_cast<const VkPartitionedAccelerationStructureFlagsNV*>(input.pNext);
        CHECK_TRUE(flags.sType == VK_STRUCTURE_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_FLAGS_NV && !flags.pNext);
        return flags.enablePartitionTranslation != 0;
    }
    void VKAPI_PTR Sizes(VkDevice, const VkPartitionedAccelerationStructureInstancesInputNV* input,
        VkAccelerationStructureBuildSizesInfoKHR* out)
    {
        ++state.queries;
        state.translation = CheckFlags(*input);
        state.queried = *input; state.queried.pNext = nullptr;
        out->accelerationStructureSize = state.asBytes;
        out->buildScratchSize = state.scratchBytes;
        out->updateScratchSize = 777; // PTLAS must ignore KHR update scratch.
    }
    void VKAPI_PTR Build(VkCommandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* input)
    {
        ++state.builds;
        CHECK_TRUE(input->sType == VK_STRUCTURE_TYPE_BUILD_PARTITIONED_ACCELERATION_STRUCTURE_INFO_NV && !input->pNext);
        state.translation = CheckFlags(input->input);
        state.built = *input; state.built.input.pNext = nullptr;
    }
    void VKAPI_PTR Queues(VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* out)
    {
        if (out) { out[0] = {}; out[0].queueFlags = state.queueFlags; out[0].queueCount = 1; }
        *count = 1;
    }
    struct Hooks
    {
        decltype(vkGetPhysicalDeviceProperties2) properties = std::exchange(vkGetPhysicalDeviceProperties2, Properties);
        decltype(vkGetPartitionedAccelerationStructuresBuildSizesNV) sizes = std::exchange(vkGetPartitionedAccelerationStructuresBuildSizesNV, Sizes);
        decltype(vkCmdBuildPartitionedAccelerationStructuresNV) build = std::exchange(vkCmdBuildPartitionedAccelerationStructuresNV, Build);
        decltype(vkGetPhysicalDeviceQueueFamilyProperties) queues = std::exchange(vkGetPhysicalDeviceQueueFamilyProperties, Queues);
        decltype(MyDevice::GetInstance().queueFamilyIndices) families = MyDevice::GetInstance().queueFamilyIndices;
        bool enabled = Probe::Enable(true);
        Hooks() { state = {}; Probe::SetComputeFamily(0); }
        ~Hooks()
        {
            vkGetPhysicalDeviceProperties2 = properties;
            vkGetPartitionedAccelerationStructuresBuildSizesNV = sizes;
            vkCmdBuildPartitionedAccelerationStructuresNV = build;
            vkGetPhysicalDeviceQueueFamilyProperties = queues;
            MyDevice::GetInstance().queueFamilyIndices = families;
            Probe::Enable(enabled);
        }
    };
    auto Configuration(bool translation = false) -> PartitionedTopLevelAccelStructCreateInfo
    {
        PartitionedTopLevelAccelStructCreateInfo info;
        info.SetMaxInstanceCount(8); info.SetPartitionCount(2); info.SetMaxInstanceCountPerPartition(4);
        info.CustomizeMaxInstanceCountInGlobalPartition(3);
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
        info.CustomizePartitionTranslation(translation);
        return info;
    }
    auto MakeUploadBuffer() -> Buffer
    {
        Buffer buffer; BufferCreateInfo info;
        info.SetBufferSize(1024).SetBufferUsage(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buffer.Create(info); return buffer;
    }
    template<class T>
    auto Read(const Buffer& buffer, size_t offset = 0) -> T
    { T value{}; std::memcpy(&value, ClasBufferTestFixture::Bytes(buffer).data() + offset, sizeof(value)); return value; }
    template<class F>
    void ExpectNoUpload(const Buffer& buffer, F&& action)
    {
        const auto bytes = ClasBufferTestFixture::Bytes(buffer);
        const std::vector<uint8_t> before(bytes.begin(), bytes.end());
        ExpectFailure(std::forward<F>(action));
        CHECK_TRUE(std::ranges::equal(before, ClasBufferTestFixture::Bytes(buffer)));
    }
#ifdef _DEBUG
    auto RejectHostAllocation(int type, void*, size_t, int, long, const unsigned char*, int) -> int
    { return type != _HOOK_ALLOC && type != _HOOK_REALLOC; }
#endif

    void Creation()
    {
        Hooks hooks;
        auto info = Configuration(true);
        PartitionedTopLevelAccelStruct owner;
        ExpectFailure([&] { owner.Create({}); });
        Probe::Enable(false); ExpectFailure([&] { owner.Create(info); }); Probe::Enable(true);
        CHECK_TRUE(!owner.IsValid() && state.queries == 0);
        auto& device = MyDevice::GetInstance();
        const auto deviceHandle = std::exchange(device.vkDevice, VK_NULL_HANDLE);
        ExpectFailure([&] { owner.Create(info); }); device.vkDevice = deviceHandle;
        const auto physical = std::exchange(device.vkPhysicalDevice, VK_NULL_HANDLE);
        ExpectFailure([&] { owner.Create(info); }); device.vkPhysicalDevice = physical;
        const auto sizeEntry = std::exchange(vkGetPartitionedAccelerationStructuresBuildSizesNV, nullptr);
        ExpectFailure([&] { owner.Create(info); }); vkGetPartitionedAccelerationStructuresBuildSizesNV = sizeEntry;
        CHECK_TRUE(state.queries == 0);
        for (auto field : {0, 1, 2})
        {
            auto invalid = info;
            if (field == 0) invalid.SetMaxInstanceCount(0);
            if (field == 1) invalid.SetPartitionCount(0);
            if (field == 2) invalid.SetMaxInstanceCountPerPartition(0);
            ExpectFailure([&] { owner.Create(invalid); });
        }
        state.maxPartitions = 4; ExpectFailure([&] { owner.Create(info); });
        state.maxPartitions = UINT32_MAX;
        auto overflow = info; overflow.SetPartitionCount(UINT32_MAX); overflow.CustomizeMaxInstanceCountInGlobalPartition(1);
        ExpectFailure([&] { owner.Create(overflow); }); state.maxPartitions = 32;
        state.limit = 512; ExpectFailure([&] { owner.Create(info); }); state.limit = 8192;
        state.asBytes = 0; ExpectFailure([&] { owner.Create(info); }); state.asBytes = 1024;
        state.scratchBytes = 9000; ExpectFailure([&] { owner.Create(info); }); state.scratchBytes = 512;
        ClasBufferTestFixture::SetAllocationFailure(true);
        ExpectFailure([&] { owner.Create(info); }); ClasBufferTestFixture::SetAllocationFailure(false);
        ClasBufferTestFixture::SetBufferAddressOverride(0x100001);
        ExpectFailure([&] { owner.Create(info); }); ClasBufferTestFixture::SetBufferAddressOverride(0);
        CHECK_TRUE(!owner.IsValid() && owner.GetMemoryRequirements().accelerationStructureSize == 0 && ClasBufferTestFixture::LiveBufferCount() == 0);
        owner.Create(info);
        CHECK_TRUE(owner.IsValid() && state.translation && state.queried.instanceCount == 8 &&
            state.queried.partitionCount == 2 && state.queried.maxInstancePerPartitionCount == 4 &&
            state.queried.maxInstanceInGlobalPartitionCount == 3 && state.builds == 0 &&
            state.queried.flags == VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
        const auto& sizes = owner.GetMemoryRequirements();
        CHECK_TRUE(sizes.accelerationStructureSize == 1024 && sizes.accelerationStructureAlignment == 256 &&
            sizes.buildScratchBufferSize == 512 && sizes.scratchAlignment == 256 &&
            sizes.updateScratchBufferSize == std::nullopt);
        const auto queries = state.queries; const auto address = owner.GetDeviceAddress();
        ExpectFailure([&] { owner.Create(info); });
        CHECK_TRUE(state.queries == queries && owner.GetDeviceAddress() == address && ClasBufferTestFixture::LiveBufferCount() == 1);
    }

    void Encoders()
    {
        Buffer empty;
        Group::CommitToDeviceInline(std::span<const Group>{}, empty, UINT64_MAX);
        Group::CommitToDeviceInline(std::span<const Group::WriteInstanceRecord>{}, empty, UINT64_MAX);
        Group::CommitToDeviceInline(std::span<const Group::UpdateInstanceRecord>{}, empty, UINT64_MAX);
        Group::CommitToDeviceInline(std::span<const Group::WritePartitionTranslationRecord>{}, empty, UINT64_MAX);
        auto buffer = MakeUploadBuffer();
        std::array<Group::WriteInstanceRecord, 2> records{};
        auto& record = records[0];
        for (uint32_t col = 0; col < 4; ++col)
            for (uint32_t row = 0; row < 4; ++row) record.transform[col][row] = float(col * 10 + row);
        record.instanceCustomIndex = UINT32_MAX; record.instanceIndex = 7;
        record.partitionIndex = VK_PARTITIONED_ACCELERATION_STRUCTURE_PARTITION_INDEX_GLOBAL_NV;
        record.shaderGroupIndexOffset = 0xFFFFFF; record.mask = 0xFF;
        record.flags = VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_ENABLE_EXPLICIT_BOUNDING_BOX_NV;
        record.explicitAABB = {-1, -2, -3, 4, 5, 6};
        records[1].instanceIndex = 8; records[1].accelStructReference = 0x123456789;
        Group::CommitToDeviceInline(records, buffer, 8);
        const auto native = Read<VkPartitionedAccelerationStructureWriteInstanceDataNV>(buffer, 8);
        CHECK_TRUE(native.transform.matrix[1][3] == 31 && native.transform.matrix[2][0] == 2 &&
            native.instanceID == UINT32_MAX && native.partitionIndex == UINT32_MAX && native.accelerationStructure == 0 &&
            native.instanceContributionToHitGroupIndex == 0xFFFFFF && native.explicitAABB[5] == 6);
        const auto before = std::vector<uint8_t>(ClasBufferTestFixture::Bytes(buffer).begin(), ClasBufferTestFixture::Bytes(buffer).end());
        const auto failsWithoutWrite = [&](const auto& action)
        { ExpectFailure(action); CHECK_TRUE(std::ranges::equal(before, ClasBufferTestFixture::Bytes(buffer))); };
        records[1].instanceIndex = 7;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); }); records[1].instanceIndex = 8;
        record.mask = 256; failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); }); record.mask = 255;
        record.shaderGroupIndexOffset = 0x1000000;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); }); record.shaderGroupIndexOffset = 0;
        record.flags = 0x80000000;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); });
        record.flags = VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_OPAQUE_BIT_NV |
            VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_NO_OPAQUE_BIT_NV;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); });
        record.flags = VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_ENABLE_EXPLICIT_BOUNDING_BOX_NV;
        record.explicitAABB.maxX = -2;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 8); }); record.flags = 0;
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 1); });
        failsWithoutWrite([&] { Group::CommitToDeviceInline(records, buffer, 1000); });
        std::array<Group::UpdateInstanceRecord, 2> updates{{{0xFFFFFF, 0, 5}, {0, 0x1234, 6}}};
        Group::CommitToDeviceInline(updates, buffer, 0);
        const auto update = Read<VkPartitionedAccelerationStructureUpdateInstanceDataNV>(buffer);
        CHECK_TRUE(update.instanceIndex == 5 && update.instanceContributionToHitGroupIndex == 0xFFFFFF && !update.accelerationStructure);
        updates[1].instanceIndex = 5; ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(updates, buffer, 0); });
        updates[1].instanceIndex = 6; updates[1].shaderGroupIndexOffset = 0x1000000;
        ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(updates, buffer, 0); });
        std::array<Group::WritePartitionTranslationRecord, 2> translations{{{0, {1, 2, 3}}, {1, {4, 5, 6}}}};
        Group::CommitToDeviceInline(translations, buffer, 4);
        const auto translation = Read<VkPartitionedAccelerationStructureWritePartitionTranslationDataNV>(buffer, 4);
        CHECK_TRUE(translation.partitionTranslation[2] == 3 && translation.partitionIndex == 0);
        translations[1].partitionIndex = 0; ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(translations, buffer, 4); });
        std::array<Group, 3> groups;
        groups[0].SetWriteInstances(0x1000, 2); groups[1].SetUpdateInstances(0, 0); groups[2].SetWritePartitionTranslations(0x2004, 1);
        ExpectFailure([&] { groups[0].SetUpdateInstances(0x1001, 2); });
        ExpectFailure([&] { groups[0].SetWriteInstances(UINT64_MAX - 7, 2); });
        Group::CommitToDeviceInline(groups, buffer, 0);
        const auto operation = Read<VkBuildPartitionedAccelerationStructureIndirectCommandNV>(buffer);
        CHECK_TRUE(operation.opType == VK_PARTITIONED_ACCELERATION_STRUCTURE_OP_TYPE_WRITE_INSTANCE_NV &&
            operation.argCount == 2 && operation.argData.startAddress == 0x1000 && operation.argData.strideInBytes == 104);
        CHECK_TRUE(Read<VkBuildPartitionedAccelerationStructureIndirectCommandNV>(buffer, 24).argData.strideInBytes == 16);
        groups[1] = groups[0]; ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(groups, buffer, 0); });
        groups[1] = {}; ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(groups, buffer, 0); });
        std::array<Group, 4> tooMany{}; ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(tooMany, buffer, 0); });
        // Even a single operation requires room for all 72 accessible outer-table bytes.
        ExpectNoUpload(buffer, [&] { Group::CommitToDeviceInline(std::span(groups).first(1), buffer, 976); });
    }

    void Lifetime()
    {
        Hooks hooks;
        auto owner = MakeIntrusive<PartitionedTopLevelAccelStruct>();
        owner->Create(Configuration());
        auto retained = owner; owner.reset();
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 1 && state.builds == 0);
        // Release uses Buffer's saved dependencies and may run on another thread.
        auto& device = MyDevice::GetInstance();
        const auto saved = std::exchange(device.vkDevice, VK_NULL_HANDLE);
        std::jthread release([resource = std::move(retained)] {});
        release.join();
        device.vkDevice = saved;
        CHECK_TRUE(ClasBufferTestFixture::LiveBufferCount() == 0 && state.builds == 0);
    }

    void Commands()
    {
        Hooks hooks;
        PartitionedTopLevelAccelStruct a, b, incompatible;
        a.Create(Configuration(true)); b.Create(Configuration(true)); incompatible.Create(Configuration());
        BuildPartitionedTopLevelAccelStructCommandInput input;
        ExpectFailure([&] { input.CustomizeSourceAccelStruct(a); });
        ExpectFailure([&] { input.GetMemoryRequirements(); });
        input.SetScratchAddress(0x400000);
        input.SetTarget(a, 0x500000, 0x600000); input.CustomizeSourceAccelStruct(a);
        PartitionedTopLevelAccelStruct empty;
        ExpectFailure([&] { input.SetTarget(empty, 0x500000, 0x600000); });
        ExpectFailure([&] { input.SetTarget(b, 0x500001, 0x600000); });
        ExpectFailure([&] { input.SetTarget(b, 0x500000, 0); });
        ExpectFailure([&] { input.SetTarget(b, 0x500000, 0x600001); });
        ExpectFailure([&] { input.SetTarget(b, UINT64_MAX - 7, 0x600000); });
        ExpectFailure([&] { input.CustomizeSourceAccelStruct(incompatible); });
        for (uint32_t field = 0; field < 5; ++field)
        {
            auto info = Configuration(true);
            switch (field)
            {
            case 0: info.SetMaxInstanceCount(9); break;
            case 1: info.SetPartitionCount(3); break;
            case 2: info.SetMaxInstanceCountPerPartition(5); break;
            case 3: info.CustomizeMaxInstanceCountInGlobalPartition(4); break;
            case 4: info.CustomizeBuildFlags(0); break;
            }
            PartitionedTopLevelAccelStruct other; other.Create(info);
            ExpectFailure([&] { input.CustomizeSourceAccelStruct(other); });
        }
        {
            PartitionedTopLevelAccelStruct overlapping;
            ClasBufferTestFixture::SetBufferAddressOverride(a.GetDeviceAddress() + 256);
            overlapping.Create(Configuration(true)); ClasBufferTestFixture::SetBufferAddressOverride(0);
            ExpectFailure([&] { input.CustomizeSourceAccelStruct(overlapping); });
        }
        auto copy = input; auto moved = std::move(copy); input = {};
        CommandBuffer commands;
        commands.BuildPartitionedTopLevelAccelStruct(moved);
        moved = {};
        const auto queries = state.queries; const auto allocations = ClasBufferTestFixture::LiveBufferCount();
#ifdef _DEBUG
        const auto previousAllocHook = _CrtSetAllocHook(RejectHostAllocation);
        try { Probe::Record(commands); }
        catch (...) { _CrtSetAllocHook(previousAllocHook); throw; }
        _CrtSetAllocHook(previousAllocHook);
#else
        Probe::Record(commands);
#endif
        CHECK_TRUE(state.builds == 1 && state.built.srcAccelerationStructureData == a.GetDeviceAddress() &&
            state.built.dstAccelerationStructureData == a.GetDeviceAddress() && state.built.scratchData == 0x400000 &&
            state.translation && state.queries == queries && ClasBufferTestFixture::LiveBufferCount() == allocations);
        CommandBuffer split;
        Probe::AppendBorrowed(split, Probe::First(commands));
        CHECK_TRUE(Probe::Splittable(split));
        input.SetScratchAddress(0x400000); input.SetTarget(b, 0x500000, 0x600000); input.CustomizeSourceAccelStruct(a);
        commands.BuildPartitionedTopLevelAccelStruct(input); Probe::Record(commands);
        CHECK_TRUE(state.built.srcAccelerationStructureData == a.GetDeviceAddress() && state.built.dstAccelerationStructureData == b.GetDeviceAddress());
        input.SetTarget(b, 0x500000, 0x600000);
        CommandBuffer reselected; reselected.BuildPartitionedTopLevelAccelStruct(input); Probe::Record(reselected);
        CHECK_TRUE(!state.built.srcAccelerationStructureData && state.built.scratchData == 0x400000);
        const auto count = Probe::Count(reselected);
        for (const auto address : {b.GetDeviceAddress(), VkDeviceAddress{0x500000}, VkDeviceAddress{0x600000}, VkDeviceAddress{0x400001}, VkDeviceAddress{0}})
        {
            input.SetScratchAddress(address); ExpectFailure([&] { reselected.BuildPartitionedTopLevelAccelStruct(input); });
            CHECK_TRUE(Probe::Count(reselected) == count);
        }
        input.SetScratchAddress(0x400000); input.SetTarget(a, 0x500000, 0x500004);
        ExpectFailure([&] { reselected.BuildPartitionedTopLevelAccelStruct(input); });
        input.SetTarget(a, a.GetDeviceAddress(), 0x600000);
        ExpectFailure([&] { reselected.BuildPartitionedTopLevelAccelStruct(input); });
        input.SetTarget(b, 0x500000, 0x600000); input.CustomizeSourceAccelStruct(a); input.SetScratchAddress(a.GetDeviceAddress());
        ExpectFailure([&] { reselected.BuildPartitionedTopLevelAccelStruct(input); });
        input.SetScratchAddress(0x400000);
        Probe::SetRendering(reselected, true);
        ExpectFailure([&] { reselected.BuildPartitionedTopLevelAccelStruct(input); });
        CommandBuffer tail; tail.BuildPartitionedTopLevelAccelStruct(input);
        ExpectFailure([&] { Probe::Merge(reselected, std::move(tail)); });
        BuildPartitionedAccelerationStructuresNVCommand borrowed;
        ExpectFailure([&] { Probe::AppendBorrowed(reselected, &borrowed); });
        Probe::SetRendering(reselected, false);
        Probe::Merge(reselected, std::move(tail));
        CommandBuffer transferred = std::move(reselected);
        Probe::Validate(transferred);
        CHECK_TRUE(Probe::Splittable(transferred));
        state.queueFlags = VK_QUEUE_TRANSFER_BIT; ExpectFailure([&] { Probe::Validate(transferred); });
        Probe::Validate(reselected); // Moving clears the source's PTLAS flag.
        state.scratchBytes = 0;
        PartitionedTopLevelAccelStruct zero; zero.Create(Configuration());
        input = {}; input.SetTarget(zero, 0x500000, 0x600000); input.SetScratchAddress(123);
        CommandBuffer zeroCommands; zeroCommands.BuildPartitionedTopLevelAccelStruct(input); Probe::Record(zeroCommands);
        CHECK_TRUE(!state.built.scratchData);
    }
}

void RunPartitionedAccelerationStructureTests()
{
    static_assert(Group::GetBuildInfoLayout().recordStride == 24 && Group::GetBuildInfoLayout().addressAlignment == 8);
    static_assert(Group::WriteInstanceRecord::GetBuildInfoLayout().recordStride == 104);
    static_assert(Group::UpdateInstanceRecord::GetBuildInfoLayout().recordStride == 16);
    static_assert(Group::WritePartitionTranslationRecord::GetBuildInfoLayout().addressAlignment == 4);
    static_assert(!std::is_copy_constructible_v<PartitionedTopLevelAccelStruct> && !std::is_move_constructible_v<PartitionedTopLevelAccelStruct>);
    static_assert(std::is_copy_constructible_v<BuildPartitionedTopLevelAccelStructCommandInput> &&
        std::is_move_constructible_v<BuildPartitionedTopLevelAccelStructCommandInput>);
    ClasBufferTestFixture::Run(Creation);
    ClasBufferTestFixture::Run(Encoders);
    ClasBufferTestFixture::Run(Lifetime);
    ClasBufferTestFixture::Run(Commands);
}
