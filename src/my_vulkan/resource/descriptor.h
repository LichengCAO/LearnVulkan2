#pragma once
#include "common.h"
#include "my_vulkan/intrusive_ptr.h"
#include "image.h"
#include <cstdint>
#include <map>
#include <variant>
#include <string_view>
class Buffer;
class BufferView;
class ImageView;
class Sampler;
class TopLevelAccelStruct;
class DescriptorSet;
class DescriptorSetLayout;
class DescriptorSetLayoutAllocator;
class DescriptorPool;
class PipelineLayout;
namespace VulkanDetail { class DynamicDescriptorPool; }

#include "pipeline/pipeline_layout_create_info.h"

class DescriptorSetLayout final
{
	// Pipeline can have multiple DescriptorSetLayout,
	// when execute the pipeline we need to provide DescriptorSets for each of the DescriptorSetLayout in order
	friend class DescriptorPool;
	friend class DescriptorSetLayoutAllocator;
	friend class VulkanDetail::DynamicDescriptorPool;
	friend class PipelineLayout;

private:
	std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_descriptorBindings;
	VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;

public:
	DescriptorSetLayout() = default;
	DescriptorSetLayout(const DescriptorSetLayout&) = default;
	auto operator=(const DescriptorSetLayout&) -> DescriptorSetLayout& = default;
	DescriptorSetLayout(DescriptorSetLayout&&) noexcept = default;
	auto operator=(DescriptorSetLayout&&) noexcept -> DescriptorSetLayout& = default;
	~DescriptorSetLayout();

	void Create(const DescriptorSetLayoutCreateInfo& inCreateInfo);

	void Destroy();

	auto GetVkDescriptorSetLayout() const -> VkDescriptorSetLayout;

	auto GetVkDescriptorSetLayoutBinding(uint32_t inBinding) const -> const VkDescriptorSetLayoutBinding&;
};

class DescriptorSetDesc final
{
	friend class CommandBuffer;
	friend struct ShaderBindingTestProbe;
	friend class ShaderResources;
	friend class DescriptorPool;
	friend class VulkanDetail::DynamicDescriptorPool;

	struct BufferBinding final
	{
		const Buffer* buffer = nullptr;
		VkDeviceSize offset = 0;
		VkDeviceSize range = VK_WHOLE_SIZE;
		auto operator==(const BufferBinding&) const -> bool = default;
	};

	struct ImageBinding final
	{
		ImageView imageView;
		const Sampler* sampler = nullptr;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		auto operator==(const ImageBinding&) const -> bool = default;
	};

public:
	using DescriptorBindingInfo = std::variant<
		BufferBinding,
		ImageBinding,
		const Sampler*,
		const BufferView*,
		const TopLevelAccelStruct*>;

private:
	struct Item final
	{
		uint32_t binding = 0;
		uint32_t arrayElement = 0;
		DescriptorBindingInfo resource{};
	};

	std::vector<Item> m_items;

	void _SetBinding(uint32_t inBinding, DescriptorBindingInfo inBindingInfo, uint32_t inArrayElement);

public:
	DescriptorSetDesc() = default;
	~DescriptorSetDesc() = default;
	DescriptorSetDesc(const DescriptorSetDesc&) = default;
	auto operator=(const DescriptorSetDesc&) -> DescriptorSetDesc& = default;
	DescriptorSetDesc(DescriptorSetDesc&& inOther) noexcept = default;
	auto operator=(DescriptorSetDesc&&) noexcept -> DescriptorSetDesc& = default;

