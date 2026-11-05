#pragma once
#include "render_pass_desc.h"

class ImageView;
namespace VulkanDetail { struct ResolvedFramebufferDesc; }

// Borrowed attachment snapshots. The caller retains the native resources through
// command preparation, recording and GPU execution. Copies do not retain them.
class FramebufferDesc final
{
    friend class MyDevice;
    friend class CommandBuffer;
    friend struct ResourceLifetimeTestProbe;
    friend struct CommandBufferTestProbe;
    friend struct ResourceSubmissionTestProbe;
private:
    struct Attachment
    {
        uint64_t resourceId = 0;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent3D size{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0;
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        uint32_t levelCount = 0;
        uint32_t layerCount = 0;
    };
    std::unordered_map<std::string, Attachment> m_attachments;
    std::optional<VkExtent2D> m_extent;
    uint32_t m_layers = 1;
    auto _Resolve(const VulkanDetail::RenderPassDescription& inPass) const -> VulkanDetail::ResolvedFramebufferDesc;
public:
    void Reset();
    void SetImageView(std::string_view inName, const ImageView& inView);
    void CustomizeSize(uint32_t inWidth, uint32_t inHeight, uint32_t inLayers = 1);
};
