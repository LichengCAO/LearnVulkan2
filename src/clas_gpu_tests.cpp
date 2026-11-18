#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/device.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/resource/private/buffer_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>
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
constexpr std::array kDeviceExtensions{
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
            if (queues[index].queueCount > 0 && (queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT))
                return index;
        }
        return std::nullopt;
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
        for (const char* name : kDeviceExtensions)
        {
            auto entry = std::find_if(extensions.begin(), extensions.begin() + count,
                [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
            if (entry == extensions.begin() + count)
                return 0;
            if (std::strcmp(name, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
                revision = entry->specVersion;
        }
        return revision;
    }

    void _SelectPhysicalDevice()
    {
        uint32_t count = 0;
        CheckResult(instanceFunctions.vkEnumeratePhysicalDevices(instance, &count, nullptr), "counting physical devices");
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
            if (revision < 2 || !family)
                continue;

            VkPhysicalDeviceClusterAccelerationStructureFeaturesNV cluster{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
            VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
            VkPhysicalDeviceBufferDeviceAddressFeatures address{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
            VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            features.pNext = &address;
            address.pNext = &accel;
            accel.pNext = &cluster;
            instanceFunctions.vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (address.bufferDeviceAddress && accel.accelerationStructure && cluster.clusterAccelerationStructure)
            {
                physicalDevice = candidate;
                properties = candidateProperties;
                queueFamily = *family;
                clusterRevision = revision;
                return;
            }
        }
        throw std::runtime_error(
            "CLAS GPU smoke requires Vulkan 1.3, a compute queue, buffer device addresses, "
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
    HeadlessDevice() = default;
    HeadlessDevice(const HeadlessDevice&) = delete;
    ~HeadlessDevice()
    {
        if (device != VK_NULL_HANDLE)
            deviceFunctions.vkDestroyDevice(device, nullptr);
        if (instance != VK_NULL_HANDLE)
            instanceFunctions.vkDestroyInstance(instance, nullptr);
    }

    void Create()
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
        _SelectPhysicalDevice();

        VkPhysicalDeviceDriverProperties driver{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 queried{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        queried.pNext = &driver;
        driver.pNext = &accelerationStructureProperties;
        accelerationStructureProperties.pNext = &clusterProperties;
        instanceFunctions.vkGetPhysicalDeviceProperties2(physicalDevice, &queried);
        accelerationStructureProperties.pNext = nullptr;
        std::cout << std::format("CLAS GPU smoke using {} (driver {}, CLAS revision {}).\n",
            properties.deviceName, driver.driverInfo, clusterRevision);

        VkPhysicalDeviceClusterAccelerationStructureFeaturesNV cluster{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV };
        cluster.clusterAccelerationStructure = VK_TRUE;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        accel.accelerationStructure = VK_TRUE;
        accel.pNext = &cluster;
        VkPhysicalDeviceBufferDeviceAddressFeatures address{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
        address.bufferDeviceAddress = VK_TRUE;
        address.pNext = &accel;
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.pNext = &address;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
        VkDevice createdDevice = VK_NULL_HANDLE;
        CheckResult(instanceFunctions.vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &createdDevice), "creating a logical device");
        device = createdDevice;
        volkLoadDeviceTable(&deviceFunctions, device);
        CHECK_TRUE(deviceFunctions.vkGetClusterAccelerationStructureBuildSizesNV &&
            deviceFunctions.vkCmdBuildClusterAccelerationStructureIndirectNV,
            "CLAS GPU smoke could not resolve the enabled CLAS device commands.");
        deviceFunctions.vkGetDeviceQueue(device, queueFamily, 0, &queue);
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

struct ClusterAccelerationStructureGpuTestProbe
{
private:
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

    static void _BuildAndReadBack(const HeadlessDevice& native)
    {
        constexpr std::array<float, 18> positions{
            0, 0, 0, 1, 0, 0, 0, 1, 0,
            2, 0, 0, 3, 0, 0, 2, 1, 0
        };
        constexpr std::array<uint16_t, 3> indices{ 0, 1, 2 };
        auto vertices = MakeBuffer(sizeof(positions), kBuildInputUsage, kHostMemory);
        auto indexBuffer = MakeBuffer(sizeof(indices), kBuildInputUsage, kHostMemory);
        vertices.CopyFromHost(positions.data(), 0, sizeof(positions));
        indexBuffer.CopyFromHost(indices.data(), 0, sizeof(indices));
        const VkDeviceAddress vertexAddress = vertices.GetDeviceAddress();
        const VkDeviceAddress indexAddress = indexBuffer.GetDeviceAddress();
        constexpr VkDeviceSize vertexStride = 3 * sizeof(float);
        const std::array clusterDescriptions{
            ClusterLevelAccelStructCreateInfo{ 1, 1, vertexAddress, vertexStride, 3, indexAddress },
            ClusterLevelAccelStructCreateInfo{ 2, 1, vertexAddress + 3 * vertexStride, vertexStride, 3, indexAddress }
        };
        ClusterLevelAccelStructBatchCreateInfo clusterInfo;
        clusterInfo.SetClusterLevelAccelStructCreateInfos(clusterDescriptions);
        clusterInfo.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
        ClusterLevelAccelStructBatch clusters;
        clusters.Create(clusterInfo);

        const std::array blasDescriptions{
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &clusters, 0, 2 },
            ClusterBasedBottomLevelAccelStructClusterCreateInfo{ &clusters, 1, 1 }
        };
        BottomLevelAccelStructBatchCreateInfo blasInfo;
        blasInfo.SetBottomLevelAccelStructCreateInfos(blasDescriptions);
        BottomLevelAccelStructBatch blases;
        blases.Create(blasInfo);
        CHECK_TRUE(clusters.GetCount() == clusterDescriptions.size() && blases.GetCount() == blasDescriptions.size(),
            "CLAS GPU smoke found unexpected output counts.");

        auto clusterTasks = MakeBuffer(clusters.GetSizesInfo().buildInfoBufferSize, kBuildInputUsage,
            kHostMemory, alignof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV));
        auto blasTasks = MakeBuffer(blases.GetSizesInfo().buildInfoBufferSize, kBuildInputUsage,
            kHostMemory, alignof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV));
        clusters.WriteBuildInfoBufferInline(clusterInfo, clusterTasks);
        blases.WriteBuildInfoBufferInline(blasInfo, blasTasks);

        const VkDeviceSize scratchAlignment = native.clusterProperties.clusterScratchByteAlignment;
        const VkDeviceSize coreScratchAlignment = native.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
        const auto makeScratch = [coreScratchAlignment](const AccelStructSizesInfo& sizes) -> Buffer
        {
            if (sizes.buildScratchBufferSize == 0)
                return {};
            return MakeBuffer(sizes.buildScratchBufferSize, kScratchUsage,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, (std::max)(coreScratchAlignment, sizes.scratchAlignment));
        };
        auto clusterScratch = makeScratch(clusters.GetSizesInfo());
        auto blasScratch = makeScratch(blases.GetSizesInfo());
        const auto scratchAddress = [scratchAlignment](const Buffer& scratch) -> VkDeviceAddress
        {
            if (!scratch.IsValid())
                return 0;
            const auto address = scratch.GetDeviceAddress();
            CHECK_TRUE(address != 0 && scratchAlignment != 0 && address % scratchAlignment == 0,
                "CLAS GPU smoke found an invalid scratch address.");
            return address;
        };

        std::array<VkDeviceAddress, clusterDescriptions.size()> clusterAddresses{};
        std::array<VkDeviceAddress, blasDescriptions.size()> blasAddresses{};
        auto readback = MakeBuffer(sizeof(clusterAddresses) + sizeof(blasAddresses), VK_BUFFER_USAGE_TRANSFER_DST_BIT, kHostMemory);
        constexpr VkPipelineStageFlags buildStage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        constexpr VkAccessFlags buildRead = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        constexpr VkAccessFlags buildWrite = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        const auto barrier = native.deviceFunctions.vkCmdPipelineBarrier;

        CommandBuffer commands;
        AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, buildStage,
            buildRead | VK_ACCESS_SHADER_READ_BIT);
        commands.BuildAccelerationStructureIndirect(ClusterLevelAccelStructBuildIndirectInfo{
            scratchAddress(clusterScratch), clusterTasks.GetDeviceAddress(), &clusters });
        AppendBarrier(commands, barrier, buildStage, buildWrite, buildStage, buildRead);
        commands.BuildAccelerationStructureIndirect(BottomLevelAccelStructBuildIndirectInfo{
            scratchAddress(blasScratch), blasTasks.GetDeviceAddress(), &blases });
        AppendBarrier(commands, barrier, buildStage, buildWrite, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        commands.CopyBuffer(*clusters.GetReferenceBuffer(), readback, 0, 0, sizeof(clusterAddresses));
        commands.CopyBuffer(*blases.GetReferenceBuffer(), readback, 0, sizeof(clusterAddresses), sizeof(blasAddresses));
        AppendBarrier(commands, barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

        commands._ValidateQueueCapabilities(QueueFamilyType::COMPUTE);
        SmokeSubmission submission(native);
        const VkCommandBuffer command = submission.Begin();
        for (const Command* recorded : commands.m_commands)
            recorded->Record(command);
        submission.SubmitAndWait(command);

        auto& allocation = readback.m_storage->allocation;
        const auto* mapped = static_cast<const std::byte*>(allocation.Map());
        std::memcpy(clusterAddresses.data(), mapped, sizeof(clusterAddresses));
        std::memcpy(blasAddresses.data(), mapped + sizeof(clusterAddresses), sizeof(blasAddresses));
        allocation.Unmap();
        _CheckAddresses(*clusters.m_storageBuffer, clusterAddresses, native.clusterProperties.clusterByteAlignment, "CLAS");
        _CheckAddresses(*blases.m_storageBuffer, blasAddresses, native.clusterProperties.clusterBottomLevelByteAlignment, "Cluster BLAS");
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
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkBindBufferMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkBindImageMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdBuildClusterAccelerationStructureIndirectNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCmdCopyBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkCreateImage);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyBuffer);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkDestroyImage);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkFlushMappedMemoryRanges);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkFreeMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetBufferDeviceAddress);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetBufferMemoryRequirements);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetClusterAccelerationStructureBuildSizesNV);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkGetImageMemoryRequirements);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkInvalidateMappedMemoryRanges);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkMapMemory);
        CLAS_BIND_VK_FUNCTION(deviceFunctions, vkUnmapMemory);
#undef CLAS_BIND_VK_FUNCTION

        const ScopedValue singleton(MyDevice::s_uptrInstance, std::make_unique<MyDevice>());
        auto& device = MyDevice::GetInstance();
        device.vkInstance = native.instance;
        device.vkPhysicalDevice = native.physicalDevice;
        device.vkDevice = native.device;
        device.queueFamilyIndices.graphicsAndComputeFamily = native.queueFamily;
        device.m_cachedPhysicalDeviceProperties = native.properties;
        device.m_clusterAccelerationStructureEnabled = true;
        device._CreateMemoryAllocator();
        _BuildAndReadBack(native);
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
