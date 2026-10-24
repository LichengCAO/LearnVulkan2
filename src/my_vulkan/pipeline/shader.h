#pragma once
#include "pipeline_common.h"
#include "shader_reflect.h"
class ShaderModule;

class SimpleShader
{
private:
	std::string m_spvFile;
	std::set<std::string> m_entries;
	auto _ReadFile(const std::string& filename) const -> std::vector<char>;
public:
	VkShaderModule vkShaderModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits vkShaderStage = static_cast<VkShaderStageFlagBits>(0);
	~SimpleShader();
	void SetSPVFile(const std::string& file);
	void Create();
	void Destroy();
	auto GetShaderStageInfo(const std::string& entry) -> VkPipelineShaderStageCreateInfo;
	auto GetShaderStageInfo() -> VkPipelineShaderStageCreateInfo;
};

class ShaderModule final
{
public:
	struct EntryPoint
	{
		VkShaderStageFlagBits stage;
		std::string name;
	};
private:
	std::vector<EntryPoint> m_entries;
	ShaderReflector m_reflector;
	VkShaderModule m_vkShaderModule = VK_NULL_HANDLE;
public:
	ShaderModule() = default;
	ShaderModule(const ShaderModule&) = delete;
	auto operator=(const ShaderModule&) -> ShaderModule& = delete;
	~ShaderModule();
	void Create(std::string_view inSpirv);
	auto GetEntryPoints() const -> const std::vector<EntryPoint>& { return m_entries; }
	auto SelectEntry(VkShaderStageFlagBits inStage, std::optional<std::string_view> inEntry = std::nullopt) const -> const EntryPoint&;
	auto GetShaderStageInfo(VkShaderStageFlagBits inStage, std::string_view inEntry) const -> VkPipelineShaderStageCreateInfo;
	auto GetEntryReflection(VkShaderStageFlagBits inStage, std::string_view inEntry) const -> ShaderEntryReflection;
	void Destroy();
};

// Internal owned requests shared by the program builders. No public API accepts these.
namespace ShaderDetail
{
	struct Request
	{
		std::string file;
		VkShaderStageFlagBits stage;
		std::optional<std::string> entry;
	};
	auto MakeRequest(std::string_view inFile, VkShaderStageFlagBits inStage,
	    std::optional<std::string_view> inEntry) -> Request;
	auto MakeOptionalRequest(std::optional<std::string_view> inFile, VkShaderStageFlagBits inStage,
	    std::optional<std::string_view> inEntry) -> std::optional<Request>;
	struct ResolvedShaders
	{
		std::vector<VkPipelineShaderStageCreateInfo> stages;
		std::vector<ShaderEntryReflection> reflection;
		std::vector<uint32_t> indices;
	};
	auto Resolve(const std::vector<Request>& inRequests,
	    std::vector<std::unique_ptr<ShaderModule>>& outModules) -> ResolvedShaders;
}
