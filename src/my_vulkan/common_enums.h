#pragma once
#include "common.h"

enum class QueueFamilyType
{
	// According to Vulkan 1.3 spec:
	GRAPHICS, // supports graphics, compute, transfer
	COMPUTE,  // supports compute, transfer
	TRANSFER  // supports transfer
};