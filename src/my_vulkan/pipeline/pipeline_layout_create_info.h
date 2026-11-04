#pragma once
#include "my_vulkan/host_common.h"
#include "private/shader_program_reflection.h"

class DescriptorSetLayoutCreateInfo final
{
    friend class MyDevice;
    friend class PipelineLayout;
    friend class PipelineLayoutCreateInfo;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_descriptorBindings;
    std::shared_ptr<const std::vector<VkDescriptorSetLayoutBinding>> m_finalizedBindings;
public:
    auto SetBinding(uint32_t inBinding, VkDescriptorType inType, VkShaderStageFlags inStages,
        uint32_t inDescriptorCount = 1) -> DescriptorSetLayoutCreateInfo&;
};

class PipelineLayoutCreateInfo final
{
    friend class PipelineLayout;
private:
    struct FinalizedData
    {
        std::vector<DescriptorSetLayoutCreateInfo> layouts;
        std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> bindings;
        std::vector<VkPushConstantRange> pushConstants;
        std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> names;
        std::set<std::string> ambiguousNames;
    };
    std::vector<DescriptorSetLayoutCreateInfo> m_layouts;
    std::vector<VkPushConstantRange> m_pushConstants;
    std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_names;
    std::set<std::string> m_ambiguousNames;
    std::shared_ptr<const FinalizedData> m_finalized;
public:
    auto AddDescriptorSetLayout(DescriptorSetLayoutCreateInfo inLayout) -> PipelineLayoutCreateInfo&;
    auto AddPushConstantRange(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize) -> PipelineLayoutCreateInfo&;
    // Replaces all layout descriptions and reflected name mappings.
    auto SetReflection(const ShaderProgramReflection& inReflection) -> PipelineLayoutCreateInfo&;
    auto Reset() -> PipelineLayoutCreateInfo&;
    auto Finalize() -> PipelineLayoutCreateInfo&;
    auto IsFinalized() const noexcept -> bool { return bool(m_finalized); }
};
