#include "shader_module_create_info.h"

auto ShaderModuleCreateInfo::SetShader(std::string_view inFile) -> ShaderModuleCreateInfo&
{
    CHECK_TRUE(!inFile.empty(), "Shader SPIR-V file is unset!");
    m_file = inFile; m_finalized.reset(); return *this;
}
auto ShaderModuleCreateInfo::Reset() -> ShaderModuleCreateInfo&
{
    m_file.clear(); m_finalized.reset(); return *this;
}
auto ShaderModuleCreateInfo::Finalize() -> ShaderModuleCreateInfo&
{
    if (m_finalized) return *this;
    auto data = std::make_shared<ShaderReflector>();
    data->Create(m_file);
    m_finalized = std::move(data);
    return *this;
}
auto ShaderModuleCreateInfo::GetCode() const -> std::span<const uint32_t>
{
    CHECK_TRUE(IsFinalized(), "ShaderModuleCreateInfo must be finalized!");
    return m_finalized->GetCode();
}
auto ShaderModuleCreateInfo::GetEntryReflection(VkShaderStageFlagBits stage) const -> const ShaderEntryReflection&
{
    CHECK_TRUE(IsFinalized(), "ShaderModuleCreateInfo must be finalized!");
    return m_finalized->ReflectEntry(stage);
}
auto ShaderDetail::MakeRequest(std::string_view file, VkShaderStageFlagBits stage) -> Request
{
    CHECK_TRUE(!file.empty(), "Shader SPIR-V file is unset!");
    return {std::string(file), stage};
}
auto ShaderDetail::MakeOptionalRequest(std::optional<std::string_view> file, VkShaderStageFlagBits stage) -> std::optional<Request>
{
    if (!file) return std::nullopt;
    return MakeRequest(*file, stage);
}
auto ShaderDetail::FinalizeShaders(const std::vector<Request>& requests) -> FinalizedShaders
{
    CHECK_TRUE(!requests.empty(), "Shader program needs shaders!");
    FinalizedShaders data;
    std::unordered_map<std::string, uint32_t> files;
    std::vector<ShaderEntryReflection> entries;
    for (const auto& request : requests)
    {
        auto it = files.find(request.file);
        if (it == files.end())
        {
            ShaderModuleCreateInfo module;
            module.SetShader(request.file).Finalize();
            const auto index = static_cast<uint32_t>(data.modules.size());
            data.modules.push_back(std::move(module));
            it = files.emplace(request.file, index).first;
        }
        const auto& entry = data.modules[it->second].GetEntryReflection(request.stage);
        uint32_t index = 0;
        for (; index < data.stages.size(); ++index)
            if (data.stages[index].moduleIndex == it->second && data.stages[index].stage == request.stage) break;
        if (index == data.stages.size())
        {
            data.stages.push_back({it->second, request.stage});
            entries.push_back(entry);
        }
        data.indices.push_back(index);
    }
    data.reflection.Create(entries);
    return data;
}
