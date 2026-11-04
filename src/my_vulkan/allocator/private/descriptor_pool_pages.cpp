#include "descriptor_pool_pages.h"
#include "../../device.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace VulkanDetail
{
DescriptorPoolPages::~DescriptorPoolPages() noexcept
{
	Destroy();
}

void DescriptorPoolPages::Create(VkDescriptorPoolCreateFlags inFlags)
{
	if (m_device != VK_NULL_HANDLE) throw std::logic_error("Descriptor pool pages already created");
	if (inFlags != 0 && inFlags != VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
		throw std::invalid_argument("Unsupported descriptor pool flags");
	const auto device = MyDevice::GetInstance().vkDevice;
	if (device == VK_NULL_HANDLE) throw std::logic_error("Descriptor pools require a device");
	m_device = device;
	m_flags = inFlags;
	m_failed = false;
}

void DescriptorPoolPages::_Validate() const
{
	if (m_device == VK_NULL_HANDLE || m_failed)
		throw std::logic_error("Descriptor pool pages are not usable");
	if (MyDevice::GetInstance().vkDevice != m_device)
		throw std::logic_error("Descriptor pool device changed");
}

auto DescriptorPoolPages::_Fits(const Page& inPage, const std::vector<VkDescriptorPoolSize>& inRequirements) const -> bool
{
	for (const auto& requirement : inRequirements)
	{
		const auto found = std::find_if(inPage.budgets.begin(), inPage.budgets.end(),
			[&](const auto& budget) -> bool { return budget.type == requirement.type; });
		if (found == inPage.budgets.end() || found->descriptorCount < requirement.descriptorCount) return false;
	}
	return true;
}

auto DescriptorPoolPages::_Allocate(const Page& inPage, VkDescriptorSetLayout inLayout, VkDescriptorSet& outSet) const -> VkResult
{
	VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	info.descriptorPool = inPage.pool;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &inLayout;
	return vkAllocateDescriptorSets(m_device, &info, &outSet);
}

auto DescriptorPoolPages::_CreatePage(const std::vector<VkDescriptorPoolSize>& inRequirements) -> Page
{
	Page page;
	page.budgets = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 500 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 500 }
	};
	if (vkCreateAccelerationStructureKHR != nullptr)
		page.budgets.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1000 });
	for (const auto& requirement : inRequirements)
	{
		const auto found = std::find_if(page.budgets.begin(), page.budgets.end(),
			[&](const auto& budget) -> bool { return budget.type == requirement.type; });
		if (found == page.budgets.end()) page.budgets.push_back(requirement);
		else found->descriptorCount = std::max(found->descriptorCount, requirement.descriptorCount);
	}
	VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	info.flags = m_flags;
	info.maxSets = MaxSets;
	info.poolSizeCount = static_cast<uint32_t>(page.budgets.size());
	info.pPoolSizes = page.budgets.data();
	VK_CHECK(vkCreateDescriptorPool(m_device, &info, nullptr, &page.pool), "Failed to create descriptor pool");
	return page;
}

auto DescriptorPoolPages::Allocate(VkDescriptorSetLayout inLayout, const std::vector<VkDescriptorPoolSize>& inRequirements) -> DescriptorPoolAllocation
{
	_Validate();
	if (inLayout == VK_NULL_HANDLE) throw std::invalid_argument("Descriptor set layout is null");
	for (auto& page : m_pages)
	{
		if (!page.available || !_Fits(page, inRequirements)) continue;
		VkDescriptorSet set = VK_NULL_HANDLE;
		const auto result = _Allocate(page, inLayout, set);
		if (result == VK_SUCCESS) return { page.pool, set };
		if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
			VK_CHECK(result, "Failed to allocate descriptor set");
		page.available = false;
	}
	auto page = _CreatePage(inRequirements);
	try { m_pages.push_back(std::move(page)); }
	catch (...) { vkDestroyDescriptorPool(m_device, page.pool, nullptr); throw; }
	auto& fresh = m_pages.back();
	VkDescriptorSet set = VK_NULL_HANDLE;
	const auto result = _Allocate(fresh, inLayout, set);
	if (result != VK_SUCCESS)
	{
		vkDestroyDescriptorPool(m_device, fresh.pool, nullptr);
		m_pages.pop_back();
		VK_CHECK(result, "Failed to allocate descriptor set from a fresh pool");
	}
	return { fresh.pool, set };
}

void DescriptorPoolPages::Free(const DescriptorPoolAllocation& inAllocation)
{
	_Validate();
	if ((m_flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0)
		throw std::logic_error("Descriptor pool does not support individual frees");
	const auto found = std::find_if(m_pages.begin(), m_pages.end(),
		[&](const auto& page) -> bool { return page.pool == inAllocation.pool; });
	if (found == m_pages.end() || inAllocation.descriptorSet == VK_NULL_HANDLE)
		throw std::invalid_argument("Invalid descriptor pool allocation");
	VK_CHECK(MyDevice::GetInstance().FreeDescriptorSet(inAllocation.pool, inAllocation.descriptorSet), "Failed to free descriptor set");
	found->available = true;
}

void DescriptorPoolPages::Reset()
{
	_Validate();
	m_failed = true;
	for (auto& page : m_pages)
	{
		VK_CHECK(vkResetDescriptorPool(m_device, page.pool, 0), "Failed to reset descriptor pool");
		page.available = true;
	}
	m_failed = false;
}

void DescriptorPoolPages::Destroy() noexcept
{
	for (const auto& page : m_pages) vkDestroyDescriptorPool(m_device, page.pool, nullptr);
	m_pages.clear();
	m_device = VK_NULL_HANDLE;
	m_flags = 0;
	m_failed = false;
}
}
