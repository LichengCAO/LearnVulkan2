#include "descriptor_set.h"

#include "../allocator/descriptor_set_allocator.h"
#include "../allocator/descriptor_set_layout_allocator.h"
#include "../device.h"
#include "acceleration_structure.h"
#include "buffer.h"
#include "image.h"
#include "sampler.h"
#include "../utility/hash_util.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace
{
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
		return inType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
			|| inType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	}

	struct DescriptorWriteBundle final
	{
		std::vector<VkWriteDescriptorSet> writes;
		std::vector<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkDescriptorImageInfo> imageInfos;
		std::vector<VkBufferView> bufferViews;
		std::vector<VkAccelerationStructureKHR> accelerationStructures;
		std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationWrites;

		void Reserve(size_t inCount)
		{
			writes.reserve(inCount);
			bufferInfos.reserve(inCount);
			imageInfos.reserve(inCount);
			bufferViews.reserve(inCount);
			accelerationStructures.reserve(inCount);
			accelerationWrites.reserve(inCount);
		}
	};

	void _CheckResource(const DescriptorSetDesc::DescriptorBindingInfo& inResource)
	{
		std::visit(
			[](const auto& resource)
			{
				if constexpr (requires { resource.buffer; })
				{
					CHECK_TRUE(resource.buffer != nullptr, "Descriptor buffer resource cannot be null!");
				}
				else if constexpr (requires { resource.imageView; })
				{
					CHECK_TRUE(resource.imageView != nullptr, "Descriptor image view cannot be null!");
				}
				else
				{
					CHECK_TRUE(resource != nullptr, "Descriptor resource cannot be null!");
				}
			},
			inResource);
	}

	template <typename ItemType>
	void _AppendWrite(
		VkDescriptorSet inDescriptorSet,
		const ItemType& inItem,
		VkDescriptorType inDescriptorType,
		DescriptorWriteBundle& inOutBundle)
	{
		_CheckResource(inItem.resource);

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = inDescriptorSet;
		write.dstBinding = inItem.binding;
		write.dstArrayElement = inItem.arrayElement;
		write.descriptorCount = 1;
		write.descriptorType = inDescriptorType;

		if (_IsBufferDescriptorType(inDescriptorType))
		{
			const auto* bufferBinding = std::get_if<0>(&inItem.resource);
			CHECK_TRUE(bufferBinding != nullptr, "Descriptor resource does not match buffer descriptor type!");
			VkDescriptorBufferInfo& bufferInfo = inOutBundle.bufferInfos.emplace_back();
			bufferInfo.buffer = bufferBinding->buffer->GetVkBuffer();
			bufferInfo.offset = bufferBinding->offset;
			bufferInfo.range = bufferBinding->range;
			write.pBufferInfo = &bufferInfo;
		}
		else if (inDescriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
		{
			const auto* sampler = std::get_if<const Sampler*>(&inItem.resource);
			CHECK_TRUE(sampler != nullptr && *sampler != nullptr, "Sampler descriptor requires a sampler!");
			VkDescriptorImageInfo& imageInfo = inOutBundle.imageInfos.emplace_back();
			imageInfo.sampler = (*sampler)->GetVkSampler();
			write.pImageInfo = &imageInfo;
		}
		else if (_IsImageDescriptorType(inDescriptorType))
		{
			const auto* imageBinding = std::get_if<1>(&inItem.resource);
			CHECK_TRUE(imageBinding != nullptr, "Descriptor resource does not match image descriptor type!");
			if (inDescriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				CHECK_TRUE(imageBinding->sampler != nullptr, "Combined image sampler requires a sampler!");
			}
			else
			{
				CHECK_TRUE(imageBinding->sampler == nullptr, "Image descriptor must not include a sampler!");
			}
			VkDescriptorImageInfo& imageInfo = inOutBundle.imageInfos.emplace_back();
			imageInfo.imageView = imageBinding->imageView->GetVkImageView();
			imageInfo.imageLayout = imageBinding->imageLayout;
			imageInfo.sampler = imageBinding->sampler != nullptr
				? imageBinding->sampler->GetVkSampler()
				: VK_NULL_HANDLE;
			write.pImageInfo = &imageInfo;
		}
		else if (_IsTexelBufferDescriptorType(inDescriptorType))
		{
			const auto* bufferView = std::get_if<const BufferView*>(&inItem.resource);
			CHECK_TRUE(bufferView != nullptr && *bufferView != nullptr, "Texel buffer descriptor requires a buffer view!");
			VkBufferView& view = inOutBundle.bufferViews.emplace_back((*bufferView)->GetVkBufferView());
			write.pTexelBufferView = &view;
		}
		else if (inDescriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
		{
			const auto* accelerationStructure = std::get_if<const TopLevelAccelStruct*>(&inItem.resource);
			CHECK_TRUE(
				accelerationStructure != nullptr && *accelerationStructure != nullptr,
				"Acceleration structure descriptor requires a top-level acceleration structure!");
			VkAccelerationStructureKHR& handle = inOutBundle.accelerationStructures.emplace_back(
				(*accelerationStructure)->GetVkAccelerationStructure());
			VkWriteDescriptorSetAccelerationStructureKHR& accelerationWrite =
				inOutBundle.accelerationWrites.emplace_back();
			accelerationWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			accelerationWrite.accelerationStructureCount = 1;
			accelerationWrite.pAccelerationStructures = &handle;
			write.pNext = &accelerationWrite;
		}
		else
		{
			throw std::runtime_error("Unsupported descriptor type!");
		}

		inOutBundle.writes.push_back(write);
	}

	template <typename ItemType>
	void _ValidateAndBuildWrites(
		VkDescriptorSet inDescriptorSet,
		const std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>& inLayoutBindings,
		const std::vector<ItemType>& inItems,
		DescriptorWriteBundle& outBundle)
	{
		outBundle.Reserve(inItems.size());
		std::set<std::pair<uint32_t, uint32_t>> writtenElements;
		for (const ItemType& item : inItems)
		{
			const auto bindingIt = inLayoutBindings.find(item.binding);
			CHECK_TRUE(bindingIt != inLayoutBindings.end(), "Descriptor binding is not present in the layout!");
			const VkDescriptorSetLayoutBinding& layoutBinding = bindingIt->second;
			CHECK_TRUE(item.arrayElement < layoutBinding.descriptorCount, "Descriptor array element is out of range!");
			CHECK_TRUE(
				writtenElements.emplace(item.binding, item.arrayElement).second,
				"Descriptor binding array element was specified more than once!");
			_AppendWrite(inDescriptorSet, item, layoutBinding.descriptorType, outBundle);
		}
	}

	void _HashPointer(size_t& ioSeed, const void* inPointer)
	{
		hash_combine(ioSeed, reinterpret_cast<std::uintptr_t>(inPointer));
	}
}

auto DescriptorSetLayoutCreateInfo::SetBinding(
	uint32_t inBinding,
	VkDescriptorType inType,
	VkShaderStageFlags inStages,
	uint32_t inDescriptorCount) -> DescriptorSetLayoutCreateInfo&
{
	CHECK_TRUE(inDescriptorCount > 0, "Descriptor count must be greater than zero!");
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = inBinding;
	binding.descriptorType = inType;
	binding.stageFlags = inStages;
	binding.descriptorCount = inDescriptorCount;
	m_descriptorBindings[inBinding] = binding;
	return *this;
}

DescriptorSetLayout::~DescriptorSetLayout() = default;

void DescriptorSetLayout::Create(const DescriptorSetLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(m_vkDescriptorSetLayout == VK_NULL_HANDLE, "Descriptor set layout is already created!");
	m_descriptorBindings = inCreateInfo.m_descriptorBindings;
	DescriptorSetLayoutAllocator* allocator = MyDevice::GetInstance().GetDescriptorSetLayoutAllocator();
	CHECK_TRUE(allocator != nullptr, "Descriptor set layout allocator is not created!");
	m_vkDescriptorSetLayout = allocator->Allocate(*this);
}

void DescriptorSetLayout::Destroy()
{
	m_vkDescriptorSetLayout = VK_NULL_HANDLE;
	m_descriptorBindings.clear();
}

auto DescriptorSetLayout::GetVkDescriptorSetLayout() const -> VkDescriptorSetLayout
{
	return m_vkDescriptorSetLayout;
}

auto DescriptorSetLayout::GetDescriptorSetLayoutBinding(uint32_t inBinding) const
	-> const VkDescriptorSetLayoutBinding&
{
	return m_descriptorBindings.at(inBinding);
}

void DescriptorSetDesc::_SetBinding(
	uint32_t inBinding,
	DescriptorBindingInfo inBindingInfo,
	uint32_t inArrayElement)
{
	for (Item& item : m_items)
	{
		if (item.binding == inBinding && item.arrayElement == inArrayElement)
		{
			item.resource = std::move(inBindingInfo);
			return;
		}
	}
	m_items.push_back(Item{ inBinding, inArrayElement, std::move(inBindingInfo) });
}

void DescriptorSetDesc::SetBuffer(
	uint32_t inBinding,
	const Buffer* inBuffer,
	VkDeviceSize inOffset,
	VkDeviceSize inRange,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, BufferBinding{ inBuffer, inOffset, inRange }, inArrayElement);
}

