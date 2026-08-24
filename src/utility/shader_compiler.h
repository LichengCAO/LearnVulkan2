#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

class ShaderCompiler
{
public:
	explicit ShaderCompiler(std::filesystem::path inShaderDirectory);

	// Compiles one GLSL file relative to the configured shader directory.
	// Absolute paths are accepted as well. The shader stage is inferred from
	// the file extension.
	auto CompileFile(const std::filesystem::path& inSourcePath) const -> std::vector<uint8_t>;

private:
	std::filesystem::path m_shaderDirectory;
};
