#include "render_pass_allocator.h"
#include "utility/hash_util.h"

#include <type_traits>

namespace
{
	template <class Enum>
	void HashEnum(size_t& seed, Enum value)
	{
		hash_combine(seed, static_cast<std::underlying_type_t<Enum>>(value));
	}

	void HashAttachmentReference(size_t& seed, const VkAttachmentReference& reference)
	{
		hash_combine(seed, reference.attachment);
		HashEnum(seed, reference.layout);
	}

	void HashAttachmentDescription(size_t& seed, const VkAttachmentDescription& attachment)
	{
		hash_combine(seed, attachment.flags);
		HashEnum(seed, attachment.format);
		HashEnum(seed, attachment.samples);
		HashEnum(seed, attachment.loadOp);
		HashEnum(seed, attachment.storeOp);
		HashEnum(seed, attachment.stencilLoadOp);
		HashEnum(seed, attachment.stencilStoreOp);
		HashEnum(seed, attachment.initialLayout);
		HashEnum(seed, attachment.finalLayout);
	}

	void HashSubpassDescription(size_t& seed, const VkSubpassDescription& subpass)
	{
		hash_combine(seed, subpass.flags);
		HashEnum(seed, subpass.pipelineBindPoint);
		hash_combine(seed, subpass.inputAttachmentCount);
		for (uint32_t i = 0; i < subpass.inputAttachmentCount; i++)
		{
			HashAttachmentReference(seed, subpass.pInputAttachments[i]);
		}

		hash_combine(seed, subpass.colorAttachmentCount);
		for (uint32_t i = 0; i < subpass.colorAttachmentCount; i++)
		{
			HashAttachmentReference(seed, subpass.pColorAttachments[i]);
		}

		hash_combine(seed, subpass.pResolveAttachments != nullptr);
		if (subpass.pResolveAttachments != nullptr)
		{
			for (uint32_t i = 0; i < subpass.colorAttachmentCount; i++)
			{
				HashAttachmentReference(seed, subpass.pResolveAttachments[i]);
			}
		}

		hash_combine(seed, subpass.pDepthStencilAttachment != nullptr);
		if (subpass.pDepthStencilAttachment != nullptr)
		{
			HashAttachmentReference(seed, *subpass.pDepthStencilAttachment);
		}

		hash_combine(seed, subpass.preserveAttachmentCount);
		for (uint32_t i = 0; i < subpass.preserveAttachmentCount; i++)
		{
			hash_combine(seed, subpass.pPreserveAttachments[i]);
		}
	}

	void HashSubpassDependency(size_t& seed, const VkSubpassDependency& dependency)
	{
		hash_combine(seed, dependency.srcSubpass);
		hash_combine(seed, dependency.dstSubpass);
		hash_combine(seed, dependency.srcStageMask);
		hash_combine(seed, dependency.dstStageMask);
		hash_combine(seed, dependency.srcAccessMask);
		hash_combine(seed, dependency.dstAccessMask);
		hash_combine(seed, dependency.dependencyFlags);
	}

	void HashRenderPassCreateInfoPNext(size_t& seed, const void* pNext)
	{
		const auto* current = static_cast<const VkBaseInStructure*>(pNext);
		while (current != nullptr)
		{
			HashEnum(seed, current->sType);

			switch (current->sType)
			{
			case VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO:
			{
				const auto* multiview = reinterpret_cast<const VkRenderPassMultiviewCreateInfo*>(current);

				hash_combine(seed, multiview->subpassCount);
				for (uint32_t i = 0; i < multiview->subpassCount; i++)
				{
					hash_combine(seed, multiview->pViewMasks[i]);
				}

				hash_combine(seed, multiview->dependencyCount);
				for (uint32_t i = 0; i < multiview->dependencyCount; i++)
				{
					hash_combine(seed, multiview->pViewOffsets[i]);
				}

				hash_combine(seed, multiview->correlationMaskCount);
				for (uint32_t i = 0; i < multiview->correlationMaskCount; i++)
				{
					hash_combine(seed, multiview->pCorrelationMasks[i]);
				}
			}
				break;

			default:
				CHECK_TRUE(false, "Unsupported render pass create info pNext!");
				break;
			}

			current = current->pNext;
		}
	}

	uint64_t HashRenderPassCreateInfo(const VkRenderPassCreateInfo& createInfo)
	{
		size_t result = 0;

		HashEnum(result, createInfo.sType);
		HashRenderPassCreateInfoPNext(result, createInfo.pNext);
		hash_combine(result, createInfo.flags);

		hash_combine(result, createInfo.attachmentCount);
		for (uint32_t i = 0; i < createInfo.attachmentCount; i++)
		{
			HashAttachmentDescription(result, createInfo.pAttachments[i]);
		}

		hash_combine(result, createInfo.subpassCount);
		for (uint32_t i = 0; i < createInfo.subpassCount; i++)
		{
			HashSubpassDescription(result, createInfo.pSubpasses[i]);
		}

		hash_combine(result, createInfo.dependencyCount);
		for (uint32_t i = 0; i < createInfo.dependencyCount; i++)
		{
			HashSubpassDependency(result, createInfo.pDependencies[i]);
		}

		return static_cast<uint64_t>(result);
	}
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

auto RenderPassAllocator::AllocateRenderPassWithResult(const VkRenderPassCreateInfo* inCreateInfo) -> std::pair<VkRenderPass, VkResult>
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Render pass allocator is not created!");
	CHECK_TRUE(inCreateInfo != nullptr, "Missing render pass create info!");

	const uint64_t hash = HashRenderPassCreateInfo(*inCreateInfo);
	const auto iter = m_mapHashToVkRenderPass.find(hash);
	if (iter != m_mapHashToVkRenderPass.end())
	{
		return { iter->second, VK_SUCCESS };
	}

	VkRenderPass renderPass = VK_NULL_HANDLE;
	const VkResult result = vkCreateRenderPass(m_vkDevice, inCreateInfo, nullptr, &renderPass);
	if (result != VK_SUCCESS)
	{
		return { VK_NULL_HANDLE, result };
	}

	m_mapHashToVkRenderPass.emplace(hash, renderPass);
	return { renderPass, VK_SUCCESS };
}

auto RenderPassAllocator::AllocateRenderPass(const VkRenderPassCreateInfo* inCreateInfo) -> VkRenderPass
{
	auto [renderPass, result] = AllocateRenderPassWithResult(inCreateInfo);
	VK_CHECK(result, "Failed to allocate render pass!");

	return renderPass;
}

void RenderPassAllocator::FreeRenderPass(VkRenderPass& inoutRenderPass)
{
	inoutRenderPass = VK_NULL_HANDLE;
}

void RenderPassAllocator::Destroy()
{
	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (auto& [hash, renderPass] : m_mapHashToVkRenderPass)
		{
			if (renderPass != VK_NULL_HANDLE)
			{
				vkDestroyRenderPass(m_vkDevice, renderPass, nullptr);
			}
		}
	}

	m_mapHashToVkRenderPass.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
