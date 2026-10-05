#pragma once
#include "common.h"
#include <cstdint>
#include <map>
#include <variant>
class Buffer;
class BufferView;
class ImageView;
class Sampler;
class TopLevelAccelStruct;
class DescriptorSet;
class DescriptorSetLayout;
class DescriptorSetLayoutAllocator;
class DescriptorSetAllocator;
class DynamicDescriptorSetAllocator;

class DescriptorSetLayoutCreateInfo final
{
private:
	std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_descriptorBindings;

	friend class DescriptorSetLayout;

public:
	void Reset();

	auto SetBinding(
		uint32_t inBinding,
		VkDescriptorType inType,
		VkShaderStageFlags inStages,
		uint32_t inDescriptorCount = 1) -> DescriptorSetLayoutCreateInfo&;
};

class DescriptorSetLayout final
{
	// Pipeline can have multiple DescriptorSetLayout,
	// when execute the pipeline we need to provide DescriptorSets for each of the DescriptorSetLayout in order
	friend class DescriptorSet;
	friend class DescriptorSetLayoutAllocator;
	friend class DynamicDescriptorSetAllocator;

private:
	std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_descriptorBindings;
	VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;

public:
	DescriptorSetLayout() = default;
	DescriptorSetLayout(const DescriptorSetLayout&) = default;
	DescriptorSetLayout& operator=(const DescriptorSetLayout&) = default;
	DescriptorSetLayout(DescriptorSetLayout&&) noexcept = default;
	DescriptorSetLayout& operator=(DescriptorSetLayout&&) noexcept = default;
	~DescriptorSetLayout();

	void Create(const DescriptorSetLayoutCreateInfo& inCreateInfo);
	void Destroy();

	auto GetVkDescriptorSetLayout() const -> VkDescriptorSetLayout;

	auto GetDescriptorSetLayoutBinding(uint32_t inBinding) const -> const VkDescriptorSetLayoutBinding&;
	auto GetDescriptorSetLayoutBindings() const
		-> const std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>&;
};

struct DescriptorBufferBinding final
{
	const Buffer* buffer = nullptr;
	VkDeviceSize offset = 0;
	VkDeviceSize range = VK_WHOLE_SIZE;
};

struct DescriptorImageBinding final
{
	const ImageView* imageView = nullptr;
	const Sampler* sampler = nullptr;
	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

class DescriptorSetDesc final
{
	friend class DescriptorSet;
	friend class DynamicDescriptorSetAllocator;

public:
	using DescriptorBindingInfo = std::variant<
		DescriptorBufferBinding,
		DescriptorImageBinding,
		const Sampler*,
		const BufferView*,
		const TopLevelAccelStruct*>;

	struct Item final
	{
		uint32_t binding = 0;
		uint32_t arrayElement = 0;
		DescriptorBindingInfo resource{};
	};

private:
	std::vector<Item> m_items;

	void _SetBinding(uint32_t inBinding, DescriptorBindingInfo inBindingInfo, uint32_t inArrayElement);

public:
	DescriptorSetDesc() = default;
	~DescriptorSetDesc() = default;
	DescriptorSetDesc(const DescriptorSetDesc&) noexcept = default;
	DescriptorSetDesc& operator=(const DescriptorSetDesc&) noexcept = default;
	DescriptorSetDesc(DescriptorSetDesc&& inOther) noexcept = default;
	DescriptorSetDesc& operator=(DescriptorSetDesc&&) noexcept = default;

	void Reset();
	void SetBuffer(
		uint32_t inBinding,
		const Buffer* inBuffer,
		VkDeviceSize inOffset = 0,
		VkDeviceSize inRange = VK_WHOLE_SIZE,
		uint32_t inArrayElement = 0);
	void SetImage(
		uint32_t inBinding,
		const ImageView* inImageView,
		VkImageLayout inImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		uint32_t inArrayElement = 0);
	void SetCombinedImageSampler(
		uint32_t inBinding,
		const ImageView* inImageView,
		const Sampler* inSampler,
		VkImageLayout inImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		uint32_t inArrayElement = 0);
	void SetSampler(uint32_t inBinding, const Sampler* inSampler, uint32_t inArrayElement = 0);
	void SetBufferView(uint32_t inBinding, const BufferView* inBufferView, uint32_t inArrayElement = 0);
	void SetAccelerationStructure(
		uint32_t inBinding,
		const TopLevelAccelStruct* inAccelerationStructure,
		uint32_t inArrayElement = 0);
	void Merge(const DescriptorSetDesc& inOther);

	auto GetItems() const -> const std::vector<Item>&;
};

class DescriptorSetCreateInfo final
{
	friend class DescriptorSet;
private:
	DescriptorSetLayout m_descriptorSetLayout{};
	DescriptorSetDesc m_descriptorSetDesc{};

public:
	void Reset();
	void SetDescriptorSetLayout(DescriptorSetLayout inLayout) { m_descriptorSetLayout = std::move(inLayout); }
	void SetDescriptorSetDesc(DescriptorSetDesc inDesc) { m_descriptorSetDesc = std::move(inDesc); }
};

class DescriptorSet final
{
private:
	friend class DynamicDescriptorSetAllocator;

	VkDescriptorSet m_vkDescriptorSet = VK_NULL_HANDLE;
	DescriptorSetLayout m_descriptorSetLayout{};
	DescriptorSetDesc m_descriptorSetDesc{};

public:
	DescriptorSet() = default;
	~DescriptorSet();

	void Create(const DescriptorSetCreateInfo& inCreateInfo);

	void Destroy();

	auto GetVkDescriptorSet() const -> VkDescriptorSet;
};

class DynamicDescriptorSetAllocator final
{
private:
	struct CachedDescriptorSetInfo
	{
		std::unordered_map<size_t, VkDescriptorSet> mapHashToDescriptorSet;
	};

public:
	class AllocateInfo final
	{
		friend class DynamicDescriptorSetAllocator;

	private:
		VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;
		DescriptorSetLayout m_descriptorSetLayout{};
		DescriptorSetDesc m_descriptorSetDesc{};

	public:
		void SetLayout(VkDescriptorSetLayout inLayout) { m_vkDescriptorSetLayout = inLayout; m_descriptorSetLayout = {}; }
		void SetLayout(DescriptorSetLayout inLayout);
		void SetDescriptorSetDesc(const DescriptorSetDesc& inDesc) { m_descriptorSetDesc = inDesc; }
	};

private:
	std::unique_ptr<DescriptorSetAllocator> m_uptrDescriptorSetAllocator;
	std::unordered_map<VkDescriptorSetLayout, CachedDescriptorSetInfo> m_cachedDescriptorSets;

	private:
	auto _AllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet;
	auto _GetCachedDescriptorSet(const AllocateInfo& inAllocateInfo) -> std::optional<VkDescriptorSet>;
	auto _HashDescriptorSetDesc(const DescriptorSetDesc& inDesc) const -> size_t;
	auto _HashDescriptorBindingInfo(const DescriptorSetDesc::DescriptorBindingInfo& inInfo) const -> size_t;
	void _WriteDescriptorSet(VkDescriptorSet inDescriptorSet, const AllocateInfo& inAllocateInfo);

public:
	void Create();
	void Reset();
	auto GetOrAllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet;
	void Destroy();
};
