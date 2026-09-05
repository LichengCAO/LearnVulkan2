#include "render_pass.h"
#include "device.h"
#include "resource/image.h"

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

auto RenderPassCreateInfo::GetAttachmentIndex(std::string_view inName) const -> uint32_t
{
	const auto iter = m_attachmentNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_attachmentNameToIndex.end(), "Attachment name does not exist!");

	return iter->second;
}

auto RenderPassCreateInfo::GetSubpassIndex(std::string_view inName) const -> uint32_t
{
	const auto iter = m_subpassNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_subpassNameToIndex.end(), "Subpass name does not exist!");

	return iter->second;
}

RenderPass::~RenderPass()
{
	assert(m_vkRenderPass == VK_NULL_HANDLE);
}

void RenderPass::Create(const RenderPassCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No render pass create info!");
	CHECK_TRUE(m_vkRenderPass == VK_NULL_HANDLE, "Render pass already created!");
	CHECK_TRUE(!inCreateInfo->m_attachments.empty(), "Render pass needs at least one attachment!");
	CHECK_TRUE(!inCreateInfo->m_subpasses.empty(), "Render pass needs at least one subpass!");

	std::vector<VkAttachmentDescription> vkAttachments;
	std::vector<VkSubpassDescription> vkSubpasses;
	std::vector<std::vector<VkAttachmentReference>> colorAttachments;
	std::vector<std::vector<VkAttachmentReference>> resolveAttachments;
	std::vector<VkAttachmentReference> depthStencilAttachments;
	std::vector<std::vector<uint32_t>> preserveAttachments;
	std::vector<VkSubpassDependency> dependencies;

	vkAttachments.reserve(inCreateInfo->m_attachments.size());
	m_clearValues.clear();
	m_clearValues.reserve(inCreateInfo->m_attachments.size());
	for (const auto& attachment : inCreateInfo->m_attachments)
	{
		CHECK_TRUE(attachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format cannot be undefined!");
		vkAttachments.push_back(attachment.m_description);
		m_clearValues.push_back(attachment.m_clearValue);
	}

	const auto resolveAttachmentReference =
		[inCreateInfo](const SubpassDescription::AttachmentReference& inReference)
		{
			if (!inReference.isUsed)
			{
				return kUnusedAttachment;
			}

			return VkAttachmentReference{
				inCreateInfo->GetAttachmentIndex(inReference.attachmentName),
				inReference.layout
			};
		};

	colorAttachments.reserve(inCreateInfo->m_subpasses.size());
	resolveAttachments.reserve(inCreateInfo->m_subpasses.size());
	depthStencilAttachments.reserve(inCreateInfo->m_subpasses.size());
	preserveAttachments.reserve(inCreateInfo->m_subpasses.size());
	vkSubpasses.reserve(inCreateInfo->m_subpasses.size());

	for (const auto& subpass : inCreateInfo->m_subpasses)
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
			resolvedPreserveAttachments.push_back(inCreateInfo->GetAttachmentIndex(attachmentName));
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

	for (uint32_t dstSubpass = 0; dstSubpass < static_cast<uint32_t>(inCreateInfo->m_subpasses.size()); ++dstSubpass)
	{
		const auto& subpass = inCreateInfo->m_subpasses[dstSubpass];
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
				srcSubpass = inCreateInfo->GetSubpassIndex(dependency.srcSubpassName);
			}
			const auto& sourceSubpass = (isExternalDependency || isSelfDependency)
				? subpass
				: inCreateInfo->m_subpasses[srcSubpass];

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

	m_vkRenderPass = MyDevice::GetInstance().CreateRenderPass(renderPassInfo);
	m_attachmentNameToIndex = inCreateInfo->m_attachmentNameToIndex;
}

void RenderPass::Destroy()
{
	if (m_vkRenderPass != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyRenderPass(m_vkRenderPass);
		m_vkRenderPass = VK_NULL_HANDLE;
	}
	m_clearValues.clear();
	m_attachmentNameToIndex.clear();
}

auto RenderPass::GetAttachmentIndex(std::string_view inName) const -> uint32_t
{
	const auto iter = m_attachmentNameToIndex.find(std::string(inName));
	CHECK_TRUE(iter != m_attachmentNameToIndex.end(), "Render pass attachment name does not exist!");

	return iter->second;
}

void FramebufferCreateInfo::SetImageView(std::string_view inTargetAttachmentName, const ImageView* inViewAttached)
{
	CHECK_TRUE(m_renderPass != nullptr, "Framebuffer render pass must be set before image views!");
	CHECK_TRUE(inViewAttached != nullptr, "No image view attached to framebuffer!");

	const uint32_t attachmentIndex = m_renderPass->GetAttachmentIndex(inTargetAttachmentName);
	if (m_imageViews.size() <= attachmentIndex)
	{
		m_imageViews.resize(static_cast<size_t>(attachmentIndex) + 1, VK_NULL_HANDLE);
	}

	const auto& viewInfo = inViewAttached->GetImageViewInformation();
	const uint32_t layerCount = viewInfo.layerCount == VK_REMAINING_ARRAY_LAYERS ? 1 : viewInfo.layerCount;
	const VkExtent2D viewExtent{
		std::max(1u, viewInfo.width >> viewInfo.baseMipLevel),
		std::max(1u, viewInfo.height >> viewInfo.baseMipLevel)
	};
	if (std::find_if(m_imageViews.begin(), m_imageViews.end(), [](VkImageView inView) { return inView != VK_NULL_HANDLE; }) != m_imageViews.end())
	{
		CHECK_TRUE(m_extent.width == viewExtent.width && m_extent.height == viewExtent.height, "Framebuffer image views must have identical extents!");
		CHECK_TRUE(m_layers == layerCount, "Framebuffer image views must have identical layer counts!");
	}
	m_imageViews[attachmentIndex] = inViewAttached->GetVkImageView();
	m_extent = viewExtent;
	m_layers = layerCount;
}

void FramebufferCreateInfo::SetRenderPass(const RenderPass* inRenderPass)
{
	CHECK_TRUE(inRenderPass != nullptr, "No render pass for framebuffer!");

	m_renderPass = inRenderPass;
}

Framebuffer::~Framebuffer()
{
	assert(m_vkFramebuffer == VK_NULL_HANDLE);
}

void Framebuffer::Create(const FramebufferCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No framebuffer create info!");
	CHECK_TRUE(inCreateInfo->m_renderPass != nullptr, "No render pass for framebuffer!");
	CHECK_TRUE(inCreateInfo->m_renderPass->GetVkRenderPass() != VK_NULL_HANDLE, "Invalid render pass for framebuffer!");
	CHECK_TRUE(!inCreateInfo->m_imageViews.empty(), "Framebuffer needs at least one image view!");
	CHECK_TRUE(inCreateInfo->m_extent.width > 0 && inCreateInfo->m_extent.height > 0, "Framebuffer extent cannot be empty!");

	for (VkImageView imageView : inCreateInfo->m_imageViews)
	{
		CHECK_TRUE(imageView != VK_NULL_HANDLE, "Framebuffer image view is missing!");
	}

	VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	framebufferInfo.renderPass = inCreateInfo->m_renderPass->GetVkRenderPass();
	framebufferInfo.attachmentCount = static_cast<uint32_t>(inCreateInfo->m_imageViews.size());
	framebufferInfo.pAttachments = inCreateInfo->m_imageViews.data();
	framebufferInfo.width = inCreateInfo->m_extent.width;
	framebufferInfo.height = inCreateInfo->m_extent.height;
	framebufferInfo.layers = inCreateInfo->m_layers;

	m_vkFramebuffer = MyDevice::GetInstance().CreateFramebuffer(framebufferInfo);
}

void Framebuffer::Destroy()
{
	if (m_vkFramebuffer != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyFramebuffer(m_vkFramebuffer);
		m_vkFramebuffer = VK_NULL_HANDLE;
	}
}

VkFramebuffer Framebuffer::GetVkFramebuffer() const
{
	return m_vkFramebuffer;
}
