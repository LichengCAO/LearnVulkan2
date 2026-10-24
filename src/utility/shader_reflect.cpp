#include "shader_reflect.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace
{
#define PRINT_STAGE(os, stage, bit) if (CONTAIN_BITS(stage, bit))\
									{\
										os << #bit <<" ";\
									}
#define PRINT_TYPE(os, vkt, vkt2) if (vkt == vkt2)\
									{\
										os << #vkt2 << " ";\
									}

	void _PrintStage(std::ostream& os, VkShaderStageFlags stages)
	{
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_VERTEX_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_GEOMETRY_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_FRAGMENT_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_COMPUTE_BIT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_MISS_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_TASK_BIT_EXT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_MESH_BIT_EXT);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI);
		PRINT_STAGE(os, stages, VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI);
	}
	
	void _PrintDescriptorType(std::ostream& os, VkDescriptorType vkType)
	{
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_SAMPLER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM);
		PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_MUTABLE_EXT);
		//PRINT_TYPE(os, vkType, VK_DESCRIPTOR_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_NV);
	}
	
	void _PrintVkFormat(std::ostream& os, VkFormat _format)
	{
		PRINT_TYPE(os, _format, VK_FORMAT_UNDEFINED);
		PRINT_TYPE(os, _format, VK_FORMAT_R16_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16A16_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16A16_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R16G16B16A16_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32A32_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32A32_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R32G32B32A32_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64_SFLOAT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64A64_UINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64A64_SINT);
		PRINT_TYPE(os, _format, VK_FORMAT_R64G64B64A64_SFLOAT);
	}

#undef PRINT_STAGE
#undef PRINT_TYPE
}


