#pragma once
#include "common.h"

namespace VulkanDetail
{
struct DescriptorPoolAllocation
{
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

class DescriptorPoolPages final
{
public:
	DescriptorPoolPages() = default;
	~DescriptorPoolPages() noexcept;
	DescriptorPoolPages(const DescriptorPoolPages&) = delete;
	auto operator=(const DescriptorPoolPages&) -> DescriptorPoolPages& = delete;
	DescriptorPoolPages(DescriptorPoolPages&&) = delete;
	auto operator=(DescriptorPoolPages&&) -> DescriptorPoolPages& = delete;
	void Create(VkDescriptorPoolCreateFlags inFlags);
	auto Allocate(VkDescriptorSetLayout inLayout, const std::vector<VkDescriptorPoolSize>& inRequirements) -> DescriptorPoolAllocation;
	void Free(const DescriptorPoolAllocation& inAllocation);
	void Reset();
	void Destroy() noexcept;

private:
	struct Page
	{
		VkDescriptorPool pool = VK_NULL_HANDLE;
		std::vector<VkDescriptorPoolSize> budgets;
		bool available = true;
	};
	static constexpr uint32_t MaxSets = 1000;
	VkDevice m_device = VK_NULL_HANDLE;
	VkDescriptorPoolCreateFlags m_flags = 0;
	bool m_failed = false;
	std::vector<Page> m_pages;
	void _Validate() const;
	auto _Fits(const Page& inPage, const std::vector<VkDescriptorPoolSize>& inRequirements) const -> bool;
	auto _Allocate(const Page& inPage, VkDescriptorSetLayout inLayout, VkDescriptorSet& outSet) const -> VkResult;
	auto _CreatePage(const std::vector<VkDescriptorPoolSize>& inRequirements) -> Page;
};
}
