#pragma once
#if defined(_WIN32)
#   define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__) || defined(__unix__)
#   define VK_USE_PLATFORM_XLIB_KHR
#elif defined(__APPLE__)
#   define VK_USE_PLATFORM_MACOS_MVK
#else
#endif
#define VK_NO_PROTOTYPES
#include <volk.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <set>
#include <array>
#include <functional>
#include <format>

//see https://stackoverflow.com/questions/77700691/getting-va-opt-to-be-recognized-by-visual-studio

#define VK_CHECK(X, ...) \
	VK_CHECK_IMPL(X __VA_OPT__(, ) __VA_ARGS__, "Vulkan check failed!")

#define VK_CHECK_IMPL(vkcommand, failedMessage, ...) \
		do\
		{\
			if((vkcommand)!=VK_SUCCESS)\
			{\
				throw std::runtime_error(failedMessage);\
			}\
		}while(0)

#define CHECK_TRUE(X, ...) \
	CHECK_TRUE_IMPL(X __VA_OPT__(, ) __VA_ARGS__, "Check failed!")

#define CHECK_TRUE_IMPL(condition, failedMessage, ...) \
do{\
if(!(condition)){\
   throw std::runtime_error(\
failedMessage);\
}\
}while(0)

#define CONTAIN_BITS(a, bits) (((a) & (bits)) == (bits)) 