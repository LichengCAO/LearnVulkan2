#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/device.h"
#include "my_vulkan/render_context/device_context.h"
#include "my_vulkan/resource/cluster_acceleration_structure.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/resource/partitioned_acceleration_structure.h"
#include "utility/task_scheduler.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
constexpr std::array kClusterDeviceExtensions{
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME
};
constexpr VkMemoryPropertyFlags kHostMemory =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr VkBufferUsageFlags kBuildInputUsage =
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
constexpr VkBufferUsageFlags kScratchUsage =
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

void CheckResult(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::format("CLAS GPU smoke {} failed with VkResult {}", operation, int(result)));
}

auto VKAPI_PTR FailFenceWait(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) -> VkResult
{
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

template<typename T>
class ScopedValue final
{
    T& m_target;
    T m_previous;
public:
    ScopedValue(T& target, T value) : m_target(target), m_previous(std::exchange(target, std::move(value))) {}
    ScopedValue(const ScopedValue&) = delete;
    ~ScopedValue() { m_target = std::move(m_previous); }
};

// Load a separate reference to the Vulkan loader without changing Volk's
// process-wide loaded module, instance, or device.
class VulkanLoader final
{
#if defined(_WIN32)
    HMODULE m_module = LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
    void* m_module = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
#else
    void* m_module = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
public:
    VulkanLoader() = default;
    VulkanLoader(const VulkanLoader&) = delete;
    ~VulkanLoader()
    {
        if (!m_module)
            return;
#if defined(_WIN32)
        FreeLibrary(m_module);
#else
        dlclose(m_module);
#endif
    }

    auto GetInstanceProcAddr() const -> PFN_vkGetInstanceProcAddr
    {
        CHECK_TRUE(m_module, "CLAS GPU smoke could not load Vulkan.");
#if defined(_WIN32)
        auto proc = GetProcAddress(m_module, "vkGetInstanceProcAddr");
#else
        auto proc = dlsym(m_module, "vkGetInstanceProcAddr");
#endif
        CHECK_TRUE(proc, "CLAS GPU smoke could not load vkGetInstanceProcAddr.");
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc);
    }
};

class HeadlessDevice final
{
    static constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;

