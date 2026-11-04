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
#include <type_traits>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static_assert(std::is_nothrow_move_assignable_v<PipelineLayout>);
static_assert(!std::is_copy_constructible_v<ComputeShaderProgram> && !std::is_copy_assignable_v<ComputeShaderProgram>);
static_assert(!std::is_move_constructible_v<ComputeShaderProgram> && !std::is_move_assignable_v<ComputeShaderProgram>);
template<class T>
concept HasPublicComputeCreate = requires(T& value, const ComputeShaderProgramCreateInfo& info) { value.Create(info); };
template<class T>
concept HasPublicGraphicsCreate = requires(T& value, const GraphicsShaderProgramCreateInfo& info) { value.Create(info); };
template<class T>
concept HasPublicRayTracingCreate = requires(T& value, const RayTracingShaderProgramCreateInfo& info) { value.Create(info); };
template<class T>
concept HasPublicDestroy = requires(T& value) { value.Destroy(); };
template<class T>
concept HasPublicReset = requires(T& value) { value.Reset(); };
static_assert(!HasPublicComputeCreate<ComputeShaderProgram> && !HasPublicDestroy<ComputeShaderProgram> &&
    !HasPublicReset<ComputeShaderProgram>);
static_assert(!std::is_copy_constructible_v<GraphicsShaderProgram> && !std::is_copy_assignable_v<GraphicsShaderProgram> &&
    !std::is_move_constructible_v<GraphicsShaderProgram> && !std::is_move_assignable_v<GraphicsShaderProgram>);
static_assert(!std::is_copy_constructible_v<RayTracingShaderProgram> && !std::is_copy_assignable_v<RayTracingShaderProgram> &&
    !std::is_move_constructible_v<RayTracingShaderProgram> && !std::is_move_assignable_v<RayTracingShaderProgram>);
static_assert(!HasPublicGraphicsCreate<GraphicsShaderProgram> && !HasPublicDestroy<GraphicsShaderProgram> &&
    !HasPublicReset<GraphicsShaderProgram>);
static_assert(!HasPublicRayTracingCreate<RayTracingShaderProgram> && !HasPublicDestroy<RayTracingShaderProgram> &&
    !HasPublicReset<RayTracingShaderProgram>);

