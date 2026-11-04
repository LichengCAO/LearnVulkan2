#include "descriptor.h"

#include "../allocator/private/descriptor_pool_pages.h"
#include "private/dynamic_descriptor_pool.h"
#include "../device.h"
#include "../pipeline/pipeline_layout.h"
#include "acceleration_structure.h"
#include "buffer.h"
#include "image.h"
#include "sampler.h"
#include "../utility/hash_util.h"

#include <algorithm>
#include <exception>
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <utility>
#include <type_traits>

namespace
{
	auto _IsImageDescriptorType(VkDescriptorType inType) -> bool
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

	auto _IsBufferDescriptorType(VkDescriptorType inType) -> bool
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

	auto _IsTexelBufferDescriptorType(VkDescriptorType inType) -> bool
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
					CHECK_TRUE(resource.imageView.IsValid(), "Descriptor image view cannot be null!");
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
			imageInfo.imageView = imageBinding->imageView.GetVkImageView();
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


auto DescriptorSetLayout::GetVkDescriptorSetLayout() const -> VkDescriptorSetLayout
{
	return m_vkDescriptorSetLayout;
}

auto DescriptorSetLayout::GetVkDescriptorSetLayoutBinding(uint32_t inBinding) const
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
	const Buffer& inBuffer,
	VkDeviceSize inOffset,
	VkDeviceSize inRange,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, BufferBinding{ &inBuffer, inOffset, inRange }, inArrayElement);
}

void DescriptorSetDesc::SetImage(
	uint32_t inBinding,
	const ImageView& inImageView,
	VkImageLayout inImageLayout,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, ImageBinding{ inImageView, nullptr, inImageLayout }, inArrayElement);
}

void DescriptorSetDesc::SetCombinedImageSampler(
	uint32_t inBinding,
	const ImageView& inImageView,
	const Sampler& inSampler,
	VkImageLayout inImageLayout,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, ImageBinding{ inImageView, &inSampler, inImageLayout }, inArrayElement);
}

void DescriptorSetDesc::SetSampler(uint32_t inBinding, const Sampler& inSampler, uint32_t inArrayElement)
{
	_SetBinding(inBinding, &inSampler, inArrayElement);
}

void DescriptorSetDesc::SetBufferView(uint32_t inBinding, const BufferView& inBufferView, uint32_t inArrayElement)
{
	_SetBinding(inBinding, &inBufferView, inArrayElement);
}

void DescriptorSetDesc::SetAccelerationStructure(
	uint32_t inBinding,
	const TopLevelAccelStruct& inAccelerationStructure,
	uint32_t inArrayElement)
{
	_SetBinding(inBinding, &inAccelerationStructure, inArrayElement);
}

namespace
{
	auto DescriptorRequirements(
		const std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding>& bindings)
		-> std::vector<VkDescriptorPoolSize>
	{
		std::map<VkDescriptorType, uint64_t> totals;
		for (const auto& [index, binding] : bindings)
		{
			CHECK_TRUE(binding.descriptorCount > 0, "Descriptor binding cannot be empty!");
			CHECK_TRUE(binding.descriptorType != VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR ||
				vkCreateAccelerationStructureKHR != nullptr, "Acceleration structure descriptors require device support!");
			auto& total = totals[binding.descriptorType];
			total += binding.descriptorCount;
			CHECK_TRUE(total <= std::numeric_limits<uint32_t>::max(), "Descriptor pool capacity overflows uint32_t!");
		}
		std::vector<VkDescriptorPoolSize> requirements;
		requirements.reserve(totals.size());
		for (const auto& [type, count] : totals)
			requirements.push_back({ type, static_cast<uint32_t>(count) });
		return requirements;
	}

	void WriteDescriptors(VkDescriptorSet set, DescriptorWriteBundle& bundle)
	{
		for (auto& write : bundle.writes) write.dstSet = set;
		if (!bundle.writes.empty()) MyDevice::GetInstance().UpdateDescriptorSets(bundle.writes);
	}

	[[noreturn]] void DescriptorFailure(const char* message) noexcept
	{
		std::cerr << "Fatal descriptor ownership error: " << message << std::endl;
		std::terminate();
	}

