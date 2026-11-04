#include "../render_pass.h"
#include "device.h"
#include "resource/image.h"
#include "allocator/private/render_pass_allocator.h"

#include <algorithm>

namespace
{
	constexpr VkAttachmentReference kUnusedAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
}

RenderPass::~RenderPass()
{
	Destroy();
}

void RenderPass::Create(const RenderPassCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.IsFinalized(), "Render pass create info must be finalized before Create!");
	const auto description = inCreateInfo.m_description;
	std::vector<VkSubpassDescription> subpasses;
	subpasses.reserve(description->subpasses.size());
	for (const auto& source : description->subpasses)
	{
		VkSubpassDescription subpass{};
		subpass.flags = source.flags;
		subpass.pipelineBindPoint = source.bindPoint;
		subpass.colorAttachmentCount = static_cast<uint32_t>(source.colors.size());
		subpass.pColorAttachments = source.colors.data();
		subpass.pResolveAttachments = source.resolves.empty() ? nullptr : source.resolves.data();
		subpass.pDepthStencilAttachment = source.depth ? &*source.depth : nullptr;
		subpass.preserveAttachmentCount = static_cast<uint32_t>(source.preserves.size());
		subpass.pPreserveAttachments = source.preserves.data();
		subpasses.push_back(subpass);
	}
	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = static_cast<uint32_t>(description->attachments.size());
	renderPassInfo.pAttachments = description->attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(description->dependencies.size());
	renderPassInfo.pDependencies = description->dependencies.data();
	auto resource = MyDevice::GetInstance().GetRenderPassAllocator()->AllocateRenderPass(renderPassInfo);
	VulkanDetail::RenderPassSnapshot snapshot{ std::move(resource), description };
	ClearFramebufferCache();
	m_snapshot = std::move(snapshot);
}

void RenderPass::Destroy()
{
	ClearFramebufferCache();
	m_snapshot = {};
}

auto RenderPass::GetAttachmentIndex(std::string_view inName) const -> uint32_t
{
	CHECK_TRUE(m_snapshot.description, "Render pass is not created!");
	const auto iter = m_snapshot.description->attachmentNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_snapshot.description->attachmentNameToIndex.end(), "Render pass attachment name does not exist!");

	return iter->second;
}

void FramebufferInfo::Reset()
{
	*this = {};
}

void FramebufferInfo::SetImageView(std::string_view inTargetAttachmentName, const ImageView& inViewAttached)
{
	CHECK_TRUE(!inTargetAttachmentName.empty(), "Framebuffer attachment name cannot be empty!");
	CHECK_TRUE(inViewAttached.IsValid(), "Framebuffer image view is invalid!");
	m_imageViews.insert_or_assign(std::string(inTargetAttachmentName), inViewAttached);
}

void FramebufferInfo::CustomizeSize(uint32_t inWidth, uint32_t inHeight, uint32_t inLayers)
{
	CHECK_TRUE(inWidth > 0 && inHeight > 0 && inLayers > 0, "Framebuffer dimensions cannot be empty!");
	m_extent = VkExtent2D{ inWidth, inHeight };
	m_layers = inLayers;
}

void Framebuffer::Reset() noexcept
{
	m_resource = nullptr;
	m_renderPass = {};
	m_extent = {};
	m_layers = 0;
}

void RenderPass::ClearFramebufferCache()
{
	m_framebuffers.clear();
}

