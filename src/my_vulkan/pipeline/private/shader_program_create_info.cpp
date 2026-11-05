#include "../shader_program_create_info.h"
#include <limits>

namespace
{
auto VertexFormatSize(VkFormat format) -> uint32_t
{
    if (format >= VK_FORMAT_R16_UINT && format <= VK_FORMAT_R16_SFLOAT) return 2;
    if (format >= VK_FORMAT_R16G16_UINT && format <= VK_FORMAT_R16G16_SFLOAT) return 4;
    if (format >= VK_FORMAT_R16G16B16_UINT && format <= VK_FORMAT_R16G16B16_SFLOAT) return 6;
    if (format >= VK_FORMAT_R16G16B16A16_UINT && format <= VK_FORMAT_R16G16B16A16_SFLOAT) return 8;
    if (format >= VK_FORMAT_R32_UINT && format <= VK_FORMAT_R32G32B32A32_SFLOAT)
        return 4 * (1 + (static_cast<uint32_t>(format) - VK_FORMAT_R32_UINT) / 3);
    return 0;
}
auto IsDepthFormat(VkFormat format) -> bool
{
    return format >= VK_FORMAT_D16_UNORM && format <= VK_FORMAT_D32_SFLOAT_S8_UINT && format != VK_FORMAT_S8_UINT;
}
auto NumericCategory(VkFormat format) -> uint32_t
{
    switch (format)
    {
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_B8G8R8_UINT: case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32: case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16_UINT: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32A32_UINT: return 1;
    case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_B8G8R8_SINT: case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32: case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32: case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16_SINT: case VK_FORMAT_R16G16B16A16_SINT: case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32_SINT: case VK_FORMAT_R32G32B32A32_SINT: return 2;
    default: return 0;
    }
}
}
auto GraphicsShaderProgramCreateInfo::Reset() -> GraphicsShaderProgramCreateInfo&
{
    m_shaders.clear(); m_defaultColors.clear(); m_defaultDepth.reset(); m_finalized.reset(); return *this;
}
auto GraphicsShaderProgramCreateInfo::_AddShader(std::string_view file, VkShaderStageFlagBits stage) -> GraphicsShaderProgramCreateInfo&
{
    CHECK_TRUE(std::none_of(m_shaders.begin(), m_shaders.end(), [stage](const auto& shader) -> bool
        { return shader.stage == stage; }), "Duplicate graphics shader stage!");
    m_shaders.push_back(ShaderDetail::MakeRequest(file, stage));
    m_finalized.reset(); return *this;
}
auto GraphicsShaderProgramCreateInfo::AddVertexShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{ return _AddShader(file, VK_SHADER_STAGE_VERTEX_BIT); }
auto GraphicsShaderProgramCreateInfo::AddFragmentShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{ return _AddShader(file, VK_SHADER_STAGE_FRAGMENT_BIT); }
auto GraphicsShaderProgramCreateInfo::AddGeometryShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{ return _AddShader(file, VK_SHADER_STAGE_GEOMETRY_BIT); }
auto GraphicsShaderProgramCreateInfo::AddTaskShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{ return _AddShader(file, VK_SHADER_STAGE_TASK_BIT_EXT); }
auto GraphicsShaderProgramCreateInfo::AddMeshShader(std::string_view file) -> GraphicsShaderProgramCreateInfo&
{ return _AddShader(file, VK_SHADER_STAGE_MESH_BIT_EXT); }
auto GraphicsShaderProgramCreateInfo::SetDefaultColorAttachment(uint32_t location, VkFormat format,
    VkImageLayout finalLayout) -> GraphicsShaderProgramCreateInfo&
{
    CHECK_TRUE(format != VK_FORMAT_UNDEFINED && format != VK_FORMAT_MAX_ENUM && !IsDepthFormat(format) &&
        format != VK_FORMAT_S8_UINT, "Invalid color attachment format!");
    CHECK_TRUE(finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
        finalLayout == VK_IMAGE_LAYOUT_GENERAL || finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
        finalLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, "Unsupported default color final layout!");
    m_defaultColors[location] = {format, finalLayout}; m_finalized.reset(); return *this;
}
auto GraphicsShaderProgramCreateInfo::SetDefaultDepthAttachment(VkFormat format) -> GraphicsShaderProgramCreateInfo&
{
    CHECK_TRUE(IsDepthFormat(format), "Invalid depth attachment format!");
    m_defaultDepth = format; m_finalized.reset(); return *this;
}
auto GraphicsShaderProgramCreateInfo::Finalize() -> GraphicsShaderProgramCreateInfo&
{
    if (m_finalized) return *this;
    const auto hasStage = [&](VkShaderStageFlagBits stage) -> bool
    { return std::any_of(m_shaders.begin(), m_shaders.end(), [stage](const auto& shader) -> bool { return shader.stage == stage; }); };
    const bool vertex = hasStage(VK_SHADER_STAGE_VERTEX_BIT), geometry = hasStage(VK_SHADER_STAGE_GEOMETRY_BIT);
    const bool mesh = hasStage(VK_SHADER_STAGE_MESH_BIT_EXT), task = hasStage(VK_SHADER_STAGE_TASK_BIT_EXT);
    CHECK_TRUE(!(vertex || geometry) || !(mesh || task), "Traditional and mesh graphics stages cannot be mixed!");
    CHECK_TRUE(!task || mesh, "Task shader requires a mesh shader!");
    CHECK_TRUE(vertex || mesh, "Graphics program requires a vertex or mesh shader!");
    auto data = std::make_shared<FinalizedData>();
    data->shaders = ShaderDetail::FinalizeShaders(m_shaders);
    data->layout.SetReflection(data->shaders.reflection).Finalize();
    const auto byLocation = [](const auto& left, const auto& right) -> bool { return left.location < right.location; };
    auto inputs = data->shaders.reflection.GetInput(VK_SHADER_STAGE_VERTEX_BIT);
    std::sort(inputs.begin(), inputs.end(), byLocation);
    uint32_t stride = 0;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        const auto& input = inputs[i];
        const auto size = VertexFormatSize(input.format);
        if (!size || (i && inputs[i - 1].location == input.location))
        { data->automaticVertexInputSupported = false; break; }
        CHECK_TRUE(size <= std::numeric_limits<uint32_t>::max() - stride, "Vertex stride overflow!");
        data->vertexAttributes.push_back({input.location, 0, input.format, stride});
        stride += size;
    }
    if (stride && data->automaticVertexInputSupported)
        data->vertexBindings.push_back({0, stride, VK_VERTEX_INPUT_RATE_VERTEX});
    if (!m_defaultColors.empty() || m_defaultDepth)
    {
        auto outputs = data->shaders.reflection.GetOutput(VK_SHADER_STAGE_FRAGMENT_BIT);
        std::sort(outputs.begin(), outputs.end(), byLocation);
        CHECK_TRUE(std::adjacent_find(outputs.begin(), outputs.end(), [](const auto& a, const auto& b) -> bool
            { return a.location == b.location; }) == outputs.end(), "Default render pass requires unique fragment output locations!");
        CHECK_TRUE(outputs.size() == m_defaultColors.size(), "Default color attachments must cover all fragment outputs!");
        auto pass = std::make_shared<RenderPassDesc>();
        SubpassDescription subpass;
        for (const auto& output : outputs)
        {
            const auto it = m_defaultColors.find(output.location);
            CHECK_TRUE(it != m_defaultColors.end(), "Missing default color attachment location!");
            CHECK_TRUE(output.format != VK_FORMAT_UNDEFINED, "Default render pass requires simple fragment outputs!");
            CHECK_TRUE(NumericCategory(output.format) == NumericCategory(it->second.format), "Fragment output and attachment numeric types differ!");
            const auto name = output.name.empty() ? "color" + std::to_string(output.location) : output.name;
            AttachmentDescription attachment;
            attachment.CustomizeFormat(it->second.format, VkClearValue{});
            attachment.CustomizeFinalLayout(it->second.finalLayout);
            pass->AddAttachment(name, attachment);
            subpass.AddColorAttachment(output.location, name);
        }
        if (m_defaultDepth)
        {
            AttachmentDescription attachment;
            VkClearValue clear{}; clear.depthStencil = {1.0f, 0};
            attachment.CustomizeFormat(*m_defaultDepth, clear);
            attachment.CustomizeFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            pass->AddAttachment("__depth", attachment);
            subpass.AddDepthStencilAttachment("__depth");
        }
        VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkAccessFlags access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        if (m_defaultDepth)
        {
            stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        subpass.CustomizeAvailableState(stages, access);
        subpass.AddExternalDependency(stages, access);
        pass->AddSubpass("default", subpass);
        pass->Finalize();
        data->defaultRenderPass = std::move(pass);
    }
    m_finalized = std::move(data);
    return *this;
}
auto ComputeShaderProgramCreateInfo::Reset() -> ComputeShaderProgramCreateInfo&
{
    m_spirvFile.clear(); m_vkPipelineCache = VK_NULL_HANDLE; m_finalized.reset(); return *this;
}
auto ComputeShaderProgramCreateInfo::SetShader(std::string_view file) -> ComputeShaderProgramCreateInfo&
{
    CHECK_TRUE(!file.empty(), "Shader SPIR-V file is unset!");
    m_spirvFile = file; m_finalized.reset(); return *this;
}
auto ComputeShaderProgramCreateInfo::CustomizePipelineCache(VkPipelineCache cache) -> ComputeShaderProgramCreateInfo&
{
    m_vkPipelineCache = cache; m_finalized.reset(); return *this;
}
auto ComputeShaderProgramCreateInfo::Finalize() -> ComputeShaderProgramCreateInfo&
{
    if (m_finalized) return *this;
    auto data = std::make_shared<FinalizedData>();
    data->shader.SetShader(m_spirvFile).Finalize();
    ShaderProgramReflection reflection;
    reflection.Create({data->shader.GetEntryReflection(VK_SHADER_STAGE_COMPUTE_BIT)});
    data->layout.SetReflection(reflection).Finalize();
    m_finalized = std::move(data); return *this;
}
auto RayTracingShaderProgramCreateInfo::Reset() -> RayTracingShaderProgramCreateInfo&
{
    m_shaders.clear(); m_shaderGroups.clear(); m_maxRayRecursionDepth = 1;
    m_vkPipelineCache = VK_NULL_HANDLE; m_finalized.reset(); return *this;
}
auto RayTracingShaderProgramCreateInfo::CustomizeMaxRecursionDepth(uint32_t depth) -> RayTracingShaderProgramCreateInfo&
{ m_maxRayRecursionDepth = depth; m_finalized.reset(); return *this; }
auto RayTracingShaderProgramCreateInfo::CustomizePipelineCache(VkPipelineCache cache) -> RayTracingShaderProgramCreateInfo&
{ m_vkPipelineCache = cache; m_finalized.reset(); return *this; }
void RayTracingShaderProgramCreateInfo::_ValidateGroupName(std::string_view name) const
{
    CHECK_TRUE(!name.empty(), "Ray tracing shader group name is empty!");
    CHECK_TRUE(std::none_of(m_shaderGroups.begin(), m_shaderGroups.end(), [&](const auto& group) -> bool
        { return group.name == name; }), "Duplicate ray tracing shader group name: " + std::string(name));
}
auto RayTracingShaderProgramCreateInfo::_RegisterShader(const ShaderDetail::Request& shader) -> uint32_t
{
    const auto index = static_cast<uint32_t>(m_shaders.size());
    m_shaders.push_back(shader); return index;
}
auto RayTracingShaderProgramCreateInfo::_AddGeneralShaderGroup(std::string_view name, const ShaderDetail::Request& shader,
    ShaderTableType type) -> RayTracingShaderProgramCreateInfo&
{
    _ValidateGroupName(name);
    const auto count = m_shaders.size();
    try
    {
        VkRayTracingShaderGroupCreateInfoKHR record{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        record.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        record.generalShader = _RegisterShader(shader);
        record.closestHitShader = record.anyHitShader = record.intersectionShader = VK_SHADER_UNUSED_KHR;
        m_shaderGroups.push_back({std::string(name), type, record});
    }
    catch (...) { m_shaders.resize(count); throw; }
    m_finalized.reset(); return *this;
}
auto RayTracingShaderProgramCreateInfo::_AddHitShaderGroup(std::string_view name,
    const std::optional<ShaderDetail::Request>& closest, const ShaderDetail::Request* intersection,
    const std::optional<ShaderDetail::Request>& any) -> RayTracingShaderProgramCreateInfo&
{
    _ValidateGroupName(name);
    const auto count = m_shaders.size();
    try
    {
        VkRayTracingShaderGroupCreateInfoKHR record{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
        record.type = intersection ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        record.generalShader = VK_SHADER_UNUSED_KHR;
        record.closestHitShader = closest ? _RegisterShader(*closest) : VK_SHADER_UNUSED_KHR;
        record.intersectionShader = intersection ? _RegisterShader(*intersection) : VK_SHADER_UNUSED_KHR;
        record.anyHitShader = any ? _RegisterShader(*any) : VK_SHADER_UNUSED_KHR;
        m_shaderGroups.push_back({std::string(name), ShaderTableType::Hit, record});
    }
    catch (...) { m_shaders.resize(count); throw; }
    m_finalized.reset(); return *this;
}
auto RayTracingShaderProgramCreateInfo::AddRayGenerationShaderGroup(std::string_view name, std::string_view file) -> RayTracingShaderProgramCreateInfo&
{ return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_RAYGEN_BIT_KHR), ShaderTableType::RayGeneration); }
auto RayTracingShaderProgramCreateInfo::AddMissShaderGroup(std::string_view name, std::string_view file) -> RayTracingShaderProgramCreateInfo&
{ return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_MISS_BIT_KHR), ShaderTableType::Miss); }
auto RayTracingShaderProgramCreateInfo::AddCallableShaderGroup(std::string_view name, std::string_view file) -> RayTracingShaderProgramCreateInfo&
{ return _AddGeneralShaderGroup(name, ShaderDetail::MakeRequest(file, VK_SHADER_STAGE_CALLABLE_BIT_KHR), ShaderTableType::Callable); }
auto RayTracingShaderProgramCreateInfo::AddTriangleHitShaderGroup(std::string_view name, std::optional<std::string_view> closestFile,
    std::optional<std::string_view> anyFile) -> RayTracingShaderProgramCreateInfo&
{
    return _AddHitShaderGroup(name, ShaderDetail::MakeOptionalRequest(closestFile, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR), nullptr,
        ShaderDetail::MakeOptionalRequest(anyFile, VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
}
auto RayTracingShaderProgramCreateInfo::AddProceduralHitShaderGroup(std::string_view name, std::optional<std::string_view> closestFile,
    std::string_view intersectionFile, std::optional<std::string_view> anyFile) -> RayTracingShaderProgramCreateInfo&
{
    const auto intersection = ShaderDetail::MakeRequest(intersectionFile, VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
    return _AddHitShaderGroup(name, ShaderDetail::MakeOptionalRequest(closestFile, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR), &intersection,
        ShaderDetail::MakeOptionalRequest(anyFile, VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
}
auto RayTracingShaderProgramCreateInfo::Finalize() -> RayTracingShaderProgramCreateInfo&
{
    if (m_finalized) return *this;
    CHECK_TRUE(m_maxRayRecursionDepth > 0, "Ray tracing recursion depth must be positive!");
    CHECK_TRUE(std::any_of(m_shaderGroups.begin(), m_shaderGroups.end(), [](const auto& group) -> bool
        { return group.type == ShaderTableType::RayGeneration; }), "Ray tracing pipeline needs a ray generation group!");
    auto data = std::make_shared<FinalizedData>();
    data->shaders = ShaderDetail::FinalizeShaders(m_shaders);
    data->layout.SetReflection(data->shaders.reflection).Finalize();
    data->groups = m_shaderGroups;
    for (auto& group : data->groups)
        for (auto* index : {&group.record.generalShader, &group.record.closestHitShader, &group.record.intersectionShader, &group.record.anyHitShader})
            if (*index != VK_SHADER_UNUSED_KHR) *index = data->shaders.indices.at(*index);
    data->recursionDepth = m_maxRayRecursionDepth;
    m_finalized = std::move(data); return *this;
}
