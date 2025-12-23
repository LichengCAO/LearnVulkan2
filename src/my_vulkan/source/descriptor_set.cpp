#include "descriptor_set.h"
#include "device.h"
#include "buffer.h"

DescriptorSet::~DescriptorSet()
{
	// Do nothing, descriptor pool will do this for us when destroyed
	m_vkDescriptorSet = VK_NULL_HANDLE;
}

void DescriptorSet::Init(const IDescriptorSetInitializer* inInitPtr)
{
	inInitPtr->InitDescriptorSet(this);
}

DescriptorSet& DescriptorSet::UpdateDescriptors(const DescriptorSet::UpdateBuffer* inUpdaterPtr)
{
	inUpdaterPtr->_UpdateDescriptorSet(*this);

	return *this;
}

VkDescriptorSet DescriptorSet::GetVkDescriptorSet() const
{
	return m_vkDescriptorSet;
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	assert(m_vkDescriptorSetLayout == VK_NULL_HANDLE);
}

void DescriptorSetLayout::Init(const IDescriptorSetLayoutInitializer* inInitialzerPtr)
{
	inInitialzerPtr->InitDescriptorSetLayout(this);
}

VkDescriptorSetLayout DescriptorSetLayout::GetVkDescriptorSetLayout() const
{
	return m_vkDescriptorSetLayout;
}

VkDescriptorPoolCreateFlags DescriptorSetLayout::GetRequiredPoolCreateFlags() const
{
	return m_requiredPoolFlags;
}

const VkDescriptorSetLayoutBinding& DescriptorSetLayout::GetDescriptorSetLayoutBinding(uint32_t inBinding) const
{
	size_t index = m_bindingToIndex.at(inBinding);

	return m_descriptorBindings[index];
}

void DescriptorSetLayout::Uninit()
{
	if (m_vkDescriptorSetLayout != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyDescriptorSetLayout(m_vkDescriptorSetLayout);
		m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	}
	m_descriptorBindings.clear();
	m_bindingToIndex.clear();
}

VkDescriptorPool DescriptorSetAllocator::_CreatePool(VkDescriptorPoolCreateFlags inPoolFlags)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	VkDescriptorPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

	poolSizes.reserve(m_poolSizes.sizes.size());
	for (auto sz : m_poolSizes.sizes)
	{
		poolSizes.push_back({ sz.first, sz.second });
	}
	
	createInfo.flags = inPoolFlags;
	createInfo.maxSets = 1000;
	createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	createInfo.pPoolSizes = poolSizes.data();
	createInfo.pNext = nullptr;

	descriptorPool = MyDevice::GetInstance().CreateDescriptorPool(createInfo);

	return descriptorPool;
}

VkDescriptorPool DescriptorSetAllocator::_GrabPool(VkDescriptorPoolCreateFlags inPoolFlags)
{
	bool hasThisTypeFreePool = 
		m_freePools.find(inPoolFlags) != m_freePools.end() 
		&& m_freePools[inPoolFlags].size() > 0;
	
	//there are reusable pools available
	if (hasThisTypeFreePool)
	{
		//grab pool from the back of the vector and remove it from there.
		auto& freePools = m_freePools[inPoolFlags];
		VkDescriptorPool pool = freePools.back();
		freePools.pop_back();
		return pool;
	}
	else
	{
		//no pools available, so create a new one
		return _CreatePool(inPoolFlags);
	}
}

void DescriptorSetAllocator::ResetPools()
{
	auto& device = MyDevice::GetInstance();
	
	//reset all used pools and add them to the free pools
	for (auto& p : m_usedPools)
	{
		auto& freePools = m_freePools.at(p.first);
		auto& toFreePools = p.second;
		
		for (VkDescriptorPool poolToFree : toFreePools)
		{
			device.ResetDescriptorPool(poolToFree);
			freePools.push_back(poolToFree);
		}

		//clear the used pools, since we've put them all in the free pools
		toFreePools.clear();
	}

	//reset the current pool handle back to null
	m_candidatePool.clear();
}

