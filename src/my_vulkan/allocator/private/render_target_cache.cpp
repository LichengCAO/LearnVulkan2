#include "render_target_cache.h"
#include "pipeline/private/render_target_description.h"
#include "utility/hash_util.h"
#include <limits>
#include <type_traits>

namespace
{
	template <typename Handle>
	auto _HandleValue(Handle inHandle) -> uint64_t
	{
		if constexpr (std::is_pointer_v<Handle>) return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(inHandle));
		else return static_cast<uint64_t>(inHandle);
	}

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

	auto _FramebufferKey(VkRenderPass inRenderPass, const VulkanDetail::ResolvedFramebufferDesc& inDescription) -> std::vector<uint64_t>
	{
		CHECK_TRUE(inRenderPass != VK_NULL_HANDLE, "Framebuffer requires a valid render pass!");
		CHECK_TRUE(inDescription.attachments.size() == inDescription.attachmentIds.size(), "Framebuffer attachment identities do not match!");
		CHECK_TRUE(inDescription.attachments.size() <= std::numeric_limits<uint32_t>::max(), "Too many framebuffer attachments!");
		CHECK_TRUE(inDescription.extent.width > 0 && inDescription.extent.height > 0 && inDescription.layers > 0, "Framebuffer dimensions must be positive!");
		std::vector<uint64_t> key{ 0, _HandleValue(inRenderPass), inDescription.attachments.size(), inDescription.extent.width, inDescription.extent.height, inDescription.layers };
		for (size_t i = 0; i < inDescription.attachments.size(); ++i)
		{
			CHECK_TRUE(inDescription.attachmentIds[i] != 0 && inDescription.attachments[i] != VK_NULL_HANDLE, "Framebuffer attachment is invalid!");
			key.push_back(inDescription.attachmentIds[i]);
			key.push_back(_HandleValue(inDescription.attachments[i]));
		}
		return key;
	}
}

namespace VulkanDetail
{
auto RenderTargetCache::KeyHash::operator()(const std::vector<uint64_t>& inKey) const -> size_t
{
	size_t hash = 0;
	for (const auto field : inKey) hash_combine(hash, field);
	return hash;
}

RenderTargetCache::~RenderTargetCache() noexcept
{
	Destroy();
}

void RenderTargetCache::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid render target cache device!");
	CHECK_TRUE(m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice, "Render target cache already uses another device!");
	m_vkDevice = inDevice;
}

auto RenderTargetCache::GetOrCreateRenderPass(const RenderPassSnapshot& inSnapshot) -> VkRenderPass
{
	CHECK_TRUE(inSnapshot.description, "Render pass description must be finalized!");
	const auto& description = *inSnapshot.description;
	std::vector<VkSubpassDescription> subpasses;
	subpasses.reserve(description.subpasses.size());
	for (const auto& source : description.subpasses)
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
	VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	info.attachmentCount = static_cast<uint32_t>(description.attachments.size());
	info.pAttachments = description.attachments.data();
	info.subpassCount = static_cast<uint32_t>(subpasses.size());
	info.pSubpasses = subpasses.data();
	info.dependencyCount = static_cast<uint32_t>(description.dependencies.size());
	info.pDependencies = description.dependencies.data();
	return GetOrCreateRenderPass(info);
}

auto RenderTargetCache::GetOrCreateRenderPass(const VkRenderPassCreateInfo& inCreateInfo) -> VkRenderPass
{
	CHECK_TRUE(IsCreated(), "Render target cache is not created!");
	auto key = _RenderPassKey(inCreateInfo);
	const auto iterator = m_renderPasses.find(key);
	if (iterator != m_renderPasses.end()) return iterator->second;
	VkRenderPass handle = VK_NULL_HANDLE;
	VK_CHECK(vkCreateRenderPass(m_vkDevice, &inCreateInfo, nullptr, &handle), "Failed to create render pass!");
	try
	{
		const auto [published, inserted] = m_renderPasses.emplace(std::move(key), handle);
		CHECK_TRUE(inserted, "Render pass description is already cached!");
	}
	catch (...)
	{
		vkDestroyRenderPass(m_vkDevice, handle, nullptr);
		throw;
	}
	return handle;
}

auto RenderTargetCache::GetOrCreateFramebuffer(VkRenderPass inRenderPass, const ResolvedFramebufferDesc& inDescription) -> VkFramebuffer
{
	CHECK_TRUE(IsCreated(), "Render target cache is not created!");
	auto key = _FramebufferKey(inRenderPass, inDescription);
	const auto iterator = m_framebuffers.find(key);
	if (iterator != m_framebuffers.end()) return iterator->second;
	VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	info.renderPass = inRenderPass;
	info.attachmentCount = static_cast<uint32_t>(inDescription.attachments.size());
	info.pAttachments = inDescription.attachments.data();
	info.width = inDescription.extent.width;
	info.height = inDescription.extent.height;
	info.layers = inDescription.layers;
	VkFramebuffer handle = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFramebuffer(m_vkDevice, &info, nullptr, &handle), "Failed to create framebuffer!");
	try
	{
		const auto [published, inserted] = m_framebuffers.emplace(std::move(key), handle);
		CHECK_TRUE(inserted, "Framebuffer description is already cached!");
	}
	catch (...)
	{
		vkDestroyFramebuffer(m_vkDevice, handle, nullptr);
		throw;
	}
	return handle;
}

void RenderTargetCache::Destroy() noexcept
{
	// Each entry balances one successful creation, even when native values repeat.
	for (const auto& [key, handle] : m_framebuffers) vkDestroyFramebuffer(m_vkDevice, handle, nullptr);
	m_framebuffers.clear();
	for (const auto& [key, handle] : m_renderPasses) vkDestroyRenderPass(m_vkDevice, handle, nullptr);
	m_renderPasses.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
}
