#include "shader.h"
#include "device.h"
#include <fstream>
#include <spirv_reflect.h>

namespace
{
auto ReadShaderFile(const std::string& filename) -> std::vector<char>
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	CHECK_TRUE(file.is_open(), "Failed to open file!");

	const size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<char> result(fileSize);
	file.seekg(0);
	file.read(result.data(), fileSize);
	file.close();

	return result;
}
}

auto SimpleShader::_ReadFile(const std::string& filename) const -> std::vector<char>
{
	return ReadShaderFile(filename);
}
SimpleShader::~SimpleShader()
{
	assert(vkShaderModule == VK_NULL_HANDLE);
}

void SimpleShader::SetSPVFile(const std::string& file)
{
	m_spvFile = file;
	bool stageSet = false;
	std::vector<VkShaderStageFlagBits> stages =
	{
		VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT,
		VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT,
		VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT,
		VkShaderStageFlagBits::VK_SHADER_STAGE_TASK_BIT_EXT,
		VkShaderStageFlagBits::VK_SHADER_STAGE_MESH_BIT_EXT,
		VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		VkShaderStageFlagBits::VK_SHADER_STAGE_MISS_BIT_KHR,
		VkShaderStageFlagBits::VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
	};
	std::vector<std::string> stageStrings =
	{
		".comp.",
		".vert.",
		".frag.",
		".task.",
		".mesh.",
		".rgen.",
		".rchit.",
		".rmiss.",
		".rint.",
	};
	for (int i = 0; i < stages.size(); ++i)
	{
		bool isFound = file.find(stageStrings[i]) != std::string::npos;
		if (isFound)
		{
			vkShaderStage = stages[i];
			stageSet = true;
			break;
		}
	}
	CHECK_TRUE(stageSet, "No stage type preset for this!");
}

void SimpleShader::Create()
{
	CHECK_TRUE(!m_spvFile.empty(), "SPV file is unset!");
	auto code = _ReadFile(m_spvFile);
	VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
	CHECK_TRUE(vkShaderModule == VK_NULL_HANDLE, "VkShaderModule is already created!");
	VK_CHECK(vkCreateShaderModule(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &vkShaderModule), "Failed to create shader module!");
}

void SimpleShader::Destroy()
{
	if (vkShaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(MyDevice::GetInstance().vkDevice, vkShaderModule, nullptr);
		vkShaderModule = VK_NULL_HANDLE;
	}
}

auto SimpleShader::GetShaderStageInfo(const std::string& entry) -> VkPipelineShaderStageCreateInfo
{
	VkPipelineShaderStageCreateInfo shaderStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	auto it = m_entries.find(entry);
	if (it == m_entries.end())
	{
		m_entries.insert(entry);
		it = m_entries.find(entry);
	}
	shaderStageInfo.stage = vkShaderStage;
	shaderStageInfo.module = vkShaderModule;
	shaderStageInfo.pName = it->c_str();
	return shaderStageInfo;
}

auto SimpleShader::GetShaderStageInfo() -> VkPipelineShaderStageCreateInfo
{
	return GetShaderStageInfo("main");
}

ShaderModule::~ShaderModule()
{
	assert(m_vkShaderModule == VK_NULL_HANDLE);
}

void ShaderModule::Create(std::string_view inSpirv)
{
	CHECK_TRUE(m_vkShaderModule == VK_NULL_HANDLE, "VkShaderModule is already created!");
	CHECK_TRUE(!inSpirv.empty(), "Shader SPIR-V file is unset!");
	try
	{
		m_reflector.Create(std::vector<std::string>{std::string(inSpirv)});
		for (const auto& selected : m_reflector.GetEntries())
			m_entries.push_back({static_cast<VkShaderStageFlagBits>(selected.entry->shader_stage), selected.entry->name});
		const auto* reflection = m_reflector.GetEntries().front().module;
		VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = spvReflectGetCodeSize(reflection);
		info.pCode = spvReflectGetCode(reflection);
		VK_CHECK(vkCreateShaderModule(MyDevice::GetInstance().vkDevice, &info, nullptr, &m_vkShaderModule),
		    "Failed to create shader module!");
	}
	catch (...)
	{
		Destroy();
		throw;
	}
}

