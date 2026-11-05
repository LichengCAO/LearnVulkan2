#include "../framebuffer_desc.h"
#include "render_target_description.h"
#include "resource/image.h"

#include <algorithm>

namespace
{
	constexpr VkAttachmentReference kUnusedAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
}

auto RenderPassDesc::_GetSnapshot() const -> VulkanDetail::RenderPassSnapshot
{
	CHECK_TRUE(IsFinalized(), "Render pass description must be finalized!");
	return { m_description };
}

void FramebufferDesc::Reset()
{
	*this = {};
}

void FramebufferDesc::SetImageView(std::string_view inTargetAttachmentName, const ImageView& inViewAttached)
{
	CHECK_TRUE(!inTargetAttachmentName.empty(), "Framebuffer attachment name cannot be empty!");
	CHECK_TRUE(inViewAttached.IsValid(), "Framebuffer image view is invalid!");
	const auto& info = inViewAttached.GetImageViewInformation();
	Attachment attachment;
	attachment.resourceId = inViewAttached.GetResourceId();
	attachment.view = inViewAttached.GetVkImageView();
	attachment.size = inViewAttached.GetViewSize();
	attachment.format = info.format;
	attachment.samples = info.samples;
	attachment.usage = info.usage;
	attachment.type = info.type;
	attachment.levelCount = info.levelCount;
	attachment.layerCount = info.layerCount;
	CHECK_TRUE(attachment.resourceId != 0 && attachment.view != VK_NULL_HANDLE, "Invalid framebuffer attachment identity!");
	m_attachments.insert_or_assign(std::string(inTargetAttachmentName), attachment);
}

void FramebufferDesc::CustomizeSize(uint32_t inWidth, uint32_t inHeight, uint32_t inLayers)
{
	CHECK_TRUE(inWidth > 0 && inHeight > 0 && inLayers > 0, "Framebuffer dimensions cannot be empty!");
	m_extent = VkExtent2D{ inWidth, inHeight };
	m_layers = inLayers;
}

auto FramebufferDesc::_Resolve(const VulkanDetail::RenderPassDescription& description) const -> VulkanDetail::ResolvedFramebufferDesc
{
	CHECK_TRUE(m_attachments.size() == description.attachments.size(), "Framebuffer attachment set must match the render pass!");
	CHECK_TRUE(!description.attachments.empty() || m_extent.has_value(), "An attachmentless framebuffer requires explicit dimensions!");
	VulkanDetail::ResolvedFramebufferDesc result;
	result.attachments.resize(description.attachments.size());
	result.attachmentIds.resize(description.attachments.size());
	auto& extent = result.extent;
	auto& layers = result.layers;
	extent = m_extent.value_or(VkExtent2D{ UINT32_MAX, UINT32_MAX });
	layers = m_extent ? m_layers : UINT32_MAX;
	for (const auto& [name, view] : m_attachments)
	{
		const auto iterator = description.attachmentNameToIndex.find(name);
		CHECK_TRUE(iterator != description.attachmentNameToIndex.end(), "Unknown framebuffer attachment name!");
		const uint32_t index = iterator->second;
		const auto& information = view;
		const auto size = view.size;
		CHECK_TRUE(view.resourceId != 0 && view.view != VK_NULL_HANDLE, "Framebuffer image view is invalid!");
		CHECK_TRUE(information.levelCount == 1, "Framebuffer attachment views must contain one mip level!");
		CHECK_TRUE(information.format == description.attachments[index].format, "Framebuffer attachment format differs from the render pass!");
		CHECK_TRUE(information.samples == description.attachments[index].samples, "Framebuffer attachment sample count differs from the render pass!");
		CHECK_TRUE((information.usage & description.attachmentUsages[index]) == description.attachmentUsages[index], "Framebuffer image usage does not support the attachment!");
		CHECK_TRUE(information.layerCount > 0 && information.layerCount != VK_REMAINING_ARRAY_LAYERS, "Framebuffer image view layer range is invalid!");
		CHECK_TRUE(information.type != VK_IMAGE_VIEW_TYPE_3D, "Framebuffer attachment cannot be a 3D view!");
		if (m_extent)
		{
			CHECK_TRUE(extent.width <= size.width && extent.height <= size.height && layers <= information.layerCount, "Framebuffer dimensions exceed an attachment view!");
		}
		else
		{
			extent.width = std::min(extent.width, size.width);
			extent.height = std::min(extent.height, size.height);
			layers = std::min(layers, information.layerCount);
		}
		result.attachments[index] = view.view;
		result.attachmentIds[index] = view.resourceId;
	}
	CHECK_TRUE(extent.width > 0 && extent.height > 0 && layers > 0, "Framebuffer dimensions cannot be empty!");
	return result;
}

auto VulkanDetail::RenderPassSnapshot::GetClearValues() const -> const std::vector<VkClearValue>&
{
	static const std::vector<VkClearValue> empty;
	return description ? description->clearValues : empty;
}

auto VulkanDetail::RenderPassSnapshot::IsCompatibleWith(const RenderPassSnapshot& inOther) const -> bool
{
	if (!description || !inOther.description) return false;
	if (description.get() == inOther.description.get()) return true;
	const auto& left = *description;
	const auto& right = *inOther.description;
	if (left.subpasses.size() != right.subpasses.size() || left.dependencies.size() != right.dependencies.size()) return false;
	for (size_t i = 0; i < left.dependencies.size(); ++i)
	{
		const auto& a = left.dependencies[i];
		const auto& b = right.dependencies[i];
		if (a.srcSubpass != b.srcSubpass || a.dstSubpass != b.dstSubpass || a.srcStageMask != b.srcStageMask || a.dstStageMask != b.dstStageMask || a.srcAccessMask != b.srcAccessMask || a.dstAccessMask != b.dstAccessMask || a.dependencyFlags != b.dependencyFlags) return false;
	}
	const auto referenceCompatible = [&](const VkAttachmentReference& a, const VkAttachmentReference& b)
	{
		if (a.attachment == VK_ATTACHMENT_UNUSED || b.attachment == VK_ATTACHMENT_UNUSED) return a.attachment == b.attachment;
		const auto& attachmentA = left.attachments.at(a.attachment);
		const auto& attachmentB = right.attachments.at(b.attachment);
		return attachmentA.format == attachmentB.format && attachmentA.samples == attachmentB.samples && attachmentA.flags == attachmentB.flags;
	};
	const auto referencesCompatible = [&](const auto& a, const auto& b)
	{
		for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
		{
			if (!referenceCompatible(i < a.size() ? a[i] : kUnusedAttachment, i < b.size() ? b[i] : kUnusedAttachment)) return false;
		}
		return true;
	};
	for (size_t i = 0; i < left.subpasses.size(); ++i)
	{
		const auto& a = left.subpasses[i];
		const auto& b = right.subpasses[i];
		if (a.flags != b.flags || a.bindPoint != b.bindPoint || a.preserves != b.preserves || !referencesCompatible(a.colors, b.colors) || !referenceCompatible(a.depth.value_or(kUnusedAttachment), b.depth.value_or(kUnusedAttachment))) return false;
		if (left.subpasses.size() != 1 && !referencesCompatible(a.resolves, b.resolves)) return false;
	}
	return true;
}
