#pragma once
#include "common.h"
#include <map>
class DescriptorSet;
class DescriptorSetLayout;

class IDescriptorSetLayoutInitializer
{
public:
	virtual void InitDescriptorSetLayout(DescriptorSetLayout* pDescriptorSetLayout) const = 0;
};

class IDescriptorSetInitializer
{
public:
	virtual void InitDescriptorSet(DescriptorSet* pDescriptorSet) const = 0;
};

// Pipeline can have multiple DescriptorSetLayout, 
// when execute the pipeline we need to provide DescriptorSets for each of the DescriptorSetLayout in order
class DescriptorSetLayout
{
private:
	std::unordered_map<uint32_t, size_t> m_bindingToIndex;
	std::vector<VkDescriptorSetLayoutBinding> m_descriptorBindings;
	VkDescriptorSetLayout m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPoolCreateFlags m_requiredPoolFlags = 0;

public:
	class Initializer : public IDescriptorSetLayoutInitializer
	{
	private:
		std::vector<VkDescriptorBindingFlags> m_extraFlags;
		std::vector<VkDescriptorSetLayoutBinding> m_layoutBindings;
		VkDescriptorPoolCreateFlags m_poolFlags = 0;

	public:
		virtual void InitDescriptorSetLayout(DescriptorSetLayout* pDescriptorSetLayout) const override;

		DescriptorSetLayout::Initializer& Reset();

		DescriptorSetLayout::Initializer& AddBinding(
			uint32_t inBinding,
			VkDescriptorType inDescriptorType,
			VkShaderStageFlags inStages,
			uint32_t inDescriptorCount = 1,
			const VkSampler* pImmutableSamplers = nullptr);

		DescriptorSetLayout::Initializer& AddBindlessBinding(
			uint32_t inBinding,
			VkDescriptorType inDescriptorType,
			VkShaderStageFlags inStages,
			uint32_t inDescriptorCount = 100,
			const VkSampler* pImmutableSamplers = nullptr);

		friend class DescriptorSetLayout;
	};

public:
	~DescriptorSetLayout();

public:
	void Init(const IDescriptorSetLayoutInitializer* inInitialzerPtr);

	VkDescriptorSetLayout GetVkDescriptorSetLayout() const;

	VkDescriptorPoolCreateFlags GetRequiredPoolCreateFlags() const;

	const VkDescriptorSetLayoutBinding& GetDescriptorSetLayoutBinding(uint32_t inBinding) const;

	void Uninit();
};

class DescriptorSet
{
private:
	enum class DescriptorType { BUFFER, IMAGE, TEXEL_BUFFER, ACCELERATION_STRUCTURE };
	struct DescriptorSetUpdate
	{
		uint32_t        binding = 0;
		uint32_t        arrayElementIndex = 0;
		DescriptorType  descriptorType = DescriptorType::BUFFER;
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkDescriptorImageInfo>  imageInfos;
		std::vector<VkBufferView>           bufferViews;
		std::vector<VkAccelerationStructureKHR> accelerationStructures;
	};

public:
	class Initializer : public IDescriptorSetInitializer
	{
	private:
		VkDescriptorPoolCreateFlags m_poolFlags = 0;
		const DescriptorSetLayout* m_pSetLayout = nullptr;

	public:
		virtual void InitDescriptorSet(DescriptorSet* pDescriptorSet) const override;

		// Reset initializer
		DescriptorSet::Initializer& Reset();

		// Optional, add pool create flags only for special cases
		DescriptorSet::Initializer& AddDescriptorPoolCreateFlags(VkDescriptorPoolCreateFlags inPoolFlags);

		// Set descriptor set layout for allocation reference in descriptor pool, mandatory
		DescriptorSet::Initializer& SetDescriptorSetLayout(const DescriptorSetLayout* inSetLayoutPtr);

		friend class DescriptorSet;
	};

	class UpdateBuffer
	{
	private:
		std::vector<DescriptorSetUpdate> m_updates;

		void _UpdateDescriptorSet(DescriptorSet& inoutDescriptorSet) const;

	public:
		DescriptorSet::UpdateBuffer& Reset();

		DescriptorSet::UpdateBuffer& WriteBinding(
			uint32_t bindingId,
			const std::vector<VkDescriptorImageInfo>& dImageInfo,
			uint32_t inArrayIndexOffset = 0);

		DescriptorSet::UpdateBuffer& WriteBinding(
			uint32_t bindingId,
			const std::vector<VkBufferView>& bufferViews,
			uint32_t inArrayIndexOffset = 0);

		DescriptorSet::UpdateBuffer& WriteBinding(
			uint32_t bindingId,
			const std::vector<VkDescriptorBufferInfo>& bufferInfos,
			uint32_t inArrayIndexOffset = 0);

		DescriptorSet::UpdateBuffer& WriteBinding(
			uint32_t bindingId,
			const std::vector<VkAccelerationStructureKHR>& _accelStructs,
			uint32_t inArrayIndexOffset = 0);

		friend class DescriptorSet;
	};

private:
	const DescriptorSetLayout* m_pSetLayout = nullptr;
	VkDescriptorSet m_vkDescriptorSet = VK_NULL_HANDLE;

public:
	~DescriptorSet();

	void Init(const IDescriptorSetInitializer* inInitPtr);

	DescriptorSet& UpdateDescriptors(const DescriptorSet::UpdateBuffer* inUpdaterPtr);

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

	void Init();

	void Uninit();
};