auto ShaderModule::SelectEntry(VkShaderStageFlagBits stage, std::optional<std::string_view> name) const -> const EntryPoint&
{
	CHECK_TRUE(!name || !name->empty(), "Shader entry name is empty!");
	const EntryPoint* result = nullptr;
	for (const auto& entry : m_entries)
		if (entry.stage == stage && (!name || entry.name == *name))
		{
			CHECK_TRUE(result == nullptr, "Shader entry selection is ambiguous; specify an entry name!");
			result = &entry;
		}
	CHECK_TRUE(result != nullptr, "Shader entry name/stage does not match SPIR-V!");
	return *result;
}

auto ShaderModule::GetShaderStageInfo(VkShaderStageFlagBits stage, std::string_view name) const -> VkPipelineShaderStageCreateInfo
{
	CHECK_TRUE(m_vkShaderModule != VK_NULL_HANDLE, "VkShaderModule is not created!");
	const auto& entry = SelectEntry(stage, name);
	VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	info.stage = stage;
	info.module = m_vkShaderModule;
	info.pName = entry.name.c_str();
	return info;
}

auto ShaderModule::GetEntryReflection(VkShaderStageFlagBits stage, std::string_view name) const -> ShaderEntryReflection
{
	SelectEntry(stage, name);
	for (const auto& selected : m_reflector.GetEntries())
		if (static_cast<VkShaderStageFlagBits>(selected.entry->shader_stage) == stage && name == selected.entry->name)
			return selected;
	throw std::runtime_error("Shader entry reflection is unavailable!");
}

void ShaderModule::Destroy()
{
	if (m_vkShaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(MyDevice::GetInstance().vkDevice, m_vkShaderModule, nullptr);
		m_vkShaderModule = VK_NULL_HANDLE;
	}
	m_entries.clear();
	m_reflector.Destroy();
}

auto ShaderDetail::MakeRequest(std::string_view file, VkShaderStageFlagBits stage,
    std::optional<std::string_view> entry) -> Request
{
	CHECK_TRUE(!file.empty(), "Shader SPIR-V file is unset!");
	CHECK_TRUE(!entry || !entry->empty(), "Shader entry name is empty!");
	return {std::string(file), stage, entry ? std::make_optional<std::string>(*entry) : std::nullopt};
}

auto ShaderDetail::MakeOptionalRequest(std::optional<std::string_view> file, VkShaderStageFlagBits stage,
    std::optional<std::string_view> entry) -> std::optional<Request>
{
	CHECK_TRUE(file || !entry, "Shader entry requires a SPIR-V path!");
	if (!file)
		return std::nullopt;
	return MakeRequest(*file, stage, entry);
}

auto ShaderDetail::Resolve(const std::vector<Request>& requests,
    std::vector<std::unique_ptr<ShaderModule>>& modules) -> ResolvedShaders
{
	CHECK_TRUE(modules.empty(), "Shader modules are already created!");
	ResolvedShaders result;
	std::unordered_map<std::string, ShaderModule*> files;
	std::vector<std::pair<std::string_view, const ShaderModule::EntryPoint*>> selected;
	for (const auto& request : requests)
	{
		auto it = files.find(request.file);
		if (it == files.end())
		{
			auto& module = modules.emplace_back(std::make_unique<ShaderModule>());
			module->Create(request.file);
			it = files.emplace(request.file, module.get()).first;
		}
		const auto& entry = it->second->SelectEntry(request.stage,
		    request.entry ? std::make_optional<std::string_view>(*request.entry) : std::nullopt);
		const auto stage = it->second->GetShaderStageInfo(entry.stage, entry.name);
		size_t index = 0;
		for (; index < result.stages.size(); ++index)
		{
			const auto& [file, previous] = selected[index];
			if (file == request.file && previous->stage == entry.stage && previous->name == entry.name)
				break;
		}
		if (index == result.stages.size())
		{
			result.stages.push_back(stage);
			result.reflection.push_back(it->second->GetEntryReflection(entry.stage, entry.name));
			selected.emplace_back(request.file, &entry);
		}
		result.indices.push_back(static_cast<uint32_t>(index));
	}
	return result;
}
