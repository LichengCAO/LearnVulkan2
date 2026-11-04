#pragma once

// Host descriptions use Vulkan value types, without a loader or window system.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifndef CHECK_TRUE
#define CHECK_TRUE(X, ...) CHECK_TRUE_IMPL(X __VA_OPT__(, ) __VA_ARGS__, "Check failed!")
#define CHECK_TRUE_IMPL(condition, failedMessage, ...) \
    do { if (!(condition)) throw std::runtime_error(failedMessage); } while (0)
#endif
