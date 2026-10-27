#include "../render_pass.h"
#include "device.h"
#include "resource/image.h"
#include "allocator/render_pass_allocator.h"

#include <algorithm>

namespace
{
	constexpr VkAttachmentReference kUnusedAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

	auto _CopyName(std::string_view inName, const char* inErrorMessage) -> std::string
	{
		CHECK_TRUE(!inName.empty(), inErrorMessage);
		return std::string(inName);
	}

	template<typename AttachmentReference>
	void _ResizeNamedAttachmentReferenceVector(
		std::vector<AttachmentReference>& inoutReferences,
		uint32_t inSlot)
	{
		if (inoutReferences.size() <= inSlot)
		{
			inoutReferences.resize(static_cast<size_t>(inSlot) + 1);
		}
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

void AttachmentDescription::CustomizeFormat(
	VkFormat inFormat,
	std::variant<std::pair<float, uint32_t>, glm::vec4> inClearValue)
{
	CHECK_TRUE(inFormat != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");

	m_description.format = inFormat;
	m_clearValue = VkClearValue{};

	if (std::holds_alternative<glm::vec4>(inClearValue))
	{
		const glm::vec4 clearColor = std::get<glm::vec4>(inClearValue);
		m_clearValue.color = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
	}
	else
	{
		const auto clearDepthStencil = std::get<std::pair<float, uint32_t>>(inClearValue);
		m_clearValue.depthStencil = { clearDepthStencil.first, clearDepthStencil.second };
	}
}

void AttachmentDescription::CustomizeFormat(VkFormat inFormat, const VkClearValue& inClearValue)
{
	CHECK_TRUE(inFormat != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
	m_description.format = inFormat;
	m_clearValue = inClearValue;
}

void AttachmentDescription::CustomizeSampleCount(VkSampleCountFlagBits inSampleCount)
{
	m_description.samples = inSampleCount;
}

void AttachmentDescription::CustomizeLoadOperation(VkAttachmentLoadOp inLoadOp)
{
	m_description.loadOp = inLoadOp;
}

void AttachmentDescription::CustomizeStoreOperation(VkAttachmentStoreOp inStoreOp)
{
	m_description.storeOp = inStoreOp;
}

void AttachmentDescription::CustomizeStencilStoreLoadOperation(
	VkAttachmentLoadOp inLoadOp,
	VkAttachmentStoreOp inStoreOp)
{
	m_description.stencilLoadOp = inLoadOp;
	m_description.stencilStoreOp = inStoreOp;
}

void AttachmentDescription::CustomizeInitialLayout(VkImageLayout inLayout)
{
	m_description.initialLayout = inLayout;
}

void AttachmentDescription::CustomizeFinalLayout(VkImageLayout inLayout)
{
	m_description.finalLayout = inLayout;
}

void SubpassDescription::AddDepthStencilAttachment(
	std::string_view inAttachmentName,
	VkImageLayout inFinalLayout)
{
	m_depthStencilAttachment = AttachmentReference{
		_CopyName(inAttachmentName, "Depth stencil attachment name cannot be empty!"),
		inFinalLayout,
		true
	};
}

void SubpassDescription::AddResolvedAttachment(
	uint32_t inOutputSlot,
	std::string_view inMultiSampleAttachmentName,
	std::string_view in1SampleAttachmentName,
	VkImageLayout inMultiSampleLayout,
	VkImageLayout in1SampleLayout)
{
	AddColorAttachment(inOutputSlot, inMultiSampleAttachmentName, inMultiSampleLayout);
	_ResizeNamedAttachmentReferenceVector(m_resolveAttachments, inOutputSlot);
	m_resolveAttachments[inOutputSlot] = AttachmentReference{
		_CopyName(in1SampleAttachmentName, "Resolve attachment name cannot be empty!"),
		in1SampleLayout,
		true
	};

	if (m_resolveAttachments.size() < m_colorAttachments.size())
	{
		m_resolveAttachments.resize(m_colorAttachments.size());
	}
}

void SubpassDescription::AddColorAttachment(
	uint32_t inOutputSlot,
	std::string_view inColorAttachmentName,
	VkImageLayout inColorLayout)
{
	_ResizeNamedAttachmentReferenceVector(m_colorAttachments, inOutputSlot);
	m_colorAttachments[inOutputSlot] = AttachmentReference{
		_CopyName(inColorAttachmentName, "Color attachment name cannot be empty!"),
		inColorLayout,
		true
	};

	if (!m_resolveAttachments.empty() && m_resolveAttachments.size() < m_colorAttachments.size())
	{
		m_resolveAttachments.resize(m_colorAttachments.size());
	}
}

void SubpassDescription::AddPreserveAttachment(std::string_view inAttachmentName)
{
	const std::string name = _CopyName(inAttachmentName, "Preserve attachment name cannot be empty!");
	CHECK_TRUE(
		std::find(m_preserveAttachments.begin(), m_preserveAttachments.end(), name) == m_preserveAttachments.end(),
		"Preserve attachment is already present!");
	m_preserveAttachments.push_back(name);
}

void SubpassDescription::CustomizeAvailableState(
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess)
{
	m_availableStage = inStage;
	m_availableAccess = inAccess;
}

void SubpassDescription::AddDependencyOnSubpass(
	std::string_view inSrcSubpassName,
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess,
	VkDependencyFlags inDependencyFlags)
{
	CHECK_TRUE(!inSrcSubpassName.empty(), "Dependency source subpass name cannot be empty!");
	CHECK_TRUE(inStage != 0, "Dependency destination stage cannot be empty!");

	m_dependencies.push_back(Dependency{
		std::string(inSrcSubpassName),
		false,
		false,
		inStage,
		inAccess,
		inDependencyFlags
	});
}

void SubpassDescription::AddExternalDependency(
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess,
	VkDependencyFlags inDependencyFlags)
{
	CHECK_TRUE(inStage != 0, "Dependency destination stage cannot be empty!");

	m_dependencies.push_back(Dependency{
		{},
		true,
		false,
		inStage,
		inAccess,
		inDependencyFlags
	});
}

void SubpassDescription::AllowLocalPipelineBarrier(
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess,
	VkDependencyFlags inDependencyFlags)
{
	CHECK_TRUE(inStage != 0, "Local pipeline barrier stage cannot be empty!");

	m_dependencies.push_back(Dependency{
		{},
		false,
		true,
		inStage,
		inAccess,
		inDependencyFlags
	});
}

void RenderPassCreateInfo::AddSubpass(std::string_view inName, const SubpassDescription& inSubpass)
{
	const std::string name = _CopyName(inName, "Subpass name cannot be empty!");
	CHECK_TRUE(!m_subpassNameToIndex.contains(name), "Subpass name already exists!");

	const uint32_t index = static_cast<uint32_t>(m_subpasses.size());
	m_subpasses.push_back(inSubpass);
	m_subpassNameToIndex.emplace(name, index);
}

void RenderPassCreateInfo::AddAttachment(std::string_view inName, const AttachmentDescription& inAttachment)
{
	const std::string name = _CopyName(inName, "Attachment name cannot be empty!");
	CHECK_TRUE(!m_attachmentNameToIndex.contains(name), "Attachment name already exists!");
	CHECK_TRUE(inAttachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format must be customized before adding it!");

	const uint32_t index = static_cast<uint32_t>(m_attachments.size());
	m_attachments.push_back(inAttachment);
	m_attachmentNameToIndex.emplace(name, index);
}

auto RenderPassCreateInfo::_GetAttachmentIndex(std::string_view inName) const -> uint32_t
{
	const auto iter = m_attachmentNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_attachmentNameToIndex.end(), "Attachment name does not exist!");

	return iter->second;
}

auto RenderPassCreateInfo::_GetSubpassIndex(std::string_view inName) const -> uint32_t
{
	const auto iter = m_subpassNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_subpassNameToIndex.end(), "Subpass name does not exist!");

	return iter->second;
}

RenderPass::~RenderPass()
{
	Destroy();
}

void RenderPass::Create(const RenderPassCreateInfo& inCreateInfo)
{
	CHECK_TRUE(!inCreateInfo.m_subpasses.empty(), "Render pass needs at least one subpass!");
	auto description = MakeIntrusive<VulkanDetail::RenderPassDescription>();

	std::vector<VkAttachmentDescription> vkAttachments;
	std::vector<VkSubpassDescription> vkSubpasses;
	std::vector<std::vector<VkAttachmentReference>> colorAttachments;
	std::vector<std::vector<VkAttachmentReference>> resolveAttachments;
	std::vector<VkAttachmentReference> depthStencilAttachments;
	std::vector<std::vector<uint32_t>> preserveAttachments;
	std::vector<VkSubpassDependency> dependencies;

	vkAttachments.reserve(inCreateInfo.m_attachments.size());
	description->clearValues.reserve(inCreateInfo.m_attachments.size());
	for (const auto& attachment : inCreateInfo.m_attachments)
	{
		CHECK_TRUE(attachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
		vkAttachments.push_back(attachment.m_description);
		description->clearValues.push_back(attachment.m_clearValue);
	}

	const auto resolveAttachmentReference =
		[&inCreateInfo](const SubpassDescription::AttachmentReference& inReference)
		{
			if (!inReference.isUsed)
			{
				return kUnusedAttachment;
			}

			return VkAttachmentReference{
				inCreateInfo._GetAttachmentIndex(inReference.attachmentName),
				inReference.layout
			};
		};

	colorAttachments.reserve(inCreateInfo.m_subpasses.size());
	resolveAttachments.reserve(inCreateInfo.m_subpasses.size());
	depthStencilAttachments.reserve(inCreateInfo.m_subpasses.size());
	preserveAttachments.reserve(inCreateInfo.m_subpasses.size());
	vkSubpasses.reserve(inCreateInfo.m_subpasses.size());

	for (const auto& subpass : inCreateInfo.m_subpasses)
	{
		if (!subpass.m_resolveAttachments.empty())
		{
			CHECK_TRUE(
				subpass.m_resolveAttachments.size() == subpass.m_colorAttachments.size(),
				"Resolve attachments must match color attachment count!");
		}

		auto& resolvedColorAttachments = colorAttachments.emplace_back();
		resolvedColorAttachments.reserve(subpass.m_colorAttachments.size());
		for (const auto& attachment : subpass.m_colorAttachments)
		{
			resolvedColorAttachments.push_back(resolveAttachmentReference(attachment));
		}

		auto& resolvedResolveAttachments = resolveAttachments.emplace_back();
		resolvedResolveAttachments.reserve(subpass.m_resolveAttachments.size());
		for (const auto& attachment : subpass.m_resolveAttachments)
		{
			resolvedResolveAttachments.push_back(resolveAttachmentReference(attachment));
		}

		depthStencilAttachments.push_back(
			subpass.m_depthStencilAttachment.has_value()
				? resolveAttachmentReference(subpass.m_depthStencilAttachment.value())
				: kUnusedAttachment);

		auto& resolvedPreserveAttachments = preserveAttachments.emplace_back();
		resolvedPreserveAttachments.reserve(subpass.m_preserveAttachments.size());
		for (const std::string& attachmentName : subpass.m_preserveAttachments)
		{
			resolvedPreserveAttachments.push_back(inCreateInfo._GetAttachmentIndex(attachmentName));
		}

		VkSubpassDescription vkSubpass{};
		vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		vkSubpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.back().size());
		vkSubpass.pColorAttachments = colorAttachments.back().data();
		if (!resolveAttachments.back().empty())
		{
			vkSubpass.pResolveAttachments = resolveAttachments.back().data();
		}
		if (subpass.m_depthStencilAttachment.has_value())
		{
			vkSubpass.pDepthStencilAttachment = &depthStencilAttachments.back();
		}
		vkSubpass.preserveAttachmentCount = static_cast<uint32_t>(resolvedPreserveAttachments.size());
		vkSubpass.pPreserveAttachments = resolvedPreserveAttachments.data();
		vkSubpasses.push_back(vkSubpass);
	}

	for (uint32_t dstSubpass = 0; dstSubpass < static_cast<uint32_t>(inCreateInfo.m_subpasses.size()); ++dstSubpass)
	{
		const auto& subpass = inCreateInfo.m_subpasses[dstSubpass];
		for (const auto& dependency : subpass.m_dependencies)
		{
			const bool isExternalDependency = dependency.isExternal;
			const bool isSelfDependency = dependency.isSelf;
			uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
			if (isSelfDependency)
			{
				srcSubpass = dstSubpass;
			}
			else if (!isExternalDependency)
			{
				srcSubpass = inCreateInfo._GetSubpassIndex(dependency.srcSubpassName);
			}
			const auto& sourceSubpass = (isExternalDependency || isSelfDependency)
				? subpass
				: inCreateInfo.m_subpasses[srcSubpass];

			VkSubpassDependency vkDependency{};
			vkDependency.srcSubpass = srcSubpass;
			vkDependency.dstSubpass = dstSubpass;
			vkDependency.srcStageMask = isSelfDependency
				? dependency.dstStage
				: isExternalDependency ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : sourceSubpass.m_availableStage;
			vkDependency.srcAccessMask = isSelfDependency
				? dependency.dstAccess
				: isExternalDependency ? 0 : sourceSubpass.m_availableAccess;
			vkDependency.dstStageMask = dependency.dstStage;
			vkDependency.dstAccessMask = dependency.dstAccess;
			vkDependency.dependencyFlags = dependency.dependencyFlags;
			dependencies.push_back(vkDependency);
		}
	}

	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
	renderPassInfo.pAttachments = vkAttachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(vkSubpasses.size());
	renderPassInfo.pSubpasses = vkSubpasses.data();
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	    std::vector<VulkanDetail::RenderPassDescription::SubpassInfo> subpassInfos;
    for (const auto& subpass : vkSubpasses)
    {
	        VulkanDetail::RenderPassDescription::SubpassInfo info;
        info.colorSlotCount = subpass.colorAttachmentCount;
        std::optional<VkSampleCountFlagBits> samples;
        const auto inspect = [&](const VkAttachmentReference& reference)
        {
            if (reference.attachment == VK_ATTACHMENT_UNUSED) return;
            const auto value = vkAttachments.at(reference.attachment).samples;
            CHECK_TRUE(!samples.has_value() || samples.value() == value, "Subpass attachment sample counts differ!");
            samples = value;
        };
        for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i) inspect(subpass.pColorAttachments[i]);
        info.hasDepth = subpass.pDepthStencilAttachment && subpass.pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED;
        if (info.hasDepth) inspect(*subpass.pDepthStencilAttachment);
        info.samples = samples.value_or(VK_SAMPLE_COUNT_1_BIT);
        subpassInfos.push_back(info);
    }
	description->subpassInfos = std::move(subpassInfos);
	description->attachmentNameToIndex = inCreateInfo.m_attachmentNameToIndex;
	description->attachments = vkAttachments;
	description->dependencies = dependencies;
	description->attachmentUsages.resize(vkAttachments.size());
	for (size_t index = 0; index < vkSubpasses.size(); ++index)
	{
		VulkanDetail::RenderPassDescription::Subpass subpass;
		subpass.colors = colorAttachments[index];
		subpass.resolves = resolveAttachments[index];
		if (vkSubpasses[index].pDepthStencilAttachment) subpass.depth = depthStencilAttachments[index];
		subpass.preserves = preserveAttachments[index];
		const auto addUsage = [&](const VkAttachmentReference& reference, VkImageUsageFlags usage)
		{
			if (reference.attachment != VK_ATTACHMENT_UNUSED) description->attachmentUsages.at(reference.attachment) |= usage;
		};
		for (const auto& reference : subpass.colors) addUsage(reference, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		for (const auto& reference : subpass.resolves) addUsage(reference, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		if (subpass.depth) addUsage(*subpass.depth, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
		description->subpasses.push_back(std::move(subpass));
	}
	auto resource = MyDevice::GetInstance().GetRenderPassAllocator()->AllocateRenderPass(renderPassInfo);
	VulkanDetail::RenderPassSnapshot snapshot{ std::move(resource), std::move(description) };
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
