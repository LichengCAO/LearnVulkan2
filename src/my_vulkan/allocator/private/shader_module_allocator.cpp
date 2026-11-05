#include "shader_module_allocator.h"
#include <algorithm>

auto VulkanDetail::ShaderModuleAllocator::_Hash(std::span<const uint32_t> code) noexcept -> size_t
{
    size_t hash = 0;
    for (const auto word : code) hash ^= std::hash<uint32_t>{}(word) + size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    return hash;
}
VulkanDetail::ShaderModuleAllocator::~ShaderModuleAllocator() noexcept { Destroy(); }
void VulkanDetail::ShaderModuleAllocator::Create(VkDevice device)
{
    CHECK_TRUE(!IsCreated(), "Shader module allocator is already created!");
    CHECK_TRUE(device != VK_NULL_HANDLE, "Shader module allocator requires a live device!");
    m_device = device;
}
auto VulkanDetail::ShaderModuleAllocator::GetOrCreate(std::span<const uint32_t> code) -> VkShaderModule
{
    CHECK_TRUE(IsCreated(), "Shader module allocator is not created!");
    CHECK_TRUE(!code.empty(), "Shader module bytecode is empty!");
    CHECK_TRUE(vkCreateShaderModule != nullptr && vkDestroyShaderModule != nullptr,
        "Shader module native entrypoints are unavailable!");
    const auto hash = _Hash(code);
    const auto found = m_entries.find(hash);
    if (found != m_entries.end())
        for (const auto& entry : found->second)
            if (entry.code.size() == code.size() && std::equal(code.begin(), code.end(), entry.code.begin()))
                return entry.module;
    Entry entry{std::vector<uint32_t>(code.begin(), code.end()), VK_NULL_HANDLE};
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = entry.code.size() * sizeof(uint32_t);
    info.pCode = entry.code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(m_device, &info, nullptr, &module), "Failed to create shader module!");
    CHECK_TRUE(module != VK_NULL_HANDLE, "Shader module creation returned a null handle!");
    entry.module = module;
    try { m_entries[hash].push_back(std::move(entry)); }
    catch (...) { vkDestroyShaderModule(m_device, module, nullptr); throw; }
    return module;
}
void VulkanDetail::ShaderModuleAllocator::Destroy() noexcept
{
    for (const auto& bucket : m_entries)
        for (const auto& entry : bucket.second)
            vkDestroyShaderModule(m_device, entry.module, nullptr);
    m_entries.clear();
    m_device = VK_NULL_HANDLE;
}
