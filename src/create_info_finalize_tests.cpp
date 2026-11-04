#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/descriptor_set_layout_allocator.h"
#include "my_vulkan/allocator/private/pipeline_layout_allocator.h"
#include "my_vulkan/allocator/private/pipeline_allocator.h"
#include "my_vulkan/allocator/private/render_pass_allocator.h"
#include "my_vulkan/pipeline/graphics_shader_program.h"
#include "my_vulkan/pipeline/compute_shader_program.h"
#include "my_vulkan/pipeline/ray_tracing_shader_program.h"
#include "utility/shader_compiler.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

struct CreateInfoFinalizeTestProbe
{
    template<class T>
    static auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }

    // Keep the real allocator implementations while replacing singleton dependencies.
    struct DeviceScope final
    {
        VkDevice device = MyDevice::GetInstance().vkDevice;
        VkPhysicalDeviceProperties properties = MyDevice::GetInstance().m_cachedPhysicalDeviceProperties;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayProperties = MyDevice::GetInstance().m_cachedRayTracingProperties;
        std::unique_ptr<DescriptorSetLayoutAllocator> descriptors;
        std::unique_ptr<PipelineLayoutAllocator> layouts;
        std::unique_ptr<GraphicsPipelineAllocator> graphics;
        std::unique_ptr<ComputePipelineAllocator> compute;
        std::unique_ptr<RayTracingPipelineAllocator> rays;
        std::unique_ptr<RenderPassAllocator> passes;

        DeviceScope()
        {
            auto& instance = MyDevice::GetInstance();
            instance.vkDevice = Handle<VkDevice>(1);
            instance.m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize = 128;
            auto& rt = instance.m_cachedRayTracingProperties;
            rt.maxRayRecursionDepth = 2;
            rt.shaderGroupHandleSize = 8;
            rt.shaderGroupHandleAlignment = 8;
            rt.shaderGroupBaseAlignment = 16;
            descriptors = std::move(instance.m_uptrDescriptorSetLayoutAllocator);
            layouts = std::move(instance.m_uptrPipelineLayoutAllocator);
            graphics = std::move(instance.m_uptrGraphicsPipelineAllocator);
            compute = std::move(instance.m_uptrComputePipelineAllocator);
            rays = std::move(instance.m_uptrRayTracingPipelineAllocator);
            passes = std::move(instance.m_uptrRenderPassAllocator);
            instance.m_uptrDescriptorSetLayoutAllocator = std::make_unique<DescriptorSetLayoutAllocator>();
            instance.m_uptrPipelineLayoutAllocator = std::make_unique<PipelineLayoutAllocator>();
            instance.m_uptrGraphicsPipelineAllocator = std::make_unique<GraphicsPipelineAllocator>();
            instance.m_uptrComputePipelineAllocator = std::make_unique<ComputePipelineAllocator>();
            instance.m_uptrRayTracingPipelineAllocator = std::make_unique<RayTracingPipelineAllocator>();
            instance.m_uptrRenderPassAllocator = std::make_unique<RenderPassAllocator>();
            instance.m_uptrDescriptorSetLayoutAllocator->Create(instance.vkDevice);
            instance.m_uptrPipelineLayoutAllocator->Create(instance.vkDevice);
            instance.m_uptrGraphicsPipelineAllocator->Create(instance.vkDevice);
            instance.m_uptrComputePipelineAllocator->Create(instance.vkDevice);
            instance.m_uptrRayTracingPipelineAllocator->Create(instance.vkDevice);
            instance.m_uptrRenderPassAllocator->Create(instance.vkDevice);
        }
        ~DeviceScope()
        {
            auto& instance = MyDevice::GetInstance();
            instance.m_uptrGraphicsPipelineAllocator = std::move(graphics);
            instance.m_uptrComputePipelineAllocator = std::move(compute);
            instance.m_uptrRayTracingPipelineAllocator = std::move(rays);
            instance.m_uptrRenderPassAllocator = std::move(passes);
            instance.m_uptrPipelineLayoutAllocator = std::move(layouts);
            instance.m_uptrDescriptorSetLayoutAllocator = std::move(descriptors);
            instance.vkDevice = device;
            instance.m_cachedPhysicalDeviceProperties = properties;
            instance.m_cachedRayTracingProperties = rayProperties;
        }
        DeviceScope(const DeviceScope&) = delete;
        auto operator=(const DeviceScope&) -> DeviceScope& = delete;
    };
};