	auto HashDescriptorBinding(const DescriptorSetDesc::DescriptorBindingInfo& info) -> size_t
	{
		size_t result = 0;
		std::visit([&](const auto& resource)
		{
			if constexpr (requires { resource.buffer; })
			{
				_HashPointer(result, resource.buffer);
				hash_combine(result, resource.offset);
				hash_combine(result, resource.range);
			}
			else if constexpr (requires { resource.imageView; })
			{
				hash_combine(result, resource.imageView.GetVkImageView());
				_HashPointer(result, resource.sampler);
				hash_combine(result, static_cast<std::underlying_type_t<VkImageLayout>>(resource.imageLayout));
			}
			else _HashPointer(result, resource);
		}, info);
		return result;
	}

	template<class Items>
	auto EqualDescriptorItems(const Items& lhs, const Items& rhs) -> bool
	{
		return lhs.size() == rhs.size() && std::all_of(lhs.begin(), lhs.end(), [&](const auto& item) -> bool
		{
			return std::any_of(rhs.begin(), rhs.end(), [&](const auto& other) -> bool
			{
				return item.binding == other.binding && item.arrayElement == other.arrayElement &&
					item.resource == other.resource;
			});
		});
	}
}

struct DescriptorPool::Impl
{
	struct Record
	{
		VulkanDetail::DescriptorPoolAllocation allocation;
		std::vector<ImageView> imageViews;
	};
	VulkanDetail::DescriptorPoolPages pages;
	std::unordered_map<VkDescriptorSet, Record> allocations;
};

DescriptorPool::DescriptorPool() noexcept = default;

DescriptorPool::~DescriptorPool() noexcept
{
	if (!m_impl) return;
	// Reclaim remaining native sets before dropping their retained dependencies.
	m_impl->pages.Destroy();
	m_impl->allocations.clear();
}