    auto _FindQueue(VkPhysicalDevice candidate) const -> std::optional<uint32_t>
    {
        uint32_t count = 0;
        instanceFunctions.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        instanceFunctions.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, queues.data());
        for (uint32_t index = 0; index < count; ++index)
        {
            if (queues[index].queueCount > 0 && CONTAIN_BITS(queues[index].queueFlags, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
                return index;
        }
        return std::nullopt;
    }

    auto _DeviceExtensions() const -> std::array<const char*, 3>
    {
        auto extensions = kClusterDeviceExtensions;
        if (partitionedMode) extensions[2] = VK_NV_PARTITIONED_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        return extensions;
    }

    auto _GetExtensionRevision(VkPhysicalDevice candidate) const -> uint32_t
    {
        uint32_t count = 0;
        CheckResult(instanceFunctions.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr),
            "counting device extensions");
        std::vector<VkExtensionProperties> extensions(count);
        CheckResult(instanceFunctions.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()),
            "enumerating device extensions");
        uint32_t revision = 0;
        for (const char* name : _DeviceExtensions())
        {
            auto entry = std::find_if(extensions.begin(), extensions.begin() + count,
                [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
            if (entry == extensions.begin() + count)
                return 0;
            if (std::strcmp(name, _DeviceExtensions()[2]) == 0)
                revision = entry->specVersion;
        }
        return revision;
    }

    auto _SelectPhysicalDevice() -> bool
    {
        uint32_t count = 0;
        CheckResult(instanceFunctions.vkEnumeratePhysicalDevices(instance, &count, nullptr), "counting physical devices");
        if (!count && partitionedMode) return false;
        CHECK_TRUE(count > 0, "CLAS GPU smoke found no Vulkan physical devices.");
        std::vector<VkPhysicalDevice> candidates(count);
        CheckResult(instanceFunctions.vkEnumeratePhysicalDevices(instance, &count, candidates.data()),
            "enumerating physical devices");
        for (uint32_t index = 0; index < count; ++index)
        {
            VkPhysicalDevice candidate = candidates[index];
            VkPhysicalDeviceProperties candidateProperties{};
            instanceFunctions.vkGetPhysicalDeviceProperties(candidate, &candidateProperties);
            if (candidateProperties.apiVersion < kApiVersion)
                continue;
            const uint32_t revision = _GetExtensionRevision(candidate);
            const auto family = _FindQueue(candidate);
            if (revision < (partitionedMode ? 1u : 2u) || !family)
                continue;

            VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV partitioned{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV};
            VkPhysicalDeviceClusterAccelerationStructureFeaturesNV cluster{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
            VkPhysicalDeviceBufferDeviceAddressFeatures address{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
            VkPhysicalDeviceSynchronization2Features sync{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
            VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            features.pNext = &sync;
            sync.pNext = &address;
            address.pNext = &accel;
            accel.pNext = partitionedMode ? static_cast<void*>(&partitioned) : static_cast<void*>(&cluster);
            instanceFunctions.vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (sync.synchronization2 && address.bufferDeviceAddress && accel.accelerationStructure &&
                (partitionedMode ? partitioned.partitionedAccelerationStructure : cluster.clusterAccelerationStructure))
            {
                physicalDevice = candidate;
                properties = candidateProperties;
                queueFamily = *family;
                clusterRevision = revision;
                return true;
            }
        }
        if (partitionedMode) return false;
        throw std::runtime_error(
            "CLAS GPU smoke requires Vulkan 1.3, a graphics and compute queue, synchronization2, buffer device addresses, "
            "acceleration structures and VK_NV_cluster_acceleration_structure revision 2 or newer.");
    }

public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    uint32_t clusterRevision = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceClusterAccelerationStructurePropertiesNV clusterProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV };
    VolkInstanceTable instanceFunctions{};
    VolkDeviceTable deviceFunctions{};
    bool partitionedMode = false;
    explicit HeadlessDevice(bool partitioned = false) : partitionedMode(partitioned) {}
    HeadlessDevice(const HeadlessDevice&) = delete;
    ~HeadlessDevice()
    {
        if (device != VK_NULL_HANDLE)
            deviceFunctions.vkDestroyDevice(device, nullptr);
        if (instance != VK_NULL_HANDLE)
            instanceFunctions.vkDestroyInstance(instance, nullptr);
    }

    auto Create() -> bool
    {
        const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
        CHECK_TRUE(createInstance, "CLAS GPU smoke could not load vkCreateInstance.");
        VkApplicationInfo application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        application.pApplicationName = "LearnVulkan CLAS GPU smoke";
        application.apiVersion = kApiVersion;
        VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instanceInfo.pApplicationInfo = &application;
        VkInstance createdInstance = VK_NULL_HANDLE;
        CheckResult(createInstance(&instanceInfo, nullptr, &createdInstance), "creating a Vulkan 1.3 instance");
        instance = createdInstance;
        volkLoadInstanceTable(&instanceFunctions, instance);
        if (!_SelectPhysicalDevice()) return false;

        VkPhysicalDeviceDriverProperties driver{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 queried{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        queried.pNext = &driver;
        driver.pNext = &accelerationStructureProperties;
        accelerationStructureProperties.pNext = partitionedMode ? nullptr : &clusterProperties;
        instanceFunctions.vkGetPhysicalDeviceProperties2(physicalDevice, &queried);
        accelerationStructureProperties.pNext = nullptr;
        std::cout << std::format("{} GPU smoke using {} (driver {}, extension revision {}).\n",
            partitionedMode ? "PTLAS" : "CLAS", properties.deviceName, driver.driverInfo, clusterRevision);

        VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV partitioned{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV};
        partitioned.partitionedAccelerationStructure = VK_TRUE;
        VkPhysicalDeviceClusterAccelerationStructureFeaturesNV cluster{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
        cluster.clusterAccelerationStructure = VK_TRUE;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        accel.accelerationStructure = VK_TRUE;
        accel.pNext = partitionedMode ? static_cast<void*>(&partitioned) : static_cast<void*>(&cluster);
        VkPhysicalDeviceBufferDeviceAddressFeatures address{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
        address.bufferDeviceAddress = VK_TRUE;
        address.pNext = &accel;
        VkPhysicalDeviceSynchronization2Features sync{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
        sync.synchronization2 = VK_TRUE;
        sync.pNext = &address;
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.pNext = &sync;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        const auto extensions = _DeviceExtensions();
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();
        VkDevice createdDevice = VK_NULL_HANDLE;
        CheckResult(instanceFunctions.vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &createdDevice), "creating a logical device");
        device = createdDevice;
        volkLoadDeviceTable(&deviceFunctions, device);
        CHECK_TRUE(partitionedMode ? (deviceFunctions.vkGetPartitionedAccelerationStructuresBuildSizesNV &&
            deviceFunctions.vkCmdBuildPartitionedAccelerationStructuresNV) : (deviceFunctions.vkGetClusterAccelerationStructureBuildSizesNV &&
            deviceFunctions.vkCmdBuildClusterAccelerationStructureIndirectNV),
            "CLAS GPU smoke could not resolve the enabled CLAS device commands.");
        deviceFunctions.vkGetDeviceQueue(device, queueFamily, 0, &queue);
        return true;
    }
};

class SmokeSubmission final
{
    const HeadlessDevice& m_device;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

public:
    explicit SmokeSubmission(const HeadlessDevice& device) : m_device(device) {}
    SmokeSubmission(const SmokeSubmission&) = delete;
    ~SmokeSubmission()
    {
        const auto& functions = m_device.deviceFunctions;
        if (m_fence != VK_NULL_HANDLE)
            functions.vkDestroyFence(m_device.device, m_fence, nullptr);
        if (m_pool != VK_NULL_HANDLE)
            functions.vkDestroyCommandPool(m_device.device, m_pool, nullptr);
    }

    auto Begin() -> VkCommandBuffer
    {
        const auto& functions = m_device.deviceFunctions;
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = m_device.queueFamily;
        VkCommandPool createdPool = VK_NULL_HANDLE;
        CheckResult(functions.vkCreateCommandPool(m_device.device, &poolInfo, nullptr, &createdPool), "creating a command pool");
        m_pool = createdPool;
        VkCommandBufferAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocateInfo.commandPool = m_pool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        CheckResult(functions.vkAllocateCommandBuffers(m_device.device, &allocateInfo, &command), "allocating a command buffer");
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckResult(functions.vkBeginCommandBuffer(command, &beginInfo), "beginning command recording");
        return command;
    }

    void SubmitAndWait(VkCommandBuffer command)
    {
        const auto& functions = m_device.deviceFunctions;
        CheckResult(functions.vkEndCommandBuffer(command), "ending command recording");
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkFence createdFence = VK_NULL_HANDLE;
        CheckResult(functions.vkCreateFence(m_device.device, &fenceInfo, nullptr, &createdFence), "creating a fence");
        m_fence = createdFence;
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        CheckResult(functions.vkQueueSubmit(m_device.queue, 1, &submit, m_fence), "submitting CLAS and Cluster BLAS builds");
        const VkResult result = functions.vkWaitForFences(m_device.device, 1, &m_fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS)
        {
            functions.vkDeviceWaitIdle(m_device.device);
            CheckResult(result, "waiting for CLAS and Cluster BLAS builds");
        }
    }
};

auto MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VkDeviceSize alignment = 1) -> Buffer
{
    Buffer buffer;
    BufferCreateInfo info;
    info.SetBufferSize(size).SetBufferUsage(usage).CustomizeMemoryProperty(memory);
    if (alignment > 1)
        info.CustomizeAlignment(alignment);
    buffer.Create(info);
    return buffer;
}

void AppendBarrier(CommandBuffer& commands, PFN_vkCmdPipelineBarrier barrier,
    VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
{
    commands.AppendCommand([=](VkCommandBuffer command)
    {
        VkMemoryBarrier memory{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        memory.srcAccessMask = srcAccess;
        memory.dstAccessMask = dstAccess;
        barrier(command, srcStage, dstStage, 0, 1, &memory, 0, nullptr, 0, nullptr);
    });
}
}

// PTLAS reuses the headless allocation/submission fixture, with CLAS disabled.
struct PartitionedAccelerationStructureGpuTestProbe
{
private:
    static void _Submit(const HeadlessDevice& native, const CommandBuffer& commands)
    {
        commands._ValidateQueueCapabilities(QueueFamilyType::COMPUTE);
        SmokeSubmission submission(native);
        const auto command = submission.Begin();
        for (const auto* recorded : commands.m_commands) recorded->Record(command);
        submission.SubmitAndWait(command);
    }
public:
    static void Build(const HeadlessDevice& native)
    {
        using Group = PartitionedTopLevelAccelStructOperationGroup;
        constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        constexpr auto read = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        constexpr auto write = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
        const std::array<float, 9> triangle{0, 0, 0, 1, 0, 0, 0, 1, 0};
        auto vertices = MakeBuffer(sizeof(triangle), kBuildInputUsage, kHostMemory);
        Buffer::UploadToDeviceInline(vertices, 0, triangle.data(), sizeof(triangle));
        AccelStructTriangleGeometryLimits geometry;
        geometry.SetMaxTriangleCount(1); geometry.SetMaxVertexCount(3);
        BottomLevelAccelStructCreateInfo blasInfo; blasInfo.SetTriangles(std::span(&geometry, 1));
        BottomLevelAccelStruct blas; blas.Create(blasInfo);
        auto blasScratch = MakeBuffer(blas.GetMemoryRequirements().buildScratchBufferSize, kScratchUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blas.GetMemoryRequirements().scratchAlignment);
        AccelStructTriangleBuildInfo triangleBuild;
        triangleBuild.SetVertexBuffer(vertices.GetDeviceAddress(), 3); triangleBuild.SetTriangleCount(1);
        BuildAccelStructCommandInput blasBuild; blasBuild.SetTrianglesTarget(blas, std::span(&triangleBuild, 1));
        blasBuild.SetScratchAddress(blasScratch.GetDeviceAddress());

        PartitionedTopLevelAccelStructCreateInfo info;
        info.SetMaxInstanceCount(2); info.SetPartitionCount(2); info.SetMaxInstanceCountPerPartition(2);
        info.CustomizeMaxInstanceCountInGlobalPartition(2); info.CustomizePartitionTranslation(true);
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
        PartitionedTopLevelAccelStruct a, b; a.Create(info); b.Create(info);
        const auto& sizes = a.GetMemoryRequirements();
        auto scratch = sizes.buildScratchBufferSize ? MakeBuffer(sizes.buildScratchBufferSize, kScratchUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizes.scratchAlignment) : Buffer{};
        auto writes = MakeBuffer(208, kBuildInputUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto updates = MakeBuffer(32, kBuildInputUsage, kHostMemory);
        auto translations = MakeBuffer(32, kBuildInputUsage, kHostMemory);
        auto operations = MakeBuffer(72, kBuildInputUsage, kHostMemory);
        auto count = MakeBuffer(4, kBuildInputUsage, kHostMemory);
        std::array<Group::WriteInstanceRecord, 2> records;
        for (uint32_t i = 0; i < records.size(); ++i)
        {
            records[i].instanceIndex = i;
            records[i].instanceCustomIndex = 0xFFFF0000 + i;
            records[i].accelStructReference = blas.GetDeviceAddress();
            records[i].flags = VK_PARTITIONED_ACCELERATION_STRUCTURE_INSTANCE_FLAG_ENABLE_EXPLICIT_BOUNDING_BOX_NV;
            records[i].explicitAABB = {-10, -10, -10, 10, 10, 10};
        }
        records[1].partitionIndex = VK_PARTITIONED_ACCELERATION_STRUCTURE_PARTITION_INDEX_GLOBAL_NV;
        Group::CommitToDeviceInline(records, writes, 0);
        const std::array<Group::WritePartitionTranslationRecord, 2> initialTranslations{{{0, {0, 0, 0}}, {1, {0, 0, 0}}}};
        Group::CommitToDeviceInline(initialTranslations, translations, 0);
        std::array<Group, 2> initialGroups;
        initialGroups[0].SetWriteInstances(writes.GetDeviceAddress(), 2);
        initialGroups[1].SetWritePartitionTranslations(translations.GetDeviceAddress(), 2);
        Group::CommitToDeviceInline(initialGroups, operations, 0);
        uint32_t groupCount = 2;
        Buffer::UploadToDeviceInline(count, 0, &groupCount, sizeof(groupCount));
        const auto append = [&](CommandBuffer& commands, const PartitionedTopLevelAccelStruct& target,
            const PartitionedTopLevelAccelStruct* source)
        {
            BuildPartitionedTopLevelAccelStructCommandInput input;
            input.SetTarget(target, operations.GetDeviceAddress(), count.GetDeviceAddress());
            input.SetScratchAddress(scratch.IsValid() ? scratch.GetDeviceAddress() : 0);
            if (source) input.CustomizeSourceAccelStruct(*source);
            commands.BuildPartitionedTopLevelAccelStruct(input);
        };
        CommandBuffer initial;
        AppendBarrier(initial, barrier, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
            stage, read | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
        initial.BuildAccelerationStructuresKHR(std::span(&blasBuild, 1));
        AppendBarrier(initial, barrier, stage, write, stage, read | write);
        append(initial, a, nullptr);
        _Submit(native, initial);

        const auto submitChanges = [&](std::span<const Group> groups, const PartitionedTopLevelAccelStruct& target,
            const PartitionedTopLevelAccelStruct& source)
        {
            Group::CommitToDeviceInline(groups, operations, 0);
            const auto actual = static_cast<uint32_t>(groups.size());
            Buffer::UploadToDeviceInline(count, 0, &actual, sizeof(actual));
            CommandBuffer commands;
            AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
                stage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | read);
            AppendBarrier(commands, barrier, stage, write | read, stage, write | read);
            append(commands, target, &source);
            _Submit(native, commands);
        };
        records[0].partitionIndex = VK_PARTITIONED_ACCELERATION_STRUCTURE_PARTITION_INDEX_GLOBAL_NV;
        records[1].partitionIndex = 1;
        Group::CommitToDeviceInline(records, writes, 0);
        submitChanges(std::span(initialGroups).first(1), a, a); // Ordinary/global migration.
        records[1].transform[3][0] = 2;
        Group::CommitToDeviceInline(records, writes, 0);
        submitChanges(std::span(initialGroups).first(1), a, a); // In-place transform modification.
        std::array<Group::UpdateInstanceRecord, 2> replacements{{{0, blas.GetDeviceAddress(), 0}, {0, blas.GetDeviceAddress(), 1}}};
        Group::CommitToDeviceInline(replacements, updates, 0);
        Group update; update.SetUpdateInstances(updates.GetDeviceAddress(), 2);
        submitChanges(std::span(&update, 1), b, a); // Copy persistent contents and replace references.
        const std::array<Group::WritePartitionTranslationRecord, 1> translated{{{1, {3, 4, 5}}}};
        Group::CommitToDeviceInline(translated, translations, 0);
        Group translation; translation.SetWritePartitionTranslations(translations.GetDeviceAddress(), 1);
        submitChanges(std::span(&translation, 1), b, b);
        std::cout << "Completed PTLAS initial build, ordinary/global migration, in-place writes, distinct source/destination and translation.\n";
    }
};

struct ClusterAccelerationStructureGpuTestProbe
{
private:
    static void _CheckInlineTransfers()
    {
        constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        constexpr std::array<uint8_t, 7> first{ 5, 17, 32, 64, 2, 124, 196 };
        constexpr std::array<uint8_t, 7> second{ 134, 67, 24, 8, 1, 56, 200 };
        constexpr VkDeviceSize offset = 3;
        Buffer buffer = MakeBuffer(19, usage);
        bool frameCompleted = false;
        DeviceContext context(1);
        context.StartFrame();
        context.AddCurrentFrameCompletionCallback([&] { frameCompleted = true; });

        std::array<uint8_t, 19> expected{};
        expected.fill(216);
        Buffer::UploadToDeviceInline(buffer, 0, expected.data(), expected.size());
        Buffer::UploadToDeviceInline(buffer, offset, first.data(), first.size());
        std::copy(first.begin(), first.end(), expected.begin() + offset);
        std::array<uint8_t, expected.size()> wholeBuffer{};
        Buffer::DownloadFromDeviceInline(context, QueueFamilyType::TRANSFER, buffer, 0, wholeBuffer.data(), wholeBuffer.size());
        CHECK_TRUE(wholeBuffer == expected, "An unaligned staging upload must only replace the supplied byte range!");
        Buffer::UploadToDeviceInline(context, QueueFamilyType::TRANSFER, buffer, offset, second.data(), second.size());
        std::array<uint8_t, first.size()> result{};
        Buffer::DownloadFromDeviceInline(buffer, offset, result.data(), result.size());
        CHECK_TRUE(result == second, "Default staging download must read the explicit upload at the supplied byte offset!");

        const auto checkFailedReadPreservesHostData = [&](const auto& download)
        {
            bool failed = false;
            result = first;
            {
                const ScopedValue wait(vkWaitForFences, FailFenceWait);
                try { download(); }
                catch (const std::runtime_error&) { failed = true; }
            }
            CHECK_TRUE(failed && result == first, "A failed staging download wait must preserve the Host destination!");
        };
        checkFailedReadPreservesHostData([&] { Buffer::DownloadFromDeviceInline(buffer, offset, result.data(), result.size()); });
        checkFailedReadPreservesHostData([&] {
            Buffer::DownloadFromDeviceInline(context, QueueFamilyType::TRANSFER, buffer, offset, result.data(), result.size());
        });
        CHECK_TRUE(!frameCompleted, "Explicit inline transfers must preserve the current frame!");
        context.EndFrame();
        context.StartFrame();
        CHECK_TRUE(frameCompleted, "Explicit inline transfers must preserve frame completion callbacks!");
        context.EndFrame();

        std::array<std::exception_ptr, 2> failures{};
        const auto roundTrip = [&](size_t index, const std::array<uint8_t, 7>& values)
        {
            try
            {
                Buffer workerBuffer = MakeBuffer(19, usage);
                std::array<uint8_t, 7> output{};
                Buffer::UploadToDeviceInline(workerBuffer, offset, values.data(), values.size());
                Buffer::DownloadFromDeviceInline(workerBuffer, offset, output.data(), output.size());
                CHECK_TRUE(output == values, "Default staging transfers must round-trip from any Host thread!");
            }
            catch (...)
            {
                failures[index] = std::current_exception();
            }
        };
        std::jthread firstWorker([&] { roundTrip(0, first); });
        std::jthread secondWorker([&] { roundTrip(1, second); });
        firstWorker.join();
        secondWorker.join();
        for (const auto& failure : failures)
        {
            if (failure)
                std::rethrow_exception(failure);
        }
        std::cout << "Verified default and explicit Buffer staging transfers and worker-thread round trips.\n";
    }

    static void _CheckAddresses(const Buffer& storage, std::span<const VkDeviceAddress> addresses,
        VkDeviceSize alignment, std::string_view batchName)
    {
        const VkDeviceAddress base = storage.GetDeviceAddress();
        const VkDeviceSize bytes = storage.GetBufferInformation().size;
        CHECK_TRUE(base != 0 && bytes != 0 && alignment != 0, "CLAS GPU smoke found invalid backing storage.");
        for (const VkDeviceAddress address : addresses)
        {
            CHECK_TRUE(address != 0 && address % alignment == 0 && address >= base && address - base < bytes,
                std::format("CLAS GPU smoke found invalid {} address 0x{:x} for backing buffer 0x{:x} ({} bytes, alignment {}).",
                    batchName, address, base, bytes, alignment));
            CHECK_TRUE(std::count(addresses.begin(), addresses.end(), address) == 1,
                std::format("CLAS GPU smoke found a duplicate {} address 0x{:x}.", batchName, address));
        }
        std::cout << std::format("Verified {} {} addresses in {} bytes of backing storage with alignment {}.\n",
            addresses.size(), batchName, bytes, alignment);
    }

    static void _Submit(const HeadlessDevice& native, const CommandBuffer& commands)
    {
        commands._ValidateQueueCapabilities(QueueFamilyType::COMPUTE);
        SmokeSubmission submission(native);
        const auto cmd = submission.Begin();
        for (const Command* recorded : commands.m_commands)
            recorded->Record(cmd);
        submission.SubmitAndWait(cmd);
    }

    static auto _MakeScratch(const HeadlessDevice& native, VkDeviceSize bytes, VkDeviceSize requiredAlignment) -> Buffer
    {
        if (!bytes) return {};
        const auto alignment = (std::max)(requiredAlignment,
            VkDeviceSize{native.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment});
        return MakeBuffer(bytes, kScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, alignment);
    }

    static auto _MakeClusterStorage(const AccelStructMemoryRequirements& memory) -> Buffer
    {
        return MakeBuffer(memory.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory.accelerationStructureAlignment);
    }

    static auto _MakeReferenceTable(VkDeviceSize bytes,
        VkMemoryPropertyFlags memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) -> Buffer
    {
        return MakeBuffer(bytes, kBuildInputUsage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memory, alignof(VkDeviceAddress));
    }

    using SizeValues = std::array<uint32_t, 2>;

    static auto _CheckSizePrefix(const Buffer& buffer, uint32_t count) -> SizeValues
    {
        SizeValues values{};
        Buffer::DownloadFromDeviceInline(buffer, 0, values.data(), sizeof(values));
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i < count) CHECK_TRUE(values[i] != 0 && values[i] != UINT32_MAX, "Missing active cluster size!");
            else CHECK_TRUE(values[i] == UINT32_MAX, "GPU wrote past the active cluster size prefix!");
        }
        return values;
    }

    template<typename BindTarget>
    static auto _ComputeSizes(const HeadlessDevice& native, const Buffer& tasks, VkDeviceSize taskBytes,
        ClusterScratchRequirements requirements, BindTarget&& bindTarget, std::string_view operation) -> SizeValues
    {
        CHECK_TRUE(taskBytes == tasks.GetBufferInformation().size);
        auto scratch = _MakeScratch(native, requirements.size, requirements.alignment);
        auto sizes = MakeBuffer(sizeof(SizeValues), kScratchUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory);
        auto gpuCount = MakeBuffer(sizeof(uint32_t), kBuildInputUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        ComputeClusterAccelStructSizesCommandInput input;
        bindTarget(input, sizes.GetDeviceAddress());
        input.SetArgumentAddress(tasks.GetDeviceAddress());
        input.SetScratchAddress(scratch.IsValid() ? scratch.GetDeviceAddress() : 0);
        input.CustomizeOperationCountAddress(gpuCount.GetDeviceAddress());
        constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
        SizeValues result{};
        for (uint32_t count = 0; count <= result.size(); ++count)
        {
            CommandBuffer commands;
            commands.FillBuffer(sizes, 0, sizeof(result), UINT32_MAX);
            commands.FillBuffer(gpuCount, 0, sizeof(uint32_t), count);
            AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT | stage,
                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                stage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
            commands.ComputeClusterAccelStructSizes(input);
            AppendBarrier(commands, barrier, stage | VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            _Submit(native, commands);
            result = _CheckSizePrefix(sizes, count);
        }
        std::cout << std::format("Computed {} sizes for counts 0, 1 and 2; full output [{}, {}] bytes.\n",
            operation, result[0], result[1]);
        return result;
    }

    struct SizedDestinations
    {
        std::array<Buffer, 2> storage;
        Buffer references;
    };

    template<typename BindTarget>
    static auto _BuildSizedDestinations(const HeadlessDevice& native, const AccelStructMemoryRequirements& requirements,
        const Buffer& tasks, const SizeValues& sizes, BindTarget&& bindTarget) -> SizedDestinations
    {
        const auto allocate = [&](uint32_t bytes) -> Buffer
        {
            return MakeBuffer(bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.accelerationStructureAlignment);
        };
        std::array<VkDeviceAddress, 2> addresses{};
        SizedDestinations destinations{{allocate(sizes[0]), allocate(sizes[1])},
            MakeBuffer(sizeof(addresses), kBuildInputUsage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostMemory)};
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            addresses[i] = destinations.storage[i].GetDeviceAddress();
            CHECK_TRUE(addresses[i] && addresses[i] % requirements.accelerationStructureAlignment == 0);
        }
        Buffer::UploadToDeviceInline(destinations.references, 0, addresses.data(), sizeof(addresses));
        auto scratch = _MakeScratch(native, requirements.buildScratchBufferSize, requirements.scratchAlignment);
        ExplicitClusterAccelStructCommandInput input;
        bindTarget(input, destinations.references.GetDeviceAddress());
        input.SetArgumentAddress(tasks.GetDeviceAddress());
        input.SetScratchAddress(scratch.IsValid() ? scratch.GetDeviceAddress() : 0);
        CommandBuffer build;
        constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
        AppendBarrier(build, barrier, VK_PIPELINE_STAGE_HOST_BIT | stage, VK_ACCESS_HOST_WRITE_BIT |
            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, stage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        build.ExecuteClusterAccelStructIndirect(input);
        AppendBarrier(build, barrier, stage, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, stage | VK_PIPELINE_STAGE_HOST_BIT,
            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
            VK_ACCESS_HOST_READ_BIT);
        _Submit(native, build);
        return destinations;
    }

    static void _CheckComputedDestinations(const HeadlessDevice& native, bool instantiate,
        const Buffer& clusterTasks, VkDeviceAddress indices)
    {
        using Sizing = ComputeClusterAccelStructSizesCommandInput;
        using Build = ExplicitClusterAccelStructCommandInput;
        constexpr uint32_t capacity = 2;
        ClusterLevelAccelStructBuildLimits layout;
        layout.SetMaxTriangleCount(1, 2);
        layout.SetMaxVertexCount(3, 6);
        if (!instantiate)
        {
            constexpr auto taskLayout = ClusterLevelAccelStructTemplateBuildInfo::GetBuildInfoLayout();
            const auto taskBytes = taskLayout.GetBufferSize(capacity);
            const auto scratchRequirements = Sizing::GetClusterTemplateBuildScratchRequirements(layout, capacity);
            using Task = VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV;
            std::array<Task, 2> topology{};
            for (auto& task : topology)
            {
                task.triangleCount = 1;
                task.vertexCount = 3;
                task.indexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_16BIT_NV;
                task.opacityMicromapIndexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV;
                task.baseGeometryIndexAndGeometryFlags.geometryFlags = VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;
                task.indexBuffer = indices;
            }
            CHECK_TRUE(taskBytes == sizeof(topology));
            auto templateTasks = MakeBuffer(taskBytes, kBuildInputUsage, kHostMemory, taskLayout.addressAlignment);
            Buffer::UploadToDeviceInline(templateTasks, 0, topology.data(), sizeof(topology));
            _ComputeSizes(native, templateTasks, taskBytes, scratchRequirements,
                [&](auto& input, VkDeviceAddress output) { input.SetClusterTemplateTarget(layout, capacity, output); }, "Template");
        }
        const auto taskLayout = instantiate ? ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout() :
            ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout();
        const auto clusterTaskBytes = taskLayout.GetBufferSize(capacity);
        const auto clusterScratchRequirements = instantiate ? Sizing::GetInstantiateClusterBuildScratchRequirements(layout, capacity) :
            Sizing::GetClusterBuildScratchRequirements(layout, capacity);
        const auto clusterMemory = instantiate ? Build::GetInstantiateClusterMemoryRequirements(layout, capacity) :
            Build::GetClusterMemoryRequirements(layout, capacity);
        const auto sizes = _ComputeSizes(native, clusterTasks, clusterTaskBytes, clusterScratchRequirements,
            [&](auto& input, VkDeviceAddress output)
            {
                if (instantiate) input.SetInstantiateClusterTarget(layout, capacity, output);
                else input.SetClusterTarget(layout, capacity, output);
            }, instantiate ? "instantiate CLAS" : "direct CLAS");
        auto clusters = _BuildSizedDestinations(native, clusterMemory, clusterTasks, sizes,
            [&](auto& input, VkDeviceAddress references)
            {
                if (instantiate) input.SetInstantiateClusterTarget(layout, capacity, references);
                else input.SetClusterTarget(layout, capacity, references);
            });
        _CheckDependentSizedBuild(native, clusters.references.GetDeviceAddress(), "dependent Cluster BLAS");
        std::cout << "Built explicit CLAS and dependent explicit Cluster BLAS in GPU-sized allocations.\n";
    }

    static void _CheckDependentSizedBuild(const HeadlessDevice& native, VkDeviceAddress clusterReferences,
        std::string_view operation)
    {
        using Sizing = ComputeClusterAccelStructSizesCommandInput;
        using Build = ExplicitClusterAccelStructCommandInput;
        constexpr uint32_t capacity = 2;
        ClusterBottomLevelAccelStructBuildLimits blasLayout;
        blasLayout.SetMaxClusterCount(2, 3);
        constexpr auto taskLayout = ClusterBottomLevelAccelStructBuildInfo::GetBuildInfoLayout();
        const auto blasTaskBytes = taskLayout.GetBufferSize(capacity);
        const auto blasScratchRequirements = Sizing::GetBottomLevelAccelStructBuildScratchRequirements(blasLayout, capacity);
        const auto blasMemory = Build::GetBottomLevelAccelStructMemoryRequirements(blasLayout, capacity);
        std::array<ClusterBottomLevelAccelStructBuildInfo, 2> descriptions{};
        descriptions[0].SetClusterReferences(clusterReferences, 2);
        descriptions[1].SetClusterReferences(clusterReferences + sizeof(VkDeviceAddress), 1);
        auto blasTasks = MakeBuffer(blasTaskBytes, kBuildInputUsage, kHostMemory, taskLayout.addressAlignment);
        ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(descriptions, blasTasks);
        const auto blasSizes = _ComputeSizes(native, blasTasks, blasTaskBytes, blasScratchRequirements,
            [&](auto& input, VkDeviceAddress output) { input.SetBottomLevelAccelStructTarget(blasLayout, capacity, output); },
            operation);
        _BuildSizedDestinations(native, blasMemory, blasTasks, blasSizes,
            [&](auto& input, VkDeviceAddress references) { input.SetBottomLevelAccelStructTarget(blasLayout, capacity, references); });
    }

    static void _CheckMoveCopies(const HeadlessDevice& native, const Buffer& sourceReferences, const Buffer& sourceStorage)
    {
        using Sizing = ComputeClusterAccelStructSizesCommandInput;
        using Explicit = ExplicitClusterAccelStructCommandInput;
        using Implicit = ImplicitClusterAccelStructCommandInput;
        using References = std::array<VkDeviceAddress, 2>;
        constexpr auto type = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV;
        constexpr VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        constexpr VkBufferUsageFlags tableUsage = kBuildInputUsage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        constexpr uint32_t capacity = 2;
        const auto sourceBytes = sourceStorage.GetBufferInformation().size;
        const auto sourceAddress = sourceReferences.GetDeviceAddress();
        const auto taskBytes = ClusterAccelStructMoveInfo::GetBuildInfoLayout().GetBufferSize(capacity);
        ClusterAccelStructMoveLimits limits;
        limits.SetObjectType(type);
        limits.SetMaxMovedBytes(sourceBytes);
        limits.CustomizeNoMoveOverlap(true);
        const auto measure = [&](const Buffer& references, std::string_view operation) -> SizeValues
        {
            return _ComputeSizes(native, references, taskBytes, Sizing::GetMoveObjectsScratchRequirements(limits, capacity),
                [&](auto& input, VkDeviceAddress output) { input.SetMoveObjectsTarget(limits, capacity, output); }, operation);
        };
        const auto sizes = measure(sourceReferences, "MOVE source CLAS");
        const VkDeviceSize movedBytes = VkDeviceSize{sizes[0]} + sizes[1];
        CHECK_TRUE(movedBytes <= sourceBytes);
        limits.SetMaxMovedBytes(movedBytes);

        const auto alignment = Explicit::GetMoveObjectsDestinationAlignment(type);
        CHECK_TRUE(alignment == native.clusterProperties.clusterByteAlignment &&
            alignment == Implicit::GetMoveObjectsDestinationAlignment(type));
        auto inputSizes = MakeBuffer(sizeof(sizes), tableUsage, kHostMemory, alignof(uint32_t));
        Buffer::UploadToDeviceInline(inputSizes, 0, sizes.data(), sizeof(sizes));
        AccelStructMemoryRequirements explicitRequirements{};
        explicitRequirements.accelerationStructureAlignment = alignment;
        const auto explicitScratch = Explicit::GetMoveObjectsScratchRequirements(limits, capacity);
        explicitRequirements.buildScratchBufferSize = explicitScratch.size;
        explicitRequirements.scratchAlignment = explicitScratch.alignment;
        VkClusterAccelerationStructureMoveObjectsInputNV nativeMove{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV };
        nativeMove.type = type;
        nativeMove.noMoveOverlap = VK_TRUE;
        nativeMove.maxMovedBytes = movedBytes;
        VkClusterAccelerationStructureInputInfoNV nativeQuery{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
        nativeQuery.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
        nativeQuery.maxAccelerationStructureCount = capacity;
        nativeQuery.opInput.pMoveObjects = &nativeMove;
        const auto checkScratch = [&](VkClusterAccelerationStructureOpModeNV mode, ClusterScratchRequirements scratch, std::string_view name)
        {
            nativeQuery.opMode = mode;
            VkAccelerationStructureBuildSizesInfoKHR nativeSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            native.deviceFunctions.vkGetClusterAccelerationStructureBuildSizesNV(native.device, &nativeQuery, &nativeSizes);
            CHECK_TRUE(scratch.size == (std::max)(nativeSizes.buildScratchSize, nativeSizes.updateScratchSize) &&
                scratch.alignment == native.clusterProperties.clusterScratchByteAlignment,
                "MOVE scratch requirements must match native sizes and alignment!");
            std::cout << std::format("MOVE {} native sizes: accelerationStructureSize {}, buildScratchSize {}, updateScratchSize {} bytes.\n",
                name, nativeSizes.accelerationStructureSize, nativeSizes.buildScratchSize, nativeSizes.updateScratchSize);
        };
        checkScratch(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV,
            Sizing::GetMoveObjectsScratchRequirements(limits, capacity), "Compute Sizes");
        checkScratch(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV,
            explicitScratch, "Explicit");
        const auto explicitCopy = _BuildSizedDestinations(native, explicitRequirements, sourceReferences, sizes,
            [&](auto& input, VkDeviceAddress references)
            {
                input.SetMoveObjectsTarget(limits, capacity, references);
                input.CustomizeInputSizesArray(inputSizes.GetDeviceAddress());
            });
        References explicitAddresses{};
        SizeValues preservedInputSizes{};
        Buffer::DownloadFromDeviceInline(explicitCopy.references, 0, explicitAddresses.data(), sizeof(explicitAddresses));
        Buffer::DownloadFromDeviceInline(inputSizes, 0, preservedInputSizes.data(), sizeof(preservedInputSizes));
        CHECK_TRUE(preservedInputSizes == sizes, "Explicit MOVE must preserve its input size table!");
        for (size_t i = 0; i < explicitAddresses.size(); ++i)
        {
            CHECK_TRUE(explicitAddresses[i] == explicitCopy.storage[i].GetDeviceAddress(),
                "Explicit MOVE must preserve its destination address table!");
            _CheckAddresses(explicitCopy.storage[i], std::span(explicitAddresses).subspan(i, 1), alignment, "explicit MOVE CLAS");
        }

        const auto alignedSize = [alignment](VkDeviceSize bytes) -> VkDeviceSize
        {
            return ((bytes + alignment - 1) / alignment) * alignment;
        };
        auto implicitStorage = MakeBuffer(alignedSize(sizes[0]) + alignedSize(sizes[1]), storageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, alignment);
        auto implicitReferences = MakeBuffer(sizeof(References), tableUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            kHostMemory, alignof(VkDeviceAddress));
        auto outputSizes = MakeBuffer(sizeof(sizes), tableUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            kHostMemory, alignof(uint32_t));
        const auto implicitScratch = Implicit::GetMoveObjectsScratchRequirements(limits, capacity);
        checkScratch(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV, implicitScratch, "Implicit");
        auto scratch = _MakeScratch(native, implicitScratch.size, implicitScratch.alignment);
        Implicit implicitMove;
        implicitMove.SetMoveObjectsTarget(limits, capacity, implicitStorage.GetDeviceAddress(), implicitReferences.GetDeviceAddress());
        implicitMove.CustomizeOutputSizesArray(outputSizes.GetDeviceAddress());
        implicitMove.SetArgumentAddress(sourceAddress);
        implicitMove.SetScratchAddress(scratch.IsValid() ? scratch.GetDeviceAddress() : 0);

        constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        constexpr auto read = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        constexpr auto write = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
        CommandBuffer commands;
        commands.FillBuffer(implicitReferences, 0, sizeof(References), UINT32_MAX);
        commands.FillBuffer(outputSizes, 0, sizeof(sizes), UINT32_MAX);
        AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT | stage,
            VK_ACCESS_TRANSFER_WRITE_BIT | write, stage, read | write);
        commands.ExecuteClusterAccelStructIndirect(implicitMove);
        AppendBarrier(commands, barrier, stage, write, stage | VK_PIPELINE_STAGE_HOST_BIT,
            read | VK_ACCESS_HOST_READ_BIT);
        _Submit(native, commands);

        References implicitAddresses{};
        Buffer::DownloadFromDeviceInline(implicitReferences, 0, implicitAddresses.data(), sizeof(implicitAddresses));
        CHECK_TRUE(_CheckSizePrefix(outputSizes, capacity) == sizes, "Implicit MOVE must report the source CLAS sizes!");
        _CheckAddresses(implicitStorage, implicitAddresses, alignment, "implicit MOVE CLAS");
        const auto implicitBase = implicitStorage.GetDeviceAddress();
        const auto implicitBytes = implicitStorage.GetBufferInformation().size;
        for (size_t i = 0; i < implicitAddresses.size(); ++i)
            CHECK_TRUE(sizes[i] <= implicitBytes - (implicitAddresses[i] - implicitBase), "Implicit MOVE CLAS exceeds its storage!");
        CHECK_TRUE(implicitAddresses[0] + sizes[0] <= implicitAddresses[1] ||
            implicitAddresses[1] + sizes[1] <= implicitAddresses[0], "Implicit MOVE CLAS ranges overlap!");

        CHECK_TRUE(measure(explicitCopy.references, "explicit MOVE copy CLAS") == sizes);
        _CheckDependentSizedBuild(native, explicitCopy.references.GetDeviceAddress(), "Cluster BLAS from explicit MOVE copies");
        CHECK_TRUE(measure(implicitReferences, "implicit MOVE copy CLAS") == sizes);
        _CheckDependentSizedBuild(native, implicitReferences.GetDeviceAddress(), "Cluster BLAS from implicit MOVE copies");
        CHECK_TRUE(measure(sourceReferences, "original CLAS after both MOVE copies") == sizes);
        _CheckDependentSizedBuild(native, sourceAddress, "Cluster BLAS from original CLAS after both MOVE copies");
        std::cout << std::format("Completed no-overlap MOVE copies of {} bytes with {}-byte destination alignment. "
            "Explicit scratch {} bytes, references [0x{:x}, 0x{:x}]. Implicit scratch {} bytes, references [0x{:x}, 0x{:x}].\n",
            movedBytes, alignment, explicitRequirements.buildScratchBufferSize, explicitAddresses[0], explicitAddresses[1],
            implicitScratch.size, implicitAddresses[0], implicitAddresses[1]);
    }

    static void _CheckTemplateQueries(const HeadlessDevice& native, const ClusterLevelAccelStructBuildLimits& limits,
        const Buffer& templateReferences)
    {
        using Sizing = ComputeClusterAccelStructSizesCommandInput;
        using Explicit = ExplicitClusterAccelStructCommandInput;
        using Implicit = ImplicitClusterAccelStructCommandInput;
        if (native.clusterRevision < VulkanDetail::ClusterTemplateIndicesMinRevision)
        {
            std::cout << "Skipped QUERY_TEMPLATE_INDICES GPU smoke because it requires CLAS extension revision 3 or newer.\n";
            return;
        }

        constexpr uint32_t capacity = 2;
        VkClusterAccelerationStructureTriangleClusterInputNV nativeLimits{
            VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV, nullptr, VK_FORMAT_R32G32B32_SFLOAT, 0, 1 };
        nativeLimits.maxClusterTriangleCount = 1;
        nativeLimits.maxTotalTriangleCount = 2;
        nativeLimits.maxClusterVertexCount = 3;
        nativeLimits.maxTotalVertexCount = 6;
        const auto check = [&](VkClusterAccelerationStructureOpModeNV mode, const VkAccelerationStructureBuildSizesInfoKHR& sizes,
            std::string_view name)
        {
            VkClusterAccelerationStructureInputInfoNV query{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
            query.opMode = mode;
            query.opType = VulkanDetail::ClusterTemplateIndicesOperation;
            query.maxAccelerationStructureCount = capacity;
            query.opInput.pTriangleClusters = &nativeLimits;
            VkAccelerationStructureBuildSizesInfoKHR expected{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
            native.deviceFunctions.vkGetClusterAccelerationStructureBuildSizesNV(native.device, &query, &expected);
            CHECK_TRUE(sizes.sType == expected.sType && sizes.pNext == nullptr &&
                sizes.accelerationStructureSize == expected.accelerationStructureSize &&
                sizes.buildScratchSize == expected.buildScratchSize && sizes.updateScratchSize == expected.updateScratchSize,
                "QUERY_TEMPLATE_INDICES wrapper must preserve native size values!");
            std::cout << std::format("QUERY_TEMPLATE_INDICES {} native sizes ({} templates): "
                "accelerationStructureSize {}, buildScratchSize {}, updateScratchSize {} bytes.\n",
                name, capacity, expected.accelerationStructureSize, expected.buildScratchSize, expected.updateScratchSize);
        };
        const auto computeSizes = Sizing::GetQueryTemplateIndicesBuildSizes(limits, capacity);
        const auto explicitSizes = Explicit::GetQueryTemplateIndicesBuildSizes(limits, capacity);
        const auto implicitSizes = Implicit::GetQueryTemplateIndicesBuildSizes(limits, capacity);
        check(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV, computeSizes, "Compute Sizes");
        check(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV, explicitSizes, "Explicit");
        check(VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV, implicitSizes, "Implicit");
        if (!explicitSizes.accelerationStructureSize || !implicitSizes.accelerationStructureSize)
        {
            std::cout << "Skipped QUERY_TEMPLATE_INDICES execution because a native destination size bound was zero.\n";
            return;
        }

        using References = std::array<VkDeviceAddress, 2>;
        const auto taskAddress = templateReferences.GetDeviceAddress();
        const auto sizes = _ComputeSizes(native, templateReferences, ClusterTemplateIndicesQueryInfo::GetBuildInfoLayout().GetBufferSize(capacity),
            { computeSizes.buildScratchSize, Sizing::GetScratchAlignment() }, [&](auto& input, VkDeviceAddress output)
            {
                input.SetQueryTemplateIndicesTarget(limits, capacity, output);
            }, "QUERY_TEMPLATE_INDICES");
        CHECK_TRUE(std::ranges::max(sizes) <= explicitSizes.accelerationStructureSize &&
            VkDeviceSize{sizes[0]} + sizes[1] <= implicitSizes.accelerationStructureSize,
            "QUERY_TEMPLATE_INDICES GPU sizes exceed the queried destination bounds!");

        // Allocate the opaque payload with the native bounds and conservatively align the allocation bases.
        constexpr VkBufferUsageFlags payloadUsage = kScratchUsage | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        const auto alignment = (std::max)({VkDeviceSize{native.clusterProperties.clusterByteAlignment},
            VkDeviceSize{native.clusterProperties.clusterTemplateByteAlignment},
            VkDeviceSize{native.clusterProperties.clusterBottomLevelByteAlignment},
            native.properties.limits.minStorageBufferOffsetAlignment});
        const auto allocateExplicit = [&]() -> Buffer
        {
            return MakeBuffer(explicitSizes.accelerationStructureSize, payloadUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, alignment);
        };
        std::array explicitPayloads{ allocateExplicit(), allocateExplicit() };
        const References explicitAddresses{ explicitPayloads[0].GetDeviceAddress(), explicitPayloads[1].GetDeviceAddress() };
        auto explicitReferences = MakeBuffer(sizeof(explicitAddresses), kBuildInputUsage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            kHostMemory, alignof(VkDeviceAddress));
        Buffer::UploadToDeviceInline(explicitReferences, 0, explicitAddresses.data(), sizeof(explicitAddresses));
        auto explicitScratch = _MakeScratch(native, explicitSizes.buildScratchSize, Explicit::GetScratchAlignment());
        Explicit explicitQuery;
        explicitQuery.SetQueryTemplateIndicesTarget(limits, capacity, explicitReferences.GetDeviceAddress());
        explicitQuery.SetArgumentAddress(taskAddress);
        explicitQuery.SetScratchAddress(explicitScratch.IsValid() ? explicitScratch.GetDeviceAddress() : 0);

        auto implicitPayload = MakeBuffer(implicitSizes.accelerationStructureSize, payloadUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, alignment);
        auto implicitReferences = MakeBuffer(sizeof(References), kScratchUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            kHostMemory, alignof(VkDeviceAddress));
        auto implicitSizesTable = MakeBuffer(sizeof(sizes), kScratchUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            kHostMemory, alignof(uint32_t));
        auto implicitScratch = _MakeScratch(native, implicitSizes.buildScratchSize, Implicit::GetScratchAlignment());
        Implicit implicitQuery;
        implicitQuery.SetQueryTemplateIndicesTarget(limits, capacity, implicitPayload.GetDeviceAddress(), implicitReferences.GetDeviceAddress());
        implicitQuery.SetArgumentAddress(taskAddress);
        implicitQuery.SetScratchAddress(implicitScratch.IsValid() ? implicitScratch.GetDeviceAddress() : 0);
        implicitQuery.CustomizeOutputSizesArray(implicitSizesTable.GetDeviceAddress());

        constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        constexpr auto read = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        constexpr auto write = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
        CommandBuffer explicitCommands;
        AppendBarrier(explicitCommands, barrier, VK_PIPELINE_STAGE_HOST_BIT | stage,
            VK_ACCESS_HOST_WRITE_BIT | write, stage, read);
        explicitCommands.ExecuteClusterAccelStructIndirect(explicitQuery);
        _Submit(native, explicitCommands);
        std::cout << "Completed explicit QUERY_TEMPLATE_INDICES in native-sized opaque buffers.\n";

        CommandBuffer implicitCommands;
        implicitCommands.FillBuffer(implicitReferences, 0, sizeof(References), UINT32_MAX);
        implicitCommands.FillBuffer(implicitSizesTable, 0, sizeof(sizes), UINT32_MAX);
        AppendBarrier(implicitCommands, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT | stage,
            VK_ACCESS_TRANSFER_WRITE_BIT | write, stage, read | write);
        implicitCommands.ExecuteClusterAccelStructIndirect(implicitQuery);
        AppendBarrier(implicitCommands, barrier, stage, write, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        _Submit(native, implicitCommands);
        References implicitAddresses{};
        Buffer::DownloadFromDeviceInline(implicitReferences, 0, implicitAddresses.data(), sizeof(implicitAddresses));
        CHECK_TRUE(_CheckSizePrefix(implicitSizesTable, capacity) == sizes,
            "Implicit QUERY_TEMPLATE_INDICES size output must agree with Compute Sizes!");
        const auto base = implicitPayload.GetDeviceAddress();
        const auto bytes = implicitSizes.accelerationStructureSize;
        for (size_t i = 0; i < implicitAddresses.size(); ++i)
        {
            CHECK_TRUE(implicitAddresses[i] >= base && implicitAddresses[i] - base < bytes &&
                sizes[i] <= bytes - (implicitAddresses[i] - base), "Implicit QUERY_TEMPLATE_INDICES output exceeds its storage!");
        }
        CHECK_TRUE(implicitAddresses[0] + sizes[0] <= implicitAddresses[1] ||
            implicitAddresses[1] + sizes[1] <= implicitAddresses[0], "Implicit QUERY_TEMPLATE_INDICES output ranges overlap!");
        std::cout << std::format("Completed implicit QUERY_TEMPLATE_INDICES with opaque output offsets [{}, {}] in {} bytes.\n",
            implicitAddresses[0] - base, implicitAddresses[1] - base, bytes);
    }

    static void _BuildAndReadBack(const HeadlessDevice& native, bool useTemplates, bool explicitDestinations)
    {
        using Explicit = ExplicitClusterAccelStructCommandInput;
        using Implicit = ImplicitClusterAccelStructCommandInput;
        constexpr uint32_t capacity = 2;
        ClusterLevelAccelStructBuildLimits clusterLimits;
        clusterLimits.SetMaxTriangleCount(1, 2);
        clusterLimits.SetMaxVertexCount(3, 6);
        ClusterBottomLevelAccelStructBuildLimits blasLimits;
        blasLimits.SetMaxClusterCount(2, 3);
        constexpr std::array<float, 18> positions{
            0, 0, 0, 1, 0, 0, 0, 1, 0,
            2, 0, 0, 3, 0, 1, 2, 2, 0
        };
        constexpr std::array<uint16_t, 3> indices{ 0, 1, 2 };
        auto vertices = MakeBuffer(sizeof(positions), kBuildInputUsage, kHostMemory);
        auto indexBuffer = MakeBuffer(sizeof(indices), kBuildInputUsage, kHostMemory);
        Buffer::UploadToDeviceInline(vertices, 0, positions.data(), sizeof(positions));
        Buffer::UploadToDeviceInline(indexBuffer, 0, indices.data(), sizeof(indices));
        const VkDeviceAddress vertexAddress = vertices.GetDeviceAddress();
        const VkDeviceAddress indexAddress = indexBuffer.GetDeviceAddress();
        constexpr VkDeviceSize vertexStride = 3 * sizeof(float);
        const std::array vertexAddresses{ vertexAddress, vertexAddress + 3 * vertexStride };
        std::optional<Buffer> templateStorage;
        std::array<VkDeviceAddress, capacity> templateAddresses{};
        if (useTemplates)
        {
            constexpr std::array<uint16_t, 6> paddedIndices{ 0, 99, 2, 99, 1, 99 };
            auto otherIndices = MakeBuffer(sizeof(paddedIndices), kBuildInputUsage, kHostMemory);
            Buffer::UploadToDeviceInline(otherIndices, 0, paddedIndices.data(), sizeof(paddedIndices));
            std::array<ClusterLevelAccelStructTemplateBuildInfo, 2> infos;
            for (auto& info : infos)
            {
                info.SetVertexCount(3);
                info.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
            }
            infos[0].SetIndexBuffer(indexAddress, 3);
            infos[1].SetIndexBuffer(otherIndices.GetDeviceAddress(), 3);
            infos[1].CustomizeIndexStride(4);
            const auto memory = Implicit::GetClusterTemplateMemoryRequirements(clusterLimits, capacity);
            templateStorage.emplace(_MakeClusterStorage(memory));
            auto templateReferences = _MakeReferenceTable(sizeof(templateAddresses));
            constexpr auto taskLayout = ClusterLevelAccelStructTemplateBuildInfo::GetBuildInfoLayout();
            auto tasks = MakeBuffer(taskLayout.GetBufferSize(capacity),
                kBuildInputUsage, kHostMemory, taskLayout.addressAlignment);
            ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(infos, tasks);
            auto scratch = _MakeScratch(native, memory.buildScratchBufferSize, memory.scratchAlignment);
            auto readback = MakeBuffer(sizeof(templateAddresses), VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory);
            Implicit input;
            input.SetClusterTemplateTarget(clusterLimits, capacity, templateStorage->GetDeviceAddress(), templateReferences.GetDeviceAddress());
            input.SetArgumentAddress(tasks.GetDeviceAddress());
            input.SetScratchAddress(scratch.IsValid() ? scratch.GetDeviceAddress() : 0);
            constexpr auto stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;
            CommandBuffer build;
            AppendBarrier(build, barrier, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, stage,
                VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
            build.ExecuteClusterAccelStructIndirect(input);
            AppendBarrier(build, barrier, stage, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                stage | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            build.CopyBuffer(templateReferences, readback, 0, 0, sizeof(templateAddresses));
            AppendBarrier(build, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            _Submit(native, build);
            Buffer::DownloadFromDeviceInline(readback, 0, templateAddresses.data(), sizeof(templateAddresses));
            _CheckAddresses(*templateStorage, templateAddresses, memory.accelerationStructureAlignment, "template");
            if (!explicitDestinations) _CheckTemplateQueries(native, clusterLimits, templateReferences);
        }
        std::array<ClusterLevelAccelStruct, capacity> clusterOwners;
        std::array<BottomLevelAccelStruct, capacity> blasOwners;
        std::array<VkDeviceAddress, capacity> fixedClusters{}, fixedBlases{};
        constexpr VkDeviceSize explicitClusterOffset = 8;
        constexpr VkDeviceSize explicitBlasOffset = 24;
        const VkDeviceSize clusterOffset = explicitDestinations ? explicitClusterOffset : 0;
        const VkDeviceSize blasOffset = explicitDestinations ? explicitBlasOffset : 0;
        auto clusterTable = explicitDestinations
            ? _MakeReferenceTable(explicitBlasOffset + sizeof(fixedBlases), kHostMemory)
            : _MakeReferenceTable(sizeof(fixedClusters));
        auto implicitBlasTable = explicitDestinations ? Buffer{} : _MakeReferenceTable(sizeof(fixedBlases));
        Buffer& blasTable = explicitDestinations ? clusterTable : implicitBlasTable;
        AccelStructMemoryRequirements clusterMemory{}, blasMemory{};
        if (explicitDestinations)
        {
            clusterMemory = useTemplates ? Explicit::GetInstantiateClusterMemoryRequirements(clusterLimits, capacity)
                : Explicit::GetClusterMemoryRequirements(clusterLimits, capacity);
            blasMemory = Explicit::GetBottomLevelAccelStructMemoryRequirements(blasLimits, capacity);
            ClusterLevelAccelStructCreateInfo ownerInfo;
            ownerInfo.SetMaxTriangleCount(1);
            ownerInfo.SetMaxVertexCount(3);
            ownerInfo.CustomizeTemplateUsage(useTemplates);
            for (uint32_t i = 0; i < capacity; ++i)
            {
                clusterOwners[i].Create(ownerInfo);
                fixedClusters[i] = clusterOwners[i].GetDeviceAddress();
                BottomLevelAccelStructCreateInfo blasOwnerInfo;
                blasOwnerInfo.SetClusters(capacity - i);
                blasOwners[i].Create(blasOwnerInfo);
                fixedBlases[i] = blasOwners[i].GetDeviceAddress();
            }
            Buffer::UploadToDeviceInline(clusterTable, clusterOffset, fixedClusters.data(), sizeof(fixedClusters));
            Buffer::UploadToDeviceInline(blasTable, blasOffset, fixedBlases.data(), sizeof(fixedBlases));
        }
        else
        {
            clusterMemory = useTemplates ? Implicit::GetInstantiateClusterMemoryRequirements(clusterLimits, capacity)
                : Implicit::GetClusterMemoryRequirements(clusterLimits, capacity);
            blasMemory = Implicit::GetBottomLevelAccelStructMemoryRequirements(blasLimits, capacity);
        }
        auto clusterStorage = explicitDestinations ? Buffer{} : _MakeClusterStorage(clusterMemory);
        auto blasStorage = explicitDestinations ? Buffer{} : _MakeClusterStorage(blasMemory);
        const auto clusterReferences = clusterTable.GetDeviceAddress() + clusterOffset;
        const auto blasReferences = blasTable.GetDeviceAddress() + blasOffset;
        std::array<ClusterBottomLevelAccelStructBuildInfo, capacity> blasDescriptions{};
        blasDescriptions[0].SetClusterReferences(clusterReferences, 2);
        blasDescriptions[1].SetClusterReferences(clusterReferences + sizeof(VkDeviceAddress), 1);

        const auto taskLayout = useTemplates ? ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout() :
            ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout();
        const auto clusterTaskBytes = taskLayout.GetBufferSize(capacity);
        constexpr auto blasTaskLayout = ClusterBottomLevelAccelStructBuildInfo::GetBuildInfoLayout();
        const auto blasTaskBytes = blasTaskLayout.GetBufferSize(capacity);
        auto clusterTasks = MakeBuffer(clusterTaskBytes,
            kBuildInputUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            useTemplates ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : kHostMemory, taskLayout.addressAlignment);
        auto blasTasks = MakeBuffer(blasTaskBytes, kBuildInputUsage,
            kHostMemory, blasTaskLayout.addressAlignment);
        if (useTemplates)
        {
            std::array<ClusterLevelAccelStructInstantiateBuildInfo, capacity> instances{};
            for (uint32_t i = 0; i < capacity; ++i)
            {
                instances[i].SetTemplateReference(templateAddresses[0]);
                instances[i].SetVertexBuffer(vertexAddresses[i]);
                instances[i].CustomizeVertexStride(vertexStride);
                instances[i].CustomizeClusterID(i + 1);
            }
            ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(instances, clusterTasks);
        }
        else
        {
            std::array<ClusterLevelAccelStructDirectBuildInfo, capacity> directTasks{};
            for (uint32_t i = 0; i < capacity; ++i)
            {
                directTasks[i].SetIndexBuffer(indexAddress, static_cast<uint32_t>(indices.size()));
                directTasks[i].SetVertexBuffer(vertexAddresses[i], 3);
                directTasks[i].CustomizeIndexType(VK_INDEX_TYPE_UINT16);
                directTasks[i].CustomizeVertexStride(vertexStride);
                directTasks[i].CustomizeClusterID(i + 1);
            }
            ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(directTasks, clusterTasks);
        }
        ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(blasDescriptions, blasTasks);

        const VkDeviceSize scratchAlignment = native.clusterProperties.clusterScratchByteAlignment;
        auto clusterScratch = _MakeScratch(native, clusterMemory.buildScratchBufferSize, clusterMemory.scratchAlignment);
        auto blasScratch = _MakeScratch(native, blasMemory.buildScratchBufferSize, blasMemory.scratchAlignment);
        const auto scratchAddress = [scratchAlignment](const Buffer& scratch) -> VkDeviceAddress
        {
            if (!scratch.IsValid())
                return 0;
            const auto address = scratch.GetDeviceAddress();
            CHECK_TRUE(address != 0 && scratchAlignment != 0 && address % scratchAlignment == 0,
                "CLAS GPU smoke found an invalid scratch address.");
            return address;
        };

        std::array<VkDeviceAddress, capacity> clusterAddresses{};
        std::array<VkDeviceAddress, blasDescriptions.size()> blasAddresses{};
        auto readback = MakeBuffer(sizeof(clusterAddresses) + sizeof(blasAddresses), VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory);
        constexpr VkPipelineStageFlags buildStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        constexpr VkAccessFlags buildRead = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        constexpr VkAccessFlags buildWrite = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;

        CommandBuffer commands;
        AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, buildStage,
            buildRead | VK_ACCESS_SHADER_READ_BIT);
        const auto appendBuilds = [&](auto& cluster, auto& blas)
        {
            cluster.SetArgumentAddress(clusterTasks.GetDeviceAddress());
            cluster.SetScratchAddress(scratchAddress(clusterScratch));
            commands.ExecuteClusterAccelStructIndirect(cluster);
            AppendBarrier(commands, barrier, buildStage, buildWrite, buildStage, buildRead);
            blas.SetArgumentAddress(blasTasks.GetDeviceAddress());
            blas.SetScratchAddress(scratchAddress(blasScratch));
            commands.ExecuteClusterAccelStructIndirect(blas);
        };
        Implicit clusterBuild, blasBuild;
        if (explicitDestinations)
        {
            Explicit cluster, blas;
            if (useTemplates) cluster.SetInstantiateClusterTarget(clusterLimits, capacity, clusterReferences);
            else cluster.SetClusterTarget(clusterLimits, capacity, clusterReferences);
            blas.SetBottomLevelAccelStructTarget(blasLimits, capacity, blasReferences);
            appendBuilds(cluster, blas);
        }
        else
        {
            const auto storage = clusterStorage.GetDeviceAddress();
            if (useTemplates) clusterBuild.SetInstantiateClusterTarget(clusterLimits, capacity, storage, clusterReferences);
            else clusterBuild.SetClusterTarget(clusterLimits, capacity, storage, clusterReferences);
            blasBuild.SetBottomLevelAccelStructTarget(blasLimits, capacity, blasStorage.GetDeviceAddress(), blasReferences);
            appendBuilds(clusterBuild, blasBuild);
        }
        AppendBarrier(commands, barrier, buildStage, buildWrite, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        commands.CopyBuffer(clusterTable, readback, clusterOffset, 0, sizeof(clusterAddresses));
        commands.CopyBuffer(blasTable, readback, blasOffset, sizeof(clusterAddresses), sizeof(blasAddresses));
        AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

        _Submit(native, commands);

        Buffer::DownloadFromDeviceInline(readback, 0, clusterAddresses.data(), sizeof(clusterAddresses));
        Buffer::DownloadFromDeviceInline(readback, sizeof(clusterAddresses), blasAddresses.data(), sizeof(blasAddresses));
        if (explicitDestinations)
        {
            CHECK_TRUE(clusterAddresses == fixedClusters && blasAddresses == fixedBlases);
            std::cout << "Completed explicit " << (useTemplates ? "instantiate" : "direct")
                << " CLAS and dependent explicit Cluster BLAS at caller-supplied destinations.\n";
            return;
        }
        _CheckAddresses(clusterStorage, clusterAddresses, native.clusterProperties.clusterByteAlignment,
            useTemplates ? "instantiated CLAS" : "direct CLAS");
        _CheckAddresses(blasStorage, blasAddresses, native.clusterProperties.clusterBottomLevelByteAlignment, "Cluster BLAS");

        _CheckComputedDestinations(native, useTemplates, clusterTasks, indexAddress);
        if (!useTemplates) _CheckMoveCopies(native, clusterTable, clusterStorage);

        auto outputSizes = MakeBuffer(sizeof(SizeValues), kScratchUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory);
        auto gpuCountBuffer = MakeBuffer(sizeof(uint32_t), kBuildInputUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        struct CountCase
        {
            uint32_t requestedCount;
            uint32_t submittedCount;
            uint32_t expectedCount;
        };
        constexpr std::array countCases{
            CountCase{ 0, 0, 0 }, CountCase{ 1, 1, 1 }, CountCase{ 2, 2, 2 },
            CountCase{ 3, 2, 2 }, // The producer bounds the requested count to capacity before the Vulkan build reads it.
        };
        constexpr uint32_t sentinelWord = 0xffffffffu;
        constexpr VkDeviceAddress sentinelAddress = 0xffffffffffffffffull;
        for (const auto& [requestedCount, submittedCount, expectedCount] : countCases)
        {
            CHECK_TRUE(submittedCount <= capacity, "Vulkan Cluster BLAS requires the submitted GPU count to fit the target capacity.");
            CommandBuffer dynamicBuild;
            dynamicBuild.FillBuffer(gpuCountBuffer, 0, sizeof(uint32_t), submittedCount);
            dynamicBuild.FillBuffer(outputSizes, 0, sizeof(SizeValues), UINT32_MAX);
            dynamicBuild.FillBuffer(blasTable, blasOffset, sizeof(blasAddresses), sentinelWord);
            AppendBarrier(dynamicBuild, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT | buildStage,
                VK_ACCESS_TRANSFER_WRITE_BIT | buildWrite, buildStage, buildRead | buildWrite);
            blasBuild.CustomizeOperationCountAddress(gpuCountBuffer.GetDeviceAddress());
            blasBuild.CustomizeOutputSizesArray(outputSizes.GetDeviceAddress());
            dynamicBuild.ExecuteClusterAccelStructIndirect(blasBuild);
            AppendBarrier(dynamicBuild, barrier, buildStage | VK_PIPELINE_STAGE_TRANSFER_BIT,
                buildWrite | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            dynamicBuild.CopyBuffer(blasTable, readback, blasOffset, sizeof(clusterAddresses), sizeof(blasAddresses));
            AppendBarrier(dynamicBuild, barrier, buildStage | VK_PIPELINE_STAGE_TRANSFER_BIT,
                buildWrite | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            _Submit(native, dynamicBuild);
            _CheckSizePrefix(outputSizes, expectedCount);

            Buffer::DownloadFromDeviceInline(readback, sizeof(clusterAddresses), blasAddresses.data(), sizeof(blasAddresses));
            const auto addresses = std::span(blasAddresses);
            _CheckAddresses(blasStorage, addresses.first(expectedCount),
                native.clusterProperties.clusterBottomLevelByteAlignment, "GPU-counted Cluster BLAS");
            for (size_t index = expectedCount; index < addresses.size(); ++index)
            {
                CHECK_TRUE(addresses[index] == sentinelAddress,
                    std::format("GPU count request {} submitted {} with capacity {} overwrote inactive Cluster BLAS address {} with 0x{:x}.",
                        requestedCount, submittedCount, capacity, index, addresses[index]));
            }
            std::cout << std::format("GPU count request {} submitted {} with capacity {} preserved {} inactive Cluster BLAS addresses.\n",
                requestedCount, submittedCount, capacity, addresses.size() - expectedCount);
        }

        for (const auto& [requestedCount, submittedCount, expectedCount] : countCases)
        {
            CHECK_TRUE(submittedCount <= capacity, "Vulkan CLAS requires the submitted GPU count to fit the target capacity.");
            CommandBuffer dynamicBuild;
            dynamicBuild.FillBuffer(gpuCountBuffer, 0, sizeof(uint32_t), submittedCount);
            dynamicBuild.FillBuffer(outputSizes, 0, sizeof(SizeValues), UINT32_MAX);
            dynamicBuild.FillBuffer(clusterTable, clusterOffset, sizeof(clusterAddresses), sentinelWord);
            AppendBarrier(dynamicBuild, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                buildStage, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | buildWrite);
            clusterBuild.CustomizeOperationCountAddress(gpuCountBuffer.GetDeviceAddress());
            clusterBuild.CustomizeOutputSizesArray(outputSizes.GetDeviceAddress());
            dynamicBuild.ExecuteClusterAccelStructIndirect(clusterBuild);
            AppendBarrier(dynamicBuild, barrier, buildStage | VK_PIPELINE_STAGE_TRANSFER_BIT,
                buildWrite | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            dynamicBuild.CopyBuffer(clusterTable, readback, clusterOffset, 0, sizeof(clusterAddresses));
            AppendBarrier(dynamicBuild, barrier, buildStage | VK_PIPELINE_STAGE_TRANSFER_BIT,
                buildWrite | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            _Submit(native, dynamicBuild);
            _CheckSizePrefix(outputSizes, expectedCount);

            Buffer::DownloadFromDeviceInline(readback, 0, clusterAddresses.data(), sizeof(clusterAddresses));
            const auto addresses = std::span(clusterAddresses);
            _CheckAddresses(clusterStorage, addresses.first(expectedCount),
                native.clusterProperties.clusterByteAlignment, "GPU-counted CLAS");
            for (size_t index = expectedCount; index < addresses.size(); ++index)
            {
                CHECK_TRUE(addresses[index] == sentinelAddress,
                    std::format("GPU count request {} submitted {} with capacity {} overwrote inactive CLAS address {} with 0x{:x}.",
                        requestedCount, submittedCount, capacity, index, addresses[index]));
            }
            std::cout << std::format("GPU count request {} submitted {} with capacity {} preserved {} inactive CLAS addresses.\n",
                requestedCount, submittedCount, capacity, addresses.size() - expectedCount);
        }
    }

public:
    static void Run(const HeadlessDevice& native)
    {
        // VMA and the RHI use global Volk pointers. Bind only the functions used
        // by this path and restore each one after all RHI resources are released.
#define CLAS_BIND_VK_FUNCTION(table, name) const ScopedValue name##Binding(::name, native.table.name)
        CLAS_BIND_VK_FUNCTION(instanceFunctions, vkGetPhysicalDeviceFormatProperties);
        CLAS_BIND_VK_FUNCTION(instanceFunctions, vkGetPhysicalDeviceProperties);
        CLAS_BIND_VK_FUNCTION(instanceFunctions, vkGetPhysicalDeviceMemoryProperties);
        CLAS_BIND_VK_FUNCTION(instanceFunctions, vkGetPhysicalDeviceProperties2);
        CLAS_BIND_VK_FUNCTION(instanceFunctions, vkGetPhysicalDeviceQueueFamilyProperties);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkAllocateMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkAllocateCommandBuffers);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkBeginCommandBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkBindBufferMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkBindImageMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdBuildClusterAccelerationStructureIndirectNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdCopyBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdFillBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdPipelineBarrier2);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateCommandPool);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateFence);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateSemaphore);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateImage);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyCommandPool);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyFence);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyImage);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroySemaphore);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDeviceWaitIdle);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkEndCommandBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkFlushMappedMemoryRanges);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkFreeMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetBufferDeviceAddress);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetBufferMemoryRequirements);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetClusterAccelerationStructureBuildSizesNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetFenceStatus);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetImageMemoryRequirements);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkInvalidateMappedMemoryRanges);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkMapMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkQueueSubmit2);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkResetCommandPool);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkResetFences);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkUnmapMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkWaitForFences);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetPartitionedAccelerationStructuresBuildSizesNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdBuildPartitionedAccelerationStructuresNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetAccelerationStructureBuildSizesKHR);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateAccelerationStructureKHR);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyAccelerationStructureKHR);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetAccelerationStructureDeviceAddressKHR);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdBuildAccelerationStructuresKHR);
