#include "descriptor_set.h"
#include "device.h"
#include "buffer.h"
#include <algorithm>
#include <stdexcept>

namespace
{
	bool IsEqual(const VkDescriptorBufferInfo& lhs, const VkDescriptorBufferInfo& rhs)
	{
		return lhs.buffer == rhs.buffer
			&& lhs.offset == rhs.offset
			&& lhs.range == rhs.range;
	}

	bool IsEqual(const VkDescriptorImageInfo& lhs, const VkDescriptorImageInfo& rhs)
	{
		return lhs.sampler == rhs.sampler
			&& lhs.imageView == rhs.imageView
			&& lhs.imageLayout == rhs.imageLayout;
	}

	bool IsEqual(const DescriptorState::DescriptorBindingInfo& lhs, const DescriptorState::DescriptorBindingInfo& rhs)
	{
		if (lhs.index() != rhs.index())
		{
			return false;
		}

		if (const auto* pLhs = std::get_if<VkDescriptorBufferInfo>(&lhs))
		{
			return IsEqual(*pLhs, std::get<VkDescriptorBufferInfo>(rhs));
		}
		if (const auto* pLhs = std::get_if<VkDescriptorImageInfo>(&lhs))
		{
			return IsEqual(*pLhs, std::get<VkDescriptorImageInfo>(rhs));
		}
		if (const auto* pLhs = std::get_if<VkBufferView>(&lhs))
		{
			return *pLhs == std::get<VkBufferView>(rhs);
		}
		if (const auto* pLhs = std::get_if<VkAccelerationStructureKHR>(&lhs))
		{
			return *pLhs == std::get<VkAccelerationStructureKHR>(rhs);
		}

		return false;
	}

	bool IsImageDescriptorType(VkDescriptorType inType)
	{
		switch (inType)
		{
		case VK_DESCRIPTOR_TYPE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			return true;
		default:
			return false;
		}
	}

	bool IsBufferDescriptorType(VkDescriptorType inType)
	{
		switch (inType)
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			return true;
		default:
			return false;
		}
	}

	bool IsTexelBufferDescriptorType(VkDescriptorType inType)
	{
		switch (inType)
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			return true;
		default:
			return false;
		}
	}

	DescriptorState MakeNullDescriptorState(const VkDescriptorSetLayoutBinding& inLayoutBinding)
	{
		DescriptorState state{};
		const uint32_t descriptorCount = inLayoutBinding.descriptorCount;

		if (IsBufferDescriptorType(inLayoutBinding.descriptorType))
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkDescriptorBufferInfo{}, i);
			}
		}
		else if (IsImageDescriptorType(inLayoutBinding.descriptorType))
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkDescriptorImageInfo{}, i);
			}
		}
		else if (IsTexelBufferDescriptorType(inLayoutBinding.descriptorType))
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkBufferView{}, i);
			}
		}
		else if (inLayoutBinding.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkAccelerationStructureKHR{}, i);
			}
		}
		else
		{
			throw std::runtime_error("Unsupported descriptor type!");
		}

		return state;
	}
}

DescriptorSet::~DescriptorSet()
{
	// Do nothing, descriptor pool will do this for us when destroyed
	m_vkDescriptorSet = VK_NULL_HANDLE;
}

void DescriptorSetCreateInfo::Reset()
{
	*this = DescriptorSetCreateInfo{};
}

void DescriptorSet::Create(const DescriptorSetCreateInfo& inCreateInfo)
{
	CHECK_TRUE(inCreateInfo.m_descriptorSetLayout != nullptr, "DescriptorSetCreateInfo must have a descriptor set layout!");
	auto pAllocator = MyDevice::GetInstance().GetDescriptorSetAllocator();
	const DescriptorSetLayout& descriptorSetLayout = *inCreateInfo.m_descriptorSetLayout;
	const VkDescriptorPoolCreateFlags poolFlags = descriptorSetLayout.GetRequiredPoolCreateFlags();

	CHECK_TRUE(
		pAllocator->Allocate(descriptorSetLayout.GetVkDescriptorSetLayout(), m_vkDescriptorSet, poolFlags),
		"Failed to allocate descriptor set!");
	m_bindingLayoutMetadata.clear();
	m_cachedState.Reset();
	for (const VkDescriptorSetLayoutBinding& layoutBinding : descriptorSetLayout.m_descriptorBindings)
	{
		BindingLayoutMetadata metadata{};
		metadata.descriptorType = layoutBinding.descriptorType;
		metadata.descriptorCount = layoutBinding.descriptorCount;
		m_bindingLayoutMetadata[layoutBinding.binding] = metadata;
		m_cachedState.SetBinding(MakeNullDescriptorState(layoutBinding), layoutBinding.binding);
	}
}