struct CreateInfoFinalizeTestProbe
{
    template<class T>
    static auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }

    static auto ExchangeLayouts(std::unique_ptr<PipelineLayoutAllocator> replacement)
        -> std::unique_ptr<PipelineLayoutAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrPipelineLayoutAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static auto ExchangeDescriptors(std::unique_ptr<DescriptorSetLayoutAllocator> replacement)
        -> std::unique_ptr<DescriptorSetLayoutAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrDescriptorSetLayoutAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static auto ExchangeCompute(std::unique_ptr<ComputePipelineAllocator> replacement)
        -> std::unique_ptr<ComputePipelineAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrComputePipelineAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static auto ExchangeGraphics(std::unique_ptr<GraphicsPipelineAllocator> replacement)
        -> std::unique_ptr<GraphicsPipelineAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrGraphicsPipelineAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static auto ExchangeRayTracing(std::unique_ptr<RayTracingPipelineAllocator> replacement)
        -> std::unique_ptr<RayTracingPipelineAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrRayTracingPipelineAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static auto EmptyComputeDestructionLeavesDeviceAbsent() -> bool
    {
        auto previous = std::move(MyDevice::s_uptrInstance);
        { ComputeShaderProgram empty; }
        const auto untouched = MyDevice::s_uptrInstance == nullptr;
        MyDevice::s_uptrInstance = std::move(previous);
        return untouched;
    }
    static auto EmptyProgramDestructionLeavesDeviceAbsent() -> bool
    {
        auto previous = std::move(MyDevice::s_uptrInstance);
        { GraphicsShaderProgram empty; RayTracingShaderProgram emptyRay; }
        const auto untouched = MyDevice::s_uptrInstance == nullptr;
        MyDevice::s_uptrInstance = std::move(previous);
        return untouched;
    }
    static void SetPushConstantLimit(uint32_t value)
    {
        MyDevice::GetInstance().m_cachedPhysicalDeviceProperties.limits.maxPushConstantsSize = value;
    }
    static void SetRayHandleProperties(uint32_t size, uint32_t alignment)
    {
        auto& properties = MyDevice::GetInstance().m_cachedRayTracingProperties;
        properties.shaderGroupHandleSize = size;
        properties.shaderGroupHandleAlignment = alignment;
    }

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
#if defined(_MSC_VER) && defined(_DEBUG)
thread_local bool failNextLayoutAllocation = false;
auto __cdecl FailLayoutAllocation(int type, void*, size_t, int block, long, const unsigned char*, int) -> int
{
    if (type == _HOOK_ALLOC && block != _CRT_BLOCK && failNextLayoutAllocation)
    {
        failNextLayoutAllocation = false;
        return 0;
    }
    return 1;
}
struct LayoutAllocationFailureScope final
{
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(FailLayoutAllocation);
    ~LayoutAllocationFailureScope()
    {
        failNextLayoutAllocation = false;
        _CrtSetAllocHook(previous);
    }
};
#endif
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
    uint32_t descriptorCreates = 0, descriptorDestroys = 0, layoutDestroys = 0;
    uint32_t pipelineDestroys = 0;
    std::set<VkPipeline> livePipelines;
    std::set<VkShaderModule> liveModules;
    VkPipelineCache computePipelineCache = VK_NULL_HANDLE;
    bool failDescriptor = false;
    bool failLayoutPublication = false;
    std::set<VkPipelineLayout> liveLayouts;
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
    nativeState->liveModules.insert(*output);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyModule(VkDevice, VkShaderModule module, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->liveModules.erase(module) == 1, "Shader modules must be destroyed exactly once!");
    ++nativeState->moduleDestroys;
}
auto VKAPI_PTR CreateDescriptorLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->descriptorCreates;
    if (nativeState->failDescriptor) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkDescriptorSetLayout>(nativeState->nextHandle++);
    auto& bindings = nativeState->bindings[*output];
    for (uint32_t index = 0; index < info->bindingCount; ++index) bindings.push_back(info->pBindings[index]);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyDescriptorLayout(VkDevice, VkDescriptorSetLayout layout, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->bindings.erase(layout) == 1, "Descriptor layouts must be destroyed exactly once!");
    ++nativeState->descriptorDestroys;
}
auto VKAPI_PTR CreateLayout(VkDevice, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkPipelineLayout* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->layoutCreates;
    nativeState->layoutSets.clear(); nativeState->pushConstants.clear();
    for (uint32_t index = 0; index < info->setLayoutCount; ++index) nativeState->layoutSets.push_back(info->pSetLayouts[index]);
    for (uint32_t index = 0; index < info->pushConstantRangeCount; ++index) nativeState->pushConstants.push_back(info->pPushConstantRanges[index]);
    if (nativeState->failLayout) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipelineLayout>(nativeState->nextHandle++);
    nativeState->liveLayouts.insert(*output);
#if defined(_MSC_VER) && defined(_DEBUG)
    // All test bookkeeping has finished. Fail the allocator's next host
    // allocation after native creation, without adding a production test seam.
    failNextLayoutAllocation = nativeState->failLayoutPublication;
#endif
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyLayout(VkDevice, VkPipelineLayout layout, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->liveLayouts.erase(layout) == 1, "Pipeline layouts must be destroyed exactly once!");
    ++nativeState->layoutDestroys;
}
void CaptureStages(uint32_t count, const VkPipelineShaderStageCreateInfo* stages)
{
    nativeState->stages.clear();
    for (uint32_t index = 0; index < count; ++index)
        nativeState->stages.push_back({stages[index].stage, stages[index].module, stages[index].pName});
}
auto VKAPI_PTR CreateCompute(VkDevice, VkPipelineCache cache, uint32_t count, const VkComputePipelineCreateInfo* info,
    const VkAllocationCallbacks*, VkPipeline* output) -> VkResult
{
    ++nativeState->calls; ++nativeState->computeCreates;
    CHECK_TRUE(count == 1 && info->layout != VK_NULL_HANDLE, "Compute creation must have a native layout!");
    CaptureStages(1, &info->stage);
    nativeState->computePipelineCache = cache;
    if (nativeState->failCompute) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = CreateInfoFinalizeTestProbe::Handle<VkPipeline>(nativeState->nextHandle++);
    nativeState->livePipelines.insert(*output);
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
    nativeState->livePipelines.insert(*output);
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
    nativeState->livePipelines.insert(*output);
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
void VKAPI_PTR DestroyPipeline(VkDevice, VkPipeline pipeline, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->livePipelines.erase(pipeline) == 1, "Pipelines must be destroyed exactly once!");
    ++nativeState->pipelineDestroys;
}
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
    GraphicsShaderProgram graphics;
    ComputeShaderProgram compute;
    RayTracingShaderProgram rays;
    PipelineLayout layout;
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
    ExpectUnfinalizedFailure([&] { MyDevice::GetInstance().CreateGraphicsShaderProgram(graphics, graphicsInfo); });
    ExpectUnfinalizedFailure([&] { MyDevice::GetInstance().CreateComputeShaderProgram(compute, computeInfo); });
    ExpectUnfinalizedFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(rays, rayInfo); });
    ExpectUnfinalizedFailure([&] { MyDevice::GetInstance().GetOrCreatePipelineLayout(layout, layoutInfo); });
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
    PipelineLayout layout;
    environment.state.failLayout = true;
    ExpectFailure([&] { MyDevice::GetInstance().GetOrCreatePipelineLayout(layout, info); });
    CHECK_TRUE(layout.GetVkPipelineLayout() == VK_NULL_HANDLE && layout.GetVkDescriptorBindings().empty() &&
        layout.GetVkPushConstantRanges().empty(), "Failed native layout creation must not publish partial runtime metadata!");
    const auto children = environment.state.bindings;
    CHECK_TRUE(children.size() == 2 && environment.state.descriptorDestroys == 0,
        "Outer creation failure must retain cached children, including the shared sparse empty layout!");
    environment.state.failLayout = false;
    MyDevice::GetInstance().GetOrCreatePipelineLayout(layout, info);
    CHECK_TRUE(environment.state.bindings.size() == children.size() && environment.state.descriptorCreates == 2,
        "Retry must reuse children published before the outer creation failed!");
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
    CHECK_TRUE(layout.GetSetBinding("storage") == expected && layout.GetVkDescriptorBindings().size() == 3 &&
        layout.GetVkDescriptorBindings()[2].at(3).descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
        layout.GetVkPushConstantRanges()[0].size == 16 &&
        layout.GetDescriptorSetLayout(2).GetVkDescriptorSetLayout() == sets[2],
        "Runtime layout metadata must survive resetting its source description!");
    PipelineLayoutCreateInfo tooLarge;
    tooLarge.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, 132).Finalize();
    PipelineLayout rejected;
    const auto calls = environment.state.calls;
    ExpectFailure([&] { MyDevice::GetInstance().GetOrCreatePipelineLayout(rejected, tooLarge); });
    CHECK_TRUE(environment.state.calls == calls, "Cached device push constant limits must be checked before native calls!");
}

