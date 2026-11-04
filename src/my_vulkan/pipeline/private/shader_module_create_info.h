#pragma once
#include "shader_program_reflection.h"

class ShaderModuleCreateInfo final
{
    friend class ShaderModule;
private:
    std::string m_file;
    std::shared_ptr<const ShaderReflector> m_finalized;
public:
    auto SetShader(std::string_view inFile) -> ShaderModuleCreateInfo&;
    auto Reset() -> ShaderModuleCreateInfo&;
    auto Finalize() -> ShaderModuleCreateInfo&;
    auto IsFinalized() const noexcept -> bool { return bool(m_finalized); }
    auto GetCode() const -> std::span<const uint32_t>;
    auto GetEntryReflection(VkShaderStageFlagBits inStage) const -> const ShaderEntryReflection&;
};
namespace ShaderDetail
{
    struct Request { std::string file; VkShaderStageFlagBits stage; };
    auto MakeRequest(std::string_view inFile, VkShaderStageFlagBits inStage) -> Request;
    auto MakeOptionalRequest(std::optional<std::string_view> inFile, VkShaderStageFlagBits inStage) -> std::optional<Request>;
    struct FinalizedShaders
    {
        struct Stage { uint32_t moduleIndex; VkShaderStageFlagBits stage; };
        std::vector<ShaderModuleCreateInfo> modules;
        std::vector<Stage> stages;
        std::vector<uint32_t> indices;
        ShaderProgramReflection reflection;
    };
    auto FinalizeShaders(const std::vector<Request>& inRequests) -> FinalizedShaders;
}
