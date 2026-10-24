#include "shader_program_reflection.h"
#include <algorithm>
#include <limits>

void ShaderProgramReflection::Create(const std::vector<ShaderEntryReflection>& entries)
{
	CHECK_TRUE(!entries.empty(), "Shader reflection needs selected entries!");
	ShaderProgramReflection result;
	std::map<VkShaderStageFlagBits, std::pair<uint32_t, uint32_t>> bounds;
	for (const auto& entry : entries)
	{
		auto& interfaces = result.m_interfaces[entry.stage];
		if (interfaces.entryCount++ == 0)
		{
			interfaces.inputs = entry.inputs;
			interfaces.outputs = entry.outputs;
		}
		for (const auto& descriptor : entry.descriptors)
		{
			CHECK_TRUE(descriptor.count > 0, "Runtime descriptor arrays unsupported: set " +
			    std::to_string(descriptor.set) + " binding " + std::to_string(descriptor.binding));
			if (result.m_descriptorBindings.size() <= descriptor.set)
				result.m_descriptorBindings.resize(static_cast<size_t>(descriptor.set) + 1);
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = descriptor.binding;
			binding.descriptorType = descriptor.type;
			binding.descriptorCount = descriptor.count;
			binding.stageFlags = entry.stage;
			auto [it, inserted] = result.m_descriptorBindings[descriptor.set].emplace(binding.binding, binding);
			CHECK_TRUE(inserted || (it->second.descriptorType == binding.descriptorType &&
			    it->second.descriptorCount == binding.descriptorCount),
			    "Incompatible descriptor: set " + std::to_string(descriptor.set) + " binding " + std::to_string(descriptor.binding));
			it->second.stageFlags |= binding.stageFlags;
			if (!descriptor.name.empty())
			{
				const auto key = std::make_pair(descriptor.set, descriptor.binding);
				auto [nameIt, added] = result.m_nameToSetBinding.emplace(descriptor.name, key);
				if (!added && nameIt->second != key)
					result.m_ambiguousNames.insert(descriptor.name);
			}
		}
		for (const auto& block : entry.pushConstants)
		{
			CHECK_TRUE(block.size > 0 && block.offset % 4 == 0 && block.size % 4 == 0 &&
			    block.size <= std::numeric_limits<uint32_t>::max() - block.offset, "Invalid push constant range!");
			const auto end = block.offset + block.size;
			auto [it, inserted] = bounds.emplace(entry.stage, std::make_pair(block.offset, end));
			if (!inserted)
			{
				it->second.first = std::min(it->second.first, block.offset);
				it->second.second = std::max(it->second.second, end);
			}
		}
	}
	for (const auto& name : result.m_ambiguousNames)
		result.m_nameToSetBinding.erase(name);

	for (const auto& [stage, extent] : bounds)
	{
		const auto size = extent.second - extent.first;
		auto it = std::find_if(result.m_pushConstantRanges.begin(), result.m_pushConstantRanges.end(),
		    [&](const auto& range) -> bool { return range.offset == extent.first && range.size == size; });
		if (it == result.m_pushConstantRanges.end())
		{
			result.m_pushConstantRanges.push_back({static_cast<VkShaderStageFlags>(stage), extent.first, size});
		}
		else
		{
			it->stageFlags |= stage;
		}
	}
	*this = std::move(result);
}

auto ShaderProgramReflection::_GetInterface(VkShaderStageFlagBits stage, bool input) const
    -> const std::vector<ShaderEntryReflection::InterfaceVariable>&
{
	static const std::vector<ShaderEntryReflection::InterfaceVariable> empty;
	const auto it = m_interfaces.find(stage);
	if (it == m_interfaces.end())
		return empty;
	CHECK_TRUE(it->second.entryCount == 1, "Multiple entries for interface stage are ambiguous!");
	return input ? it->second.inputs : it->second.outputs;
}
