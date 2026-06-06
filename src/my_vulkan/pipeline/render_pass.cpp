#include "render_pass.h"
#include "device.h"

namespace
{
	constexpr VkAttachmentReference kUnusedAttachment{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

	void ResizeAttachmentReferenceVector(
		std::vector<VkAttachmentReference>& inoutReferences,
		uint32_t inSlot)
	{
		if (inoutReferences.size() <= inSlot)
		{
			inoutReferences.resize(static_cast<size_t>(inSlot) + 1, kUnusedAttachment);
		}
	}
}

RenderPassCreateInfo::AttachmentDescription::AttachmentDescription()
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

void RenderPassCreateInfo::AttachmentDescription::CustomizeFormat(
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

void RenderPassCreateInfo::AttachmentDescription::CustomizeSampleCount(VkSampleCountFlagBits inSampleCount)
{
	m_description.samples = inSampleCount;
}

void RenderPassCreateInfo::AttachmentDescription::CustomizeLoadOperation(VkAttachmentLoadOp inLoadOp)
{
	m_description.loadOp = inLoadOp;
}

void RenderPassCreateInfo::AttachmentDescription::CustomizeStoreOperation(VkAttachmentStoreOp inStoreOp)
{
	m_description.storeOp = inStoreOp;
}

void RenderPassCreateInfo::AttachmentDescription::CustomizeStencilStoreLoadOperation(
	VkAttachmentLoadOp inLoadOp,
	VkAttachmentStoreOp inStoreOp)
{
	m_description.stencilLoadOp = inLoadOp;
	m_description.stencilStoreOp = inStoreOp;
}

void RenderPassCreateInfo::AttachmentDescription::CustomizeInitialLayout(VkImageLayout inLayout)
{
	m_description.initialLayout = inLayout;
}

void RenderPassCreateInfo::AttachmentDescription::CustomizeFinalLayout(VkImageLayout inLayout)
{
	m_description.finalLayout = inLayout;
}

void RenderPassCreateInfo::SubpassDescription::AddDepthStencilAttachment(
	RenderPassCreateInfo::AttachmentHandle inHandle,
	VkImageLayout inFinalLayout)
{
	m_depthStencilAttachment = VkAttachmentReference{ inHandle, inFinalLayout };
}

void RenderPassCreateInfo::SubpassDescription::AddResolvedAttachment(
	uint32_t inOutputSlot,
	RenderPassCreateInfo::AttachmentHandle inMultiSampleAttachmentHandle,
	VkImageLayout inMultiSampleLayout,
	RenderPassCreateInfo::AttachmentHandle in1SampleAttachmentHandle,
	VkImageLayout in1SampleLayout)
{
	AddColorAttachment(inOutputSlot, inMultiSampleAttachmentHandle, inMultiSampleLayout);
	ResizeAttachmentReferenceVector(m_resolveAttachments, inOutputSlot);
	m_resolveAttachments[inOutputSlot] = VkAttachmentReference{ in1SampleAttachmentHandle, in1SampleLayout };

	if (m_resolveAttachments.size() < m_colorAttachments.size())
	{
		m_resolveAttachments.resize(m_colorAttachments.size(), kUnusedAttachment);
	}
}

void RenderPassCreateInfo::SubpassDescription::AddColorAttachment(
	uint32_t inOutputSlot,
	RenderPassCreateInfo::AttachmentHandle inColorAttachmentHandle,
	VkImageLayout inColorLayout)
{
	ResizeAttachmentReferenceVector(m_colorAttachments, inOutputSlot);
	m_colorAttachments[inOutputSlot] = VkAttachmentReference{ inColorAttachmentHandle, inColorLayout };

	if (!m_resolveAttachments.empty() && m_resolveAttachments.size() < m_colorAttachments.size())
	{
		m_resolveAttachments.resize(m_colorAttachments.size(), kUnusedAttachment);
	}
}

void RenderPassCreateInfo::SubpassDescription::CustomizeAvailableState(
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess)
{
	m_availableStage = inStage;
	m_availableAccess = inAccess;
}

void RenderPassCreateInfo::SubpassDescription::AddDependencyOnSubpass(
	RenderPassCreateInfo::SubpassHandle inSrcSubpass,
	VkPipelineStageFlags inStage,
	VkAccessFlags inAccess)
{
	CHECK_TRUE(inStage != 0, "Dependency destination stage cannot be empty!");

	m_dependencies.push_back(Dependency{ inSrcSubpass, inStage, inAccess });
}

void RenderPassCreateInfo::SubpassDescription::AllowLocalPipelineBarrier()
{
	m_dependencyFlags |= VK_DEPENDENCY_BY_REGION_BIT;
}

auto RenderPassCreateInfo::AddSubpass(const SubpassDescription& inSubpass) -> RenderPassCreateInfo::SubpassHandle
{
	const SubpassHandle result = static_cast<SubpassHandle>(m_subpasses.size());
	m_subpasses.push_back(inSubpass);

	return result;
}

auto RenderPassCreateInfo::AddAttachment(const AttachmentDescription& inAttachment) -> RenderPassCreateInfo::AttachmentHandle
{
	CHECK_TRUE(inAttachment.m_description.format != VK_FORMAT_UNDEFINED, "Attachment format must be customized before adding it!");

	const AttachmentHandle result = static_cast<AttachmentHandle>(m_attachments.size());
	m_attachments.push_back(inAttachment);

	return result;
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

	const auto validateAttachmentHandle =
		[attachmentCount = static_cast<uint32_t>(inCreateInfo->m_attachments.size())](VkAttachmentReference inReference)
		{
			CHECK_TRUE(
				inReference.attachment == VK_ATTACHMENT_UNUSED || inReference.attachment < attachmentCount,
				"Attachment reference is out of range!");
		};

	colorAttachments.reserve(inCreateInfo->m_subpasses.size());
	resolveAttachments.reserve(inCreateInfo->m_subpasses.size());
	depthStencilAttachments.reserve(inCreateInfo->m_subpasses.size());
	vkSubpasses.reserve(inCreateInfo->m_subpasses.size());

	for (const auto& subpass : inCreateInfo->m_subpasses)
	{
		for (const auto& attachment : subpass.m_colorAttachments)
		{
			validateAttachmentHandle(attachment);
		}
		for (const auto& attachment : subpass.m_resolveAttachments)
		{
			validateAttachmentHandle(attachment);
		}
		if (subpass.m_depthStencilAttachment.has_value())
		{
			validateAttachmentHandle(subpass.m_depthStencilAttachment.value());
		}
		if (!subpass.m_resolveAttachments.empty())
		{
			CHECK_TRUE(
				subpass.m_resolveAttachments.size() == subpass.m_colorAttachments.size(),
				"Resolve attachments must match color attachment count!");
		}

		colorAttachments.push_back(subpass.m_colorAttachments);
		resolveAttachments.push_back(subpass.m_resolveAttachments);
		depthStencilAttachments.push_back(subpass.m_depthStencilAttachment.value_or(kUnusedAttachment));

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
		vkSubpasses.push_back(vkSubpass);
	}

	for (uint32_t dstSubpass = 0; dstSubpass < static_cast<uint32_t>(inCreateInfo->m_subpasses.size()); ++dstSubpass)
	{
		const auto& subpass = inCreateInfo->m_subpasses[dstSubpass];
		for (const auto& dependency : subpass.m_dependencies)
		{
			CHECK_TRUE(
				dependency.srcSubpass == VK_SUBPASS_EXTERNAL || dependency.srcSubpass < inCreateInfo->m_subpasses.size(),
				"Dependency source subpass is out of range!");

			const bool isExternalDependency = dependency.srcSubpass == VK_SUBPASS_EXTERNAL;
			const auto& sourceSubpass = isExternalDependency
				? subpass
				: inCreateInfo->m_subpasses[dependency.srcSubpass];

			VkSubpassDependency vkDependency{};
			vkDependency.srcSubpass = dependency.srcSubpass;
			vkDependency.dstSubpass = dstSubpass;
			vkDependency.srcStageMask = isExternalDependency ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : sourceSubpass.m_availableStage;
			vkDependency.srcAccessMask = isExternalDependency ? 0 : sourceSubpass.m_availableAccess;
			vkDependency.dstStageMask = dependency.dstStage;
			vkDependency.dstAccessMask = dependency.dstAccess;
			vkDependency.dependencyFlags = subpass.m_dependencyFlags;
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
}

void RenderPass::Destroy()
{
	if (m_vkRenderPass != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyRenderPass(m_vkRenderPass);
		m_vkRenderPass = VK_NULL_HANDLE;
	}
	m_clearValues.clear();
}
