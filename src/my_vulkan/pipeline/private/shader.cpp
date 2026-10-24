#include "shader.h"
#include "device.h"

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
		m_reflector.Create(inSpirv);
		const auto code = m_reflector.GetCode();
		VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = code.size_bytes();
		info.pCode = code.data();
		VK_CHECK(vkCreateShaderModule(MyDevice::GetInstance().vkDevice, &info, nullptr, &m_vkShaderModule),
		    "Failed to create shader module!");
	}
	catch (...)
	{
		Destroy();
		throw;
	}
}

auto ShaderModule::GetShaderStageInfo(VkShaderStageFlagBits stage) const -> VkPipelineShaderStageCreateInfo
{
	CHECK_TRUE(m_vkShaderModule != VK_NULL_HANDLE, "VkShaderModule is not created!");
	VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	info.stage = stage;
	info.module = m_vkShaderModule;
	info.pName = m_reflector.ReflectEntry(stage).name.c_str();
	return info;
}

auto ShaderModule::GetEntryReflection(VkShaderStageFlagBits stage) const -> const ShaderEntryReflection&
{
	return m_reflector.ReflectEntry(stage);
}

void ShaderModule::Destroy()
{
	if (m_vkShaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(MyDevice::GetInstance().vkDevice, m_vkShaderModule, nullptr);
		m_vkShaderModule = VK_NULL_HANDLE;
	}
	m_reflector.Destroy();
}

auto ShaderDetail::MakeRequest(std::string_view file, VkShaderStageFlagBits stage) -> Request
{
	CHECK_TRUE(!file.empty(), "Shader SPIR-V file is unset!");
	return {std::string(file), stage};
}

auto ShaderDetail::MakeOptionalRequest(std::optional<std::string_view> file, VkShaderStageFlagBits stage) -> std::optional<Request>
{
	if (!file)
		return std::nullopt;
	return MakeRequest(*file, stage);
}

auto ShaderDetail::Resolve(const std::vector<Request>& requests,
    std::vector<std::unique_ptr<ShaderModule>>& modules) -> ResolvedShaders
{
	CHECK_TRUE(modules.empty(), "Shader modules are already created!");
	ResolvedShaders result;
	std::vector<ShaderEntryReflection> reflection;
	std::unordered_map<std::string, ShaderModule*> files;
	for (const auto& request : requests)
	{
		auto it = files.find(request.file);
		if (it == files.end())
		{
			auto& module = modules.emplace_back(std::make_unique<ShaderModule>());
			module->Create(request.file);
			it = files.emplace(request.file, module.get()).first;
		}
		const auto stage = it->second->GetShaderStageInfo(request.stage);
		size_t index = 0;
		for (; index < result.stages.size(); ++index)
		{
			const auto& previous = result.stages[index];
			if (previous.module == stage.module && previous.stage == stage.stage)
				break;
		}
		if (index == result.stages.size())
		{
			result.stages.push_back(stage);
			reflection.push_back(it->second->GetEntryReflection(request.stage));
		}
		result.indices.push_back(static_cast<uint32_t>(index));
	}
	result.reflection.Create(reflection);
	return result;
}
