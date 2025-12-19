#include "descriptor_set.h"
#include "device.h"
#include "buffer.h"

void DescriptorSet::_PreSetLayout(const DescriptorSetLayout* _layout)
{
	m_pLayout = _layout;
}

VkDescriptorPoolCreateFlags DescriptorSet::GetRequiredPoolFlags() const
{
	return m_requiredPoolFlags;
}

void DescriptorSet::PreSetRequiredPoolFlags(VkDescriptorPoolCreateFlags inFlags)
{
	if (vkDescriptorSet != VK_NULL_HANDLE) return;
	m_requiredPoolFlags = inFlags;
}

void DescriptorSet::Init()
{
	CHECK_TRUE(m_pLayout != nullptr, "No descriptor layout set! Apply a layout before initialization!");
	CHECK_TRUE(vkDescriptorSet == VK_NULL_HANDLE, "VkDescriptorSet is already created!");

	auto pAllocator = MyDevice::GetInstance().GetDescriptorSetAllocator();
	pAllocator->Allocate(m_pLayout->vkDescriptorSetLayout, vkDescriptorSet);
}

void DescriptorSet::StartUpdate()
{
	if (m_pLayout == nullptr)
	{
		throw std::runtime_error("Layout is not set!");
	}
	if (vkDescriptorSet == VK_NULL_HANDLE)
	{
		throw std::runtime_error("Descriptorset is not initialized");
	}
	m_updates.clear();
}

void DescriptorSet::UpdateBinding(
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
}

void DescriptorSet::UpdateBinding(
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
}

void DescriptorSet::UpdateBinding(
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
}

void DescriptorSet::UpdateBinding(
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
}

void DescriptorSet::FinishUpdate()
{
	std::vector<VkWriteDescriptorSet> writes;
	std::vector<std::unique_ptr<VkWriteDescriptorSetAccelerationStructureKHR>> uptrASwrites;
	CHECK_TRUE(m_pLayout != nullptr, "Layout is not initialized!");
	for (auto& eUpdate : m_updates)
	{
		VkWriteDescriptorSet wds{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		bool bindingFound = false;
		wds.dstSet = vkDescriptorSet;
		wds.dstBinding = eUpdate.binding;
		wds.dstArrayElement = eUpdate.arrayElementIndex;
		for (const auto& binding : m_pLayout->bindings)
		{
			if (binding.binding == eUpdate.binding)
			{
				wds.descriptorType = binding.descriptorType;
				bindingFound = true;
				break;
			}
		}
		CHECK_TRUE(bindingFound, "The descriptor doesn't have this binding!");
		
		// wds.descriptorCount = m_pLayout->bindings[eUpdate.binding].descriptorCount; the number of elements to update doens't need to be the same as the total number of the elements
		switch (eUpdate.descriptorType)
		{
		case DescriptorType::BUFFER:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.bufferInfos.size());
			wds.pBufferInfo = eUpdate.bufferInfos.data();
			break;
		}
		case DescriptorType::IMAGE:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.imageInfos.size());
			wds.pImageInfo = eUpdate.imageInfos.data();
			break;
		}
		case DescriptorType::TEXEL_BUFFER:
		{
			wds.descriptorCount = static_cast<uint32_t>(eUpdate.bufferViews.size());
			wds.pTexelBufferView = eUpdate.bufferViews.data();
			break;
		}
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
			break;
		}
		default:
		{
			std::runtime_error("No such descriptor type!");
			break;
		}
		}
		writes.push_back(wds);
	}
	MyDevice::GetInstance().UpdateDescriptorSets(writes);
	m_updates.clear();
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	assert(vkDescriptorSetLayout == VK_NULL_HANDLE);
}

DescriptorSetLayout& DescriptorSetLayout::SetFollowingBindless(bool inBindless)
{
	m_allowBindless = inBindless;
	return *this;
}