namespace
{
	void CheckReflect(SpvReflectResult result, const std::string& operation)
	{
		if (result != SPV_REFLECT_RESULT_SUCCESS)
			throw std::runtime_error(operation + ": SPIRV-Reflect error " + std::to_string(result));
	}
	template <typename T, typename F>
	auto Enumerate(const SpvReflectShaderModule* module, F function) -> std::vector<T*>
	{
		uint32_t count = 0;
		CheckReflect(function(module, &count, nullptr), "Enumerate reflection count");
		std::vector<T*> result(count);
		if (count)
			CheckReflect(function(module, &count, result.data()), "Enumerate reflection data");
		return result;
	}
	void ReflectInterface(const SpvReflectShaderModule* module, bool input,
	    std::unordered_map<std::string, uint32_t>& names, std::map<uint32_t, VkFormat>& formats)
	{
		names.clear();
		formats.clear();
		if (!module)
			return;
		for (const auto* variable : Enumerate<SpvReflectInterfaceVariable>(
		         module, input ? spvReflectEnumerateInputVariables : spvReflectEnumerateOutputVariables))
		{
			if (variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
				continue;
			const bool simple = variable->member_count == 0 && variable->array.dims_count == 0 &&
			                    variable->numeric.matrix.column_count == 0 &&
			                    (variable->component == 0 || variable->component == UINT32_MAX);
			const auto [position, inserted] = formats.emplace(
			    variable->location, simple ? static_cast<VkFormat>(variable->format) : VK_FORMAT_UNDEFINED);
			if (!inserted)
				position->second = VK_FORMAT_UNDEFINED;
			if (variable->name && *variable->name)
				names[variable->name] = variable->location;
		}
	}
} // namespace
void ShaderReflector::ModuleDeleter::operator()(SpvReflectShaderModule* module) const
{
	if (module)
	{
		spvReflectDestroyShaderModule(module);
		delete module;
	}
}
auto ShaderReflector::_FindShaderModule(VkShaderStageFlagBits stage) const -> const SpvReflectShaderModule*
{
	const SpvReflectShaderModule* result = nullptr;
	for (const auto& module : m_vecReflectModule)
		if (static_cast<VkShaderStageFlagBits>(module->shader_stage) == stage)
		{
			CHECK_TRUE(result == nullptr, "Multiple modules for interface stage are ambiguous!");
			result = module.get();
		}
	return result;
}
ShaderReflector::~ShaderReflector() = default;
void ShaderReflector::Create(const std::vector<std::string>& files)
{
	CHECK_TRUE(!files.empty(), "Shader reflection needs SPIR-V files!");
	decltype(m_vecReflectModule) modules;
	for (const auto& name : files)
	{
		std::ifstream file(name, std::ios::binary | std::ios::ate);
		CHECK_TRUE(file.is_open(), "Cannot open SPIR-V: " + name);
		const auto size = file.tellg();
		CHECK_TRUE(size >= 20 && size % 4 == 0, "Invalid SPIR-V size: " + name);
		std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
		file.seekg(0);
		CHECK_TRUE(
		    static_cast<bool>(file.read(reinterpret_cast<char*>(code.data()), size)), "Cannot read SPIR-V: " + name);
		std::unique_ptr<SpvReflectShaderModule, ModuleDeleter> module(new SpvReflectShaderModule{});
		CheckReflect(spvReflectCreateShaderModule(static_cast<size_t>(size), code.data(), module.get()), name);
		CHECK_TRUE(module->entry_point_count == 1, "Reflection requires one entry point per SPIR-V module: " + name);
		modules.push_back(std::move(module));
	}
	m_vecReflectModule.swap(modules);
}
void ShaderReflector::ReflectDescriptorSets(std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& outNames,
    std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& outSets,
    std::set<std::string>* outAmbiguousNames) const
{
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> names;
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> sets;
	std::set<std::string> ambiguous;
	for (const auto& module : m_vecReflectModule)
		for (const auto* descriptor :
		    Enumerate<SpvReflectDescriptorBinding>(module.get(), spvReflectEnumerateDescriptorBindings))
		{
			CHECK_TRUE(descriptor->count > 0, "Runtime descriptor arrays unsupported: set " +
			                                      std::to_string(descriptor->set) + " binding " +
			                                      std::to_string(descriptor->binding));
			if (sets.size() <= descriptor->set)
				sets.resize(static_cast<size_t>(descriptor->set) + 1);
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = descriptor->binding;
			binding.descriptorType = static_cast<VkDescriptorType>(descriptor->descriptor_type);
			binding.descriptorCount = descriptor->count;
			binding.stageFlags = static_cast<VkShaderStageFlags>(module->shader_stage);
			auto [it, inserted] = sets[descriptor->set].emplace(binding.binding, binding);
			CHECK_TRUE(inserted || (it->second.descriptorType == binding.descriptorType &&
			                           it->second.descriptorCount == binding.descriptorCount),
			    "Incompatible descriptor: set " + std::to_string(descriptor->set) + " binding " +
			        std::to_string(descriptor->binding));
			it->second.stageFlags |= binding.stageFlags;
			if (descriptor->name && *descriptor->name)
			{
				const std::string name(descriptor->name);
				const auto key = std::make_pair(descriptor->set, descriptor->binding);
				auto [nameIt, added] = names.emplace(name, key);
				if (!added && nameIt->second != key)
					ambiguous.insert(name);
			}
		}
	for (const auto& name : ambiguous)
		names.erase(name);
	if (outAmbiguousNames)
		*outAmbiguousNames = ambiguous;
	else
		CHECK_TRUE(ambiguous.empty(), "Ambiguous descriptor name!");
	outNames = std::move(names);
	outSets = std::move(sets);
}
void ShaderReflector::ReflectInput(VkShaderStageFlagBits stage, std::unordered_map<std::string, uint32_t>& names,
    std::map<uint32_t, VkFormat>& formats) const
{
	ReflectInterface(_FindShaderModule(stage), true, names, formats);
}
void ShaderReflector::ReflectOutput(VkShaderStageFlagBits stage, std::unordered_map<std::string, uint32_t>& names,
    std::map<uint32_t, VkFormat>& formats) const
{
	ReflectInterface(_FindShaderModule(stage), false, names, formats);
}
void ShaderReflector::ReflectPushConst(std::unordered_map<std::string, uint32_t>& names,
    std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>>& ranges) const
{
	names.clear();
	ranges.clear();
	std::map<VkShaderStageFlagBits, std::pair<uint32_t, uint32_t>> bounds;
	std::vector<std::pair<std::string, VkShaderStageFlagBits>> blockNames;
	for (const auto& module : m_vecReflectModule)
	{
		const auto stage = static_cast<VkShaderStageFlagBits>(module->shader_stage);
		for (const auto* block :
		    Enumerate<SpvReflectBlockVariable>(module.get(), spvReflectEnumeratePushConstantBlocks))
		{
			CHECK_TRUE(block->size > block->offset && block->offset % 4 == 0 && block->size % 4 == 0,
			    "Invalid push constant range!");
			auto [it, inserted] = bounds.emplace(stage, std::make_pair(block->offset, block->size));
			if (!inserted)
			{
				it->second.first = std::min(it->second.first, block->offset);
				it->second.second = std::max(it->second.second, block->size);
			}
			if (block->name && *block->name)
				blockNames.emplace_back(block->name, stage);
		}
	}
	std::map<VkShaderStageFlagBits, uint32_t> indices;
	for (const auto& [stage, extent] : bounds)
	{
		const auto range = std::make_pair(extent.first, extent.second - extent.first);
		auto it =
		    std::find_if(ranges.begin(), ranges.end(), [&](const auto& item) -> bool { return item.second == range; });
		if (it == ranges.end())
		{
			indices[stage] = static_cast<uint32_t>(ranges.size());
			ranges.emplace_back(stage, range);
		}
		else
		{
			it->first |= stage;
			indices[stage] = static_cast<uint32_t>(it - ranges.begin());
		}
	}
	std::set<std::string> ambiguous;
	for (const auto& [name, stage] : blockNames)
	{
		const auto [it, added] = names.emplace(name, indices.at(stage));
		if (!added && it->second != indices.at(stage))
			ambiguous.insert(name);
	}
	for (const auto& name : ambiguous)
		names.erase(name);
}
void ShaderReflector::PrintReflectResult() const
{
	for (const auto& module : m_vecReflectModule)
	{
		_PrintStage(std::cout, static_cast<VkShaderStageFlags>(module->shader_stage));
		std::cout << " entry: " << module->entry_point_name << '\n';
		for (const auto* binding :
		    Enumerate<SpvReflectDescriptorBinding>(module.get(), spvReflectEnumerateDescriptorBindings))
		{
			std::cout << "  " << (binding->name ? binding->name : "") << " set=" << binding->set
			          << " binding=" << binding->binding << " count=" << binding->count << " ";
			_PrintDescriptorType(std::cout, static_cast<VkDescriptorType>(binding->descriptor_type));
			std::cout << '\n';
		}
		for (bool input : {true, false})
			for (const auto* variable : Enumerate<SpvReflectInterfaceVariable>(
			         module.get(), input ? spvReflectEnumerateInputVariables : spvReflectEnumerateOutputVariables))
			{
				if (variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
					continue;
				std::cout << (input ? "  input " : "  output ") << (variable->name ? variable->name : "")
				          << " location=" << variable->location << " ";
				_PrintVkFormat(std::cout, static_cast<VkFormat>(variable->format));
				std::cout << '\n';
			}
		for (const auto* block :
		    Enumerate<SpvReflectBlockVariable>(module.get(), spvReflectEnumeratePushConstantBlocks))
			std::cout << "  push constant " << (block->name ? block->name : "") << " offset=" << block->offset
			          << " size=" << block->size - block->offset << '\n';
	}
}
void ShaderReflector::Destroy()
{
	m_vecReflectModule.clear();
}
