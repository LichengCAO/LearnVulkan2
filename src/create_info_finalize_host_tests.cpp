#include "my_vulkan/pipeline/shader_program_create_info.h"
#include "utility/shader_compiler.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace
{
template<class F>
void ExpectFinalizeFailure(F&& action)
{
    bool failed = false;
    try { action(); }
    catch (const std::runtime_error&) { failed = true; }
    CHECK_TRUE(failed, "Invalid or stale descriptions must be rejected!");
}
class ShaderFiles final
{
    std::filesystem::path m_directory;
public:
    ShaderFiles()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory = std::filesystem::temp_directory_path() / ("learn_vulkan_finalize_" + std::to_string(stamp));
        CHECK_TRUE(std::filesystem::create_directory(m_directory), "Cannot create isolated shader fixtures!");
    }
    ~ShaderFiles()
    {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }
    auto Write(std::string_view name, std::string_view source) const -> std::string
    {
        const auto path = m_directory / name;
        { std::ofstream file(path); file << source; CHECK_TRUE(bool(file), "Cannot write shader fixture!"); }
        const auto code = ShaderCompiler(m_directory).CompileFile(path);
        const auto spirv = path.string() + ".spv";
        std::ofstream file(spirv, std::ios::binary);
        file.write(reinterpret_cast<const char*>(code.data()), static_cast<std::streamsize>(code.size()));
        CHECK_TRUE(bool(file), "Cannot write SPIR-V fixture!");
        return spirv;
    }
};

void TestShaderModuleSnapshotAndReload()
{
    ShaderFiles files;
    const auto path = files.Write("host.comp", "#version 450\nlayout(local_size_x=1) in; void main() {}\n");
    ShaderModuleCreateInfo info;
    CHECK_TRUE(!info.IsFinalized(), "A new shader description must not be finalized!");
    info.SetShader(path).Finalize();
    CHECK_TRUE(info.GetCode().front() == 0x07230203 && info.GetEntryReflection(VK_SHADER_STAGE_COMPUTE_BIT).name == "main",
        "Finalization must load SPIR-V and own the reflected entry name!");
    const auto* code = info.GetCode().data();
    auto copy = info;
    ShaderModuleCreateInfo assigned;
    assigned = copy;
    auto moved = std::move(assigned);
    CHECK_TRUE(moved.IsFinalized() && !assigned.IsFinalized(), "Moving a shader description must transfer its snapshot!");
    std::filesystem::remove(path);
    info.Finalize();
    CHECK_TRUE(info.GetCode().data() == code, "Unchanged Finalize must reuse its loaded snapshot!");
    info.SetShader(path);
    CHECK_TRUE(!info.IsFinalized() && copy.IsFinalized(), "Changing one description must not invalidate copies!");
    ExpectFinalizeFailure([&] { info.Finalize(); });
    CHECK_TRUE(!info.IsFinalized() && copy.GetCode().front() == 0x07230203, "Failed reload must leave other snapshots valid!");
    CHECK_TRUE(moved.GetEntryReflection(VK_SHADER_STAGE_COMPUTE_BIT).name == "main", "Assigned and moved snapshots must retain their owned reflection!");
    info.Reset();
    ExpectFinalizeFailure([&] { info.Finalize(); });
}

void TestProgramFinalizationAndInvalidation()
{
    ShaderFiles files;
    const auto compute = files.Write("program.comp", "#version 450\nlayout(local_size_x=1) in; void main() {}\n");
    const auto vertex = files.Write("program.vert", "#version 450\nlayout(location=0) in vec3 position; void main(){ gl_Position=vec4(position,1); }\n");
    const auto fragment = files.Write("program.frag", "#version 450\nlayout(location=0) out vec4 color; void main(){ color=vec4(1); }\n");
    ComputeShaderProgramCreateInfo c;
    c.SetShader(compute).Finalize();
    auto ccopy = c;
    c.CustomizePipelineCache(VK_NULL_HANDLE);
    CHECK_TRUE(!c.IsFinalized() && ccopy.IsFinalized(), "Even opaque creation-option setters invalidate the finalized input!");
    c.Finalize(); c.Reset();
    ExpectFinalizeFailure([&] { c.Finalize(); });

    GraphicsShaderProgramCreateInfo g;
    g.AddVertexShader(vertex).AddFragmentShader(fragment).SetDefaultColorAttachment(0, VK_FORMAT_R8G8B8A8_UNORM).Finalize();
    auto gcopy = g;
    g.SetDefaultColorAttachment(0, VK_FORMAT_R32_UINT);
    ExpectFinalizeFailure([&] { g.Finalize(); });
    CHECK_TRUE(!g.IsFinalized() && gcopy.IsFinalized(), "Attachment numeric mismatch must fail before device creation!");
    g.SetDefaultColorAttachment(0, VK_FORMAT_R8G8B8A8_UNORM).Finalize();
    g.SetDefaultDepthAttachment(VK_FORMAT_D32_SFLOAT);
    CHECK_TRUE(!g.IsFinalized(), "Depth changes must invalidate generated default pass metadata!");
    g.Finalize();
    ExpectFinalizeFailure([&] { g.AddVertexShader(vertex); });
    CHECK_TRUE(g.IsFinalized(), "A rejected duplicate setter must preserve the existing description!");
    std::filesystem::remove(vertex); std::filesystem::remove(fragment);
    g.Finalize();
    CHECK_TRUE(g.IsFinalized(), "Repeated program finalization must not reload deleted files!");
    g.Reset();
    ExpectFinalizeFailure([&] { g.Finalize(); });

    GraphicsShaderProgramCreateInfo stages;
    stages.AddTaskShader("unused.spv");
    ExpectFinalizeFailure([&] { stages.Finalize(); });
    CHECK_TRUE(!stages.IsFinalized(), "A task stage without mesh must be rejected!");
}