auto RenderPass::GetFramebuffer(const FramebufferInfo& inInfo) const -> Framebuffer
{
	CHECK_TRUE(GetVkRenderPass() != VK_NULL_HANDLE && m_snapshot.description, "Render pass is not created!");
	const auto& description = *m_snapshot.description;
	CHECK_TRUE(inInfo.m_imageViews.size() == description.attachments.size(), "Framebuffer attachment set must match the render pass!");
	CHECK_TRUE(!description.attachments.empty() || inInfo.m_extent.has_value(), "An attachmentless framebuffer requires explicit dimensions!");
	std::vector<ImageView> views(description.attachments.size());
	std::vector<VkImageView> handles(description.attachments.size());
	std::vector<VulkanDetail::ResourceReference> dependencies{ m_snapshot.resource };
	VkExtent2D extent = inInfo.m_extent.value_or(VkExtent2D{ UINT32_MAX, UINT32_MAX });
	uint32_t layers = inInfo.m_extent ? inInfo.m_layers : UINT32_MAX;
	for (const auto& [name, view] : inInfo.m_imageViews)
	{
		const auto iterator = description.attachmentNameToIndex.find(name);
		CHECK_TRUE(iterator != description.attachmentNameToIndex.end(), "Unknown framebuffer attachment name!");
		const uint32_t index = iterator->second;
		const auto& information = view.GetImageViewInformation();
		const auto size = view.GetViewSize();
		CHECK_TRUE(view.IsValid(), "Framebuffer image view is invalid!");
		CHECK_TRUE(information.levelCount == 1, "Framebuffer attachment views must contain one mip level!");
		CHECK_TRUE(information.format == description.attachments[index].format, "Framebuffer attachment format differs from the render pass!");
		CHECK_TRUE(information.samples == description.attachments[index].samples, "Framebuffer attachment sample count differs from the render pass!");
		CHECK_TRUE((information.usage & description.attachmentUsages[index]) == description.attachmentUsages[index], "Framebuffer image usage does not support the attachment!");
		CHECK_TRUE(information.layerCount > 0 && information.layerCount != VK_REMAINING_ARRAY_LAYERS, "Framebuffer image view layer range is invalid!");
		CHECK_TRUE(information.type != VK_IMAGE_VIEW_TYPE_3D, "Framebuffer attachment cannot be a 3D view!");
		if (inInfo.m_extent)
		{
			CHECK_TRUE(extent.width <= size.width && extent.height <= size.height && layers <= information.layerCount, "Framebuffer dimensions exceed an attachment view!");
		}
		else
		{
			extent.width = std::min(extent.width, size.width);
			extent.height = std::min(extent.height, size.height);
			layers = std::min(layers, information.layerCount);
		}
		views[index] = view;
		handles[index] = view.GetVkImageView();
		dependencies.push_back(view._GetResourceReference());
	}
	CHECK_TRUE(extent.width > 0 && extent.height > 0 && layers > 0, "Framebuffer dimensions cannot be empty!");
	for (const auto& cached : m_framebuffers)
	{
		const auto size = cached.framebuffer.GetSize();
		if (size.width == extent.width && size.height == extent.height && cached.framebuffer.GetLayerCount() == layers && cached.views == views) return cached.framebuffer;
	}
	VkFramebufferCreateInfo createInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	createInfo.renderPass = GetVkRenderPass();
	createInfo.attachmentCount = static_cast<uint32_t>(handles.size());
	createInfo.pAttachments = handles.data();
	createInfo.width = extent.width;
	createInfo.height = extent.height;
	createInfo.layers = layers;
	const auto handle = MyDevice::GetInstance().CreateFramebuffer(createInfo);
	Framebuffer framebuffer;
	const auto device = MyDevice::GetInstance().GetVkDevice();
	try
	{
		framebuffer.m_resource = MakeIntrusive<VulkanDetail::FramebufferResource>(handle, [device](VkFramebuffer value) { vkDestroyFramebuffer(device, value, nullptr); }, std::move(dependencies));
	}
	catch (...)
	{
		vkDestroyFramebuffer(device, handle, nullptr);
		throw;
	}
	framebuffer.m_renderPass = m_snapshot;
	framebuffer.m_extent = extent;
	framebuffer.m_layers = layers;
	m_framebuffers.push_back({ std::move(views), framebuffer });
	return framebuffer;
}

auto VulkanDetail::RenderPassSnapshot::GetClearValues() const -> const std::vector<VkClearValue>&
{
	static const std::vector<VkClearValue> empty;
	return description ? description->clearValues : empty;
}

auto VulkanDetail::RenderPassSnapshot::IsCompatibleWith(const RenderPassSnapshot& inOther) const -> bool
{
	if (!resource || !inOther.resource || !description || !inOther.description) return false;
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