bool DescriptorSetAllocator::Allocate(
	VkDescriptorSetLayout inLayout, 
	VkDescriptorSet& outDescriptorSet, 
	VkDescriptorPoolCreateFlags inPoolFlags, 
	const void* inNextPtr)
{
	auto& device = MyDevice::GetInstance();
	VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	VkDescriptorPool candidatePool = VK_NULL_HANDLE;
	VkResult allocResult;
	bool needReallocate = false;

	//initialize the candidate pool handle if it's null
	if (m_candidatePool.find(inPoolFlags) == m_candidatePool.end()
		|| m_candidatePool[inPoolFlags] == VK_NULL_HANDLE)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);
	}
	else
	{
		candidatePool = m_candidatePool[inPoolFlags];
	}

	allocInfo.pNext = nullptr;
	allocInfo.pSetLayouts = &inLayout;
	allocInfo.descriptorPool = candidatePool;
	allocInfo.descriptorSetCount = 1;

	//try to allocate the descriptor set
	outDescriptorSet = device.AllocateDescriptorSet(
		inLayout, 
		candidatePool,
		allocResult);

	switch (allocResult)
	{
	case VK_SUCCESS:
		//all good, return
		return true;
	case VK_ERROR_FRAGMENTED_POOL:
	case VK_ERROR_OUT_OF_POOL_MEMORY:
		//reallocate pool
		needReallocate = true;
		break;
	default:
		//unrecoverable error
		return false;
	}

	if (needReallocate)
	{
		//allocate a new pool and retry
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);

		outDescriptorSet = device.AllocateDescriptorSet(
			inLayout,
			candidatePool,
			allocResult);

		//if it still fails then we have big issues
		if (allocResult == VK_SUCCESS)
		{
			return true;
		}
	}

	return false;
}

void DescriptorSetAllocator::Init()
{
}

void DescriptorSetAllocator::Uninit()
{
	auto& device = MyDevice::GetInstance();
	//delete every pool held
	for (auto& p : m_freePools)
	{
		auto& poolsToDestroy = p.second;
		for (VkDescriptorPool pool : poolsToDestroy)
		{
			device.DestroyDescriptorPool(pool);
		}
	}
	m_freePools.clear();
	for (auto& p : m_usedPools)
	{
		auto& poolsToDestroy = p.second;
		for (VkDescriptorPool pool : poolsToDestroy)
		{
			device.DestroyDescriptorPool(pool);
		}
	}
	m_usedPools.clear();
}

void DescriptorSet::Initializer::InitDescriptorSet(DescriptorSet* pDescriptorSet) const
{
	auto pAllocator = MyDevice::GetInstance().GetDescriptorSetAllocator();
	DescriptorSet& inoutDescriptorSet = *pDescriptorSet;

	pAllocator->Allocate(m_pSetLayout->GetVkDescriptorSetLayout(), inoutDescriptorSet.m_vkDescriptorSet, m_poolFlags);
	inoutDescriptorSet.m_pSetLayout = m_pSetLayout;
}

DescriptorSet::Initializer& DescriptorSet::Initializer::Reset()
{
	*this = DescriptorSet::Initializer{};

	return *this;
}

DescriptorSet::Initializer& DescriptorSet::Initializer::AddDescriptorPoolCreateFlags(VkDescriptorPoolCreateFlags inPoolFlags)
{
	m_poolFlags |= inPoolFlags;

	return *this;
}

DescriptorSet::Initializer& DescriptorSet::Initializer::SetDescriptorSetLayout(const DescriptorSetLayout* inSetLayoutPtr)
{
	m_pSetLayout = inSetLayoutPtr;
	AddDescriptorPoolCreateFlags(inSetLayoutPtr->GetRequiredPoolCreateFlags());

	return *this;
}

