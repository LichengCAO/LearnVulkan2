#pragma once
#include "common.h"
#include <cstdint>
#include <map>
#include <variant>
class DescriptorSet;
class DescriptorSetLayout;
class DescriptorSetAllocator;
class DynamicDescriptorSetAllocator;

class DescriptorSetLayoutCreateInfo final
{
	friend class DescriptorSetLayout;
private:
	std::vector<VkDescriptorSetLayoutBinding> m_layoutBindings;
	std::vector<VkDescriptorBindingFlags> m_bindingFlags;
	VkDescriptorPoolCreateFlags m_poolFlags = 0;

public:
	enum class ExtraInfoType
	{
		UNSET,
		ARRAY_SIZE,
		BINDLESS,
	};

	struct ExtraInfo
	{
		ExtraInfoType			type = ExtraInfoType::UNSET;
		const ExtraInfo*		extraInfo = nullptr;

	public:
		ExtraInfo() = default;
		ExtraInfo(ExtraInfoType inType) : type(inType) {};
	};

	struct ArraySizeInfo final : public ExtraInfo
	{
		uint32_t size = 0;

	public:
		ArraySizeInfo() : ExtraInfo{ ExtraInfoType::ARRAY_SIZE } {};
	};

	struct BindlessInfo final : public ExtraInfo
	{
	public:
		BindlessInfo() : ExtraInfo{ ExtraInfoType::BINDLESS } {};
	};

public:
	void Reset();
	void SetBinding(
		uint32_t inBinding,
		VkDescriptorType inType,
		VkShaderStageFlags inStages,
		const DescriptorSetLayoutCreateInfo::ExtraInfo* inExtraInfo = nullptr);
};

class DescriptorSetLayout final
{
	// Pipeline can have multiple DescriptorSetLayout,
	// when execute the pipeline we need to provide DescriptorSets for each of the DescriptorSetLayout in order
	friend class DescriptorSet;
	friend class DynamicDescriptorSetAllocator;

private:
	std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_descriptorBindings;
	VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPoolCreateFlags m_requiredPoolFlags = 0;

public:
	~DescriptorSetLayout();

	void Create(const DescriptorSetLayoutCreateInfo& inCreateInfo);

	VkDescriptorSetLayout GetVkDescriptorSetLayout() const;

	VkDescriptorPoolCreateFlags GetRequiredPoolCreateFlags() const;

	const VkDescriptorSetLayoutBinding& GetDescriptorSetLayoutBinding(uint32_t inBinding) const;

	void Destroy();
};

class DescriptorState final
{
	template<typename T>
	friend struct std::hash;
	friend class DescriptorSet;
	friend class DynamicDescriptorSetAllocator;
	friend bool operator==(const DescriptorState&, const DescriptorState&);
	friend bool operator!=(const DescriptorState&, const DescriptorState&);
	friend bool operator<(const DescriptorState&, const DescriptorState&);

public:
	using DescriptorBindingInfo = std::variant<VkDescriptorBufferInfo, VkDescriptorImageInfo, VkBufferView, VkAccelerationStructureKHR>;

private:
	std::vector<DescriptorBindingInfo> m_bindingInfo;
	VkWriteDescriptorSet m_writeInfo{};

public:
	DescriptorState() = default;
	~DescriptorState() = default;
	DescriptorState(const DescriptorState&) noexcept = default;
	DescriptorState& operator=(const DescriptorState&) noexcept = default;
	DescriptorState(DescriptorState&& inOther) noexcept = default;
	DescriptorState& operator=(DescriptorState&&) noexcept = default;

	void Reset();
	void SetBinding(const VkDescriptorBufferInfo& inBindingInfo, uint32_t inIndex = 0);
	void SetBinding(const VkDescriptorImageInfo& inBindingInfo, uint32_t inIndex = 0);
	void SetBinding(const VkBufferView& inBindingInfo, uint32_t inIndex = 0);
	void SetBinding(const VkAccelerationStructureKHR& inBindingInfo, uint32_t inIndex = 0);
	uint32_t GetDstArrayElement() const { return m_writeInfo.dstArrayElement; }
	const std::vector<DescriptorBindingInfo>& GetBindingInfo() const { return m_bindingInfo; }
};