DescriptorSetLayout& DescriptorSetLayout::PreAddBinding(
	uint32_t descriptorCount,
	VkDescriptorType descriptorType,
	VkShaderStageFlags stageFlags,
	const VkSampler* pImmutableSamplers,
	uint32_t* outBindingPtr)
{
	uint32_t binding = static_cast<uint32_t>(bindings.size());
	if (outBindingPtr != nullptr)
	{
		*outBindingPtr = binding;
	}

	return PreAddBinding(binding, descriptorCount, descriptorType, stageFlags, pImmutableSamplers);
}

DescriptorSetLayout& DescriptorSetLayout::PreAddBinding(
	uint32_t binding,
	uint32_t descriptorCount,
	VkDescriptorType descriptorType,
	VkShaderStageFlags stageFlags,
	const VkSampler* pImmutableSamplers)
{
	VkDescriptorSetLayoutBinding layoutBinding{};

	layoutBinding.binding = binding;
	layoutBinding.descriptorCount = descriptorCount;
	layoutBinding.descriptorType = descriptorType;
	layoutBinding.pImmutableSamplers = pImmutableSamplers;
	layoutBinding.stageFlags = stageFlags;
	m_bindingToIndex[binding] = bindings.size();
	bindings.push_back(layoutBinding);
	if (m_allowBindless)
	{
		PreSetBindingFlags(binding, 
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT
			| VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT);
	}

	return *this;
}

DescriptorSetLayout& DescriptorSetLayout::PreSetBindingFlags(
	uint32_t binding,
	VkDescriptorBindingFlags flags)
{
	if (m_bindingToIndex.find(binding) == m_bindingToIndex.end()) return *this;
	
	size_t index = m_bindingToIndex.at(binding);
	
	m_bindingFlags.resize(index + 1, 0);
	m_bindingFlags[index] = flags;

	return *this;
}

void DescriptorSetLayout::Init()
{
	CHECK_TRUE(bindings.size() != 0, "No bindings set!");
	CHECK_TRUE(vkDescriptorSetLayout == VK_NULL_HANDLE, "VkDescriptorSetLayout is already created!");

	VkDescriptorSetLayoutCreateInfo createInfo{};
	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{};
	void const** ppNextChain = &(createInfo.pNext);

	createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	createInfo.pBindings = bindings.data();

	// Which indicates that the descriptor set layout will use the extended binding flags
	if (m_bindingFlags.size() > 0)
	{
		m_bindingFlags.resize(bindings.size(), 0);
		bindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
		bindingFlagsCreateInfo.pNext = nullptr;
		bindingFlagsCreateInfo.bindingCount = static_cast<uint32_t>(m_bindingFlags.size());
		bindingFlagsCreateInfo.pBindingFlags = m_bindingFlags.data();

		(*ppNextChain) = &bindingFlagsCreateInfo;
		ppNextChain = &(bindingFlagsCreateInfo.pNext);
	}

	vkDescriptorSetLayout = MyDevice::GetInstance().CreateDescriptorSetLayout(createInfo);
}

DescriptorSet DescriptorSetLayout::NewDescriptorSet() const
{
	DescriptorSet result{};

	ApplyToDescriptorSet(result);

	return result;
}

DescriptorSet* DescriptorSetLayout::NewDescriptorSetPtr() const
{
	DescriptorSet* pDescriptor = new DescriptorSet();

	ApplyToDescriptorSet(*pDescriptor);

	return pDescriptor;
}

void DescriptorSetLayout::ApplyToDescriptorSet(DescriptorSet& outDescriptorSet) const
{
	bool supportBindless = m_bindingFlags.size() > 0;
	outDescriptorSet._PreSetLayout(this);
	if (supportBindless)
	{
		// For bindless descriptor sets, 
		// change which buffer descriptor is pointing to after bound, 
		// before command buffer submission
		outDescriptorSet.PreSetRequiredPoolFlags(
			outDescriptorSet.GetRequiredPoolFlags()
			| VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT
		);
	}
}

void DescriptorSetLayout::Uninit()
{
	if (vkDescriptorSetLayout != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyDescriptorSetLayout(vkDescriptorSetLayout);
		vkDescriptorSetLayout = VK_NULL_HANDLE;
	}
	bindings.clear();
	m_bindingFlags.clear();
	m_bindingToIndex.clear();
	m_allowBindless = false;
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