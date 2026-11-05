#include "shader.h"
#include "device.h"

auto ShaderModule::GetShaderStageInfo(VkShaderStageFlagBits stage) const -> VkPipelineShaderStageCreateInfo
{
    CHECK_TRUE(m_vkShaderModule != VK_NULL_HANDLE, "VkShaderModule is not created!");
    VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage = stage;
    info.module = m_vkShaderModule;
    info.pName = m_reflector->ReflectEntry(stage).name.c_str();
    return info;
}
auto ShaderModule::GetEntryReflection(VkShaderStageFlagBits stage) const -> const ShaderEntryReflection&
{
    CHECK_TRUE(m_reflector != nullptr, "Shader module is not created!");
    return m_reflector->ReflectEntry(stage);
}
void ShaderDetail::CreateStages(const FinalizedShaders& shaders,
    std::vector<std::unique_ptr<ShaderModule>>& modules,
    std::vector<VkPipelineShaderStageCreateInfo>& stages)
{
    for (const auto& description : shaders.modules)
    {
        modules.push_back(std::make_unique<ShaderModule>());
        MyDevice::GetInstance()._GetOrCreateShaderModule(*modules.back(), description);
    }
    for (const auto& stage : shaders.stages)
        stages.push_back(modules.at(stage.moduleIndex)->GetShaderStageInfo(stage.stage));
}
