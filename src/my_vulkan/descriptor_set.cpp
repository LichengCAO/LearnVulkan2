#include "descriptor_set.h"
#include "device.h"
#include "buffer.h"
#include "utility/hash_util.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace
{
	bool _IsEqual(const VkDescriptorBufferInfo& lhs, const VkDescriptorBufferInfo& rhs)
	{
		return lhs.buffer == rhs.buffer
			&& lhs.offset == rhs.offset
			&& lhs.range == rhs.range;
	}

	bool _IsEqual(const VkDescriptorImageInfo& lhs, const VkDescriptorImageInfo& rhs)
	{
		return lhs.sampler == rhs.sampler
			&& lhs.imageView == rhs.imageView
			&& lhs.imageLayout == rhs.imageLayout;
	}

	bool _IsEqual(const DescriptorState::DescriptorBindingInfo& lhs, const DescriptorState::DescriptorBindingInfo& rhs)
	{
		if (lhs.index() != rhs.index())
		{
			return false;
		}

		if (const auto* pLhs = std::get_if<VkDescriptorBufferInfo>(&lhs))
		{
			return _IsEqual(*pLhs, std::get<VkDescriptorBufferInfo>(rhs));
		}
		if (const auto* pLhs = std::get_if<VkDescriptorImageInfo>(&lhs))
		{
			return _IsEqual(*pLhs, std::get<VkDescriptorImageInfo>(rhs));
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

	bool _IsImageDescriptorType(VkDescriptorType inType)
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

	bool _IsBufferDescriptorType(VkDescriptorType inType)
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

	bool _IsTexelBufferDescriptorType(VkDescriptorType inType)
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

	struct DescriptorWriteBundle
	{
		struct BindingLayoutMetadata
		{
			VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
			uint32_t descriptorCount = 0;
		};

		std::unordered_map<uint32_t, BindingLayoutMetadata> layoutMetadata;
		std::vector<VkWriteDescriptorSet> writes;
		std::vector<std::vector<VkDescriptorBufferInfo>> bufferInfoStorage;
		std::vector<std::vector<VkDescriptorImageInfo>> imageInfoStorage;
		std::vector<std::vector<VkBufferView>> bufferViewStorage;
		std::vector<std::vector<VkAccelerationStructureKHR>> accelerationStructureStorage;
		std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureWriteStorage;
	};

	static void _FillDescriptorWrite(
		VkDescriptorSet inDescriptorSet,
		const DescriptorSetState& inDescriptorSetState,
		DescriptorWriteBundle& inOutWriteBundle)
	{
		const auto& mapBindingToDescriptor = inDescriptorSetState.GetMapBindingToDescriptor();

		inOutWriteBundle.writes.clear();
		inOutWriteBundle.bufferInfoStorage.clear();
		inOutWriteBundle.imageInfoStorage.clear();
		inOutWriteBundle.bufferViewStorage.clear();
		inOutWriteBundle.accelerationStructureStorage.clear();
		inOutWriteBundle.accelerationStructureWriteStorage.clear();

		inOutWriteBundle.writes.reserve(mapBindingToDescriptor.size());
		inOutWriteBundle.bufferInfoStorage.reserve(mapBindingToDescriptor.size());
		inOutWriteBundle.imageInfoStorage.reserve(mapBindingToDescriptor.size());
		inOutWriteBundle.bufferViewStorage.reserve(mapBindingToDescriptor.size());
		inOutWriteBundle.accelerationStructureStorage.reserve(mapBindingToDescriptor.size());
		inOutWriteBundle.accelerationStructureWriteStorage.reserve(mapBindingToDescriptor.size());

		for (const auto& [bindingId, descriptorState] : mapBindingToDescriptor)
		{
			const auto& bindingInfoList = descriptorState.GetBindingInfo();
			if (bindingInfoList.empty())
			{
				continue;
			}

			const auto metadataIt = inOutWriteBundle.layoutMetadata.find(bindingId);
			CHECK_TRUE(metadataIt != inOutWriteBundle.layoutMetadata.end(), "DescriptorState contains a binding not present in the descriptor set layout!");
			const auto& layoutMetadataEntry = metadataIt->second;
			CHECK_TRUE(
				descriptorState.GetDstArrayElement() + bindingInfoList.size() <= layoutMetadataEntry.descriptorCount,
				"DescriptorState has more bindings than the descriptor set layout allows!");

			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = inDescriptorSet;
			write.dstBinding = bindingId;
			write.dstArrayElement = descriptorState.GetDstArrayElement();
			write.descriptorType = layoutMetadataEntry.descriptorType;
			write.descriptorCount = static_cast<uint32_t>(bindingInfoList.size());

			if (_IsBufferDescriptorType(layoutMetadataEntry.descriptorType))
			{
				auto& currentStorage = inOutWriteBundle.bufferInfoStorage.emplace_back();
				currentStorage.reserve(bindingInfoList.size());
				for (const auto& bindingInfo : bindingInfoList)
				{
					const auto* pBufferInfo = std::get_if<VkDescriptorBufferInfo>(&bindingInfo);
					CHECK_TRUE(pBufferInfo != nullptr, "DescriptorState binding info does not match buffer descriptor type!");
					currentStorage.push_back(*pBufferInfo);
				}
				write.pBufferInfo = currentStorage.data();
			}
			else if (_IsImageDescriptorType(layoutMetadataEntry.descriptorType))
			{
				auto& currentStorage = inOutWriteBundle.imageInfoStorage.emplace_back();
				currentStorage.reserve(bindingInfoList.size());
				for (const auto& bindingInfo : bindingInfoList)
				{
					const auto* pImageInfo = std::get_if<VkDescriptorImageInfo>(&bindingInfo);
					CHECK_TRUE(pImageInfo != nullptr, "DescriptorState binding info does not match image descriptor type!");
					currentStorage.push_back(*pImageInfo);
				}
				write.pImageInfo = currentStorage.data();
			}
			else if (_IsTexelBufferDescriptorType(layoutMetadataEntry.descriptorType))
			{
				auto& currentStorage = inOutWriteBundle.bufferViewStorage.emplace_back();
				currentStorage.reserve(bindingInfoList.size());
				for (const auto& bindingInfo : bindingInfoList)
				{
					const auto* pBufferView = std::get_if<VkBufferView>(&bindingInfo);
					CHECK_TRUE(pBufferView != nullptr, "DescriptorState binding info does not match texel buffer descriptor type!");
					currentStorage.push_back(*pBufferView);
				}
				write.pTexelBufferView = currentStorage.data();
			}
			else if (layoutMetadataEntry.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
			{
				auto& currentStorage = inOutWriteBundle.accelerationStructureStorage.emplace_back();
				currentStorage.reserve(bindingInfoList.size());
				for (const auto& bindingInfo : bindingInfoList)
				{
					const auto* pAccelerationStructure = std::get_if<VkAccelerationStructureKHR>(&bindingInfo);
					CHECK_TRUE(pAccelerationStructure != nullptr, "DescriptorState binding info does not match acceleration structure descriptor type!");
					currentStorage.push_back(*pAccelerationStructure);
				}

				auto& accelerationStructureWrite = inOutWriteBundle.accelerationStructureWriteStorage.emplace_back();
				accelerationStructureWrite = {};
				accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
				accelerationStructureWrite.accelerationStructureCount = static_cast<uint32_t>(currentStorage.size());
				accelerationStructureWrite.pAccelerationStructures = currentStorage.data();
				write.pNext = &accelerationStructureWrite;
			}
			else
			{
				throw std::runtime_error("Unsupported descriptor type!");
			}

			inOutWriteBundle.writes.push_back(write);
		}
	}

	DescriptorState _MakeNullDescriptorState(const VkDescriptorSetLayoutBinding& inLayoutBinding)
	{
		DescriptorState state{};
		const uint32_t descriptorCount = inLayoutBinding.descriptorCount;

		if (_IsBufferDescriptorType(inLayoutBinding.descriptorType))
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkDescriptorBufferInfo{}, i);
			}
		}
		else if (_IsImageDescriptorType(inLayoutBinding.descriptorType))
		{
			for (uint32_t i = 0; i < descriptorCount; ++i)
			{
				state.SetBinding(VkDescriptorImageInfo{}, i);
			}
		}
		else if (_IsTexelBufferDescriptorType(inLayoutBinding.descriptorType))
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

void DynamicDescriptorSetAllocator::AllocateInfo::SetLayout(const DescriptorSetLayout* inLayout)
{
	m_descriptorSetLayout = inLayout;
	m_vkDescriptorSetLayout = inLayout != nullptr ? inLayout->GetVkDescriptorSetLayout() : VK_NULL_HANDLE;
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
	for (const auto& [bindingId, layoutBinding] : descriptorSetLayout.m_descriptorBindings)
	{
		BindingLayoutMetadata metadata{};
		metadata.descriptorType = layoutBinding.descriptorType;
		metadata.descriptorCount = layoutBinding.descriptorCount;
		m_bindingLayoutMetadata[bindingId] = metadata;
		m_cachedState.SetBinding(_MakeNullDescriptorState(layoutBinding), bindingId);
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
	m_descriptorBindings.reserve(inCreateInfo.m_layoutBindings.size());
	m_requiredPoolFlags = inCreateInfo.m_poolFlags;
	for (const VkDescriptorSetLayoutBinding& currentBinding : inCreateInfo.m_layoutBindings)
	{
		m_descriptorBindings[currentBinding.binding] = currentBinding;
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
	return m_descriptorBindings.at(inBinding);
}

void DescriptorSetLayout::Destroy()
{
	if (m_vkDescriptorSetLayout != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyDescriptorSetLayout(m_vkDescriptorSetLayout);
		m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	}
	m_descriptorBindings.clear();
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
		if (!_IsEqual(lhs.m_bindingInfo[i], rhs.m_bindingInfo[i]))
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

	DescriptorSetState changedBindingState;
	DescriptorWriteBundle writeBundle;

	writeBundle.layoutMetadata.reserve(inBinding.m_mapBindingToDescriptor.size());

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
			if (!_IsEqual(cachedIt->second.m_bindingInfo[cachedIndex], descriptorState.m_bindingInfo[i]))
			{
				isStateChanged = true;
				break;
			}
		}
		if (!isStateChanged)
		{
			continue;
		}

		changedBindingState.SetBinding(descriptorState, bindingId);
		writeBundle.layoutMetadata[bindingId] = { layoutMetadata.descriptorType, layoutMetadata.descriptorCount };
		for (size_t i = 0; i < descriptorState.m_bindingInfo.size(); ++i)
		{
			cachedIt->second.m_bindingInfo[descriptorState.m_writeInfo.dstArrayElement + i] = descriptorState.m_bindingInfo[i];
		}
	}

	_FillDescriptorWrite(m_vkDescriptorSet, changedBindingState, writeBundle);
	if (!writeBundle.writes.empty())
	{
		MyDevice::GetInstance().UpdateDescriptorSets(writeBundle.writes);
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
		auto& freePools = m_freePools[p.first];
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

auto DescriptorSetAllocator::AllocateDescriptorSet(
	VkDescriptorSetLayout inLayout,
	VkDescriptorPoolCreateFlags inPoolFlags,
	const void* inNextPtr) -> std::pair<VkDescriptorSet, VkResult>
{
	auto& device = MyDevice::GetInstance();
	VkDescriptorPool candidatePool = VK_NULL_HANDLE;
	VkResult allocResult = VK_SUCCESS;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

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

	descriptorSet = device.AllocateDescriptorSet(
		inLayout,
		candidatePool,
		allocResult,
		inNextPtr);

	if (allocResult == VK_ERROR_FRAGMENTED_POOL || allocResult == VK_ERROR_OUT_OF_POOL_MEMORY)
	{
		candidatePool = _GrabPool(inPoolFlags);
		m_candidatePool[inPoolFlags] = candidatePool;
		m_usedPools[inPoolFlags].push_back(candidatePool);

		descriptorSet = device.AllocateDescriptorSet(
			inLayout,
			candidatePool,
			allocResult,
			inNextPtr);
	}

	return { descriptorSet, allocResult };
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

auto DynamicDescriptorSetAllocator::_HashDescriptorBindingInfo(const DescriptorState::DescriptorBindingInfo& inInfo) const -> size_t
{
	size_t result = 0;
	::hash_combine(result, inInfo.index());

	if (const auto* pBufferInfo = std::get_if<VkDescriptorBufferInfo>(&inInfo))
	{
		::hash_combine(result, *pBufferInfo);
	}
	else if (const auto* pImageInfo = std::get_if<VkDescriptorImageInfo>(&inInfo))
	{
		::hash_combine(result, *pImageInfo);
	}
	else if (const auto* pBufferView = std::get_if<VkBufferView>(&inInfo))
	{
		::hash_combine(result, *pBufferView);
	}
	else if (const auto* pAccel = std::get_if<VkAccelerationStructureKHR>(&inInfo))
	{
		::hash_combine(result, *pAccel);
	}

	return result;
}

auto DynamicDescriptorSetAllocator::_HashDescriptorState(const DescriptorState& inState) const -> size_t
{
	size_t result = 0;
	::hash_combine(result, inState.m_writeInfo.dstArrayElement);
	::hash_combine(result, inState.m_bindingInfo.size());
	for (const auto& bindingInfo : inState.m_bindingInfo)
	{
		::hash_combine(result, _HashDescriptorBindingInfo(bindingInfo));
	}
	return result;
}

auto DynamicDescriptorSetAllocator::_HashDescriptorSetState(const DescriptorSetState& inState) const -> size_t
{
	size_t result = 0;
	std::vector<uint32_t> sortedBindings;

	::hash_combine(result, inState.m_mapBindingToDescriptor.size());
	sortedBindings.reserve(inState.m_mapBindingToDescriptor.size());
	for (const auto& [bindingId, descriptorState] : inState.m_mapBindingToDescriptor)
	{
		sortedBindings.push_back(bindingId);
	}
	std::sort(sortedBindings.begin(), sortedBindings.end());

	for (uint32_t bindingId : sortedBindings)
	{
		::hash_combine(result, bindingId);
		::hash_combine(result, _HashDescriptorState(inState.m_mapBindingToDescriptor.at(bindingId)));
	}
	return result;
}

void DynamicDescriptorSetAllocator::_WriteDescriptorSet(VkDescriptorSet inDescriptorSet, const AllocateInfo& inAllocateInfo)
{
	CHECK_TRUE(inDescriptorSet != VK_NULL_HANDLE, "Invalid descriptor set!");
	if (inAllocateInfo.m_state.m_mapBindingToDescriptor.empty())
	{
		return;
	}
	CHECK_TRUE(inAllocateInfo.m_descriptorSetLayout != nullptr, "Dynamic descriptor allocation requires a DescriptorSetLayout for writes!");

	DescriptorWriteBundle writeBundle;
	for (const auto& [bindingId, layoutBinding] : inAllocateInfo.m_descriptorSetLayout->m_descriptorBindings)
	{
		writeBundle.layoutMetadata[bindingId] = { layoutBinding.descriptorType, layoutBinding.descriptorCount };
	}

	_FillDescriptorWrite(inDescriptorSet, inAllocateInfo.m_state, writeBundle);
	if (!writeBundle.writes.empty())
	{
		MyDevice::GetInstance().UpdateDescriptorSets(writeBundle.writes);
	}
}

auto DynamicDescriptorSetAllocator::_GetCachedDescriptorSet(const AllocateInfo& inAllocateInfo) -> std::optional<VkDescriptorSet>
{
	const auto layoutIt = m_cachedDescriptorSets.find(inAllocateInfo.m_vkDescriptorSetLayout);
	if (layoutIt == m_cachedDescriptorSets.end())
	{
		return std::nullopt;
	}

	const size_t stateHash = _HashDescriptorSetState(inAllocateInfo.m_state);
	const auto cachedIt = layoutIt->second.mapHashToDescriptorSet.find(stateHash);
	if (cachedIt == layoutIt->second.mapHashToDescriptorSet.end())
	{
		return std::nullopt;
	}

	return cachedIt->second;
}

auto DynamicDescriptorSetAllocator::_AllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet
{
	CHECK_TRUE(inAllocateInfo.m_vkDescriptorSetLayout != VK_NULL_HANDLE, "Dynamic descriptor allocator requires a descriptor set layout!");
	CHECK_TRUE(m_uptrDescriptorSetAllocator != nullptr, "Dynamic descriptor allocator must be created before allocation!");

	auto [descriptorSet, allocResult] = m_uptrDescriptorSetAllocator->AllocateDescriptorSet(inAllocateInfo.m_vkDescriptorSetLayout);

	VK_CHECK(allocResult, "Failed to allocate dynamic descriptor set!");
	_WriteDescriptorSet(descriptorSet, inAllocateInfo);

	const size_t stateHash = _HashDescriptorSetState(inAllocateInfo.m_state);
	m_cachedDescriptorSets[inAllocateInfo.m_vkDescriptorSetLayout].mapHashToDescriptorSet[stateHash] = descriptorSet;
	return descriptorSet;
}

void DynamicDescriptorSetAllocator::Create()
{
	m_uptrDescriptorSetAllocator = std::make_unique<DescriptorSetAllocator>();
	m_uptrDescriptorSetAllocator->Create();
}

void DynamicDescriptorSetAllocator::Reset()
{
	CHECK_TRUE(m_uptrDescriptorSetAllocator != nullptr, "Dynamic descriptor allocator must be created before reset!");
	m_uptrDescriptorSetAllocator->ResetPools();
	m_cachedDescriptorSets.clear();
}

auto DynamicDescriptorSetAllocator::GetOrAllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet
{
	CHECK_TRUE(inAllocateInfo.m_vkDescriptorSetLayout != VK_NULL_HANDLE, "Dynamic descriptor allocator requires a descriptor set layout!");

	if (auto cached = _GetCachedDescriptorSet(inAllocateInfo); cached.has_value())
	{
		return cached.value();
	}

	return _AllocateDescriptorSet(inAllocateInfo);
}

void DynamicDescriptorSetAllocator::Destroy()
{
	if (m_uptrDescriptorSetAllocator != nullptr)
	{
		m_uptrDescriptorSetAllocator->Destroy();
		m_uptrDescriptorSetAllocator.reset();
	}

	m_cachedDescriptorSets.clear();
}