void TestLayoutCacheAndIndependentMetadata()
{
    NativeEnvironment environment;
    auto& device = MyDevice::GetInstance();
    auto makeInfo = [](const char* name) -> PipelineLayoutCreateInfo
    {
        ShaderEntryReflection entry{};
        entry.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        entry.descriptors.push_back({name, 0, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
        ShaderProgramReflection reflection;
        reflection.Create({entry});
        PipelineLayoutCreateInfo info;
        info.SetReflection(reflection).Finalize();
        return info;
    };
    auto info = makeInfo("first");
    PipelineLayout first, second;
    device.GetOrCreatePipelineLayout(first, info);
    info.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, 16);
    const auto otherInfo = makeInfo("second");
    device.GetOrCreatePipelineLayout(second, otherInfo);
    PipelineLayout afterSourceDestruction;
    {
        auto transientInfo = makeInfo("temporary");
        device.GetOrCreatePipelineLayout(afterSourceDestruction, transientInfo);
    }
    CHECK_TRUE(first.GetVkPipelineLayout() == second.GetVkPipelineLayout() && environment.state.layoutCreates == 1,
        "Reflection names must not participate in native cache identity!");
    CHECK_TRUE(first.GetSetBinding("first") == second.GetSetBinding("second"),
        "Each wrapper must retain its own names after input mutation!");
    CHECK_TRUE(afterSourceDestruction.GetSetBinding("temporary").second == 3 &&
        afterSourceDestruction.GetVkPipelineLayout() == first.GetVkPipelineLayout(),
        "A wrapper must retain its owned metadata after its entire source description is destroyed!");
    ExpectFailure([&] { first.GetSetBinding("second"); });
    ExpectFailure([&] { second.GetSetBinding("first"); });
    ExpectFailure([&] { first.GetDescriptorSetLayout(1); });
    const auto handle = first.GetVkPipelineLayout();
    const auto calls = environment.state.calls;
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(first, otherInfo); });
    CHECK_TRUE(first.GetVkPipelineLayout() == handle && first.GetSetBinding("first").second == 3 &&
        calls == environment.state.calls, "Nonempty targets must be rejected unchanged before native calls!");

    {
        PipelineLayout copied(first), assigned;
        assigned = second;
        PipelineLayout moved(std::move(copied)), moveAssigned;
        moveAssigned = std::move(assigned);
        first = {};
        CHECK_TRUE(moved.GetSetBinding("first").second == 3 && moveAssigned.GetSetBinding("second").second == 3 &&
            moved.GetVkPipelineLayout() == handle && moveAssigned.GetVkPipelineLayout() == handle &&
            first.GetVkPipelineLayout() == VK_NULL_HANDLE && first.GetVkDescriptorBindings().empty(),
            "Copied and moved targets must own metadata independently of cleared wrappers!");
    }
    CHECK_TRUE(environment.state.calls == calls && environment.state.layoutDestroys == 0,
        "Wrapper copy, move, clearing and destruction must never access native resources!");

    PipelineLayoutCreateInfo emptyInfo;
    emptyInfo.Finalize();
    PipelineLayout empty, emptyAgain;
    device.GetOrCreatePipelineLayout(empty, emptyInfo);
    device.GetOrCreatePipelineLayout(emptyAgain, emptyInfo);
    CHECK_TRUE(empty.GetVkPipelineLayout() != VK_NULL_HANDLE && empty.GetVkPipelineLayout() == emptyAgain.GetVkPipelineLayout() &&
        empty.GetVkDescriptorBindings().empty(), "Empty layouts must be valid cached objects!");
    info.Finalize();
    PipelineLayout withPush;
    device.GetOrCreatePipelineLayout(withPush, info);
    CHECK_TRUE(withPush.GetVkPipelineLayout() != handle, "Push constants must affect native cache identity!");
    for (const auto& range : std::vector<VkPushConstantRange>{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 16}, {VK_SHADER_STAGE_COMPUTE_BIT, 4, 16}, {VK_SHADER_STAGE_COMPUTE_BIT, 0, 20}})
    {
        PipelineLayoutCreateInfo changed;
        DescriptorSetLayoutCreateInfo set;
        set.SetBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
        changed.AddDescriptorSetLayout(set).AddPushConstantRange(range.stageFlags, range.offset, range.size).Finalize();
        PipelineLayout value;
        device.GetOrCreatePipelineLayout(value, changed);
        CHECK_TRUE(value.GetVkPipelineLayout() != withPush.GetVkPipelineLayout(),
            "Every push constant field must affect native identity!");
    }
    CHECK_TRUE(environment.state.liveLayouts.size() == 6, "Each distinct native description must be cached separately!");

    ShaderEntryReflection ambiguousEntry{};
    ambiguousEntry.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ambiguousEntry.descriptors.push_back({"duplicate", 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
    ambiguousEntry.descriptors.push_back({"duplicate", 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
    ShaderProgramReflection ambiguousReflection;
    ambiguousReflection.Create({ambiguousEntry});
    PipelineLayoutCreateInfo ambiguousInfo;
    ambiguousInfo.SetReflection(ambiguousReflection).Finalize();
    PipelineLayout ambiguous;
    device.GetOrCreatePipelineLayout(ambiguous, ambiguousInfo);
    ambiguousInfo.Reset();
    ExpectFailure([&] { ambiguous.GetSetBinding("duplicate"); });
    ExpectFailure([&] { ambiguous.GetSetBinding("missing"); });
    CHECK_TRUE(ambiguous.GetVkPipelineLayout() != handle, "Different set layouts must produce independent pipeline layouts!");

    const auto live = environment.state.liveLayouts.size();
    auto allocator = CreateInfoFinalizeTestProbe::ExchangeLayouts({});
    allocator->Destroy();
    allocator->Destroy();
    allocator.reset();
    CHECK_TRUE(environment.state.liveLayouts.empty() && environment.state.layoutDestroys == live,
        "Cache teardown and repeated cleanup must release each successful native creation exactly once!");
    const auto afterTeardown = environment.state.calls;
    device.vkDevice = VK_NULL_HANDLE;
    second = {}; withPush = {}; ambiguous = {};
    CHECK_TRUE(environment.state.calls == afterTeardown && environment.state.layoutDestroys == live,
        "Surviving borrowed wrappers must clear without accessing a destroyed cache or device!");
}

void TestLayoutCreationDependenciesAndChildFailure()
{
    NativeEnvironment environment;
    auto& device = MyDevice::GetInstance();
    PipelineLayoutCreateInfo info;
    DescriptorSetLayoutCreateInfo set;
    set.SetBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    info.AddDescriptorSetLayout(set).Finalize();
    PipelineLayout target;
    const auto calls = environment.state.calls;
    const auto nativeDevice = device.vkDevice;
    device.vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    device.vkDevice = nativeDevice;
    auto layouts = CreateInfoFinalizeTestProbe::ExchangeLayouts({});
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeLayouts(std::make_unique<PipelineLayoutAllocator>());
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeLayouts(std::move(layouts));
    auto descriptors = CreateInfoFinalizeTestProbe::ExchangeDescriptors({});
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeDescriptors(std::make_unique<DescriptorSetLayoutAllocator>());
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeDescriptors(std::move(descriptors));
    CHECK_TRUE(environment.state.calls == calls && target.GetVkPipelineLayout() == VK_NULL_HANDLE &&
        target.GetVkDescriptorBindings().empty(), "Missing or uninitialized dependencies must fail before any native creation!");
    environment.state.failDescriptor = true;
    ExpectFailure([&] { device.GetOrCreatePipelineLayout(target, info); });
    CHECK_TRUE(environment.state.layoutCreates == 0 && target.GetVkDescriptorBindings().empty() &&
        target.GetVkPipelineLayout() == VK_NULL_HANDLE, "Child creation failure must not publish partial metadata!");
    environment.state.failDescriptor = false;
    device.GetOrCreatePipelineLayout(target, info);
    CHECK_TRUE(target.GetVkPipelineLayout() != VK_NULL_HANDLE && environment.state.descriptorCreates == 2,
        "The same input and target must support retry after child failure!");
}

void TestLayoutCachePublicationRollback()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    NativeEnvironment environment;
    auto& device = MyDevice::GetInstance();
    PipelineLayoutCreateInfo existingInfo;
    existingInfo.Finalize();
    PipelineLayout existing;
    device.GetOrCreatePipelineLayout(existing, existingInfo);
    const auto handle = existing.GetVkPipelineLayout();
    PipelineLayoutCreateInfo info;
    DescriptorSetLayoutCreateInfo set;
    set.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    info.AddDescriptorSetLayout(set).Finalize();
    PipelineLayout target;
    bool failed = false;
    environment.state.failLayoutPublication = true;
    {
        LayoutAllocationFailureScope failureScope;
        try { device.GetOrCreatePipelineLayout(target, info); }
        catch (const std::bad_alloc&) { failed = true; }
    }
    CHECK_TRUE(failed && target.GetVkPipelineLayout() == VK_NULL_HANDLE && target.GetVkDescriptorBindings().empty() &&
        environment.state.layoutCreates == 2 && environment.state.layoutDestroys == 1 &&
        environment.state.liveLayouts.size() == 1 && environment.state.liveLayouts.contains(handle) &&
        environment.state.bindings.size() == 1 && environment.state.descriptorDestroys == 0,
        "Publication failure must destroy only the new native layout, retain cached children and leave its target empty!");
    environment.state.failLayoutPublication = false;
    device.GetOrCreatePipelineLayout(target, info);
    PipelineLayout cached;
    device.GetOrCreatePipelineLayout(cached, existingInfo);
    CHECK_TRUE(environment.state.descriptorCreates == 1 && environment.state.layoutCreates == 3 &&
        target.GetVkPipelineLayout() != handle && cached.GetVkPipelineLayout() == handle,
        "Publication failure must allow retry and preserve existing cache entries!");
#endif
}

void TestComputeCreationDependencies()
{
    CHECK_TRUE(CreateInfoFinalizeTestProbe::EmptyComputeDestructionLeavesDeviceAbsent(),
        "An empty Compute Program must not obtain or initialize the Device during destruction!");
    CHECK_TRUE(CreateInfoFinalizeTestProbe::EmptyProgramDestructionLeavesDeviceAbsent(),
        "Empty Graphics and RayTracing Programs must not obtain or initialize the Device during destruction!");
    NativeEnvironment environment;
    ShaderFiles files;
    const auto file = files.Write("dependencies.comp", "#version 450\nlayout(local_size_x=1) in; "
        "layout(set=2,binding=3) buffer Data { uint value; } data; "
        "layout(push_constant) uniform Push { uint increment; } pushData; "
        "void main(){ data.value += pushData.increment; }\n");
    ComputeShaderProgramCreateInfo info;
    info.SetShader(file).Finalize();
    auto& device = MyDevice::GetInstance();
    ComputeShaderProgram target;
    const auto calls = environment.state.calls;
    const auto nativeDevice = device.vkDevice;
    device.vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    device.vkDevice = nativeDevice;
    auto descriptors = CreateInfoFinalizeTestProbe::ExchangeDescriptors({});
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeDescriptors(std::make_unique<DescriptorSetLayoutAllocator>());
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeDescriptors(std::move(descriptors));
    auto layouts = CreateInfoFinalizeTestProbe::ExchangeLayouts({});
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeLayouts(std::make_unique<PipelineLayoutAllocator>());
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeLayouts(std::move(layouts));
    auto compute = CreateInfoFinalizeTestProbe::ExchangeCompute({});
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeCompute(std::make_unique<ComputePipelineAllocator>());
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::ExchangeCompute(std::move(compute));
    CHECK_TRUE(environment.state.calls == calls && target.GetVkPipeline() == VK_NULL_HANDLE &&
        target.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE,
        "Missing or uninitialized dependencies must be rejected before every native call and leave the target empty!");

    const auto limits = device.GetCachedPhysicalDeviceProperties().limits;
    // DeviceScope owns the mutable cached properties; lower the test device limit.
    CreateInfoFinalizeTestProbe::SetPushConstantLimit(0);
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    CreateInfoFinalizeTestProbe::SetPushConstantLimit(limits.maxPushConstantsSize);
    CHECK_TRUE(environment.state.calls == calls, "Invalid device push constant sizes must fail before shader creation!");

    device.CreateComputeShaderProgram(target, info);
    const auto pipeline = target.GetVkPipeline();
    const auto layout = target.GetLayout().GetVkPipelineLayout();
    const auto createdCalls = environment.state.calls;
    ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
    ComputeShaderProgramCreateInfo unfinalized;
    ExpectUnfinalizedFailure([&] { device.CreateComputeShaderProgram(target, unfinalized); });
    CHECK_TRUE(environment.state.calls == createdCalls && target.GetVkPipeline() == pipeline &&
        target.GetLayout().GetVkPipelineLayout() == layout && target.GetLayout().GetSetBinding("data").first == 2,
        "Rejecting recreation must preserve an existing Program and its reflection without native calls!");
}

void TestComputeCreationFailureStagesAndRetry()
{
    ShaderFiles files;
    const auto baselineFile = files.Write("baseline.comp", "#version 450\nlayout(local_size_x=1) in; void main() {}\n");
    const auto file = files.Write("retry.comp", "#version 450\nlayout(local_size_x=1) in; "
        "layout(set=2,binding=3) buffer Data { uint value; } data; void main(){ data.value = 1; }\n");
    ComputeShaderProgramCreateInfo baselineInfo, info;
    baselineInfo.SetShader(baselineFile).Finalize();
    info.SetShader(file).Finalize();
    CHECK_TRUE(std::filesystem::remove(baselineFile) && std::filesystem::remove(file),
        "Retry tests must use finalized snapshots after removing source files!");
    for (auto failure : {&NativeState::failModule, &NativeState::failDescriptor, &NativeState::failLayout, &NativeState::failCompute})
    {
        NativeEnvironment environment;
        auto& device = MyDevice::GetInstance();
        ComputeShaderProgram baseline, target;
        device.CreateComputeShaderProgram(baseline, baselineInfo);
        const auto pipeline = baseline.GetVkPipeline();
        const auto layout = baseline.GetLayout().GetVkPipelineLayout();
        environment.state.*failure = true;
        ExpectFailure([&] { device.CreateComputeShaderProgram(target, info); });
        CHECK_TRUE(target.GetVkPipeline() == VK_NULL_HANDLE && target.GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE &&
            target.GetLayout().GetVkDescriptorBindings().empty() && environment.state.liveModules.size() == 1 &&
            environment.state.livePipelines.size() == 1 && environment.state.livePipelines.contains(pipeline) &&
            environment.state.liveLayouts.contains(layout) && environment.state.pipelineDestroys == 0,
            "Each failure must roll back temporary ownership, preserve existing caches and leave the output empty!");
        const auto descriptors = environment.state.descriptorCreates;
        const auto layouts = environment.state.layoutCreates;
        environment.state.*failure = false;
        device.CreateComputeShaderProgram(target, info);
        CHECK_TRUE(target.GetVkPipeline() != VK_NULL_HANDLE && target.GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE &&
            target.GetLayout().GetSetBinding("data") == std::make_pair(uint32_t{2}, uint32_t{3}) &&
            target.GetLayout().GetVkDescriptorBindings().size() == 3 && environment.state.liveModules.size() == 2 &&
            baseline.GetVkPipeline() == pipeline && baseline.GetLayout().GetVkPipelineLayout() == layout,
            "Every creation stage must allow retry from the same finalized input and retain independent sparse reflection!");
        if (failure == &NativeState::failCompute)
        {
            CHECK_TRUE(environment.state.descriptorCreates == descriptors && environment.state.layoutCreates == layouts,
                "A failed Compute pipeline must leave published descriptor/layout entries available for retry!");
        }
    }
}

void TestComputeAutomaticLifetimeAndCacheOwnership()
{
    NativeEnvironment environment;
    ShaderFiles files;
    const auto file = files.Write("lifetime.comp", "#version 450\nlayout(local_size_x=1) in; "
        "layout(set=1,binding=4) buffer Values { uint value; } values; void main(){ values.value = 1; }\n");
    auto& device = MyDevice::GetInstance();
    const auto cache = CreateInfoFinalizeTestProbe::Handle<VkPipelineCache>(99);
    ComputeShaderProgramCreateInfo retryInfo;
    retryInfo.SetShader(file).Finalize();
    VkPipeline pipeline = VK_NULL_HANDLE;
    {
        ComputeShaderProgram program;
        {
            ComputeShaderProgramCreateInfo info;
            info.SetShader(file).CustomizePipelineCache(cache).Finalize();
            device.CreateComputeShaderProgram(program, info);
            info.Reset();
        }
        pipeline = program.GetVkPipeline();
        CHECK_TRUE(program.GetLayout().GetSetBinding("values") == std::make_pair(uint32_t{1}, uint32_t{4}) &&
            environment.state.computePipelineCache == cache && environment.state.liveModules.size() == 1,
            "Published Compute ownership, reflection and pipeline cache selection must survive destruction of its input!");
        // Reuse a live native module to exercise the existing allocator's exact-key hit.
        const auto stage = environment.state.stages.front();
        VkComputePipelineCreateInfo nativeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        nativeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        nativeInfo.stage.stage = stage.stage;
        nativeInfo.stage.module = stage.module;
        nativeInfo.stage.pName = stage.entry.c_str();
        nativeInfo.layout = program.GetLayout().GetVkPipelineLayout();
        CHECK_TRUE(device.CreateComputePipeline(nativeInfo, cache) == pipeline && environment.state.computeCreates == 1,
            "The migration must preserve existing compute cache hits!");
    }
    CHECK_TRUE(environment.state.liveModules.empty() && environment.state.moduleDestroys == 1 &&
        environment.state.livePipelines.contains(pipeline) && environment.state.pipelineDestroys == 0 &&
        environment.state.layoutDestroys == 0 && environment.state.descriptorDestroys == 0,
        "Scope exit must release the owned shader and retain borrowed cached pipelines and layouts!");

    const auto destroyed = environment.state.moduleDestroys;
    ExpectFailure([&]
    {
        ComputeShaderProgram program;
        device.CreateComputeShaderProgram(program, retryInfo);
        throw std::runtime_error("Test exception after successful creation");
    });
    CHECK_TRUE(environment.state.liveModules.empty() && environment.state.moduleDestroys == destroyed + 1 &&
        environment.state.pipelineDestroys == 0, "Exception unwinding must release only the Program's owned native resources!");
    auto program = std::make_unique<ComputeShaderProgram>();
    device.CreateComputeShaderProgram(*program, retryInfo);
    program.reset();
    CHECK_TRUE(environment.state.liveModules.empty() && environment.state.moduleDestroys == destroyed + 2 &&
        environment.state.pipelineDestroys == 0, "unique_ptr reset must release a Compute Program without an explicit Destroy!");

    const auto pipelines = environment.state.livePipelines.size();
    const auto layouts = environment.state.liveLayouts.size();
    const auto descriptors = environment.state.bindings.size();
    auto computeAllocator = CreateInfoFinalizeTestProbe::ExchangeCompute({});
    computeAllocator->Destroy(); computeAllocator->Destroy(); computeAllocator.reset();
    auto layoutAllocator = CreateInfoFinalizeTestProbe::ExchangeLayouts({});
    layoutAllocator->Destroy(); layoutAllocator->Destroy(); layoutAllocator.reset();
    auto descriptorAllocator = CreateInfoFinalizeTestProbe::ExchangeDescriptors({});
    descriptorAllocator->Destroy(); descriptorAllocator->Destroy(); descriptorAllocator.reset();
    CHECK_TRUE(environment.state.livePipelines.empty() && environment.state.liveLayouts.empty() && environment.state.bindings.empty() &&
        environment.state.pipelineDestroys == pipelines && environment.state.layoutDestroys == layouts &&
        environment.state.descriptorDestroys == descriptors,
        "Cache teardown must release every borrowed native resource once, including those retained through failed outer lifetimes!");
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
    auto compute = std::make_unique<ComputeShaderProgram>();
    auto graphics = std::make_unique<GraphicsShaderProgram>();
    auto rays = std::make_unique<RayTracingShaderProgram>();
    computeInfo.SetShader(computeFile).Finalize();
    graphicsInfo.AddVertexShader(vertexFile).AddFragmentShader(fragmentFile)
        .SetDefaultColorAttachment(2, VK_FORMAT_R8G8B8A8_UNORM).Finalize();
    rayInfo.AddRayGenerationShaderGroup("primary", rayFile).AddRayGenerationShaderGroup("secondary", rayFile)
        .CustomizeMaxRecursionDepth(2).Finalize();
    RayTracingShaderProgramCreateInfo overLimitRayInfo;
    overLimitRayInfo.AddRayGenerationShaderGroup("tooDeep", rayFile).CustomizeMaxRecursionDepth(3).Finalize();
    const auto nativeCallsBeforeMissingAllocators = environment.state.calls;
    auto graphicsAllocator = CreateInfoFinalizeTestProbe::ExchangeGraphics({});
    ExpectFailure([&] { MyDevice::GetInstance().CreateGraphicsShaderProgram(*graphics, graphicsInfo); });
    graphicsAllocator = CreateInfoFinalizeTestProbe::ExchangeGraphics(std::move(graphicsAllocator));
    auto rayAllocator = CreateInfoFinalizeTestProbe::ExchangeRayTracing({});
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo); });
    rayAllocator = CreateInfoFinalizeTestProbe::ExchangeRayTracing(std::move(rayAllocator));
    CHECK_TRUE(environment.state.calls == nativeCallsBeforeMissingAllocators,
        "Missing program allocators must be rejected before native creation!");
    CreateInfoFinalizeTestProbe::SetRayHandleProperties(0, 8);
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo); });
    CreateInfoFinalizeTestProbe::SetRayHandleProperties(8, 8);
    CHECK_TRUE(environment.state.calls == nativeCallsBeforeMissingAllocators,
        "Invalid ray group handle properties must be rejected before native creation!");
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, overLimitRayInfo); });
    CHECK_TRUE(environment.state.calls == nativeCallsBeforeMissingAllocators,
        "Ray recursion depth beyond the cached device limit must be rejected before native creation!");
    CHECK_TRUE(std::filesystem::remove(computeFile) && std::filesystem::remove(vertexFile) &&
        std::filesystem::remove(fragmentFile) && std::filesystem::remove(rayFile),
        "Program shader files must be absent throughout native creation and retry!");
    environment.state.failCompute = true;
    ExpectFailure([&] { MyDevice::GetInstance().CreateComputeShaderProgram(*compute, computeInfo); });
    CHECK_TRUE(compute->GetVkPipeline() == VK_NULL_HANDLE && compute->GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE &&
        environment.state.moduleDestroys == 1, "Failed compute creation must clear its runtime layout and shader module!");
    environment.state.failCompute = false;
    MyDevice::GetInstance().CreateComputeShaderProgram(*compute, computeInfo);
    computeInfo.Reset();
    CHECK_TRUE(compute->GetVkPipeline() != VK_NULL_HANDLE && environment.state.stages.size() == 1 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_COMPUTE_BIT && environment.state.stages[0].entry == "main",
        "Compute retry must use the finalized stage snapshot!");

    auto destroyed = environment.state.moduleDestroys;
    environment.state.failGraphics = true;
    ExpectFailure([&] { MyDevice::GetInstance().CreateGraphicsShaderProgram(*graphics, graphicsInfo); });
    CHECK_TRUE(graphics->GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE && environment.state.moduleDestroys == destroyed + 2,
        "Failed default graphics pipeline creation must release both shaders and its layout!");
    environment.state.failGraphics = false;
    MyDevice::GetInstance().CreateGraphicsShaderProgram(*graphics, graphicsInfo);
    CHECK_TRUE(graphics->GetVkPipeline({}) != VK_NULL_HANDLE && environment.state.stages.size() == 2 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_VERTEX_BIT && environment.state.stages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
        environment.state.stages[0].entry == "main" && environment.state.stages[1].entry == "main" &&
        graphics->GetDefaultRenderPass().GetVkRenderPass() != VK_NULL_HANDLE,
        "Graphics retry and cached default pipeline must retain finalized stages and generated render pass!");
    const auto nativeCallsBeforeOccupiedGraphics = environment.state.calls;
    ExpectFailure([&] { MyDevice::GetInstance().CreateGraphicsShaderProgram(*graphics, graphicsInfo); });
    CHECK_TRUE(environment.state.calls == nativeCallsBeforeOccupiedGraphics,
        "Creation into an occupied Graphics target must leave the existing program untouched!");
    graphicsInfo.Reset();
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
    const auto graphicsCreatesBeforeLazyFailure = environment.state.graphicsCreates;
    GraphicsPipelineStateInfo explicitState;
    VkVertexInputBindingDescription customBinding{0, 64, VK_VERTEX_INPUT_RATE_VERTEX};
    explicitState.AddVertexInputDescription(customBinding, {});
    explicitState.SetRenderPassSubpass(graphics->GetDefaultRenderPass());
    environment.state.failGraphics = true;
    ExpectFailure([&] { graphics->GetVkPipeline(explicitState); });
    CHECK_TRUE(environment.state.graphicsCreates == graphicsCreatesBeforeLazyFailure + 1,
        "A failed lazy Graphics PSO request must not be published in the program-local map!");
    environment.state.failGraphics = false;
    const auto lazyPipeline = graphics->GetVkPipeline(explicitState);
    CHECK_TRUE(lazyPipeline != VK_NULL_HANDLE && graphics->GetVkPipeline(explicitState) == lazyPipeline,
        "A successful lazy Graphics PSO must be cached and reused!");

    destroyed = environment.state.moduleDestroys;
    environment.state.failRay = true;
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo); });
    CHECK_TRUE(rays->GetVkPipeline() == VK_NULL_HANDLE && rays->GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE &&
        rays->GetShaderGroupCount() == 0 && environment.state.moduleDestroys == destroyed + 1,
        "Failed RT pipeline creation must clear runtime groups, layout and deduplicated shader module!");
    environment.state.failRay = false;
    environment.state.failGroups = true;
    const auto rayCreatesBeforeGroupFailure = environment.state.rayCreates;
    const auto pipelineDestroysBeforeGroupFailure = environment.state.pipelineDestroys;
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo); });
    CHECK_TRUE(rays->GetVkPipeline() == VK_NULL_HANDLE && rays->GetShaderGroupCount() == 0 &&
        rays->GetLayout().GetVkPipelineLayout() == VK_NULL_HANDLE && environment.state.moduleDestroys == destroyed + 2 &&
        environment.state.rayCreates == rayCreatesBeforeGroupFailure + 1 &&
        environment.state.pipelineDestroys == pipelineDestroysBeforeGroupFailure &&
        environment.state.livePipelines.size() >= 1,
        "Failure after RT pipeline creation must also leave the runtime object reusable!");
    environment.state.failGroups = false;
    MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo);
    CHECK_TRUE(environment.state.rayCreates == rayCreatesBeforeGroupFailure + 2 &&
        environment.state.pipelineDestroys == pipelineDestroysBeforeGroupFailure,
        "Handle-fetch failure must leave its pipeline cache-owned while the retry can create a fresh keyed pipeline!");
    CHECK_TRUE(rays->GetVkPipeline() != VK_NULL_HANDLE && rays->GetShaderGroupCount() == 2 &&
        environment.state.recursionDepth == 2 && environment.state.groups.size() == 2 && environment.state.stages.size() == 1 &&
        environment.state.stages[0].stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR && environment.state.stages[0].entry == "main" &&
        environment.state.groups[0].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR && environment.state.groups[0].generalShader == 0 &&
        environment.state.groups[1].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR && environment.state.groups[1].generalShader == 0,
        "RT native creation must preserve named group order while sharing one shader stage!");
    const auto nativeCallsBeforeOccupiedRay = environment.state.calls;
    ExpectFailure([&] { MyDevice::GetInstance().CreateRayTracingShaderProgram(*rays, rayInfo); });
    CHECK_TRUE(environment.state.calls == nativeCallsBeforeOccupiedRay,
        "Creation into an occupied RayTracing target must leave the existing program untouched!");
    rayInfo.Reset();
    const auto& primary = rays->GetShaderGroupSet().GetGroupData("primary");
    const auto& secondary = rays->GetShaderGroupSet().GetGroupData("secondary");
    CHECK_TRUE(primary.type == ShaderTableType::RayGeneration && secondary.type == ShaderTableType::RayGeneration &&
        primary.handle == std::vector<uint8_t>(8, 0x5a) && secondary.handle == std::vector<uint8_t>(8, 0x5b),
        "Named RT group metadata and handles must remain owned after resetting the description!");
    const auto copiedGroups = rays->GetShaderGroupSet();
    const auto pipelineDestroysBeforeProgramRelease = environment.state.pipelineDestroys;
    compute.reset(); graphics.reset(); rays.reset();
    CHECK_TRUE(environment.state.pipelineDestroys == pipelineDestroysBeforeProgramRelease &&
        copiedGroups.GetGroupData("primary").handle == std::vector<uint8_t>(8, 0x5a),
        "Program release must preserve Device-cached pipelines and copied RT host metadata!");
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
    TestLayoutCacheAndIndependentMetadata();
    TestLayoutCreationDependenciesAndChildFailure();
    TestLayoutCachePublicationRollback();
    TestComputeCreationDependencies();
    TestComputeCreationFailureStagesAndRetry();
    TestComputeAutomaticLifetimeAndCacheOwnership();
    TestProgramNativeCreationUsesFinalizedSnapshotsAndCleansFailures();
    TestRenderPassNativeFailureDoesNotConsumeFinalizedInfo();
}
