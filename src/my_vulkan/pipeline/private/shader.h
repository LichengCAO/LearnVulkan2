#pragma once
#include "common.h"
#include <string_view>
#include "shader_reflect.h"
#include "shader_program_reflection.h"
#include "shader_module_create_info.h"

class ShaderModule final
{
    friend class MyDevice;
private:
	std::shared_ptr<const ShaderReflector> m_reflector;
	VkShaderModule m_vkShaderModule = VK_NULL_HANDLE;
public:
	// Native handle borrows the Device cache; this wrapper owns only Host metadata.
	ShaderModule() = default;
	ShaderModule(const ShaderModule&) = delete;
	auto operator=(const ShaderModule&) -> ShaderModule& = delete;
	~ShaderModule() noexcept = default;
	auto GetShaderStageInfo(VkShaderStageFlagBits inStage) const -> VkPipelineShaderStageCreateInfo;
	auto GetEntryReflection(VkShaderStageFlagBits inStage) const -> const ShaderEntryReflection&;
};

// Internal owned requests shared by the program builders. No public API accepts these.
namespace ShaderDetail
{
	void CreateStages(const FinalizedShaders& inShaders,
	    std::vector<std::unique_ptr<ShaderModule>>& outModules,
	    std::vector<VkPipelineShaderStageCreateInfo>& outStages);
}
