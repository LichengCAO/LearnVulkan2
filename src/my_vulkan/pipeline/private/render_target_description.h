#pragma once
#include "../render_pass_desc.h"
#include "../framebuffer_desc.h"

namespace VulkanDetail
{
// CPU-only immutable snapshot, independent of Device and native resource lifetime.
struct RenderPassSnapshot
{
    std::shared_ptr<const RenderPassDescription> description;
    auto GetSubpassCount() const -> uint32_t { return description ? static_cast<uint32_t>(description->subpassInfos.size()) : 0; }
    auto GetClearValues() const -> const std::vector<VkClearValue>&;
    auto IsCompatibleWith(const RenderPassSnapshot& inOther) const -> bool;
};

struct ResolvedFramebufferDesc
{
    std::vector<VkImageView> attachments;
    std::vector<uint64_t> attachmentIds;
    VkExtent2D extent{};
    uint32_t layers = 0;
};
}
