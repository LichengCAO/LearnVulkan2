#pragma once
#include "pipeline_common.h"
#include "shader_reflect.h"
#include "shader_program_reflection.h"

class ShaderModule final
{
private:
	ShaderReflector m_reflector;
	VkShaderModule m_vkShaderModule = VK_NULL_HANDLE;
public:
	ShaderModule() = default;
	ShaderModule(const ShaderModule&) = delete;
	auto operator=(const ShaderModule&) -> ShaderModule& = delete;
	~ShaderModule();
	void Create(std::string_view inSpirv);
	auto GetShaderStageInfo(VkShaderStageFlagBits inStage) const -> VkPipelineShaderStageCreateInfo;
	auto GetEntryReflection(VkShaderStageFlagBits inStage) const -> const ShaderEntryReflection&;
	void Destroy();
};

// Internal owned requests shared by the program builders. No public API accepts these.
namespace ShaderDetail
{
	struct Request
	{
		std::string file;
		VkShaderStageFlagBits stage;
	};
	auto MakeRequest(std::string_view inFile, VkShaderStageFlagBits inStage) -> Request;
	auto MakeOptionalRequest(std::optional<std::string_view> inFile, VkShaderStageFlagBits inStage) -> std::optional<Request>;
	struct ResolvedShaders
	{
		std::vector<VkPipelineShaderStageCreateInfo> stages;
		ShaderProgramReflection reflection;
		std::vector<uint32_t> indices;
	};
	auto Resolve(const std::vector<Request>& inRequests,
	    std::vector<std::unique_ptr<ShaderModule>>& outModules) -> ResolvedShaders;
}