void DescriptorSet::Destroy()
{
	// Do nothing, descriptor pool will do this for us when destroyed
	m_vkDescriptorSet = VK_NULL_HANDLE;
	m_bindingLayoutMetadata.clear();
	m_cachedState.Reset();
}

VkDescriptorSet DescriptorSet::GetVkDescriptorSet() const
{
	return m_vkDescriptorSet;
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	assert(m_vkDescriptorSetLayout == VK_NULL_HANDLE);
}

void DescriptorSetLayoutCreateInfo::Reset()
{
	*this = DescriptorSetLayoutCreateInfo{};
}

void DescriptorSetLayoutCreateInfo::SetBinding(
	uint32_t inBinding,
	VkDescriptorType inType,
	VkShaderStageFlags inStages,
	const DescriptorSetLayoutCreateInfo::ExtraInfo* inExtraInfo)
{
	uint32_t descriptorCount = 1;
	VkDescriptorBindingFlags bindingFlags = 0;
	for (const ExtraInfo* currentExtraInfo = inExtraInfo; currentExtraInfo != nullptr; currentExtraInfo = currentExtraInfo->extraInfo)
	{
		switch (currentExtraInfo->type)
		{
		case ExtraInfoType::UNSET:
			break;
		case ExtraInfoType::ARRAY_SIZE:
		{
			const ArraySizeInfo& arraySizeInfo = static_cast<const ArraySizeInfo&>(*currentExtraInfo);
			CHECK_TRUE(arraySizeInfo.size > 0, "Descriptor array size must be greater than zero!");
			descriptorCount = arraySizeInfo.size;
		}
		break;
		case ExtraInfoType::BINDLESS:
			bindingFlags |= (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT);
			m_poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
			break;
		default:
			throw std::runtime_error("Unsupported descriptor set layout create info extra type!");
		}
	}

	VkDescriptorSetLayoutBinding layoutBinding{};
	layoutBinding.binding = inBinding;
	layoutBinding.descriptorCount = descriptorCount;
	layoutBinding.descriptorType = inType;
	layoutBinding.stageFlags = inStages;

	auto bindingIt = std::find_if(
		m_layoutBindings.begin(),
		m_layoutBindings.end(),
		[inBinding](const VkDescriptorSetLayoutBinding& binding)
		{
			return binding.binding == inBinding;
		});

	if (bindingIt != m_layoutBindings.end())
	{
		const size_t bindingIndex = static_cast<size_t>(std::distance(m_layoutBindings.begin(), bindingIt));
		*bindingIt = layoutBinding;
		m_bindingFlags[bindingIndex] = bindingFlags;
	}
	else
	{
		m_layoutBindings.push_back(layoutBinding);
		m_bindingFlags.push_back(bindingFlags);
	}
}

