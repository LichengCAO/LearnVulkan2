#include "my_vulkan/allocator/private/pipeline_allocator.h"
#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/device.h"
#include "my_vulkan/pipeline/ray_tracing_shader_program.h"
#include "utility/shader_compiler.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

struct ClasPipelineTestProbe
{
    class DeviceScope final
    {
    public:
        DeviceScope() : m_previous(std::move(MyDevice::s_uptrInstance))
        {
            try
            {
                auto& device = MyDevice::GetInstance();
                device.vkDevice = reinterpret_cast<VkDevice>(uintptr_t{1});
                device.vkPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(uintptr_t{1});
                device.queueFamilyIndices.graphicsAndComputeFamily = 0;
                auto& properties = device.m_cachedRayTracingProperties;
                properties.maxRayRecursionDepth = 1;
                properties.shaderGroupHandleSize = 8;
                properties.shaderGroupHandleAlignment = 8;
                device._CreateDescriptorSetLayoutAllocator();
                device._CreatePipelineLayoutAllocator();
                device._CreatePipelineAllocators();
                device._CreateShaderModuleAllocator();
            }
            catch (...)
            {
                MyDevice::s_uptrInstance = std::move(m_previous);
                throw;
            }
        }

        ~DeviceScope() { MyDevice::s_uptrInstance = std::move(m_previous); }
        DeviceScope(const DeviceScope&) = delete;
        auto operator=(const DeviceScope&) -> DeviceScope& = delete;

    private:
        std::unique_ptr<MyDevice> m_previous;
    };

    static void EnableClusterMode()
    {
        MyDevice::GetInstance().m_clusterAccelerationStructureEnabled = true;
    }

    static auto AllowsClusterMode(const RayTracingShaderProgram& program) -> bool
    {
        return program.m_allowClusterAccelerationStructure;
    }

    static void AppendPartitionedBuild(CommandBuffer& stream, const BuildPartitionedAccelerationStructuresNVCommand& build)
    {
        stream._AppendCommand(build);
    }

    static void AppendClusterBuild(CommandBuffer& stream, const BuildClusterAccelerationStructuresNVCommand& build)
    {
        stream._AppendCommand(build);
    }

    static void AppendStream(CommandBuffer& stream, CommandBuffer&& suffix)
    {
        stream._Append(std::move(suffix));
    }

    static void ValidateComputeQueue(const CommandBuffer& commands)
    {
        commands._ValidateQueueCapabilities(QueueFamilyType::COMPUTE);
    }
};

namespace
{
template<class Handle>
auto FakeHandle(uintptr_t value) -> Handle
{
    return reinterpret_cast<Handle>(value);
}

template<class Function>
class ScopedVulkanFunction final
{
public:
    ScopedVulkanFunction(Function& slot, Function replacement) : m_slot(slot), m_previous(slot)
    {
        m_slot = replacement;
    }
    ~ScopedVulkanFunction() { m_slot = m_previous; }
    ScopedVulkanFunction(const ScopedVulkanFunction&) = delete;
    auto operator=(const ScopedVulkanFunction&) -> ScopedVulkanFunction& = delete;

private:
    Function& m_slot;
    Function m_previous;
};

std::vector<std::optional<VkBool32>> nativeClusterModes;
size_t nativeShaderModuleCreates = 0;
bool failNativeGroupReads = false;

auto VKAPI_PTR CreateRayPipeline(VkDevice, VkDeferredOperationKHR, VkPipelineCache, uint32_t count,
    const VkRayTracingPipelineCreateInfoKHR* info, const VkAllocationCallbacks*, VkPipeline* output) -> VkResult
{
    CHECK_TRUE(count == 1);
    if (info->pNext != nullptr)
    {
        const auto* entry = static_cast<const VkBaseInStructure*>(info->pNext);
        CHECK_TRUE(entry->sType == VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV);
        CHECK_TRUE(entry->pNext == nullptr);
        const auto* cluster = reinterpret_cast<const VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV*>(entry);
        nativeClusterModes.emplace_back(cluster->allowClusterAccelerationStructure);
    }
    else
    {
        nativeClusterModes.emplace_back(std::nullopt);
    }
    *output = FakeHandle<VkPipeline>(nativeClusterModes.size());
    return VK_SUCCESS;
}

void VKAPI_PTR DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) {}

