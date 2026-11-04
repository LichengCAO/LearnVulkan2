#pragma once

#include "common.h"

class DescriptorSetLayout;
class DescriptorSetDesc;

namespace VulkanDetail
{
// Internal frame-owned allocation cache. Returned sets are borrowed until the
// next Reset/Destroy. The owner excludes concurrent access and completes all
// recording and GPU use before recycling this pool.
class DynamicDescriptorPool final
{
private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	auto _HashDescriptorSetDesc(const DescriptorSetDesc& inDesc) const -> size_t;

public:
	DynamicDescriptorPool() noexcept;
	DynamicDescriptorPool(const DynamicDescriptorPool&) = delete;
	DynamicDescriptorPool(DynamicDescriptorPool&&) = delete;
	auto operator=(const DynamicDescriptorPool&) -> DynamicDescriptorPool& = delete;
	auto operator=(DynamicDescriptorPool&&) -> DynamicDescriptorPool& = delete;
	~DynamicDescriptorPool() noexcept;
	void Create();
	void Reset();
	void Destroy();
	[[nodiscard]] auto GetOrAllocateDescriptorSet(const DescriptorSetLayout& inLayout,
		const DescriptorSetDesc& inDesc) -> VkDescriptorSet;
};
}
