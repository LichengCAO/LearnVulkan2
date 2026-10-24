#pragma once
#include "shader_reflection_data.h"
#include <span>

class ShaderReflector final
{
private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
public:
	ShaderReflector();
	ShaderReflector(const ShaderReflector&) = delete;
	auto operator=(const ShaderReflector&) -> ShaderReflector& = delete;
	~ShaderReflector();

	void Create(std::string_view inSpirv);
	auto GetCode() const -> std::span<const uint32_t>;
	// Cached references remain valid until the next successful Create() or Destroy().
	auto ReflectEntry(VkShaderStageFlagBits inStage) const -> const ShaderEntryReflection&;
	void Destroy();
};
