#include "../shader_resources.h"
#include "resource/buffer.h"
#include "shader_program_layout.h"
#include <algorithm>

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
    std::string_view name, const Buffer* buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t element)
{
	_Set(name, element, DescriptorSetDesc::BufferBinding{buffer, offset, range});
}
void ShaderResources::SetImage(std::string_view name, const ImageView* image, VkImageLayout layout, uint32_t element)
{
	_Set(name, element, DescriptorSetDesc::ImageBinding{image, nullptr, layout});
}
void ShaderResources::SetCombinedImageSampler(
    std::string_view name, const ImageView* image, const Sampler* sampler, VkImageLayout layout, uint32_t element)
{
	CHECK_TRUE(sampler != nullptr, "Combined image sampler requires a sampler!");
	_Set(name, element, DescriptorSetDesc::ImageBinding{image, sampler, layout});
}
void ShaderResources::SetSampler(std::string_view name, const Sampler* sampler, uint32_t element)
{
	_Set(name, element, sampler);
}
void ShaderResources::SetBufferView(std::string_view name, const BufferView* view, uint32_t element)
{
	_Set(name, element, view);
}
void ShaderResources::SetAccelerationStructure(
    std::string_view name, const TopLevelAccelStruct* acceleration, uint32_t element)
{
	_Set(name, element, acceleration);
}

auto ShaderResources::_Resolve(const ShaderProgramLayout& layout) const -> std::vector<DescriptorSetDesc>
{
	const auto& bindings = layout.GetDescriptorBindings();
	std::vector<DescriptorSetDesc> sets(bindings.size());
	for (const auto& item : m_items)
	{
		CHECK_TRUE(!layout.GetAmbiguousNames().contains(item.name), "Ambiguous shader resource: " + item.name);
		const auto name = layout.GetNameToSetBinding().find(item.name);
		CHECK_TRUE(name != layout.GetNameToSetBinding().end(), "Unknown shader resource: " + item.name);
		const auto [set, binding] = name->second;
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
					valid = value->imageView != nullptr &&
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
