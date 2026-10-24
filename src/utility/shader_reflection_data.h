#pragma once
#include "common.h"

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