void DescriptorSet::UpdateBuffer::_UpdateDescriptorSet(DescriptorSet& inoutDescriptorSet) const
{
	std::vector<VkWriteDescriptorSet> writes;
	std::vector<std::unique_ptr<VkWriteDescriptorSetAccelerationStructureKHR>> uptrASwrites;
	const auto& setLayout = *inoutDescriptorSet.m_pSetLayout;
	VkDescriptorSet dstDescriptor = inoutDescriptorSet.m_vkDescriptorSet;

	for (auto& eUpdate : m_updates)
	{
		VkWriteDescriptorSet wds{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		wds.dstSet = dstDescriptor;
		wds.dstBinding = eUpdate.binding;
		wds.dstArrayElement = eUpdate.arrayElementIndex;
		wds.descriptorType = setLayout.GetDescriptorSetLayoutBinding(eUpdate.binding).descriptorType;
		switch (eUpdate.descriptorType)
		{
		case DescriptorType::BUFFER:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.bufferInfos.size());
			wds.pBufferInfo = eUpdate.bufferInfos.data();
		}
		break;
		case DescriptorType::IMAGE:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.imageInfos.size());
			wds.pImageInfo = eUpdate.imageInfos.data();
		}
		break;
		case DescriptorType::TEXEL_BUFFER:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.bufferViews.size());
			wds.pTexelBufferView = eUpdate.bufferViews.data();
		}
		break;
		case DescriptorType::ACCELERATION_STRUCTURE:
		{
			std::unique_ptr<VkWriteDescriptorSetAccelerationStructureKHR> uptrASInfo = std::make_unique<VkWriteDescriptorSetAccelerationStructureKHR>();
			uptrASInfo->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
			uptrASInfo->accelerationStructureCount = static_cast<uint32_t>(eUpdate.accelerationStructures.size());
			uptrASInfo->pAccelerationStructures = eUpdate.accelerationStructures.data();
			uptrASInfo->pNext = nullptr;
			// pNext<VkWriteDescriptorSetAccelerationStructureKHR>.accelerationStructureCount must be equal to descriptorCount
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.accelerationStructures.size());
			wds.pNext = uptrASInfo.get();
			uptrASwrites.push_back(std::move(uptrASInfo));
		}
		break;
		default:
		{
			std::runtime_error("No such descriptor type!");
		}
		break;
		}
		writes.push_back(wds);
	}

	MyDevice::GetInstance().UpdateDescriptorSets(writes);
}

DescriptorSet::UpdateBuffer& DescriptorSet::UpdateBuffer::Reset()
{
	m_updates.clear();

	return *this;
}

DescriptorSet::UpdateBuffer& DescriptorSet::UpdateBuffer::WriteBinding(
	uint32_t bindingId, 
	const std::vector<VkDescriptorImageInfo>& dImageInfo, 
	uint32_t inArrayIndexOffset)
{
	DescriptorSetUpdate newUpdate{};
	newUpdate.binding = bindingId;
	newUpdate.imageInfos = dImageInfo;
	newUpdate.arrayElementIndex = inArrayIndexOffset;
	newUpdate.descriptorType = DescriptorType::IMAGE;
	m_updates.push_back(newUpdate);

	return *this;
}

DescriptorSet::UpdateBuffer& DescriptorSet::UpdateBuffer::WriteBinding(
	uint32_t bindingId,
	const std::vector<VkBufferView>& bufferViews,
	uint32_t inArrayIndexOffset)
{
	DescriptorSetUpdate newUpdate{};
	newUpdate.binding = bindingId;
	newUpdate.bufferViews = bufferViews;
	newUpdate.arrayElementIndex = inArrayIndexOffset;
	newUpdate.descriptorType = DescriptorType::TEXEL_BUFFER;
	m_updates.push_back(newUpdate);

	return *this;
}

DescriptorSet::UpdateBuffer& DescriptorSet::UpdateBuffer::WriteBinding(
	uint32_t bindingId,
	const std::vector<VkDescriptorBufferInfo>& bufferInfos,
	uint32_t inArrayIndexOffset)
{
	DescriptorSetUpdate newUpdate{};
	newUpdate.binding = bindingId;
	newUpdate.bufferInfos = bufferInfos;
	newUpdate.arrayElementIndex = inArrayIndexOffset;
	newUpdate.descriptorType = DescriptorType::BUFFER;
	m_updates.push_back(newUpdate);

	return *this;
}

