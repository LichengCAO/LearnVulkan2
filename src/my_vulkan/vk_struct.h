#pragma once
#include "common.h"

struct QueueFamilyIndices 
{
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> graphicsAndComputeFamily;
	std::optional<uint32_t> transferFamily;
	bool isComplete() 
	{
		bool ok1 = graphicsFamily.has_value()
			&& presentFamily.has_value()
			&& graphicsAndComputeFamily.has_value()
			&& transferFamily.has_value();
		return ok1;
	}
};

struct SwapChainSupportDetails 
{
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

enum class QueueFamilyType
{
	// According to Vulkan 1.3 spec:
	GRAPHICS, // supports graphics, compute, transfer
	COMPUTE,  // supports compute, transfer
	TRANSFER  // supports transfer
};