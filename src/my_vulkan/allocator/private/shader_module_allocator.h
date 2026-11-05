#pragma once
#include "common.h"
#include <span>
#include <unordered_map>
struct CreateInfoFinalizeTestProbe;

namespace VulkanDetail
{
// Device-owned native modules. Callers serialize lookup, creation and teardown.
class ShaderModuleAllocator final
{
    friend struct ::CreateInfoFinalizeTestProbe;
    struct Entry { std::vector<uint32_t> code; VkShaderModule module; };
    VkDevice m_device = VK_NULL_HANDLE;
    std::unordered_map<size_t, std::vector<Entry>> m_entries;
    static auto _Hash(std::span<const uint32_t> inCode) noexcept -> size_t;
public:
    ShaderModuleAllocator() = default;
    ShaderModuleAllocator(const ShaderModuleAllocator&) = delete;
    auto operator=(const ShaderModuleAllocator&) -> ShaderModuleAllocator& = delete;
    ~ShaderModuleAllocator() noexcept;
    void Create(VkDevice inDevice);
    auto IsCreated() const noexcept -> bool { return m_device != VK_NULL_HANDLE; }
    auto GetOrCreate(std::span<const uint32_t> inCode) -> VkShaderModule;
    void Destroy() noexcept;
};
}