auto VKAPI_PTR CreateShaderModule(VkDevice, const VkShaderModuleCreateInfo* info,
    const VkAllocationCallbacks*, VkShaderModule* output) -> VkResult
{
    CHECK_TRUE(info->pCode != nullptr && info->codeSize > 0);
    *output = FakeHandle<VkShaderModule>(++nativeShaderModuleCreates);
    return VK_SUCCESS;
}

void VKAPI_PTR DestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*) {}

auto VKAPI_PTR CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkPipelineLayout* output) -> VkResult
{
    CHECK_TRUE(info->setLayoutCount == 0 && info->pushConstantRangeCount == 0);
    *output = FakeHandle<VkPipelineLayout>(1);
    return VK_SUCCESS;
}

void VKAPI_PTR DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {}

void VKAPI_PTR GetComputeQueueFamily(VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* properties)
{
    if (properties != nullptr)
    {
        CHECK_TRUE(*count >= 1);
        properties[0] = {};
        properties[0].queueFlags = VK_QUEUE_COMPUTE_BIT;
        properties[0].queueCount = 1;
    }
    *count = 1;
}

auto VKAPI_PTR GetShaderGroupHandles(VkDevice, VkPipeline pipeline, uint32_t firstGroup,
    uint32_t groupCount, size_t size, void* data) -> VkResult
{
    CHECK_TRUE(pipeline != VK_NULL_HANDLE && firstGroup == 0 && groupCount == 1 && size == 8);
    if (failNativeGroupReads)
        return VK_ERROR_DEVICE_LOST;
    std::memset(data, 0x5a, size);
    return VK_SUCCESS;
}

class RayGenShaderFixture final
{
public:
    RayGenShaderFixture()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory = std::filesystem::temp_directory_path() / ("learn_vulkan_clas_pipeline_" + std::to_string(stamp));
        CHECK_TRUE(std::filesystem::create_directory(m_directory), "Cannot create CLAS shader fixture directory!");
    }

    ~RayGenShaderFixture()
    {
        std::error_code error;
        std::filesystem::remove(m_directory / "test.rgen.spv", error);
        std::filesystem::remove(m_directory / "test.rgen", error);
        std::filesystem::remove(m_directory, error);
    }

    auto Compile() const -> std::string
    {
        const auto sourcePath = m_directory / "test.rgen";
        {
            std::ofstream source(sourcePath);
            source << "#version 460\n#extension GL_EXT_ray_tracing : require\nvoid main() {}\n";
            CHECK_TRUE(bool(source), "Cannot write the CLAS raygen shader fixture!");
        }
        const auto code = ShaderCompiler(m_directory).CompileFile(sourcePath);
        const auto binaryPath = m_directory / "test.rgen.spv";
        std::ofstream output(binaryPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        CHECK_TRUE(bool(output), "Cannot write the CLAS raygen SPIR-V fixture!");
        return binaryPath.string();
    }

private:
    std::filesystem::path m_directory;
};

template<class Function>
void ExpectRejected(Function&& operation)
{
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    CHECK_TRUE(false, "Expected CLAS pipeline request to be rejected!");
}

void TestRayTracingAllocatorKeysClusterPermission()
{
    nativeClusterModes.clear();
    ScopedVulkanFunction create(vkCreateRayTracingPipelinesKHR, &CreateRayPipeline);
    ScopedVulkanFunction destroy(vkDestroyPipeline, &DestroyPipeline);
    RayTracingPipelineAllocator allocator;
    allocator.Create(FakeHandle<VkDevice>(1));

    VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    const auto regular = allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE);
    VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV cluster{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV};
    cluster.allowClusterAccelerationStructure = VK_TRUE;
    info.pNext = &cluster;
    const auto enabled = allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE);
    CHECK_TRUE(enabled != regular, "CLAS and ordinary pipeline requests must use different cache entries!");

    auto clusterCopy = cluster;
    info.pNext = &clusterCopy;
    CHECK_TRUE(allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE) == enabled,
        "Equal CLAS permission must reuse the pipeline across different extension-node addresses!");
    clusterCopy.allowClusterAccelerationStructure = VK_FALSE;
    const auto disabled = allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE);
    CHECK_TRUE(disabled != regular && disabled != enabled,
        "A CLAS node with VK_FALSE must have a distinct cache entry from absent or VK_TRUE nodes!");
    auto disabledCopy = clusterCopy;
    info.pNext = &disabledCopy;
    CHECK_TRUE(allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE) == disabled,
        "An explicit VK_FALSE node must reuse its cache entry across different node addresses!");

    VkBaseInStructure unsupported{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    info.pNext = &unsupported;
    ExpectRejected([&] { allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE); });
    info.pNext = &cluster;
    cluster.pNext = &clusterCopy;
    ExpectRejected([&] { allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE); });
    cluster.pNext = &unsupported;
    ExpectRejected([&] { allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE); });

    cluster.pNext = nullptr;
    info.pNext = nullptr;
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.pNext = &cluster;
    info.stageCount = 1;
    info.pStages = &stage;
    ExpectRejected([&] { allocator.AllocateRayTracingPipeline(info, VK_NULL_HANDLE); });
    const std::vector<std::optional<VkBool32>> expected{std::nullopt, VK_TRUE, VK_FALSE};
    CHECK_TRUE(nativeClusterModes == expected, "Only valid cache misses may reach Vulkan!");
}