void DescriptorSetDesc::SetImage(
	uint32_t inBinding,
	const ImageView* inImageView,
	VkImageLayout inImageLayout,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, ImageBinding{ inImageView, nullptr, inImageLayout }, inArrayElement);
}

void DescriptorSetDesc::SetCombinedImageSampler(
	uint32_t inBinding,
	const ImageView* inImageView,
	const Sampler* inSampler,
	VkImageLayout inImageLayout,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, ImageBinding{ inImageView, inSampler, inImageLayout }, inArrayElement);
}

void DescriptorSetDesc::SetSampler(uint32_t inBinding, const Sampler* inSampler, uint32_t inArrayElement)
{
	_SetBinding(inBinding, inSampler, inArrayElement);
}

void DescriptorSetDesc::SetBufferView(uint32_t inBinding, const BufferView* inBufferView, uint32_t inArrayElement)
{
	_SetBinding(inBinding, inBufferView, inArrayElement);
}

void DescriptorSetDesc::SetAccelerationStructure(
	uint32_t inBinding,
	const TopLevelAccelStruct* inAccelerationStructure,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, inAccelerationStructure, inArrayElement);
}

DescriptorSet::~DescriptorSet()
{
	Destroy();
}

void DescriptorSet::Create(const DescriptorSetCreateInfo& inCreateInfo)
{
	CHECK_TRUE(
		inCreateInfo.m_descriptorSetLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE,
		"Descriptor set create info must contain a valid layout!");
	CHECK_TRUE(m_vkDescriptorSet == VK_NULL_HANDLE, "Descriptor set is already created!");

	DescriptorWriteBundle writeBundle;
	const VkDescriptorSet descriptorSet = MyDevice::GetInstance().GetDescriptorSetAllocator()->AllocateDescriptorSet(
		inCreateInfo.m_descriptorSetLayout.GetVkDescriptorSetLayout());
	_ValidateAndBuildWrites(
		descriptorSet,
		inCreateInfo.m_descriptorSetLayout.m_descriptorBindings,
		inCreateInfo.m_descriptorSetDesc.m_items,
		writeBundle);
	if (!writeBundle.writes.empty())
	{
		MyDevice::GetInstance().UpdateDescriptorSets(writeBundle.writes);
	}

	m_vkDescriptorSet = descriptorSet;
	m_descriptorSetLayout = inCreateInfo.m_descriptorSetLayout;
	m_descriptorSetDesc = inCreateInfo.m_descriptorSetDesc;
}

