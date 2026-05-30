#pragma once
#include "common.h"
#include <cstdint>
#include <map>
#include <variant>
class DescriptorSet;
class DescriptorSetLayout;

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

// Pipeline can have multiple DescriptorSetLayout, 
// when execute the pipeline we need to provide DescriptorSets for each of the DescriptorSetLayout in order
class DescriptorSetLayout final
{
	friend class DescriptorSet;

private:
	std::unordered_map<uint32_t, size_t> m_bindingToIndex;
	std::vector<VkDescriptorSetLayoutBinding> m_descriptorBindings;
	VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPoolCreateFlags m_requiredPoolFlags = 0;

public:
	~DescriptorSetLayout();

public:
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
};

class DescriptorSetState final
{
	template<typename T>
	friend struct std::hash;
	friend class DescriptorSet;
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
};

class DescriptorSetCreateInfo final
{
	friend class DescriptorSet;
private:
	const DescriptorSetLayout* m_descriptorSetLayout{};

public:
	void Reset();
	void SetDescriptorSeLayout(const DescriptorSetLayout* inLayout) { m_descriptorSetLayout = inLayout; };
};

class DescriptorSet final
{
private:
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

class DescriptorSetAllocator
{
	// https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/
public:
	struct PoolSizes {
		std::vector<std::pair<VkDescriptorType, uint32_t>> sizes =
		{
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
	};

private:
	PoolSizes m_poolSizes{};
	std::map<VkDescriptorPoolCreateFlags, VkDescriptorPool> m_candidatePool;
	std::map<VkDescriptorPoolCreateFlags, std::vector<VkDescriptorPool>> m_usedPools;
	std::map<VkDescriptorPoolCreateFlags, std::vector<VkDescriptorPool>> m_freePools;

private:
	VkDescriptorPool _CreatePool(VkDescriptorPoolCreateFlags inPoolFlags);
	VkDescriptorPool _GrabPool(VkDescriptorPoolCreateFlags inPoolFlags);

public:
	void ResetPools();

	bool Allocate(
		VkDescriptorSetLayout inLayout, 
		VkDescriptorSet& outDescriptorSet, 
		VkDescriptorPoolCreateFlags inPoolFlags = 0,
		const void* inNextPtr = nullptr);

	void Create();

	void Destroy();
};