void TestClusterBuildValidatesThePipelineBoundAtItsPosition(
    RayTracingShaderProgram& regularProgram, RayTracingShaderProgram& clusterProgram)
{
    ScopedVulkanFunction getQueueFamilies(vkGetPhysicalDeviceQueueFamilyProperties, &GetComputeQueueFamily);
    const BuildClusterAccelerationStructuresNVCommand clusterBuild{};
    CommandBuffer buildOnly;
    ClasPipelineTestProbe::AppendClusterBuild(buildOnly, clusterBuild);
    ClasPipelineTestProbe::ValidateComputeQueue(buildOnly);

    CommandBuffer regularBeforeBuild;
    regularBeforeBuild.BindRayTracingProgram(regularProgram);
    ClasPipelineTestProbe::AppendStream(regularBeforeBuild, std::move(buildOnly));
    ExpectRejected([&] { ClasPipelineTestProbe::ValidateComputeQueue(regularBeforeBuild); });

    CommandBuffer moved(std::move(regularBeforeBuild));
    ExpectRejected([&] { ClasPipelineTestProbe::ValidateComputeQueue(moved); });

    CommandBuffer reboundBeforeBuild;
    reboundBeforeBuild.BindRayTracingProgram(regularProgram);
    reboundBeforeBuild.BindRayTracingProgram(clusterProgram);
    ClasPipelineTestProbe::AppendClusterBuild(reboundBeforeBuild, clusterBuild);
    ClasPipelineTestProbe::ValidateComputeQueue(reboundBeforeBuild);

    const BuildPartitionedAccelerationStructuresNVCommand partitionedBuild{};
    CommandBuffer partitioned;
    partitioned.BindRayTracingProgram(regularProgram);
    ClasPipelineTestProbe::AppendPartitionedBuild(partitioned, partitionedBuild);
    ClasPipelineTestProbe::ValidateComputeQueue(partitioned);

    CommandBuffer regularAfterBuild;
    ClasPipelineTestProbe::AppendClusterBuild(regularAfterBuild, clusterBuild);
    regularAfterBuild.BindRayTracingProgram(regularProgram);
    ClasPipelineTestProbe::ValidateComputeQueue(regularAfterBuild);
}