void DescriptorSetLayout::Create(const DescriptorSetLayoutCreateInfo& inCreateInfo)
{
	VkDescriptorSetLayoutCreateInfo createInfo{};
	VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
	auto& device = MyDevice::GetInstance();

	createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	createInfo.bindingCount = static_cast<uint32_t>(inCreateInfo.m_layoutBindings.size());
	createInfo.pBindings = inCreateInfo.m_layoutBindings.data();

	m_descriptorBindings.clear();
	m_bindingToIndex.clear();
	m_descriptorBindings.reserve(inCreateInfo.m_layoutBindings.size());
	m_requiredPoolFlags = inCreateInfo.m_poolFlags;
	for (size_t i = 0; i < inCreateInfo.m_layoutBindings.size(); ++i)
	{
		const auto& currentBinding = inCreateInfo.m_layoutBindings[i];

		m_descriptorBindings.push_back(currentBinding);
		m_bindingToIndex[currentBinding.binding] = i;
	}

	const bool hasBindingFlags = std::any_of(
		inCreateInfo.m_bindingFlags.begin(),
		inCreateInfo.m_bindingFlags.end(),
		[](VkDescriptorBindingFlags flags)
		{
			return flags != 0;
		});

	if (hasBindingFlags)
	{
		CHECK_TRUE(inCreateInfo.m_bindingFlags.size() == inCreateInfo.m_layoutBindings.size());
		flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagInfo.pBindingFlags = inCreateInfo.m_bindingFlags.data();
		flagInfo.bindingCount = static_cast<uint32_t>(inCreateInfo.m_bindingFlags.size());
		createInfo.pNext = &flagInfo;
	}

	m_vkDescriptorSetLayout = device.CreateDescriptorSetLayout(createInfo);
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

void DescriptorSetLayout::Destroy()
{
	if (m_vkDescriptorSetLayout != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyDescriptorSetLayout(m_vkDescriptorSetLayout);
		m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	}
	m_descriptorBindings.clear();
	m_bindingToIndex.clear();
}

void DescriptorState::Reset()
{
	m_bindingInfo.clear();
	m_writeInfo = {};
}

void DescriptorState::SetBinding(const VkDescriptorBufferInfo& inBindingInfo, uint32_t inIndex)
{
	while (m_bindingInfo.size() <= inIndex)
	{
		m_bindingInfo.push_back(VkDescriptorBufferInfo{});
	}
	m_bindingInfo[inIndex] = inBindingInfo;
}

void DescriptorState::SetBinding(const VkDescriptorImageInfo& inBindingInfo, uint32_t inIndex)
{
	while (m_bindingInfo.size() <= inIndex)
	{
		m_bindingInfo.push_back(VkDescriptorImageInfo{});
	}
	m_bindingInfo[inIndex] = inBindingInfo;
}

void DescriptorState::SetBinding(const VkBufferView& inBindingInfo, uint32_t inIndex)
{
	while (m_bindingInfo.size() <= inIndex)
	{
		m_bindingInfo.push_back(VkBufferView{});
	}
	m_bindingInfo[inIndex] = inBindingInfo;
}

void DescriptorState::SetBinding(const VkAccelerationStructureKHR& inBindingInfo, uint32_t inIndex)
{
	while (m_bindingInfo.size() <= inIndex)
	{
		m_bindingInfo.push_back(VkAccelerationStructureKHR{});
	}
	m_bindingInfo[inIndex] = inBindingInfo;
}

bool operator==(const DescriptorState& lhs, const DescriptorState& rhs)
{
	if (lhs.m_writeInfo.dstArrayElement != rhs.m_writeInfo.dstArrayElement
		|| lhs.m_bindingInfo.size() != rhs.m_bindingInfo.size())
	{
		return false;
	}

	for (size_t i = 0; i < lhs.m_bindingInfo.size(); ++i)
	{
		if (!IsEqual(lhs.m_bindingInfo[i], rhs.m_bindingInfo[i]))
		{
			return false;
		}
	}

	return true;
}

bool operator!=(const DescriptorState& lhs, const DescriptorState& rhs)
{
	return !(lhs == rhs);
}

void DescriptorSetState::Reset()
{
	m_mapBindingToDescriptor.clear();
}

void DescriptorSetState::SetBinding(DescriptorState inBindingInfo, uint32_t inBinding)
{
	m_mapBindingToDescriptor[inBinding] = std::move(inBindingInfo);
}

void DescriptorSet::WriteBindings(const DescriptorSetState& inBinding)
{
	CHECK_TRUE(m_vkDescriptorSet != VK_NULL_HANDLE, "DescriptorSet must be created before writing bindings!");

	std::vector<VkWriteDescriptorSet> writes;
	std::vector<std::vector<VkDescriptorBufferInfo>> bufferInfoStorage;
	std::vector<std::vector<VkDescriptorImageInfo>> imageInfoStorage;
	std::vector<std::vector<VkBufferView>> bufferViewStorage;
	std::vector<std::vector<VkAccelerationStructureKHR>> accelerationStructureStorage;
	std::vector<std::unique_ptr<VkWriteDescriptorSetAccelerationStructureKHR>> accelerationStructureWriteStorage;

	const size_t updateCount = inBinding.m_mapBindingToDescriptor.size();
	writes.reserve(updateCount);
	bufferInfoStorage.reserve(updateCount);
	imageInfoStorage.reserve(updateCount);
	bufferViewStorage.reserve(updateCount);
	accelerationStructureStorage.reserve(updateCount);
	accelerationStructureWriteStorage.reserve(updateCount);

	for (const auto& [bindingId, descriptorState] : inBinding.m_mapBindingToDescriptor)
	{
		if (descriptorState.m_bindingInfo.empty())
		{
			continue;
		}

		const auto metadataIt = m_bindingLayoutMetadata.find(bindingId);
		CHECK_TRUE(metadataIt != m_bindingLayoutMetadata.end(), "DescriptorState contains a binding not present in the descriptor set layout!");
		const BindingLayoutMetadata& layoutMetadata = metadataIt->second;
		CHECK_TRUE(
			descriptorState.m_writeInfo.dstArrayElement + descriptorState.m_bindingInfo.size() <= layoutMetadata.descriptorCount,
			"DescriptorState has more bindings than the descriptor set layout allows!");

		auto cachedIt = m_cachedState.m_mapBindingToDescriptor.find(bindingId);
		CHECK_TRUE(cachedIt != m_cachedState.m_mapBindingToDescriptor.end(), "DescriptorSet cache is missing a layout binding!");
		CHECK_TRUE(
			cachedIt->second.m_bindingInfo.size() >= descriptorState.m_writeInfo.dstArrayElement + descriptorState.m_bindingInfo.size(),
			"DescriptorSet cached state is smaller than the update range!");

		bool isStateChanged = false;
		for (size_t i = 0; i < descriptorState.m_bindingInfo.size(); ++i)
		{
			const size_t cachedIndex = descriptorState.m_writeInfo.dstArrayElement + i;
			if (!IsEqual(cachedIt->second.m_bindingInfo[cachedIndex], descriptorState.m_bindingInfo[i]))
			{
				isStateChanged = true;
				break;
			}
		}
		if (!isStateChanged)
		{
			continue;
		}

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = m_vkDescriptorSet;
		write.dstBinding = bindingId;
		write.dstArrayElement = descriptorState.m_writeInfo.dstArrayElement;
		write.descriptorType = layoutMetadata.descriptorType;
		write.descriptorCount = static_cast<uint32_t>(descriptorState.m_bindingInfo.size());

		if (IsBufferDescriptorType(layoutMetadata.descriptorType))
		{
			auto& currentStorage = bufferInfoStorage.emplace_back();
			currentStorage.reserve(descriptorState.m_bindingInfo.size());
			for (const auto& bindingInfo : descriptorState.m_bindingInfo)
			{
				const auto* pBufferInfo = std::get_if<VkDescriptorBufferInfo>(&bindingInfo);
				CHECK_TRUE(pBufferInfo != nullptr, "DescriptorState binding info does not match buffer descriptor type!");
				currentStorage.push_back(*pBufferInfo);
			}
			write.pBufferInfo = currentStorage.data();
		}
		else if (IsImageDescriptorType(layoutMetadata.descriptorType))
		{
			auto& currentStorage = imageInfoStorage.emplace_back();
			currentStorage.reserve(descriptorState.m_bindingInfo.size());
			for (const auto& bindingInfo : descriptorState.m_bindingInfo)
			{
				const auto* pImageInfo = std::get_if<VkDescriptorImageInfo>(&bindingInfo);
				CHECK_TRUE(pImageInfo != nullptr, "DescriptorState binding info does not match image descriptor type!");
				currentStorage.push_back(*pImageInfo);
			}
			write.pImageInfo = currentStorage.data();
		}
		else if (IsTexelBufferDescriptorType(layoutMetadata.descriptorType))
		{
			auto& currentStorage = bufferViewStorage.emplace_back();
			currentStorage.reserve(descriptorState.m_bindingInfo.size());
			for (const auto& bindingInfo : descriptorState.m_bindingInfo)
			{
				const auto* pBufferView = std::get_if<VkBufferView>(&bindingInfo);
				CHECK_TRUE(pBufferView != nullptr, "DescriptorState binding info does not match texel buffer descriptor type!");
				currentStorage.push_back(*pBufferView);
			}
			write.pTexelBufferView = currentStorage.data();
		}
		else if (layoutMetadata.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
		{
			auto& currentStorage = accelerationStructureStorage.emplace_back();
			currentStorage.reserve(descriptorState.m_bindingInfo.size());
			for (const auto& bindingInfo : descriptorState.m_bindingInfo)
			{
				const auto* pAccelerationStructure = std::get_if<VkAccelerationStructureKHR>(&bindingInfo);
				CHECK_TRUE(pAccelerationStructure != nullptr, "DescriptorState binding info does not match acceleration structure descriptor type!");
				currentStorage.push_back(*pAccelerationStructure);
			}

			auto accelerationStructureWrite = std::make_unique<VkWriteDescriptorSetAccelerationStructureKHR>();
			accelerationStructureWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
			accelerationStructureWrite->accelerationStructureCount = static_cast<uint32_t>(currentStorage.size());
			accelerationStructureWrite->pAccelerationStructures = currentStorage.data();
			write.pNext = accelerationStructureWrite.get();
			accelerationStructureWriteStorage.push_back(std::move(accelerationStructureWrite));
		}
		else
		{
			throw std::runtime_error("Unsupported descriptor type!");
		}

		writes.push_back(write);
		for (size_t i = 0; i < descriptorState.m_bindingInfo.size(); ++i)
		{
			cachedIt->second.m_bindingInfo[descriptorState.m_writeInfo.dstArrayElement + i] = descriptorState.m_bindingInfo[i];
		}
	}

	if (!writes.empty())
	{
		MyDevice::GetInstance().UpdateDescriptorSets(writes);
	}
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

void DescriptorSetAllocator::Create()
{
}

void DescriptorSetAllocator::Destroy()
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
