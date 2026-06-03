#include "framebuffer_allocator.h"
#include "utility/hash_util.h"

#include <type_traits>

namespace
{
	template <class Enum>
	void HashEnum(size_t& seed, Enum value)
	{
		hash_combine(seed, static_cast<std::underlying_type_t<Enum>>(value));
	}

	void HashFramebufferAttachmentsCreateInfo(size_t& seed, const VkFramebufferAttachmentsCreateInfo& attachmentsInfo)
	{
		hash_combine(seed, attachmentsInfo.attachmentImageInfoCount);
		for (uint32_t i = 0; i < attachmentsInfo.attachmentImageInfoCount; i++)
		{
			const VkFramebufferAttachmentImageInfo& imageInfo = attachmentsInfo.pAttachmentImageInfos[i];

			HashEnum(seed, imageInfo.sType);
			hash_combine(seed, imageInfo.flags);
			hash_combine(seed, imageInfo.usage);
			hash_combine(seed, imageInfo.width);
			hash_combine(seed, imageInfo.height);
			hash_combine(seed, imageInfo.layerCount);
			hash_combine(seed, imageInfo.viewFormatCount);
			for (uint32_t j = 0; j < imageInfo.viewFormatCount; j++)
			{
				HashEnum(seed, imageInfo.pViewFormats[j]);
			}
		}
	}

	void HashFramebufferCreateInfoPNext(size_t& seed, const void* pNext)
	{
		const auto* current = static_cast<const VkBaseInStructure*>(pNext);
		while (current != nullptr)
		{
			HashEnum(seed, current->sType);

			switch (current->sType)
			{
			case VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO:
			{
				const auto* attachmentsInfo = reinterpret_cast<const VkFramebufferAttachmentsCreateInfo*>(current);
				HashFramebufferAttachmentsCreateInfo(seed, *attachmentsInfo);
			}
				break;

			default:
				CHECK_TRUE(false, "Unsupported framebuffer create info pNext!");
				break;
			}

			current = current->pNext;
		}
	}

	uint64_t HashFramebufferCreateInfo(const VkFramebufferCreateInfo& createInfo)
	{
		size_t result = 0;

		HashEnum(result, createInfo.sType);
		HashFramebufferCreateInfoPNext(result, createInfo.pNext);
		hash_combine(result, createInfo.flags);
		hash_combine(result, createInfo.renderPass);
		hash_combine(result, createInfo.attachmentCount);
		for (uint32_t i = 0; i < createInfo.attachmentCount; i++)
		{
			hash_combine(result, createInfo.pAttachments[i]);
		}
		hash_combine(result, createInfo.width);
		hash_combine(result, createInfo.height);
		hash_combine(result, createInfo.layers);

		return static_cast<uint64_t>(result);
	}
}

FramebufferAllocator::~FramebufferAllocator()
{
	Destroy();
}

void FramebufferAllocator::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid framebuffer allocator device!");
	CHECK_TRUE(m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice, "Framebuffer allocator already uses another device!");

	m_vkDevice = inDevice;
}

bool FramebufferAllocator::AllocateFramebuffer(const VkFramebufferCreateInfo* inCreateInfo, VkFramebuffer& outFramebuffer)
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Framebuffer allocator is not created!");
	CHECK_TRUE(inCreateInfo != nullptr, "Missing framebuffer create info!");

	const uint64_t hash = HashFramebufferCreateInfo(*inCreateInfo);
	const auto iter = m_mapHashToVkFramebuffer.find(hash);
	if (iter != m_mapHashToVkFramebuffer.end())
	{
		outFramebuffer = iter->second;
		return true;
	}

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VK_CHECK(vkCreateFramebuffer(m_vkDevice, inCreateInfo, nullptr, &framebuffer));

	m_mapHashToVkFramebuffer.emplace(hash, framebuffer);
	outFramebuffer = framebuffer;
	return true;
}

void FramebufferAllocator::FreeFramebuffer(VkFramebuffer& inoutFramebuffer)
{
	inoutFramebuffer = VK_NULL_HANDLE;
}

void FramebufferAllocator::Destroy()
{
	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (auto& [hash, framebuffer] : m_mapHashToVkFramebuffer)
		{
			if (framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(m_vkDevice, framebuffer, nullptr);
			}
		}
	}

	m_mapHashToVkFramebuffer.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