void DescriptorPool::Create()
{
	CHECK_TRUE(!m_impl, "Descriptor pool is already created!");
	auto created = std::make_unique<Impl>();
	created->pages.Create(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
	m_impl = std::move(created);
}

auto DescriptorPool::Allocate(const DescriptorSetLayout& inLayout, const DescriptorSetDesc& inDesc) -> VkDescriptorSet
{
	CHECK_TRUE(m_impl, "Descriptor pool must be created before allocation!");
	CHECK_TRUE(inLayout.m_vkDescriptorSetLayout != VK_NULL_HANDLE, "Descriptor allocation requires a created layout!");
	const auto requirements = DescriptorRequirements(inLayout.m_descriptorBindings);
	DescriptorWriteBundle bundle;
	_ValidateAndBuildWrites(VK_NULL_HANDLE, inLayout.m_descriptorBindings, inDesc.m_items, bundle);
	std::vector<ImageView> imageViews;
	imageViews.reserve(inDesc.m_items.size());
	for (const auto& item : inDesc.m_items)
		if (const auto* image = std::get_if<DescriptorSetDesc::ImageBinding>(&item.resource))
			imageViews.push_back(image->imageView);

	const auto allocation = m_impl->pages.Allocate(inLayout.m_vkDescriptorSetLayout, requirements);
	try
	{
		WriteDescriptors(allocation.descriptorSet, bundle);
		const auto [iterator, inserted] = m_impl->allocations.emplace(allocation.descriptorSet,
			Impl::Record{ allocation, std::move(imageViews) });
		CHECK_TRUE(inserted, "Descriptor set allocation is already tracked!");
	}
	catch (...)
	{
		try { m_impl->pages.Free(allocation); }
		catch (...) { DescriptorFailure("Failed to roll back descriptor allocation"); }
		throw;
	}
	return allocation.descriptorSet;
}

void DescriptorPool::Free(VkDescriptorSet& inOutSet)
{
	if (inOutSet == VK_NULL_HANDLE) return;
	CHECK_TRUE(m_impl, "Descriptor pool must be created before release!");
	const auto found = m_impl->allocations.find(inOutSet);
	CHECK_TRUE(found != m_impl->allocations.end(), "Descriptor set allocation is not tracked by this pool!");
	m_impl->pages.Free(found->second.allocation);
	m_impl->allocations.erase(found);
	inOutSet = VK_NULL_HANDLE;
}

namespace VulkanDetail
{
struct DynamicDescriptorPool::Impl
{
	struct Entry
	{
		DescriptorSetDesc description;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};
	DescriptorPoolPages pages;
	std::unordered_map<VkDescriptorSetLayout, std::unordered_map<size_t, std::vector<Entry>>> cache;
};

DynamicDescriptorPool::DynamicDescriptorPool() noexcept = default;
DynamicDescriptorPool::~DynamicDescriptorPool() noexcept { Destroy(); }

void DynamicDescriptorPool::Create()
{
	CHECK_TRUE(!m_impl, "Dynamic descriptor pool is already created!");
	auto created = std::make_unique<Impl>();
	created->pages.Create(0);
	m_impl = std::move(created);
}

void DynamicDescriptorPool::Reset()
{
	CHECK_TRUE(m_impl, "Dynamic descriptor pool must be created before reset!");
	// Even partial native reset invalidates the cache. The page owner rejects
	// further allocations on failure until it has been destroyed/recreated.
	m_impl->cache.clear();
	m_impl->pages.Reset();
}

void DynamicDescriptorPool::Destroy()
{
	if (!m_impl) return;
	m_impl->pages.Destroy();
	m_impl.reset();
}

auto DynamicDescriptorPool::_HashDescriptorSetDesc(const DescriptorSetDesc& inDesc) const -> size_t
{
	std::vector<const DescriptorSetDesc::Item*> sorted;
	sorted.reserve(inDesc.m_items.size());
	for (const auto& item : inDesc.m_items) sorted.push_back(&item);
	std::sort(sorted.begin(), sorted.end(), [](const auto* lhs, const auto* rhs) -> bool
	{
		return lhs->binding != rhs->binding ? lhs->binding < rhs->binding : lhs->arrayElement < rhs->arrayElement;
	});
	size_t hash = 0;
	hash_combine(hash, sorted.size());
	for (const auto* item : sorted)
	{
		hash_combine(hash, item->binding);
		hash_combine(hash, item->arrayElement);
		hash_combine(hash, item->resource.index());
		hash_combine(hash, HashDescriptorBinding(item->resource));
	}
	return hash;
}

auto DynamicDescriptorPool::GetOrAllocateTransient(const DescriptorSetLayout& inLayout,
	const DescriptorSetDesc& inDesc) -> VkDescriptorSet
{
	CHECK_TRUE(m_impl, "Dynamic descriptor pool must be created before allocation!");
	CHECK_TRUE(inLayout.m_vkDescriptorSetLayout != VK_NULL_HANDLE, "Dynamic descriptor allocation requires a created layout!");
	const size_t hash = _HashDescriptorSetDesc(inDesc);
	const auto layout = inLayout.m_vkDescriptorSetLayout;
	if (const auto layouts = m_impl->cache.find(layout); layouts != m_impl->cache.end())
		if (const auto bucket = layouts->second.find(hash); bucket != layouts->second.end())
			for (const auto& entry : bucket->second)
				if (EqualDescriptorItems(entry.description.m_items, inDesc.m_items)) return entry.descriptorSet;

	const auto requirements = DescriptorRequirements(inLayout.m_descriptorBindings);
	DescriptorWriteBundle bundle;
	_ValidateAndBuildWrites(VK_NULL_HANDLE, inLayout.m_descriptorBindings, inDesc.m_items, bundle);
	DescriptorSetDesc retainedDescription = inDesc;
	const auto allocation = m_impl->pages.Allocate(layout, requirements);
	WriteDescriptors(allocation.descriptorSet, bundle);
	// Once allocated, unpublished sets also belong to the pages until Reset.
	// No failure path here may individually free a dynamic descriptor set.
	m_impl->cache[layout][hash].push_back({ std::move(retainedDescription), allocation.descriptorSet });
	return allocation.descriptorSet;
}
}

void ShaderResources::_Set(
    std::string_view inName, uint32_t inArrayElement, DescriptorSetDesc::DescriptorBindingInfo inResource)
{
	CHECK_TRUE(!inName.empty(), "Shader resource name cannot be empty!");
	// Move repeated settings to the end so aliases also obey last-write-wins.
	std::erase_if(
	    m_items, [&](const Item& item) -> bool { return item.name == inName && item.arrayElement == inArrayElement; });
	m_items.push_back({std::string(inName), inArrayElement, std::move(inResource)});
}
void ShaderResources::Clear()
{
	m_items.clear();
}
void ShaderResources::SetBuffer(
    std::string_view name, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t element)
{
	_Set(name, element, DescriptorSetDesc::BufferBinding{&buffer, offset, range});
}
void ShaderResources::SetImage(std::string_view name, const ImageView& image, VkImageLayout layout, uint32_t element)
{
	_Set(name, element, DescriptorSetDesc::ImageBinding{image, nullptr, layout});
}
void ShaderResources::SetCombinedImageSampler(
    std::string_view name, const ImageView& image, const Sampler& sampler, VkImageLayout layout, uint32_t element)
{
	_Set(name, element, DescriptorSetDesc::ImageBinding{image, &sampler, layout});
}
void ShaderResources::SetSampler(std::string_view name, const Sampler& sampler, uint32_t element)
{
	_Set(name, element, &sampler);
}
void ShaderResources::SetBufferView(std::string_view name, const BufferView& view, uint32_t element)
{
	_Set(name, element, &view);
}
void ShaderResources::SetAccelerationStructure(
    std::string_view name, const TopLevelAccelStruct& acceleration, uint32_t element)
{
	_Set(name, element, &acceleration);
}

auto ShaderResources::_Resolve(const PipelineLayout& layout) const -> std::vector<DescriptorSetDesc>
{
	const auto& bindings = layout.GetVkDescriptorBindings();
	std::vector<DescriptorSetDesc> sets(bindings.size());
	for (const auto& item : m_items)
	{
		const auto [set, binding] = layout.GetSetBinding(item.name);
		const auto& info = bindings.at(set).at(binding);
		CHECK_TRUE(item.arrayElement < info.descriptorCount, "Descriptor array element out of range: " + item.name);
		// Array elements are independent; aliases for an element use the last value.
		sets[set]._SetBinding(binding, item.resource, item.arrayElement);
	}
	for (size_t set = 0; set < sets.size(); ++set)
		for (const auto& resolved : sets[set].m_items)
		{
			const auto& info = bindings[set].at(resolved.binding);
			const auto& resource = resolved.resource;
			bool valid = false;
			switch (info.descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				if (const auto* value = std::get_if<0>(&resource))
				{
					valid = value->buffer != nullptr && value->range != 0;
					if (valid)
					{
						const auto size = value->buffer->GetBufferInformation().size;
						CHECK_TRUE(value->offset < size &&
						               (value->range == VK_WHOLE_SIZE || value->range <= size - value->offset),
						    "Shader buffer offset/range out of bounds: set " + std::to_string(set) + " binding " +
						        std::to_string(resolved.binding));
					}
				}
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				if (const auto* value = std::get_if<1>(&resource))
					valid = value->imageView.IsValid() &&
					        ((info.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ==
					            (value->sampler != nullptr)) &&
					        value->imageLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
					        (info.descriptorType != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
					            value->imageLayout == VK_IMAGE_LAYOUT_GENERAL);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				if (const auto* value = std::get_if<const Sampler*>(&resource))
					valid = *value != nullptr;
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				if (const auto* value = std::get_if<const BufferView*>(&resource))
					valid = *value != nullptr;
				break;
			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
				if (const auto* value = std::get_if<const TopLevelAccelStruct*>(&resource))
					valid = *value != nullptr;
				break;
			default:
				break;
			}
			CHECK_TRUE(valid, "Shader resource type, layout or value mismatch: set " + std::to_string(set) +
			                      " binding " + std::to_string(resolved.binding) + " element " +
			                      std::to_string(resolved.arrayElement));
		}
	for (size_t set = 0; set < bindings.size(); ++set)
		for (const auto& [binding, info] : bindings[set])
			for (uint32_t element = 0; element < info.descriptorCount; ++element)
				CHECK_TRUE(std::any_of(sets[set].m_items.begin(), sets[set].m_items.end(),
				               [&](const auto& item) -> bool
				               { return item.binding == binding && item.arrayElement == element; }),
				    "Missing shader resource: set " + std::to_string(set) + " binding " + std::to_string(binding) +
				        " element " + std::to_string(element));
	return sets;
}
