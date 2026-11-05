#pragma once
#include "my_vulkan/host_common.h"
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

namespace VulkanDetail { struct RenderPassSnapshot; }

class SubpassDescription final
{
    friend class RenderPassDesc;
private:
    struct AttachmentReference final
    {
        std::string attachmentName;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool isUsed = false;
    };
    struct Dependency final
    {
        std::string srcSubpassName;
        bool isExternal = false;
        bool isSelf = false;
        VkPipelineStageFlags dstStage = 0;
        VkAccessFlags dstAccess = 0;
        VkDependencyFlags dependencyFlags = 0;
    };
    std::vector<AttachmentReference> m_colorAttachments;
    std::vector<AttachmentReference> m_resolveAttachments;
    std::optional<AttachmentReference> m_depthStencilAttachment;
    std::vector<std::string> m_preserveAttachments;
    std::vector<Dependency> m_dependencies;
    VkPipelineStageFlags m_availableStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags m_availableAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
public:
    void AddDepthStencilAttachment(
		std::string_view inAttachmentName,
        VkImageLayout inFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    void AddResolvedAttachment(
		uint32_t inOutputSlot, 
		std::string_view inMultiSampleAttachmentName,
        std::string_view in1SampleAttachmentName,
        VkImageLayout inMultiSampleLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VkImageLayout in1SampleLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    void AddColorAttachment(
		uint32_t inOutputSlot, 
		std::string_view inColorAttachmentName,
        VkImageLayout inColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    void AddPreserveAttachment(std::string_view inAttachmentName);
    void CustomizeAvailableState(VkPipelineStageFlags inStage, VkAccessFlags inAccess);
    // Adds a dependency from inSrcSubpassName to this subpass.
    // inStage/inAccess describe where data must be visible for this subpass.
    // Use CustomizeAvailableState on the source subpass to describe where data is produced.
    // BY_REGION dependencies are local to overlapping framebuffer regions.
    void AddDependencyOnSubpass(
		std::string_view inSrcSubpassName, 
		VkPipelineStageFlags inStage,
        VkAccessFlags inAccess, 
		VkDependencyFlags inDependencyFlags = VK_DEPENDENCY_BY_REGION_BIT);
    void AddExternalDependency(
		VkPipelineStageFlags inStage, 
		VkAccessFlags inAccess,
        VkDependencyFlags inDependencyFlags = 0);
    // Adds a self-dependency that permits vkCmdPipelineBarrier inside this subpass.
    // Any in-render-pass pipeline barrier must use scopes covered by this dependency.
    // The same stage/access masks are used for both source and destination scopes.
    // The default BY_REGION flag keeps framebuffer-space barriers framebuffer-local.
    void AllowLocalPipelineBarrier(VkPipelineStageFlags inStage =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VkAccessFlags inAccess = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VkDependencyFlags inDependencyFlags = VK_DEPENDENCY_BY_REGION_BIT);
};

class AttachmentDescription final
{
    friend class RenderPassDesc;
private:
    VkAttachmentDescription m_description{};
    VkClearValue m_clearValue{};
public:
    AttachmentDescription();
    void CustomizeFormat(VkFormat inFormat, std::variant<std::pair<float, uint32_t>, glm::vec4> inClearValue);
    void CustomizeFormat(VkFormat inFormat, const VkClearValue& inClearValue);
    void CustomizeSampleCount(VkSampleCountFlagBits inSampleCount);
    void CustomizeLoadOperation(VkAttachmentLoadOp inLoadOp);
    void CustomizeStoreOperation(VkAttachmentStoreOp inStoreOp);
    void CustomizeStencilStoreLoadOperation(VkAttachmentLoadOp inLoadOp, VkAttachmentStoreOp inStoreOp);
    void CustomizeInitialLayout(VkImageLayout inLayout);
    void CustomizeFinalLayout(VkImageLayout inLayout);
};

namespace VulkanDetail
{
    // Owned CPU metadata. Native pointer views are assembled only during Device creation.
    struct RenderPassDescription final
    {
        struct SubpassInfo
        {
            uint32_t colorSlotCount = 0;
            bool hasDepth = false;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        };
        struct Subpass
        {
            VkSubpassDescriptionFlags flags = 0;
            VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            std::vector<VkAttachmentReference> colors;
            std::vector<VkAttachmentReference> resolves;
            std::optional<VkAttachmentReference> depth;
            std::vector<uint32_t> preserves;
        };
        std::vector<SubpassInfo> subpassInfos;
        std::vector<VkClearValue> clearValues;
        std::unordered_map<std::string, uint32_t> attachmentNameToIndex;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<Subpass> subpasses;
        std::vector<VkSubpassDependency> dependencies;
        std::vector<VkImageUsageFlags> attachmentUsages;
    };
}

class RenderPassDesc final
{
    friend class MyDevice;
    friend class GraphicsShaderProgramCreateInfo;
    friend class CommandBuffer;
    friend class GraphicsShaderProgram;
    friend class GraphicsPipelineStateInfo;
    friend struct ResourceLifetimeTestProbe;
public:
    RenderPassDesc() = default;
    RenderPassDesc(const RenderPassDesc&) = delete;
    RenderPassDesc(RenderPassDesc&&) = delete;
    auto operator=(const RenderPassDesc&) -> RenderPassDesc& = delete;
    auto operator=(RenderPassDesc&&) -> RenderPassDesc& = delete;
    ~RenderPassDesc() = default;
    void AddSubpass(std::string_view inName, const SubpassDescription& inSubpass);
    void AddAttachment(std::string_view inName, const AttachmentDescription& inAttachment);
    auto Finalize() -> RenderPassDesc&;
    auto IsFinalized() const noexcept -> bool { return static_cast<bool>(m_description); }
    auto GetSubpassCount() const -> uint32_t;
    auto GetClearValues() const -> const std::vector<VkClearValue>&;
    auto GetAttachmentIndex(std::string_view inName) const -> uint32_t;
private:
    auto _GetSnapshot() const -> VulkanDetail::RenderPassSnapshot;
    auto _GetAttachmentIndex(std::string_view inName) const -> uint32_t;
    auto _GetSubpassIndex(std::string_view inName) const -> uint32_t;
    std::vector<AttachmentDescription> m_attachments;
    std::vector<SubpassDescription> m_subpasses;
    std::unordered_map<std::string, uint32_t> m_attachmentNameToIndex;
    std::unordered_map<std::string, uint32_t> m_subpassNameToIndex;
    std::shared_ptr<const VulkanDetail::RenderPassDescription> m_description;
};