void DescriptorSet::Destroy()
{
	m_vkDescriptorSet = VK_NULL_HANDLE;
	m_descriptorSetLayout = {};
	m_descriptorSetDesc = {};
}

auto DescriptorSet::GetVkDescriptorSet() const -> VkDescriptorSet
{
	return m_vkDescriptorSet;
}

void DynamicDescriptorSetAllocator::AllocateInfo::SetLayout(DescriptorSetLayout inLayout)
{
	m_descriptorSetLayout = std::move(inLayout);
	m_vkDescriptorSetLayout = m_descriptorSetLayout.GetVkDescriptorSetLayout();
}

auto DynamicDescriptorSetAllocator::_HashDescriptorBindingInfo(
	const DescriptorSetDesc::DescriptorBindingInfo& inInfo) const -> size_t
{
	size_t result = 0;
		std::visit(
			[&result](const auto& resource)
			{
			if constexpr (requires { resource.buffer; })
			{
				_HashPointer(result, resource.buffer);
				hash_combine(result, resource.offset);
				hash_combine(result, resource.range);
			}
			else if constexpr (requires { resource.imageView; })
			{
				_HashPointer(result, resource.imageView);
				_HashPointer(result, resource.sampler);
				hash_combine(result, static_cast<std::underlying_type_t<VkImageLayout>>(resource.imageLayout));
			}
			else
			{
				_HashPointer(result, resource);
			}
		},
		inInfo);
	return result;
}

