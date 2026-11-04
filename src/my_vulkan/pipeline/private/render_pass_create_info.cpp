#include "../render_pass_create_info.h"
#include <algorithm>

namespace
{
constexpr VkAttachmentReference kUnusedAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
auto CopyName(std::string_view name, const char* message) -> std::string
{
    CHECK_TRUE(!name.empty(), message);
    return std::string(name);
}
template<typename Reference>
void ResizeReferences(std::vector<Reference>& references, uint32_t slot)
{
    if (references.size() <= slot) references.resize(static_cast<size_t>(slot) + 1);
}
}

AttachmentDescription::AttachmentDescription()
{
    m_description.format = VK_FORMAT_UNDEFINED;
    m_description.samples = VK_SAMPLE_COUNT_1_BIT;
    m_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    m_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    m_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_description.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
void AttachmentDescription::CustomizeFormat(VkFormat format, std::variant<std::pair<float, uint32_t>, glm::vec4> clear)
{
    CHECK_TRUE(format != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
    m_description.format = format;
    m_clearValue = {};
    if (std::holds_alternative<glm::vec4>(clear))
    {
        const auto value = std::get<glm::vec4>(clear);
        m_clearValue.color = { value.r, value.g, value.b, value.a };
    }
    else
    {
        const auto value = std::get<std::pair<float, uint32_t>>(clear);
        m_clearValue.depthStencil = { value.first, value.second };
    }
}
void AttachmentDescription::CustomizeFormat(VkFormat format, const VkClearValue& clear)
{
    CHECK_TRUE(format != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
    m_description.format = format;
    m_clearValue = clear;
}
void AttachmentDescription::CustomizeSampleCount(VkSampleCountFlagBits samples) { m_description.samples = samples; }
void AttachmentDescription::CustomizeLoadOperation(VkAttachmentLoadOp op) { m_description.loadOp = op; }
void AttachmentDescription::CustomizeStoreOperation(VkAttachmentStoreOp op) { m_description.storeOp = op; }
void AttachmentDescription::CustomizeStencilStoreLoadOperation(VkAttachmentLoadOp load, VkAttachmentStoreOp store)
{
    m_description.stencilLoadOp = load;
    m_description.stencilStoreOp = store;
}
void AttachmentDescription::CustomizeInitialLayout(VkImageLayout layout) { m_description.initialLayout = layout; }
void AttachmentDescription::CustomizeFinalLayout(VkImageLayout layout) { m_description.finalLayout = layout; }

void SubpassDescription::AddDepthStencilAttachment(std::string_view name, VkImageLayout layout)
{
    m_depthStencilAttachment = AttachmentReference{ CopyName(name, "Depth stencil attachment name cannot be empty!"), layout, true };
}
void SubpassDescription::AddResolvedAttachment(uint32_t slot, std::string_view multi, std::string_view single,
    VkImageLayout multiLayout, VkImageLayout singleLayout)
{
    AddColorAttachment(slot, multi, multiLayout);
    ResizeReferences(m_resolveAttachments, slot);
    m_resolveAttachments[slot] = AttachmentReference{ CopyName(single, "Resolve attachment name cannot be empty!"), singleLayout, true };
    if (m_resolveAttachments.size() < m_colorAttachments.size()) m_resolveAttachments.resize(m_colorAttachments.size());
}
void SubpassDescription::AddColorAttachment(uint32_t slot, std::string_view name, VkImageLayout layout)
{
    ResizeReferences(m_colorAttachments, slot);
    m_colorAttachments[slot] = AttachmentReference{ CopyName(name, "Color attachment name cannot be empty!"), layout, true };
    if (!m_resolveAttachments.empty() && m_resolveAttachments.size() < m_colorAttachments.size())
        m_resolveAttachments.resize(m_colorAttachments.size());
}
void SubpassDescription::AddPreserveAttachment(std::string_view name)
{
    const auto copied = CopyName(name, "Preserve attachment name cannot be empty!");
    CHECK_TRUE(std::find(m_preserveAttachments.begin(), m_preserveAttachments.end(), copied) == m_preserveAttachments.end(), "Preserve attachment is already present!");
    m_preserveAttachments.push_back(copied);
}
void SubpassDescription::CustomizeAvailableState(VkPipelineStageFlags stage, VkAccessFlags access)
{
    m_availableStage = stage;
    m_availableAccess = access;
}
void SubpassDescription::AddDependencyOnSubpass(std::string_view name, VkPipelineStageFlags stage,
    VkAccessFlags access, VkDependencyFlags flags)
{
    CHECK_TRUE(!name.empty(), "Dependency source subpass name cannot be empty!");
    CHECK_TRUE(stage != 0, "Dependency destination stage cannot be empty!");
    m_dependencies.push_back(Dependency{ std::string(name), false, false, stage, access, flags });
}
void SubpassDescription::AddExternalDependency(VkPipelineStageFlags stage, VkAccessFlags access, VkDependencyFlags flags)
{
    CHECK_TRUE(stage != 0, "Dependency destination stage cannot be empty!");
    m_dependencies.push_back(Dependency{ {}, true, false, stage, access, flags });
}
void SubpassDescription::AllowLocalPipelineBarrier(VkPipelineStageFlags stage, VkAccessFlags access, VkDependencyFlags flags)
{
    CHECK_TRUE(stage != 0, "Local pipeline barrier stage cannot be empty!");
    m_dependencies.push_back(Dependency{ {}, false, true, stage, access, flags });
}

void RenderPassCreateInfo::AddSubpass(std::string_view name, const SubpassDescription& subpass)
{
    const auto copied = CopyName(name, "Subpass name cannot be empty!");
    CHECK_TRUE(!m_subpassNameToIndex.contains(copied), "Subpass name already exists!");
    const auto iterator = m_subpassNameToIndex.emplace(copied, static_cast<uint32_t>(m_subpasses.size())).first;
    try { m_subpasses.push_back(subpass); }
    catch (...) { m_subpassNameToIndex.erase(iterator); throw; }
    m_description.reset();
}
void RenderPassCreateInfo::AddAttachment(std::string_view name, const AttachmentDescription& attachment)
{
    const auto copied = CopyName(name, "Attachment name cannot be empty!");
    CHECK_TRUE(!m_attachmentNameToIndex.contains(copied), "Attachment name already exists!");
    CHECK_TRUE(attachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format must be customized before adding it!");
    const auto iterator = m_attachmentNameToIndex.emplace(copied, static_cast<uint32_t>(m_attachments.size())).first;
    try { m_attachments.push_back(attachment); }
    catch (...) { m_attachmentNameToIndex.erase(iterator); throw; }
    m_description.reset();
}
auto RenderPassCreateInfo::_GetAttachmentIndex(std::string_view name) const -> uint32_t
{
    const auto iterator = m_attachmentNameToIndex.find(std::string(name));
    CHECK_TRUE(iterator != m_attachmentNameToIndex.end(), "Attachment name does not exist!");
    return iterator->second;
}
auto RenderPassCreateInfo::_GetSubpassIndex(std::string_view name) const -> uint32_t
{
    const auto iterator = m_subpassNameToIndex.find(std::string(name));
    CHECK_TRUE(iterator != m_subpassNameToIndex.end(), "Subpass name does not exist!");
    return iterator->second;
}

auto RenderPassCreateInfo::Finalize() -> RenderPassCreateInfo&
{
    if (IsFinalized()) return *this;
    CHECK_TRUE(!m_subpasses.empty(), "Render pass needs at least one subpass!");
    auto description = std::make_shared<VulkanDetail::RenderPassDescription>();
    description->attachmentNameToIndex = m_attachmentNameToIndex;
    description->attachments.reserve(m_attachments.size());
    description->clearValues.reserve(m_attachments.size());
    description->attachmentUsages.resize(m_attachments.size());
    for (const auto& attachment : m_attachments)
    {
        CHECK_TRUE(attachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
        const auto samples = static_cast<uint32_t>(attachment.m_description.samples);
        CHECK_TRUE(samples != 0 && (samples & (samples - 1)) == 0 && samples <= VK_SAMPLE_COUNT_64_BIT, "Attachment sample count is invalid!");
        description->attachments.push_back(attachment.m_description);
        description->clearValues.push_back(attachment.m_clearValue);
    }
    const auto resolveReference = [this](const SubpassDescription::AttachmentReference& reference) -> VkAttachmentReference
    {
        return reference.isUsed ? VkAttachmentReference{ _GetAttachmentIndex(reference.attachmentName), reference.layout } : kUnusedAttachment;
    };
    description->subpasses.reserve(m_subpasses.size());
    description->subpassInfos.reserve(m_subpasses.size());
    for (const auto& source : m_subpasses)
    {
        CHECK_TRUE(source.m_resolveAttachments.empty() || source.m_resolveAttachments.size() == source.m_colorAttachments.size(), "Resolve attachments must match color attachment count!");
        VulkanDetail::RenderPassDescription::Subpass subpass;
        for (const auto& reference : source.m_colorAttachments) subpass.colors.push_back(resolveReference(reference));
        for (const auto& reference : source.m_resolveAttachments) subpass.resolves.push_back(resolveReference(reference));
        if (source.m_depthStencilAttachment) subpass.depth = resolveReference(*source.m_depthStencilAttachment);
        for (const auto& name : source.m_preserveAttachments) subpass.preserves.push_back(_GetAttachmentIndex(name));
        VulkanDetail::RenderPassDescription::SubpassInfo info;
        info.colorSlotCount = static_cast<uint32_t>(subpass.colors.size());
        info.hasDepth = subpass.depth && subpass.depth->attachment != VK_ATTACHMENT_UNUSED;
        std::optional<VkSampleCountFlagBits> samples;
        const auto inspect = [&](const VkAttachmentReference& reference, VkImageUsageFlags usage)
        {
            if (reference.attachment == VK_ATTACHMENT_UNUSED) return;
            const auto value = description->attachments.at(reference.attachment).samples;
            CHECK_TRUE(!samples || *samples == value, "Subpass attachment sample counts differ!");
            samples = value;
            description->attachmentUsages.at(reference.attachment) |= usage;
        };
        for (const auto& reference : subpass.colors) inspect(reference, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (info.hasDepth) inspect(*subpass.depth, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        info.samples = samples.value_or(VK_SAMPLE_COUNT_1_BIT);
        for (size_t slot = 0; slot < subpass.resolves.size(); ++slot)
        {
            const auto& resolve = subpass.resolves[slot];
            if (resolve.attachment == VK_ATTACHMENT_UNUSED) continue;
            const auto& color = subpass.colors[slot];
            CHECK_TRUE(color.attachment != VK_ATTACHMENT_UNUSED, "Resolve attachment requires a color attachment!");
            const auto& colorInfo = description->attachments.at(color.attachment);
            const auto& resolveInfo = description->attachments.at(resolve.attachment);
            CHECK_TRUE(colorInfo.samples != VK_SAMPLE_COUNT_1_BIT && resolveInfo.samples == VK_SAMPLE_COUNT_1_BIT, "Resolve attachments require multisampled color and single-sampled output!");
            CHECK_TRUE(colorInfo.format == resolveInfo.format, "Resolve attachment formats differ!");
            description->attachmentUsages.at(resolve.attachment) |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        description->subpassInfos.push_back(info);
        description->subpasses.push_back(std::move(subpass));
    }
    for (uint32_t dst = 0; dst < static_cast<uint32_t>(m_subpasses.size()); ++dst)
    {
        const auto& subpass = m_subpasses[dst];
        for (const auto& dependency : subpass.m_dependencies)
        {
            const uint32_t src = dependency.isSelf ? dst : dependency.isExternal ? VK_SUBPASS_EXTERNAL : _GetSubpassIndex(dependency.srcSubpassName);
            const auto& source = dependency.isExternal || dependency.isSelf ? subpass : m_subpasses[src];
            VkSubpassDependency native{};
            native.srcSubpass = src;
            native.dstSubpass = dst;
            native.srcStageMask = dependency.isSelf ? dependency.dstStage : dependency.isExternal ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : source.m_availableStage;
            native.srcAccessMask = dependency.isSelf ? dependency.dstAccess : dependency.isExternal ? 0 : source.m_availableAccess;
            native.dstStageMask = dependency.dstStage;
            native.dstAccessMask = dependency.dstAccess;
            native.dependencyFlags = dependency.dependencyFlags;
            description->dependencies.push_back(native);
        }
    }
    m_description = std::move(description);
    return *this;
}
