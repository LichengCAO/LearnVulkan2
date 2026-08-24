#include "shader_compiler.h"

#include <shaderc/shaderc.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace
{
auto ReadTextFile(const std::filesystem::path& inPath) -> std::string
{
	std::ifstream file(inPath, std::ios::binary);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open shader source file: " + inPath.string());
	}

	return std::string(
		std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>());
}

enum class ShaderStage
{
	VERTEX,
	FRAGMENT,
	COMPUTE,
	GEOMETRY,
	TESSELLATION_CONTROL,
	TESSELLATION_EVALUATION,
	TASK,
	MESH,
	RAYGEN,
	ANY_HIT,
	CLOSEST_HIT,
	MISS,
	INTERSECTION,
	CALLABLE,
};

auto GetShaderStage(const std::filesystem::path& inPath) -> ShaderStage
{
	const std::string extension = inPath.extension().string();
	if (extension == ".vert") return ShaderStage::VERTEX;
	if (extension == ".frag") return ShaderStage::FRAGMENT;
	if (extension == ".comp") return ShaderStage::COMPUTE;
	if (extension == ".geom") return ShaderStage::GEOMETRY;
	if (extension == ".tesc") return ShaderStage::TESSELLATION_CONTROL;
	if (extension == ".tese") return ShaderStage::TESSELLATION_EVALUATION;
	if (extension == ".task") return ShaderStage::TASK;
	if (extension == ".mesh") return ShaderStage::MESH;
	if (extension == ".rgen") return ShaderStage::RAYGEN;
	if (extension == ".rahit") return ShaderStage::ANY_HIT;
	if (extension == ".rchit") return ShaderStage::CLOSEST_HIT;
	if (extension == ".rmiss") return ShaderStage::MISS;
	if (extension == ".rint") return ShaderStage::INTERSECTION;
	if (extension == ".rcall") return ShaderStage::CALLABLE;

	throw std::runtime_error("Unsupported shader file extension: " + extension);
}

auto ToShadercKind(ShaderStage inStage) -> shaderc_shader_kind
{
	switch (inStage)
	{
	case ShaderStage::VERTEX: return shaderc_glsl_vertex_shader;
	case ShaderStage::FRAGMENT: return shaderc_glsl_fragment_shader;
	case ShaderStage::COMPUTE: return shaderc_glsl_compute_shader;
	case ShaderStage::GEOMETRY: return shaderc_glsl_geometry_shader;
	case ShaderStage::TESSELLATION_CONTROL: return shaderc_glsl_tess_control_shader;
	case ShaderStage::TESSELLATION_EVALUATION: return shaderc_glsl_tess_evaluation_shader;
	case ShaderStage::TASK: return shaderc_glsl_task_shader;
	case ShaderStage::MESH: return shaderc_glsl_mesh_shader;
	case ShaderStage::RAYGEN: return shaderc_glsl_raygen_shader;
	case ShaderStage::ANY_HIT: return shaderc_glsl_anyhit_shader;
	case ShaderStage::CLOSEST_HIT: return shaderc_glsl_closesthit_shader;
	case ShaderStage::MISS: return shaderc_glsl_miss_shader;
	case ShaderStage::INTERSECTION: return shaderc_glsl_intersection_shader;
	case ShaderStage::CALLABLE: return shaderc_glsl_callable_shader;
	}

	throw std::runtime_error("Unsupported shader stage.");
}
}

ShaderCompiler::ShaderCompiler(std::filesystem::path inShaderDirectory)
	: m_shaderDirectory(std::move(inShaderDirectory))
{
}

auto ShaderCompiler::CompileFile(const std::filesystem::path& inSourcePath) const -> std::vector<uint8_t>
{
	const std::filesystem::path sourcePath = inSourcePath.is_absolute()
		? inSourcePath
		: m_shaderDirectory / inSourcePath;
	const std::string source = ReadTextFile(sourcePath);

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
	options.SetSourceLanguage(shaderc_source_language_glsl);
	options.SetGenerateDebugInfo();

	const shaderc_shader_kind shaderKind = ToShadercKind(GetShaderStage(sourcePath));
	const std::string inputName = sourcePath.string();
	const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
		source,
		shaderKind,
		inputName.c_str(),
		"main",
		options);

	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		throw std::runtime_error(
			"Failed to compile shader '" + inputName + "':\n" + result.GetErrorMessage());
	}

	const size_t wordCount = static_cast<size_t>(result.cend() - result.cbegin());
	std::vector<uint8_t> spirv(wordCount * sizeof(uint32_t));
	if (!spirv.empty())
	{
		std::memcpy(spirv.data(), result.cbegin(), spirv.size());
	}
	return spirv;
}