auto DynamicDescriptorSetAllocator::_HashDescriptorSetDesc(const DescriptorSetDesc& inDesc) const -> size_t
{
	std::vector<const DescriptorSetDesc::Item*> sortedItems;
	sortedItems.reserve(inDesc.m_items.size());
	for (const DescriptorSetDesc::Item& item : inDesc.m_items)
	{
		sortedItems.push_back(&item);
	}
	std::sort(
		sortedItems.begin(),
		sortedItems.end(),
		[](const auto* lhs, const auto* rhs)
		{
			return lhs->binding != rhs->binding
				? lhs->binding < rhs->binding
				: lhs->arrayElement < rhs->arrayElement;
		});

	size_t result = 0;
	hash_combine(result, sortedItems.size());
	for (const DescriptorSetDesc::Item* item : sortedItems)
	{
		hash_combine(result, item->binding);
		hash_combine(result, item->arrayElement);
		hash_combine(result, item->resource.index());
		hash_combine(result, _HashDescriptorBindingInfo(item->resource));
	}
	return result;
}

void DynamicDescriptorSetAllocator::_WriteDescriptorSet(
	VkDescriptorSet inDescriptorSet,
	const AllocateInfo& inAllocateInfo)
{
	CHECK_TRUE(inDescriptorSet != VK_NULL_HANDLE, "Invalid descriptor set!");
	CHECK_TRUE(
		inAllocateInfo.m_descriptorSetLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE,
		"Dynamic descriptor allocation requires a descriptor set layout for writes!");
	DescriptorWriteBundle writeBundle;
	_ValidateAndBuildWrites(
		inDescriptorSet,
		inAllocateInfo.m_descriptorSetLayout.m_descriptorBindings,
		inAllocateInfo.m_descriptorSetDesc.m_items,
		writeBundle);
	if (!writeBundle.writes.empty())
	{
		MyDevice::GetInstance().UpdateDescriptorSets(writeBundle.writes);
	}
}

auto DynamicDescriptorSetAllocator::_GetCachedDescriptorSet(
	const AllocateInfo& inAllocateInfo) -> std::optional<VkDescriptorSet>
{
	const auto layoutIt = m_cachedDescriptorSets.find(inAllocateInfo.m_vkDescriptorSetLayout);
	if (layoutIt == m_cachedDescriptorSets.end())
	{
		return std::nullopt;
	}
	const size_t hash = _HashDescriptorSetDesc(inAllocateInfo.m_descriptorSetDesc);
	const auto cachedIt = layoutIt->second.mapHashToDescriptorSet.find(hash);
	return cachedIt == layoutIt->second.mapHashToDescriptorSet.end()
		? std::nullopt
		: std::optional<VkDescriptorSet>{ cachedIt->second };
}

auto DynamicDescriptorSetAllocator::_AllocateDescriptorSet(const AllocateInfo& inAllocateInfo) -> VkDescriptorSet
{
	CHECK_TRUE(inAllocateInfo.m_vkDescriptorSetLayout != VK_NULL_HANDLE, "Dynamic descriptor allocator requires a descriptor set layout!");
	CHECK_TRUE(m_uptrDescriptorSetAllocator != nullptr, "Dynamic descriptor allocator must be created before allocation!");
	auto [descriptorSet, result] = m_uptrDescriptorSetAllocator->AllocateDescriptorSetWithResult(
		inAllocateInfo.m_vkDescriptorSetLayout);
	VK_CHECK(result, "Failed to allocate dynamic descriptor set!");
	_WriteDescriptorSet(descriptorSet, inAllocateInfo);
	m_cachedDescriptorSets[inAllocateInfo.m_vkDescriptorSetLayout].mapHashToDescriptorSet[
		_HashDescriptorSetDesc(inAllocateInfo.m_descriptorSetDesc)] = descriptorSet;
	return descriptorSet;
}

void DynamicDescriptorSetAllocator::Create()
{
	if (m_uptrDescriptorSetAllocator == nullptr)
	{
		m_uptrDescriptorSetAllocator = std::make_unique<DescriptorSetAllocator>();
		m_uptrDescriptorSetAllocator->Create();
	}
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
	if (const auto cached = _GetCachedDescriptorSet(inAllocateInfo); cached.has_value())
	{
		return *cached;
	}
	return _AllocateDescriptorSet(inAllocateInfo);
}

void DynamicDescriptorSetAllocator::Destroy()
{
	m_cachedDescriptorSets.clear();
	if (m_uptrDescriptorSetAllocator != nullptr)
	{
		m_uptrDescriptorSetAllocator->Destroy();
		m_uptrDescriptorSetAllocator.reset();
	}
}