DescriptorSet::UpdateBuffer& DescriptorSet::UpdateBuffer::WriteBinding(
	uint32_t bindingId,
	const std::vector<VkAccelerationStructureKHR>& _accelStructs,
	uint32_t inArrayIndexOffset)
{
	DescriptorSetUpdate newUpdate{};
	newUpdate.binding = bindingId;
	newUpdate.accelerationStructures = _accelStructs;
	newUpdate.arrayElementIndex = inArrayIndexOffset;
	newUpdate.descriptorType = DescriptorType::ACCELERATION_STRUCTURE;
	m_updates.push_back(newUpdate);

	return *this;
}

void DescriptorSetLayout::Initializer::InitDescriptorSetLayout(DescriptorSetLayout* pDescriptorSetLayout) const
{
	VkDescriptorSetLayoutCreateInfo createInfo{};
	VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
	auto& device = MyDevice::GetInstance();
	void const** ppNextChain = &(createInfo.pNext);
	DescriptorSetLayout& outDescriptorSetLayout = *pDescriptorSetLayout;

	createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	createInfo.bindingCount = static_cast<uint32_t>(m_layoutBindings.size());
	createInfo.pBindings = m_layoutBindings.data();

	outDescriptorSetLayout.m_descriptorBindings.clear();
	outDescriptorSetLayout.m_bindingToIndex.clear();
	outDescriptorSetLayout.m_descriptorBindings.reserve(m_layoutBindings.size());
	outDescriptorSetLayout.m_requiredPoolFlags = m_poolFlags;
	for (size_t i = 0; i < m_layoutBindings.size(); ++i)
	{
		const auto& currentBinding = m_layoutBindings[i];

		outDescriptorSetLayout.m_descriptorBindings.push_back(currentBinding);
		outDescriptorSetLayout.m_bindingToIndex[currentBinding.binding] = i;
	}

	// For binding flags
	if (m_extraFlags.size() > 0)
	{
		CHECK_TRUE(m_extraFlags.size() == m_layoutBindings.size());
		flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagInfo.pBindingFlags = m_extraFlags.data();
		flagInfo.bindingCount = static_cast<uint32_t>(m_extraFlags.size());

		(*ppNextChain) = &flagInfo;
		ppNextChain = &(flagInfo.pNext);
	}

	outDescriptorSetLayout.m_vkDescriptorSetLayout = device.CreateDescriptorSetLayout(createInfo);
}

DescriptorSetLayout::Initializer& DescriptorSetLayout::Initializer::Reset()
{
	*this = DescriptorSetLayout::Initializer{};
	return *this;
}

DescriptorSetLayout::Initializer& DescriptorSetLayout::Initializer::AddBinding(
	uint32_t inBinding, 
	VkDescriptorType inDescriptorType, 
	VkShaderStageFlags inStages, 
	uint32_t inDescriptorCount, 
	const VkSampler* pImmutableSamplers)
{
	VkDescriptorSetLayoutBinding layoutBinding{};

	layoutBinding.binding = inBinding;
	layoutBinding.descriptorCount = inDescriptorCount;
	layoutBinding.descriptorType = inDescriptorType;
	layoutBinding.pImmutableSamplers = pImmutableSamplers;
	layoutBinding.stageFlags = inStages;

	m_layoutBindings.push_back(layoutBinding);

	// so that m_extraFlags will always equal to bindings if we have extra flags
	if (m_extraFlags.size() > 0)
	{
		m_extraFlags.resize(m_layoutBindings.size(), 0);
	}
	return *this;
}

DescriptorSetLayout::Initializer& DescriptorSetLayout::Initializer::AddBindlessBinding(
	uint32_t inBinding, 
	VkDescriptorType inDescriptorType, 
	VkShaderStageFlags inStages, 
	uint32_t inDescriptorCount, 
	const VkSampler* pImmutableSamplers)
{
	AddBinding(inBinding, inDescriptorType, inStages, inDescriptorCount, pImmutableSamplers);
	m_extraFlags.resize(m_layoutBindings.size(), 0);
	m_extraFlags.back() |= (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT);
	m_poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
	return *this;
}
