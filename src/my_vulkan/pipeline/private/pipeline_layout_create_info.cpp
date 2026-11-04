#include "../pipeline_layout_create_info.h"
#include <limits>

auto DescriptorSetLayoutCreateInfo::SetBinding(uint32_t inBinding, VkDescriptorType inType,
    VkShaderStageFlags inStages, uint32_t inDescriptorCount) -> DescriptorSetLayoutCreateInfo&
{
    CHECK_TRUE(inDescriptorCount > 0, "Descriptor count must be greater than zero!");
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = inBinding;
    binding.descriptorType = inType;
    binding.stageFlags = inStages;
    binding.descriptorCount = inDescriptorCount;
    m_descriptorBindings[inBinding] = binding;
    m_finalizedBindings.reset();
    return *this;
}

auto PipelineLayoutCreateInfo::AddDescriptorSetLayout(DescriptorSetLayoutCreateInfo inLayout) -> PipelineLayoutCreateInfo&
{
    m_layouts.push_back(std::move(inLayout));
    m_finalized.reset();
    return *this;
}
auto PipelineLayoutCreateInfo::AddPushConstantRange(VkShaderStageFlags inStages,
    uint32_t inOffset, uint32_t inSize) -> PipelineLayoutCreateInfo&
{
    CHECK_TRUE(inStages != 0 && inSize > 0, "Push constant stages and size cannot be empty!");
    m_pushConstants.push_back({inStages, inOffset, inSize});
    m_finalized.reset();
    return *this;
}
auto PipelineLayoutCreateInfo::SetReflection(const ShaderProgramReflection& reflection) -> PipelineLayoutCreateInfo&
{
    std::vector<DescriptorSetLayoutCreateInfo> layouts;
    for (const auto& bindings : reflection.GetVkDescriptorBindings())
    {
        DescriptorSetLayoutCreateInfo layout;
        for (const auto& [index, binding] : bindings)
        {
            CHECK_TRUE(binding.pImmutableSamplers == nullptr, "Immutable samplers are unsupported!");
            layout.SetBinding(index, binding.descriptorType, binding.stageFlags, binding.descriptorCount);
        }
        layouts.push_back(std::move(layout));
    }
    auto ranges = reflection.GetVkPushConstantRanges();
    auto names = reflection.GetNameToSetBinding();
    auto ambiguous = reflection.GetAmbiguousNames();
    m_layouts = std::move(layouts);
    m_pushConstants = std::move(ranges);
    m_names = std::move(names);
    m_ambiguousNames = std::move(ambiguous);
    m_finalized.reset();
    return *this;
}
auto PipelineLayoutCreateInfo::Reset() -> PipelineLayoutCreateInfo&
{
    m_layouts.clear(); m_pushConstants.clear(); m_names.clear(); m_ambiguousNames.clear();
    m_finalized.reset();
    return *this;
}
auto PipelineLayoutCreateInfo::Finalize() -> PipelineLayoutCreateInfo&
{
    if (m_finalized) return *this;
    auto data = std::make_shared<FinalizedData>();
    data->layouts = m_layouts;
    data->pushConstants = m_pushConstants;
    data->names = m_names;
    data->ambiguousNames = m_ambiguousNames;
    VkShaderStageFlags stages = 0;
    for (const auto& range : data->pushConstants)
    {
        CHECK_TRUE(range.stageFlags != 0 && range.size > 0 && range.offset % 4 == 0 && range.size % 4 == 0 &&
            range.size <= std::numeric_limits<uint32_t>::max() - range.offset,
            "Invalid push constant range!");
        CHECK_TRUE((stages & range.stageFlags) == 0, "Push constant ranges repeat a shader stage!");
        stages |= range.stageFlags;
    }
    for (auto& layout : data->layouts)
    {
        std::map<uint32_t, VkDescriptorSetLayoutBinding> bindings;
        for (const auto& [index, binding] : layout.m_descriptorBindings)
        {
            CHECK_TRUE(binding.descriptorCount > 0 && binding.stageFlags != 0 && binding.pImmutableSamplers == nullptr,
                "Invalid descriptor layout binding!");
            bindings.emplace(index, binding);
        }
        auto nativeBindings = std::make_shared<std::vector<VkDescriptorSetLayoutBinding>>();
        nativeBindings->reserve(bindings.size());
        for (const auto& [index, binding] : bindings) nativeBindings->push_back(binding);
        layout.m_finalizedBindings = std::move(nativeBindings);
        data->bindings.push_back(std::move(bindings));
    }
    m_finalized = std::move(data);
    return *this;
}
