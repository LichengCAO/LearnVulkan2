#include "shader.h"
#include "device.h"

ShaderModule::~ShaderModule()
{
    assert(m_vkShaderModule == VK_NULL_HANDLE);
}
void ShaderModule::Create(const ShaderModuleCreateInfo& info)
{
    CHECK_TRUE(info.IsFinalized(), "ShaderModuleCreateInfo must be finalized before Create!");
    CHECK_TRUE(m_vkShaderModule == VK_NULL_HANDLE, "VkShaderModule is already created!");
    const auto code = info.GetCode();
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size_bytes();
    createInfo.pCode = code.data();
    VkShaderModule handle = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &handle),
        "Failed to create shader module!");
    m_reflector = info.m_finalized;
    m_vkShaderModule = handle;
}
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
void ShaderModule::Destroy()
{
    if (m_vkShaderModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(MyDevice::GetInstance().vkDevice, m_vkShaderModule, nullptr);
    m_vkShaderModule = VK_NULL_HANDLE;
    m_reflector.reset();
}
void ShaderDetail::CreateStages(const FinalizedShaders& shaders,
    std::vector<std::unique_ptr<ShaderModule>>& modules,
    std::vector<VkPipelineShaderStageCreateInfo>& stages)
{
    for (const auto& description : shaders.modules)
    {
        modules.push_back(std::make_unique<ShaderModule>());
        modules.back()->Create(description);
    }
    for (const auto& stage : shaders.stages)
        stages.push_back(modules.at(stage.moduleIndex)->GetShaderStageInfo(stage.stage));
}