class DescriptorSetState final
{
	template<typename T>
	friend struct std::hash;
	friend class DescriptorSet;
	friend class DynamicDescriptorSetAllocator;
	friend bool operator==(const DescriptorState&, const DescriptorState&);
	friend bool operator!=(const DescriptorState&, const DescriptorState&);
	friend bool operator<(const DescriptorState&, const DescriptorState&);

private:
	std::unordered_map<uint32_t, DescriptorState> m_mapBindingToDescriptor;

public:
	DescriptorSetState() = default;
	~DescriptorSetState() = default;
	DescriptorSetState(const DescriptorSetState&) noexcept = default;
	DescriptorSetState& operator=(const DescriptorSetState&) noexcept = default;
	DescriptorSetState(DescriptorSetState&& inOther) noexcept = default;
	DescriptorSetState& operator=(DescriptorSetState&&) noexcept = default;

	void Reset();
	void SetBinding(DescriptorState inBindingInfo, uint32_t inBinding);
	void SetBindings(const DescriptorState* inBindingInfo, const uint32_t* inBindings, size_t inCount);
	void Merge(const DescriptorSetState& inOther);
	const std::unordered_map<uint32_t, DescriptorState>& GetMapBindingToDescriptor() const { return m_mapBindingToDescriptor; }
};

class DescriptorSetCreateInfo final
{
	friend class DescriptorSet;
private:
	const DescriptorSetLayout* m_descriptorSetLayout{};

public:
	void Reset();
	void SetDescriptorSetLayout(const DescriptorSetLayout* inLayout) { m_descriptorSetLayout = inLayout; };
};

class DescriptorSet final
{
private:
	friend class DynamicDescriptorSetAllocator;
	struct BindingLayoutMetadata
	{
		VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		uint32_t descriptorCount = 0;
	};

	VkDescriptorSet m_vkDescriptorSet = VK_NULL_HANDLE;
	std::unordered_map<uint32_t, BindingLayoutMetadata> m_bindingLayoutMetadata;
	DescriptorSetState m_cachedState{};

public:
	DescriptorSet() = default;
	~DescriptorSet();

	void Create(const DescriptorSetCreateInfo& inCreateInfo);

	void WriteBindings(const DescriptorSetState& inBinding);

	void Destroy();

	VkDescriptorSet GetVkDescriptorSet() const;

	friend class DescriptorSetLayout;
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
		DescriptorSetState m_state{};

	public:
		void SetLayout(VkDescriptorSetLayout inLayout){ m_vkDescriptorSetLayout = inLayout; }
		void SetDescriptorSetState(const DescriptorSetState& inState) { m_state = inState; }
	};

private:
	std::unique_ptr<DescriptorSetAllocator> m_uptrDescriptorSetAllocator;
	std::unordered_map<VkDescriptorSetLayout, CachedDescriptorSetInfo> m_cachedDescriptorSets;

private:
	auto _AllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet;
	auto _GetCachedDescriptorSet(const AllocateInfo& inAllocateInfo) -> std::optional<VkDescriptorSet>;
	auto _HashDescriptorSetState(const DescriptorSetState& inState) const -> size_t;
	auto _HashDescriptorState(const DescriptorState& inState) const -> size_t;
	auto _HashDescriptorBindingInfo(const DescriptorState::DescriptorBindingInfo& inInfo) const -> size_t;
	void _WriteDescriptorSet(VkDescriptorSet inDescriptorSet, const AllocateInfo& inAllocateInfo);

public:
	void Create();
	void Reset();
	auto GetOrAllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet;
	void Destroy();
};
