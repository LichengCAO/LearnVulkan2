#include "render_pass_allocator.h"
#include "utility/hash_util.h"

namespace
{
	auto _RenderPassKey(const VkRenderPassCreateInfo& inInfo) -> std::vector<uint64_t>
	{
		CHECK_TRUE(inInfo.sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, "Invalid render pass create info!");
		std::vector<uint64_t> key{ inInfo.flags, inInfo.attachmentCount, inInfo.subpassCount, inInfo.dependencyCount };
		const auto reference = [&](const VkAttachmentReference& value)
		{
			key.push_back(value.attachment);
			key.push_back(value.layout);
		};
		for (uint32_t i = 0; i < inInfo.attachmentCount; ++i)
		{
			const auto& a = inInfo.pAttachments[i];
			key.insert(key.end(), { a.flags, static_cast<uint64_t>(a.format), static_cast<uint64_t>(a.samples), static_cast<uint64_t>(a.loadOp), static_cast<uint64_t>(a.storeOp), static_cast<uint64_t>(a.stencilLoadOp), static_cast<uint64_t>(a.stencilStoreOp), static_cast<uint64_t>(a.initialLayout), static_cast<uint64_t>(a.finalLayout) });
		}
		for (uint32_t i = 0; i < inInfo.subpassCount; ++i)
		{
			const auto& s = inInfo.pSubpasses[i];
			key.insert(key.end(), { s.flags, static_cast<uint64_t>(s.pipelineBindPoint), s.inputAttachmentCount, s.colorAttachmentCount, static_cast<uint64_t>(s.pResolveAttachments != nullptr), static_cast<uint64_t>(s.pDepthStencilAttachment != nullptr), s.preserveAttachmentCount });
			for (uint32_t j = 0; j < s.inputAttachmentCount; ++j) reference(s.pInputAttachments[j]);
			for (uint32_t j = 0; j < s.colorAttachmentCount; ++j) reference(s.pColorAttachments[j]);
			if (s.pResolveAttachments) for (uint32_t j = 0; j < s.colorAttachmentCount; ++j) reference(s.pResolveAttachments[j]);
			if (s.pDepthStencilAttachment) reference(*s.pDepthStencilAttachment);
			for (uint32_t j = 0; j < s.preserveAttachmentCount; ++j) key.push_back(s.pPreserveAttachments[j]);
		}
		for (uint32_t i = 0; i < inInfo.dependencyCount; ++i)
		{
			const auto& d = inInfo.pDependencies[i];
			key.insert(key.end(), { d.srcSubpass, d.dstSubpass, d.srcStageMask, d.dstStageMask, d.srcAccessMask, d.dstAccessMask, d.dependencyFlags });
		}
		for (auto* extension = static_cast<const VkBaseInStructure*>(inInfo.pNext); extension; extension = extension->pNext)
		{
			CHECK_TRUE(extension->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO, "Unsupported render pass create info pNext!");
			const auto& m = *reinterpret_cast<const VkRenderPassMultiviewCreateInfo*>(extension);
			key.insert(key.end(), { static_cast<uint64_t>(extension->sType), m.subpassCount, m.dependencyCount, m.correlationMaskCount });
			for (uint32_t i = 0; i < m.subpassCount; ++i) key.push_back(m.pViewMasks[i]);
			for (uint32_t i = 0; i < m.dependencyCount; ++i) key.push_back(static_cast<uint64_t>(static_cast<int64_t>(m.pViewOffsets[i])));
			for (uint32_t i = 0; i < m.correlationMaskCount; ++i) key.push_back(m.pCorrelationMasks[i]);
		}
		return key;
	}
}

auto RenderPassAllocator::KeyHash::operator()(const std::vector<uint64_t>& inKey) const -> size_t
{
	size_t hash = 0;
	for (const auto field : inKey) hash_combine(hash, field);
	return hash;
}

RenderPassAllocator::~RenderPassAllocator()
{
	Destroy();
}

void RenderPassAllocator::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid render pass allocator device!");
	CHECK_TRUE(m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice, "Render pass allocator already uses another device!");
	m_vkDevice = inDevice;
}

auto RenderPassAllocator::AllocateRenderPassWithResult(const VkRenderPassCreateInfo& inCreateInfo) -> std::pair<IntrusivePtr<VulkanDetail::RenderPassResource>, VkResult>
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Render pass allocator is not created!");
	auto key = _RenderPassKey(inCreateInfo);
	const auto iterator = m_renderPasses.find(key);
	if (iterator != m_renderPasses.end()) return { iterator->second, VK_SUCCESS };
	VkRenderPass handle = VK_NULL_HANDLE;
	const VkResult result = vkCreateRenderPass(m_vkDevice, &inCreateInfo, nullptr, &handle);
	if (result != VK_SUCCESS) return { {}, result };
	IntrusivePtr<VulkanDetail::RenderPassResource> resource;
	try
	{
		resource = MakeIntrusive<VulkanDetail::RenderPassResource>(handle, [device = m_vkDevice](VkRenderPass value) { vkDestroyRenderPass(device, value, nullptr); });
	}
	catch (...)
	{
		vkDestroyRenderPass(m_vkDevice, handle, nullptr);
		throw;
	}
	m_renderPasses.emplace(std::move(key), resource);
	return { std::move(resource), VK_SUCCESS };
}

auto RenderPassAllocator::AllocateRenderPass(const VkRenderPassCreateInfo& inCreateInfo) -> IntrusivePtr<VulkanDetail::RenderPassResource>
{
	auto [resource, result] = AllocateRenderPassWithResult(inCreateInfo);
	VK_CHECK(result, "Failed to allocate render pass!");
	return resource;
}

void RenderPassAllocator::Destroy()
{
	m_renderPasses.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
