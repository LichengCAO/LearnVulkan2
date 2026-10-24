#pragma once
#include "common.h"
#include <span>
#include <string_view>

struct ShaderEntryReflection
{
	struct InterfaceVariable
	{
		std::string name;
		uint32_t location;
		VkFormat format;
	};
	struct Descriptor
	{
		std::string name;
		uint32_t set;
		uint32_t binding;
		VkDescriptorType type;
		uint32_t count;
	};
	struct PushConstant
	{
		std::string name;
		uint32_t offset;
		uint32_t size;
	};
	VkShaderStageFlagBits stage;
	std::string name;
	std::vector<Descriptor> descriptors;
	std::vector<PushConstant> pushConstants;
	std::vector<InterfaceVariable> inputs;
	std::vector<InterfaceVariable> outputs;
};

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