void TestRayTracingProgramUsesFinalizedClusterOptIn()
{
    const RayGenShaderFixture shader;
    const auto shaderPath = shader.Compile();
    RayTracingShaderProgramCreateInfo builder;
    builder.AddRayGenerationShaderGroup("main", shaderPath).Finalize();
    const auto defaultInfo = builder;

    builder.CustomizeAllowClusterAccelerationStructure(true);
    CHECK_TRUE(!builder.IsFinalized() && defaultInfo.IsFinalized(), "Opting in must invalidate only the edited description!");
    builder.Finalize();
    const auto enabledInfo = builder;

    builder.CustomizeAllowClusterAccelerationStructure(false);
    CHECK_TRUE(!builder.IsFinalized() && enabledInfo.IsFinalized(), "Opting out must invalidate only the edited description!");
    builder.Finalize();
    const auto disabledInfo = builder;

    builder.CustomizeAllowClusterAccelerationStructure(true).Finalize().Reset();
    CHECK_TRUE(!builder.IsFinalized(), "Reset must invalidate the CLAS pipeline description!");
    builder.AddRayGenerationShaderGroup("main", shaderPath).Finalize();

    nativeClusterModes.clear();
    nativeShaderModuleCreates = 0;
    failNativeGroupReads = false;
    ScopedVulkanFunction createPipeline(vkCreateRayTracingPipelinesKHR, &CreateRayPipeline);
    ScopedVulkanFunction destroyPipeline(vkDestroyPipeline, &DestroyPipeline);
    ScopedVulkanFunction createModule(vkCreateShaderModule, &CreateShaderModule);
    ScopedVulkanFunction destroyModule(vkDestroyShaderModule, &DestroyShaderModule);
    ScopedVulkanFunction createLayout(vkCreatePipelineLayout, &CreatePipelineLayout);
    ScopedVulkanFunction destroyLayout(vkDestroyPipelineLayout, &DestroyPipelineLayout);
    ScopedVulkanFunction getGroups(vkGetRayTracingShaderGroupHandlesKHR, &GetShaderGroupHandles);
    ClasPipelineTestProbe::DeviceScope deviceScope;
    auto& device = MyDevice::GetInstance();
    const VkRayTracingPipelineCreateInfoKHR rawInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    const auto deferred = FakeHandle<VkDeferredOperationKHR>(1);
    VkAllocationCallbacks callbacks{};
    ExpectRejected([&] { device.CreateRayTracingPipeline(rawInfo, VK_NULL_HANDLE, deferred); });
    ExpectRejected([&] { device.CreateRayTracingPipeline(rawInfo, VK_NULL_HANDLE, deferred, &callbacks); });
    CHECK_TRUE(nativeClusterModes.empty(), "Synchronous creation must reject deferral before calling Vulkan!");
    RayTracingShaderProgram clusterProgram;
    ExpectRejected([&] { device.CreateRayTracingShaderProgram(clusterProgram, enabledInfo); });
    CHECK_TRUE(clusterProgram.GetVkPipeline() == VK_NULL_HANDLE && nativeShaderModuleCreates == 0 && nativeClusterModes.empty(),
        "An unsupported CLAS opt-in must leave the target empty and avoid native shader and pipeline creation!");

    RayTracingShaderProgram defaultProgram;
    device.CreateRayTracingShaderProgram(defaultProgram, defaultInfo);
    CHECK_TRUE(defaultProgram.GetVkPipeline() != VK_NULL_HANDLE);
    CHECK_TRUE(!ClasPipelineTestProbe::AllowsClusterMode(defaultProgram));

    ClasPipelineTestProbe::EnableClusterMode();
    failNativeGroupReads = true;
    ExpectRejected([&] { device.CreateRayTracingShaderProgram(clusterProgram, enabledInfo); });
    failNativeGroupReads = false;
    CHECK_TRUE(clusterProgram.GetVkPipeline() == VK_NULL_HANDLE && !ClasPipelineTestProbe::AllowsClusterMode(clusterProgram),
        "A failed Program creation must publish neither the pipeline nor its CLAS permission!");

    device.CreateRayTracingShaderProgram(clusterProgram, enabledInfo);
    CHECK_TRUE(clusterProgram.GetVkPipeline() != VK_NULL_HANDLE && clusterProgram.GetVkPipeline() != defaultProgram.GetVkPipeline(),
        "Retrying the finalized opt-in on a capable device must create a dedicated CLAS pipeline!");
    CHECK_TRUE(ClasPipelineTestProbe::AllowsClusterMode(clusterProgram));

    RayTracingShaderProgram disabledProgram;
    RayTracingShaderProgram resetProgram;
    device.CreateRayTracingShaderProgram(disabledProgram, disabledInfo);
    device.CreateRayTracingShaderProgram(resetProgram, builder);
    CHECK_TRUE(disabledProgram.GetVkPipeline() == defaultProgram.GetVkPipeline(), "Opting out must use the regular cache entry!");
    CHECK_TRUE(resetProgram.GetVkPipeline() == defaultProgram.GetVkPipeline(), "Reset must restore default CLAS permission!");
    CHECK_TRUE(!ClasPipelineTestProbe::AllowsClusterMode(disabledProgram) && !ClasPipelineTestProbe::AllowsClusterMode(resetProgram));
    const std::vector<std::optional<VkBool32>> expected{std::nullopt, VK_TRUE};
    CHECK_TRUE(nativeClusterModes == expected, "Only the opted-in pipeline may chain CLAS permission into Vulkan!");
    CHECK_TRUE(nativeShaderModuleCreates == 1, "Opting into CLAS must still reuse identical native shader modules!");

    TestClusterBuildValidatesThePipelineBoundAtItsPosition(defaultProgram, clusterProgram);
}
}

void RunClasPipelineTests()
{
    TestRayTracingAllocatorKeysClusterPermission();
    TestRayTracingProgramUsesFinalizedClusterOptIn();
}