namespace
{
struct NativeStage final
{
    VkShaderStageFlagBits stage;
    VkShaderModule module;
    std::string entry;
};
struct NativeState final
{
    uintptr_t nextHandle = 100;
    uint32_t calls = 0, moduleCreates = 0, moduleDestroys = 0;
    uint32_t layoutCreates = 0, computeCreates = 0, graphicsCreates = 0, rayCreates = 0, groupReads = 0, passCreates = 0;
    bool failModule = false, failLayout = false, failCompute = false, failGraphics = false, failRay = false, failGroups = false, failPass = false;
    std::vector<std::vector<uint32_t>> shaderCode;
    std::map<VkDescriptorSetLayout, std::vector<VkDescriptorSetLayoutBinding>> bindings;
    std::vector<VkDescriptorSetLayout> layoutSets;
    std::vector<VkPushConstantRange> pushConstants;
    std::vector<NativeStage> stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    std::vector<VkAttachmentDescription> passAttachments;
    std::vector<VkAttachmentReference> passColors;
    uint32_t recursionDepth = 0;
};
NativeState* nativeState = nullptr;

auto VKAPI_PTR CreateModule(VkDevice, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks*, VkShaderModule* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->moduleCreates;
    CHECK_TRUE(info->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO && info->codeSize % sizeof(uint32_t) == 0,
        "Native shader module creation must receive complete SPIR-V words!");
    nativeState->shaderCode.emplace_back(info->pCode, info->pCode + info->codeSize / sizeof(uint32_t));
    if (nativeState->failModule) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkShaderModule>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*) { ++nativeState->moduleDestroys; }
auto VKAPI_PTR CreateDescriptorLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* output) -> VkResult
{
    ++nativeState->calls;
    *output = CreateInfoFinalizeTestProbe::Handle<VkDescriptorSetLayout>(nativeState->nextHandle++);
    auto& bindings = nativeState->bindings[*output];
    for (uint32_t index = 0; index < info->bindingCount; ++index) bindings.push_back(info->pBindings[index]);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyDescriptorLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {}
auto VKAPI_PTR CreateLayout(VkDevice, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkPipelineLayout* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->layoutCreates;
    nativeState->layoutSets.clear(); nativeState->pushConstants.clear();
    for (uint32_t index = 0; index < info->setLayoutCount; ++index) nativeState->layoutSets.push_back(info->pSetLayouts[index]);
    for (uint32_t index = 0; index < info->pushConstantRangeCount; ++index) nativeState->pushConstants.push_back(info->pPushConstantRanges[index]);
    if (nativeState->failLayout) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipelineLayout>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {}
void CaptureStages(uint32_t count, const VkPipelineShaderStageCreateInfo* stages)
{
    nativeState->stages.clear();
    for (uint32_t index = 0; index < count; ++index)
        nativeState->stages.push_back({stages[index].stage, stages[index].module, stages[index].pName});
}
auto VKAPI_PTR CreateCompute(VkDevice, VkPipelineCache, uint32_t count, const VkComputePipelineCreateInfo* info,
    const VkAllocationCallbacks*, VkPipeline* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->computeCreates;
    CHECK_TRUE(count == 1 && info->layout != VK_NULL_HANDLE, "Compute creation must have a native layout!");
    CaptureStages(1, &info->stage);
    if (nativeState->failCompute) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipeline>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
auto VKAPI_PTR CreateGraphics(VkDevice, VkPipelineCache, uint32_t count, const VkGraphicsPipelineCreateInfo* info,
    const VkAllocationCallbacks*, VkPipeline* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->graphicsCreates;
    CHECK_TRUE(count == 1 && info->layout != VK_NULL_HANDLE && info->renderPass != VK_NULL_HANDLE,
        "Graphics creation must have native layout and render pass!");
    CaptureStages(info->stageCount, info->pStages);
    nativeState->vertexBindings.clear(); nativeState->vertexAttributes.clear();
    const auto& input = *info->pVertexInputState;
    for (uint32_t index = 0; index < input.vertexBindingDescriptionCount; ++index)
        nativeState->vertexBindings.push_back(input.pVertexBindingDescriptions[index]);
    for (uint32_t index = 0; index < input.vertexAttributeDescriptionCount; ++index)
        nativeState->vertexAttributes.push_back(input.pVertexAttributeDescriptions[index]);
    if (nativeState->failGraphics) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipeline>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
auto VKAPI_PTR CreateRay(VkDevice, VkDeferredOperationKHR, VkPipelineCache, uint32_t count,
    const VkRayTracingPipelineCreateInfoKHR* info, const VkAllocationCallbacks*, VkPipeline* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->rayCreates;
    CHECK_TRUE(count == 1 && info->layout != VK_NULL_HANDLE, "Ray tracing creation must have a native layout!");
    CaptureStages(info->stageCount, info->pStages);
    nativeState->groups.assign(info->pGroups, info->pGroups + info->groupCount);
    nativeState->recursionDepth = info->maxPipelineRayRecursionDepth;
    if (nativeState->failRay) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipeline>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
auto VKAPI_PTR GetGroups(VkDevice, VkPipeline, uint32_t, uint32_t count, size_t size, void* data) -> VkResult
{
    ++nativeState->calls; ++nativeState->groupReads;
    CHECK_TRUE(size == count * 8, "Native RT handles must use cached device handle size!");
    if (nativeState->failGroups) return VK_ERROR_DEVICE_LOST;
    for (uint32_t index = 0; index < count; ++index)
        std::memset(static_cast<uint8_t*>(data) + index * 8, 0x5a + index, 8);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) {}
auto VKAPI_PTR CreatePass(VkDevice, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->passCreates;
    nativeState->passAttachments.clear(); nativeState->passColors.clear();
    for (uint32_t index = 0; index < info->attachmentCount; ++index) nativeState->passAttachments.push_back(info->pAttachments[index]);
    if (info->subpassCount != 0)
        for (uint32_t index = 0; index < info->pSubpasses[0].colorAttachmentCount; ++index)
            nativeState->passColors.push_back(info->pSubpasses[0].pColorAttachments[index]);
    if (nativeState->failPass) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkRenderPass>(nativeState->nextHandle++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*) {}

// Declare before runtime owners, so their cleanup and cache cleanup run under hooks.
struct NativeEnvironment final
{
    NativeState state;
    PFN_vkCreateShaderModule module = vkCreateShaderModule;
    PFN_vkDestroyShaderModule destroyModule = vkDestroyShaderModule;
    PFN_vkCreateDescriptorSetLayout descriptor = vkCreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptor = vkDestroyDescriptorSetLayout;
    PFN_vkCreatePipelineLayout layout = vkCreatePipelineLayout;
    PFN_vkDestroyPipelineLayout destroyLayout = vkDestroyPipelineLayout;
    PFN_vkCreateComputePipelines compute = vkCreateComputePipelines;
    PFN_vkCreateGraphicsPipelines graphics = vkCreateGraphicsPipelines;
    PFN_vkCreateRayTracingPipelinesKHR rays = vkCreateRayTracingPipelinesKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR groups = vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkDestroyPipeline destroyPipeline = vkDestroyPipeline;
    PFN_vkCreateRenderPass pass = vkCreateRenderPass;
    PFN_vkDestroyRenderPass destroyPass = vkDestroyRenderPass;
    std::unique_ptr<CreateInfoFinalizeTestProbe::DeviceScope> device;
    NativeEnvironment()
    {
        nativeState = &state;
        vkCreateShaderModule = CreateModule; vkDestroyShaderModule = DestroyModule;
        vkCreateDescriptorSetLayout = CreateDescriptorLayout; vkDestroyDescriptorSetLayout = DestroyDescriptorLayout;
        vkCreatePipelineLayout = CreateLayout; vkDestroyPipelineLayout = DestroyLayout;
        vkCreateComputePipelines = CreateCompute; vkCreateGraphicsPipelines = CreateGraphics;
        vkCreateRayTracingPipelinesKHR = CreateRay; vkGetRayTracingShaderGroupHandlesKHR = GetGroups;
        vkDestroyPipeline = DestroyPipeline; vkCreateRenderPass = CreatePass; vkDestroyRenderPass = DestroyPass;
        device = std::make_unique<CreateInfoFinalizeTestProbe::DeviceScope>();
    }
    ~NativeEnvironment()
    {
        device.reset();
        vkCreateShaderModule = module; vkDestroyShaderModule = destroyModule;
        vkCreateDescriptorSetLayout = descriptor; vkDestroyDescriptorSetLayout = destroyDescriptor;
        vkCreatePipelineLayout = layout; vkDestroyPipelineLayout = destroyLayout;
        vkCreateComputePipelines = compute; vkCreateGraphicsPipelines = graphics;
        vkCreateRayTracingPipelinesKHR = rays; vkGetRayTracingShaderGroupHandlesKHR = groups;
        vkDestroyPipeline = destroyPipeline; vkCreateRenderPass = pass; vkDestroyRenderPass = destroyPass;
        nativeState = nullptr;
    }
    NativeEnvironment(const NativeEnvironment&) = delete;
    auto operator=(const NativeEnvironment&) -> NativeEnvironment& = delete;
};
template<class T>
struct Owner final
{
    T value;
    ~Owner() { value.Destroy(); }
};
template<class F>
void ExpectFailure(F&& action)
{
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    CHECK_TRUE(failed, "Invalid input or a failed native operation must throw!");
}
template<class F>
void ExpectUnfinalizedFailure(F&& action)
{
    bool failed = false;
    try { action(); }
    catch (const std::runtime_error& error)
    {
        failed = true;
        CHECK_TRUE(std::string(error.what()).find("finalized") != std::string::npos,
            "Unfinalized Create must report its Finalize guard before shader loading or other validation!");
    }
    CHECK_TRUE(failed, "Runtime Create must reject an unfinalized description!");
}
class ShaderFiles final
{
    std::filesystem::path m_directory;
public:
    ShaderFiles()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory = std::filesystem::temp_directory_path() / ("learn_vulkan_native_finalize_" + std::to_string(stamp));
        CHECK_TRUE(std::filesystem::create_directory(m_directory), "Cannot create isolated native shader fixtures!");
    }
    ~ShaderFiles()
    {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }
    auto Write(std::string_view name, std::string_view source) const -> std::string
    {
        const auto sourcePath = m_directory / name;
        { std::ofstream file(sourcePath); file << source; CHECK_TRUE(bool(file), "Cannot write native shader fixture!"); }
        const auto code = ShaderCompiler(m_directory).CompileFile(sourcePath);
        const auto path = sourcePath.string() + ".spv";
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        CHECK_TRUE(bool(file), "Cannot write native SPIR-V fixture!");
        return path;
    }
};

void TestUnfinalizedDescriptionsNeverReachVulkan()
{
    NativeEnvironment environment;
    Owner<ShaderModule> module;
    Owner<GraphicsShaderProgram> graphics;
    Owner<ComputeShaderProgram> compute;
    Owner<RayTracingShaderProgram> rays;
    Owner<PipelineLayout> layout;
    Owner<RenderPass> pass;
    ShaderModuleCreateInfo moduleInfo;
    GraphicsShaderProgramCreateInfo graphicsInfo;
    ComputeShaderProgramCreateInfo computeInfo;
    RayTracingShaderProgramCreateInfo rayInfo;
    PipelineLayoutCreateInfo layoutInfo;
    RenderPassCreateInfo passInfo;
    // Valid host-only descriptions expose accidental implicit Finalize too.
    passInfo.AddSubpass("main", SubpassDescription{});
    ExpectUnfinalizedFailure([&] { module.value.Create(moduleInfo); });
    ExpectUnfinalizedFailure([&] { graphics.value.Create(graphicsInfo); });
    ExpectUnfinalizedFailure([&] { compute.value.Create(computeInfo); });
    ExpectUnfinalizedFailure([&] { rays.value.Create(rayInfo); });
    ExpectUnfinalizedFailure([&] { layout.value.Create(layoutInfo); });
    ExpectUnfinalizedFailure([&] { pass.value.Create(passInfo); });
    CHECK_TRUE(environment.state.calls == 0, "Unfinalized runtime Create must reject before every native API!");
    CHECK_TRUE(!moduleInfo.IsFinalized() && !graphicsInfo.IsFinalized() && !computeInfo.IsFinalized() &&
        !rayInfo.IsFinalized() && !layoutInfo.IsFinalized() && !passInfo.IsFinalized(),
        "Runtime Create must never finalize its input implicitly!");
}

void TestShaderModuleOwnsFinalizedCodeAndEntryNames()
{
    NativeEnvironment environment;
    ShaderFiles files;
    const auto path = files.Write("module.comp", "#version 450\nlayout(local_size_x=1) in; void main() {}\n");
    Owner<ShaderModule> first, second;
    std::vector<uint32_t> expected;
    {
        ShaderModuleCreateInfo info;
        info.SetShader(path).Finalize();
        expected.assign(info.GetCode().begin(), info.GetCode().end());
        CHECK_TRUE(std::filesystem::remove(path), "Shader fixture must be deleted before native creation!");
        environment.state.failModule = true;
        ExpectFailure([&] { first.value.Create(info); });
        environment.state.failModule = false;
        first.value.Create(info);
        second.value.Create(info);
        info.Reset();
    }
    const auto stage = first.value.GetShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT);
    const auto other = second.value.GetShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT);
    CHECK_TRUE(stage.module != other.module && stage.stage == VK_SHADER_STAGE_COMPUTE_BIT &&
        std::string(stage.pName) == "main" && std::string(other.pName) == "main" &&
        first.value.GetEntryReflection(VK_SHADER_STAGE_COMPUTE_BIT).name == "main",
        "Each module must own its native handle and retain entry reflection after description destruction!");
    CHECK_TRUE(environment.state.shaderCode == std::vector<std::vector<uint32_t>>(3, expected),
        "Failed creation and both retries must receive the exact finalized SPIR-V bytes without file I/O!");
    first.value.Destroy(); first.value.Destroy(); second.value.Destroy();
    CHECK_TRUE(environment.state.moduleCreates == 3 && environment.state.moduleDestroys == 2,
        "Only successfully created shader handles must be destroyed, exactly once!");
}

void TestSparseLayoutNativeSnapshotAndFailureRetry()
{
    NativeEnvironment environment;
    ShaderEntryReflection entry{};
    entry.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    entry.descriptors.push_back({"storage", 2, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
    entry.pushConstants.push_back({"pc", 0, 16});
    ShaderProgramReflection reflection;
    reflection.Create({entry});
    PipelineLayoutCreateInfo info;
    info.SetReflection(reflection).Finalize();
    Owner<PipelineLayout> layout;
    environment.state.failLayout = true;
    ExpectFailure([&] { layout.value.Create(info); });
    CHECK_TRUE(layout.value.GetVkPipelineLayout() == VK_NULL_HANDLE && layout.value.GetVkDescriptorBindings().empty() &&
        layout.value.GetVkPushConstantRanges().empty(), "Failed native layout creation must not publish partial runtime metadata!");
    environment.state.failLayout = false;
    layout.value.Create(info);
    info.Reset();
    const auto& sets = environment.state.layoutSets;
    CHECK_TRUE(environment.state.layoutCreates == 2 && sets.size() == 3 &&
        environment.state.bindings.at(sets[0]).empty() && environment.state.bindings.at(sets[1]).empty(),
        "Sparse reflection must retain empty native layouts at descriptor sets zero and one!");
    const auto& bindings = environment.state.bindings.at(sets[2]);
    CHECK_TRUE(bindings.size() == 1 && bindings[0].binding == 3 && bindings[0].descriptorCount == 1 &&
        bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && bindings[0].stageFlags == VK_SHADER_STAGE_COMPUTE_BIT,
        "Native descriptors must preserve reflected binding, count, type and stages!");
    const auto& ranges = environment.state.pushConstants;
    CHECK_TRUE(ranges.size() == 1 && ranges[0].offset == 0 && ranges[0].size == 16 && ranges[0].stageFlags == VK_SHADER_STAGE_COMPUTE_BIT,
        "Native push constants must preserve reflected byte ranges and stages!");
    const std::pair<uint32_t, uint32_t> expected{2, 3};
    CHECK_TRUE(layout.value.GetSetBinding("storage") == expected && layout.value.GetVkDescriptorBindings().size() == 3 &&
        layout.value.GetVkDescriptorBindings()[2].at(3).descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
        layout.value.GetVkPushConstantRanges()[0].size == 16 &&
        layout.value.GetDescriptorSetLayout(2).GetVkDescriptorSetLayout() == sets[2],
        "Runtime layout metadata must survive resetting its source description!");
    PipelineLayoutCreateInfo tooLarge;
    tooLarge.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, 132).Finalize();
    Owner<PipelineLayout> rejected;
    const auto calls = environment.state.calls;
    ExpectFailure([&] { rejected.value.Create(tooLarge); });
    CHECK_TRUE(environment.state.calls == calls, "Cached device push constant limits must be checked before native calls!");
}

void TestProgramNativeCreationUsesFinalizedSnapshotsAndCleansFailures()
{
    NativeEnvironment environment;
    ShaderFiles files;
    const auto computeFile = files.Write("program.comp", "#version 450\nlayout(local_size_x=1) in; void main() {}\n");
    const auto vertexFile = files.Write("program.vert", "#version 450\nlayout(location=0) in vec3 position; layout(location=1) in vec2 uv; void main(){ gl_Position=vec4(position.xy+uv,position.z,1); }\n");
    const auto fragmentFile = files.Write("program.frag", "#version 450\nlayout(location=2) out vec4 color; void main(){ color=vec4(1); }\n");
    const auto rayFile = files.Write("program.rgen", "#version 460\n#extension GL_EXT_ray_tracing : require\nvoid main() {}\n");
    ComputeShaderProgramCreateInfo computeInfo;
    GraphicsShaderProgramCreateInfo graphicsInfo;
    RayTracingShaderProgramCreateInfo rayInfo;
    computeInfo.SetShader(computeFile).Finalize();
    graphicsInfo.AddVertexShader(vertexFile).AddFragmentShader(fragmentFile)
        .SetDefaultColorAttachment(2, VK_FORMAT_R8G8B8A8_UNORM).Finalize();
    rayInfo.AddRayGenerationShaderGroup("primary", rayFile).AddRayGenerationShaderGroup("secondary", rayFile)
        .CustomizeMaxRecursionDepth(2).Finalize();
    CHECK_TRUE(std::filesystem::remove(computeFile) && std::filesystem::remove(vertexFile) &&
        std::filesystem::remove(fragmentFile) && std::filesystem::remove(rayFile),
        "Program shader files must be absent throughout native creation and retry!");
    Owner<ComputeShaderProgram> compute;
    Owner<GraphicsShaderProgram> graphics;
    Owner<RayTracingShaderProgram> rays;
    environment.state.failCompute = true;
    ExpectFailure([&] { compute.value.Create(computeInfo); });
    CHECK_TRUE(compute.value.GetVkPipeline() == VK_NULL_HANDLE && compute.value.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE &&
        environment.state.moduleDestroys == 1, "Failed compute creation must clear its runtime layout and shader module!");
    environment.state.failCompute = false;
    compute.value.Create(computeInfo);
    computeInfo.Reset();
    CHECK_TRUE(compute.value.GetVkPipeline() != VK_NULL_HANDLE && environment.state.stages.size() == 1 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_COMPUTE_BIT && environment.state.stages[0].entry == "main",
        "Compute retry must use the finalized stage snapshot!");

    auto destroyed = environment.state.moduleDestroys;
    environment.state.failGraphics = true;
    ExpectFailure([&] { graphics.value.Create(graphicsInfo); });
    CHECK_TRUE(graphics.value.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE && environment.state.moduleDestroys == destroyed + 2,
        "Failed default graphics pipeline creation must release both shaders and its layout!");
    environment.state.failGraphics = false;
    graphics.value.Create(graphicsInfo);
    graphicsInfo.Reset();
    CHECK_TRUE(graphics.value.GetVkPipeline({}) != VK_NULL_HANDLE && environment.state.stages.size() == 2 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_VERTEX_BIT && environment.state.stages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
        environment.state.stages[0].entry == "main" && environment.state.stages[1].entry == "main" &&
        graphics.value.GetDefaultRenderPass().GetVkRenderPass() != VK_NULL_HANDLE,
        "Graphics retry and cached default pipeline must retain finalized stages and generated render pass!");
    const auto& colors = environment.state.passColors;
    const auto& attachments = environment.state.passAttachments;
    CHECK_TRUE(colors.size() == 3 && colors[0].attachment == VK_ATTACHMENT_UNUSED && colors[1].attachment == VK_ATTACHMENT_UNUSED &&
        colors[2].attachment == 0 && attachments.size() == 1 && attachments[0].format == VK_FORMAT_R8G8B8A8_UNORM &&
        attachments[0].finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        "Generated default pass must preserve sparse fragment locations, format and final layout!");
    const auto& vertexBindings = environment.state.vertexBindings;
    const auto& attributes = environment.state.vertexAttributes;
    CHECK_TRUE(vertexBindings.size() == 1 && vertexBindings[0].binding == 0 && vertexBindings[0].stride == 20 &&
        vertexBindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX && attributes.size() == 2 &&
        attributes[0].location == 0 && attributes[0].binding == 0 && attributes[0].offset == 0 && attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT &&
        attributes[1].location == 1 && attributes[1].binding == 0 && attributes[1].offset == 12 && attributes[1].format == VK_FORMAT_R32G32_SFLOAT,
        "Automatic vertex input must reach native creation with packed stride, offsets and formats!");

    destroyed = environment.state.moduleDestroys;
    environment.state.failRay = true;
    ExpectFailure([&] { rays.value.Create(rayInfo); });
    CHECK_TRUE(rays.value.GetVkPipeline() == VK_NULL_HANDLE && rays.value.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE &&
        rays.value.GetShaderGroupCount() == 0 && environment.state.moduleDestroys == destroyed + 1,
        "Failed RT pipeline creation must clear runtime groups, layout and deduplicated shader module!");
    environment.state.failRay = false;
    environment.state.failGroups = true;
    ExpectFailure([&] { rays.value.Create(rayInfo); });
    CHECK_TRUE(rays.value.GetVkPipeline() == VK_NULL_HANDLE && rays.value.GetShaderGroupCount() == 0 &&
        rays.value.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE && environment.state.moduleDestroys == destroyed + 2,
        "Failure after RT pipeline creation must also leave the runtime object reusable!");
    environment.state.failGroups = false;
    rays.value.Create(rayInfo);
    rayInfo.Reset();
    CHECK_TRUE(rays.value.GetVkPipeline() != VK_NULL_HANDLE && rays.value.GetShaderGroupCount() == 2 &&
        environment.state.recursionDepth == 2 && environment.state.groups.size() == 2 && environment.state.stages.size() == 1 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR && environment.state.stages[0].entry == "main" &&
        environment.state.groups[0].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR && environment.state.groups[0].generalShader == 0 &&
        environment.state.groups[1].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR && environment.state.groups[1].generalShader == 0,
        "RT native creation must preserve named group order while sharing one shader stage!");
    const auto& primary = rays.value.GetShaderGroupSet().GetGroupData("primary");
    const auto& secondary = rays.value.GetShaderGroupSet().GetGroupData("secondary");
    CHECK_TRUE(primary.type == ShaderTableType::RayGeneration && secondary.type == ShaderTableType::RayGeneration &&
        primary.handle == std::vector<uint8_t>(8, 0x5a) && secondary.handle == std::vector<uint8_t>(8, 0x5b),
        "Named RT group metadata and handles must remain owned after resetting the description!");
    compute.value.Destroy(); graphics.value.Destroy(); rays.value.Destroy();
    CHECK_TRUE(environment.state.moduleCreates == environment.state.moduleDestroys,
        "All program creation retries and successful lifetimes must release every shader module!");
}

void TestRenderPassNativeFailureDoesNotConsumeFinalizedInfo()
{
    NativeEnvironment environment;
    RenderPassCreateInfo info;
    info.AddSubpass("main", SubpassDescription{});
    info.Finalize();
    Owner<RenderPass> pass;
    environment.state.failPass = true;
    ExpectFailure([&] { pass.value.Create(info); });
    CHECK_TRUE(pass.value.GetVkRenderPass() == VK_NULL_HANDLE && info.IsFinalized(),
        "Failed native render pass creation must preserve the finalized description for retry!");
    environment.state.failPass = false;
    pass.value.Create(info);
    CHECK_TRUE(pass.value.GetVkRenderPass() != VK_NULL_HANDLE && environment.state.passCreates == 2,
        "A finalized render pass description must support retry after a native failure!");
}
}

void RunCreateInfoFinalizeTests()
{
    TestUnfinalizedDescriptionsNeverReachVulkan();
    TestShaderModuleOwnsFinalizedCodeAndEntryNames();
    TestSparseLayoutNativeSnapshotAndFailureRetry();
    TestProgramNativeCreationUsesFinalizedSnapshotsAndCleansFailures();
    TestRenderPassNativeFailureDoesNotConsumeFinalizedInfo();
}