#undef CLAS_BIND_VK_FUNCTION

        const ScopedValue singleton(MyDevice::s_uptrInstance, std::make_unique<MyDevice>());
        auto& device = MyDevice::GetInstance();
        device.vkInstance = native.instance;
        device.vkPhysicalDevice = native.physicalDevice;
        device.vkDevice = native.device;
        device.queueFamilyIndices.graphicsAndComputeFamily = native.queueFamily;
        device.queueFamilyIndices.transferFamily = native.queueFamily;
        device.m_vkGraphicsQueue = native.queue;
        device.m_vkComputeQueue = native.queue;
        device.m_vkTransferQueue = native.queue;
        device.m_cachedPhysicalDeviceProperties = native.properties;
        device.m_clusterAccelerationStructureEnabled = !native.partitionedMode;
        if (!native.partitionedMode) device.m_clusterAccelerationStructureRevision = native.clusterRevision;
        device.m_partitionedAccelerationStructureEnabled = native.partitionedMode;
        device._CreateMemoryAllocator();
        const auto destroyInlineExecution = [](MyDevice* owner) { owner->_DestroyInlineExecution(); };
        const std::unique_ptr<MyDevice, decltype(destroyInlineExecution)> inlineExecutionCleanup(&device, destroyInlineExecution);
        auto& scheduler = MyTaskScheduler::GetInstance();
        scheduler.Create();
        const auto shutdownScheduler = [](MyTaskScheduler* owner) { owner->Destroy(); };
        const std::unique_ptr<MyTaskScheduler, decltype(shutdownScheduler)> schedulerCleanup(&scheduler, shutdownScheduler);
        if (native.partitionedMode)
        {
            PartitionedAccelerationStructureGpuTestProbe::Build(native);
            return;
        }
        _CheckInlineTransfers();
        _BuildAndReadBack(native, true, false);
        _BuildAndReadBack(native, false, false);
        _BuildAndReadBack(native, false, true);
        _BuildAndReadBack(native, true, true);
    }
};

