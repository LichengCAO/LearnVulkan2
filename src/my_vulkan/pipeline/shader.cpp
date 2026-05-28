#include "shader.h"
#include "device.h"
#include <fstream>

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

std::vector<char> SimpleShader::_ReadFile(const std::string& filename) const
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

VkPipelineShaderStageCreateInfo SimpleShader::GetShaderStageInfo(const std::string& entry)
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

VkPipelineShaderStageCreateInfo SimpleShader::GetShaderStageInfo()
{
	return GetShaderStageInfo("main");
}

ShaderModule::~ShaderModule()
{
	assert(m_vkShaderModule == VK_NULL_HANDLE);
}

void ShaderModule::Create(const ShaderModuleCreateInfo& inCreateInfo)
{
	CHECK_TRUE(m_vkShaderModule == VK_NULL_HANDLE, "VkShaderModule is already created!");
	CHECK_TRUE(!inCreateInfo.m_spirvFile.empty(), "SPIR-V file is unset!");
	CHECK_TRUE(!inCreateInfo.m_entries.empty(), "At least one shader stage entry is required!");

	auto code = ReadShaderFile(inCreateInfo.m_spirvFile);
	CHECK_TRUE((code.size() % sizeof(uint32_t)) == 0, "SPIR-V bytecode size must be aligned to uint32_t.");

	VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	m_spirvFile = inCreateInfo.m_spirvFile;
	m_entries = inCreateInfo.m_entries;
	VK_CHECK(
		vkCreateShaderModule(MyDevice::GetInstance().vkDevice, &createInfo, nullptr, &m_vkShaderModule),
		"Failed to create shader module!");
}

auto ShaderModule::_GetVkPipelineShaderStageCreateInfo(VkShaderStageFlagBits inStage) const -> VkPipelineShaderStageCreateInfo
{
	CHECK_TRUE(m_vkShaderModule != VK_NULL_HANDLE, "VkShaderModule is not created!");

	const auto it = m_entries.find(inStage);
	CHECK_TRUE(it != m_entries.end(), "Requested shader stage entry was not registered.");

	VkPipelineShaderStageCreateInfo shaderStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderStageInfo.stage = inStage;
	shaderStageInfo.module = m_vkShaderModule;
	shaderStageInfo.pName = it->second.c_str();

	return shaderStageInfo;
}

void ShaderModule::Destroy()
{
	m_spirvFile.clear();
	m_entries.clear();
	if (m_vkShaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(MyDevice::GetInstance().vkDevice, m_vkShaderModule, nullptr);
		m_vkShaderModule = VK_NULL_HANDLE;
	}
}