	void SetBuffer(
		uint32_t inBinding,
		const Buffer& inBuffer,
		VkDeviceSize inOffset = 0,
		VkDeviceSize inRange = VK_WHOLE_SIZE,
		uint32_t inArrayElement = 0);
	void SetImage(
		uint32_t inBinding,
		const ImageView& inImageView,
		VkImageLayout inImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		uint32_t inArrayElement = 0);
	void SetCombinedImageSampler(
		uint32_t inBinding,
		const ImageView& inImageView,
		const Sampler& inSampler,
		VkImageLayout inImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		uint32_t inArrayElement = 0);
	void SetSampler(uint32_t inBinding, const Sampler& inSampler, uint32_t inArrayElement = 0);
	void SetBufferView(uint32_t inBinding, const BufferView& inBufferView, uint32_t inArrayElement = 0);
	void SetAccelerationStructure(uint32_t inBinding, const TopLevelAccelStruct& inAccelerationStructure, uint32_t inArrayElement = 0);
};

// Image views retain their native versions. Other referenced resources must survive GPU completion.
class ShaderResources final
{
	friend class CommandBuffer;
	friend struct ShaderBindingTestProbe;

private:
	struct Item
	{
		std::string name;
		uint32_t arrayElement = 0;
		DescriptorSetDesc::DescriptorBindingInfo resource;
	};
	std::vector<Item> m_items;
	void _Set(std::string_view inName, uint32_t inArrayElement, DescriptorSetDesc::DescriptorBindingInfo inResource);
	auto _Resolve(const PipelineLayout& inLayout) const -> std::vector<DescriptorSetDesc>;

public:
	void Clear();
	void SetBuffer(std::string_view inName, const Buffer& inBuffer, VkDeviceSize inOffset = 0,
	    VkDeviceSize inRange = VK_WHOLE_SIZE, uint32_t inArrayElement = 0);
	void SetImage(std::string_view inName, const ImageView& inImageView,
	    VkImageLayout inLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t inArrayElement = 0);
	void SetCombinedImageSampler(std::string_view inName, const ImageView& inImageView, const Sampler& inSampler,
	    VkImageLayout inLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t inArrayElement = 0);
	void SetSampler(std::string_view inName, const Sampler& inSampler, uint32_t inArrayElement = 0);
	void SetBufferView(std::string_view inName, const BufferView& inView, uint32_t inArrayElement = 0);
	void SetAccelerationStructure(
	    std::string_view inName, const TopLevelAccelStruct& inAccelerationStructure, uint32_t inArrayElement = 0);
};

// Owns multiple native pools. Create with MakeIntrusive; allocated sets retain
// their pool until they are released. Distinct sets may be allocated/released
// concurrently after Create. Create and access to the same set require
// caller-side exclusion. The last strong reference destroys the native pools.
class DescriptorPool final : public RefCounted
{
	friend class DescriptorSet;
private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	void _Free(VkDescriptorSet inSet) noexcept;
	~DescriptorPool() noexcept override;

public:
	DescriptorPool() noexcept;
	DescriptorPool(const DescriptorPool&) = delete;
	DescriptorPool(DescriptorPool&&) = delete;
	auto operator=(const DescriptorPool&) -> DescriptorPool& = delete;
	auto operator=(DescriptorPool&&) -> DescriptorPool& = delete;
	void Create();
	[[nodiscard]] auto Allocate(const DescriptorSetLayout& inLayout,
		const DescriptorSetDesc& inDesc) -> DescriptorSet;
};

// Owns one allocation and retains its pool. The caller completes GPU use before
// releasing the set; scope exit never waits. The native getter is borrowed.
class DescriptorSet final
{
private:
	friend class DescriptorPool;
	IntrusivePtr<DescriptorPool> m_pool;
	VkDescriptorSet m_vkDescriptorSet = VK_NULL_HANDLE;
	void _Release() noexcept;

public:
	DescriptorSet() noexcept = default;
	DescriptorSet(const DescriptorSet&) = delete;
	auto operator=(const DescriptorSet&) -> DescriptorSet& = delete;
	DescriptorSet(DescriptorSet&& inOther) noexcept;
	auto operator=(DescriptorSet&& inOther) noexcept -> DescriptorSet&;
	~DescriptorSet() noexcept;
	auto IsValid() const noexcept -> bool;
	auto GetVkDescriptorSet() const noexcept -> VkDescriptorSet;
};