void RunClasGpuTests()
{
    bool enabled = false;
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    CHECK_TRUE(_dupenv_s(&value, &length, "LEARN_VULKAN_CLAS_GPU_TESTS") == 0,
        "Could not read the CLAS smoke-test environment!");
    const std::unique_ptr<char, decltype(&std::free)> owner(value, &std::free);
    enabled = value && std::string_view(value) == "1";
#else
    const char* value = std::getenv("LEARN_VULKAN_CLAS_GPU_TESTS");
    enabled = value && std::string_view(value) == "1";
#endif
    if (!enabled)
        return;

    const VulkanLoader loader;
    const ScopedValue instanceProc(vkGetInstanceProcAddr, loader.GetInstanceProcAddr());
    // Loading a VolkInstanceTable updates this global device proc loader.
    const ScopedValue deviceProc(vkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr{});
    HeadlessDevice headless;
    headless.Create();
    ClusterAccelerationStructureGpuTestProbe::Run(headless);
    std::cout << "CLAS GPU smoke passed.\n";
}


void RunPartitionedAccelerationStructureGpuTests()
{
    bool enabled = false;
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    CHECK_TRUE(_dupenv_s(&value, &length, "LEARN_VULKAN_PTLAS_GPU_TESTS") == 0);
    const std::unique_ptr<char, decltype(&std::free)> owner(value, &std::free);
    enabled = value && std::string_view(value) == "1";
#else
    const char* value = std::getenv("LEARN_VULKAN_PTLAS_GPU_TESTS");
    enabled = value && std::string_view(value) == "1";
#endif
    if (!enabled) return;
    const VulkanLoader loader;
    const ScopedValue instanceProc(vkGetInstanceProcAddr, loader.GetInstanceProcAddr());
    const ScopedValue deviceProc(vkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr{});
    HeadlessDevice headless(true);
    if (!headless.Create())
    {
        std::cout << "PTLAS GPU smoke SKIPPED: no device supports the required PTLAS feature and Vulkan 1.3 build dependencies.\n";
        return;
    }
    ClusterAccelerationStructureGpuTestProbe::Run(headless);
    std::cout << "PTLAS GPU smoke passed (build completion only; traversal is not tested).\n";
}