void TestRayTracingFinalization()
{
    ShaderFiles files;
    const auto raygen = files.Write("program.rgen", "#version 460\n#extension GL_EXT_ray_tracing : require\nvoid main() {}\n");
    RayTracingShaderProgramCreateInfo info;
    ExpectFinalizeFailure([&] { info.Finalize(); });
    info.AddRayGenerationShaderGroup("first", raygen).AddRayGenerationShaderGroup("second", raygen).Finalize();
    auto copy = info;
    info.CustomizeMaxRecursionDepth(0);
    ExpectFinalizeFailure([&] { info.Finalize(); });
    CHECK_TRUE(!info.IsFinalized() && copy.IsFinalized(), "Invalid RT depth must not publish an incomplete snapshot!");
    info.CustomizeMaxRecursionDepth(1).Finalize();
    info.CustomizePipelineCache(VK_NULL_HANDLE);
    CHECK_TRUE(!info.IsFinalized(), "RT device-option modification must invalidate Finalize!");
    info.Finalize();
    ExpectFinalizeFailure([&] { info.AddMissShaderGroup("first", raygen); });
    CHECK_TRUE(info.IsFinalized(), "Rejected duplicate group names must not invalidate valid data!");
    info.Reset();
    ExpectFinalizeFailure([&] { info.Finalize(); });
}

void TestLayoutFinalization()
{
    PipelineLayoutCreateInfo info;
    info.Finalize();
    CHECK_TRUE(info.IsFinalized(), "An empty pipeline layout is a valid Host description!");
    info.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, 16);
    CHECK_TRUE(!info.IsFinalized(), "Adding push constants must invalidate the layout!");
    info.Finalize();
    auto copy = info;
    info.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 1, 4);
    ExpectFinalizeFailure([&] { info.Finalize(); });
    CHECK_TRUE(!info.IsFinalized() && copy.IsFinalized(), "Misaligned push constants must not reach device creation!");
    DescriptorSetLayoutCreateInfo set;
    set.SetBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    info.Reset().AddDescriptorSetLayout(set).Finalize();
    set.SetBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    CHECK_TRUE(info.IsFinalized(), "Child descriptions are copied by value!");
    info.AddDescriptorSetLayout(set);
    CHECK_TRUE(!info.IsFinalized(), "Adding a set must invalidate the parent layout!");
    info.Finalize();
    ShaderEntryReflection entry{};
    entry.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    entry.descriptors.push_back({"input", 2, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1});
    entry.pushConstants.push_back({"pc", 0, 16});
    ShaderProgramReflection reflection;
    reflection.Create({entry});
    info.SetReflection(reflection);
    CHECK_TRUE(!info.IsFinalized(), "Replacing reflection must invalidate existing bindings!");
    info.Finalize();
    PipelineLayoutCreateInfo assigned;
    assigned = info;
    auto moved = std::move(assigned);
    CHECK_TRUE(moved.IsFinalized() && !assigned.IsFinalized(), "Moving a layout must transfer the finalized snapshot!");
    info.Reset();
    CHECK_TRUE(moved.IsFinalized(), "Resetting an assigned source must not invalidate the moved copy!");
}
}

void RunHostCreateInfoFinalizeTests()
{
    TestShaderModuleSnapshotAndReload();
    TestProgramFinalizationAndInvalidation();
    TestRayTracingFinalization();
    TestLayoutFinalization();
}

#ifdef RHI_HOST_TEST_MAIN
void RunRenderPassFinalizeTests();
auto main() -> int
{
    try { RunHostCreateInfoFinalizeTests(); RunRenderPassFinalizeTests(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
#endif
