#include "shader_reflect.h"
#include <spirv_reflect.h>
#include <fstream>
#include <algorithm>
#include <stdexcept>

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
	auto IsUsed(const uint32_t* ids, uint32_t count, uint32_t id) -> bool
	{
		return count > 0 && std::find(ids, ids + count, id) != ids + count;
	}
	auto ReflectInterface(const SpvReflectEntryPoint& entry, bool input) -> std::vector<ShaderEntryReflection::InterfaceVariable>
	{
		std::vector<ShaderEntryReflection::InterfaceVariable> result;
		const auto count = input ? entry.input_variable_count : entry.output_variable_count;
		const auto variables = input ? entry.input_variables : entry.output_variables;
		for (uint32_t i = 0; i < count; ++i)
		{
			const auto* variable = variables[i];
			if (variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
				continue;
			const bool simple = variable->member_count == 0 && variable->array.dims_count == 0 &&
			                    variable->numeric.matrix.column_count == 0 &&
			                    (variable->component == 0 || variable->component == UINT32_MAX);
			result.push_back({variable->name ? variable->name : "", variable->location,
			    simple ? static_cast<VkFormat>(variable->format) : VK_FORMAT_UNDEFINED});
		}
		return result;
	}
	auto ReflectShaderEntry(const SpvReflectShaderModule& module, const SpvReflectEntryPoint& entry) -> ShaderEntryReflection
	{
		ShaderEntryReflection result{static_cast<VkShaderStageFlagBits>(entry.shader_stage), entry.name};
		for (const auto* descriptor : Enumerate<SpvReflectDescriptorBinding>(&module, spvReflectEnumerateDescriptorBindings))
			if (IsUsed(entry.used_uniforms, entry.used_uniform_count, descriptor->spirv_id))
				result.descriptors.push_back({descriptor->name ? descriptor->name : "", descriptor->set,
				    descriptor->binding, static_cast<VkDescriptorType>(descriptor->descriptor_type), descriptor->count});
		for (const auto* block : Enumerate<SpvReflectBlockVariable>(&module, spvReflectEnumeratePushConstantBlocks))
			if (IsUsed(entry.used_push_constants, entry.used_push_constant_count, block->spirv_id))
			{
				CHECK_TRUE(block->size > block->offset && block->offset % 4 == 0 && block->size % 4 == 0,
				    "Invalid push constant range!");
				result.pushConstants.push_back({block->name ? block->name : "", block->offset, block->size - block->offset});
			}
		result.inputs = ReflectInterface(entry, true);
		result.outputs = ReflectInterface(entry, false);
		return result;
	}
}

struct ShaderReflector::Impl
{
	SpvReflectShaderModule module{};
	std::vector<uint32_t> code;
	std::unordered_map<VkShaderStageFlagBits, ShaderEntryReflection> entries;
	~Impl() { spvReflectDestroyShaderModule(&module); }
};

ShaderReflector::ShaderReflector() = default;
ShaderReflector::~ShaderReflector() = default;

void ShaderReflector::Create(std::string_view inSpirv)
{
	CHECK_TRUE(!inSpirv.empty(), "Shader SPIR-V file is unset!");
	const std::string name(inSpirv);
	std::ifstream file(name, std::ios::binary | std::ios::ate);
	CHECK_TRUE(file.is_open(), "Cannot open SPIR-V: " + name);
	const auto size = file.tellg();
	CHECK_TRUE(size >= 20 && size % 4 == 0, "Invalid SPIR-V size: " + name);
	auto impl = std::make_unique<Impl>();
	impl->code.resize(static_cast<size_t>(size) / 4);
	file.seekg(0);
	CHECK_TRUE(static_cast<bool>(file.read(reinterpret_cast<char*>(impl->code.data()), size)), "Cannot read SPIR-V: " + name);
	CheckReflect(spvReflectCreateShaderModule(static_cast<size_t>(size), impl->code.data(), &impl->module), name);
	CHECK_TRUE(impl->module.entry_point_count > 0, "SPIR-V module has no entry points: " + name);
	for (uint32_t i = 0; i < impl->module.entry_point_count; ++i)
	{
		const auto& entry = impl->module.entry_points[i];
		const auto stage = static_cast<VkShaderStageFlagBits>(entry.shader_stage);
		const auto [it, inserted] = impl->entries.try_emplace(stage);
		CHECK_TRUE(inserted,
		    "SPIR-V module has multiple entries for stage " + std::to_string(stage) + ": " + name);
		it->second = ReflectShaderEntry(impl->module, entry);
	}
	m_impl.swap(impl);
}

auto ShaderReflector::GetCode() const -> std::span<const uint32_t>
{
	CHECK_TRUE(m_impl != nullptr, "Shader reflection module is not created!");
	return {spvReflectGetCode(&m_impl->module), spvReflectGetCodeSize(&m_impl->module) / sizeof(uint32_t)};
}

auto ShaderReflector::ReflectEntry(VkShaderStageFlagBits stage) const -> const ShaderEntryReflection&
{
	CHECK_TRUE(m_impl != nullptr, "Shader reflection module is not created!");
	const auto it = m_impl->entries.find(stage);
	CHECK_TRUE(it != m_impl->entries.end(), "Shader stage does not match SPIR-V!");
	return it->second;
}

void ShaderReflector::Destroy()
{
	m_impl.reset();
}
