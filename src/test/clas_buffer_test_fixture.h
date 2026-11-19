#pragma once
#include "my_vulkan/resource/buffer.h"
#include <span>

namespace ClasBufferTestFixture
{
void Run(void (*inTest)());
void WithInlineExecutor(void (*inTest)());
void FailNextReferenceBufferCreation();
void SetAllocationFailure(bool inFail);
void SetBufferAddressOverride(VkDeviceAddress inAddress);
auto Bytes(const Buffer& inBuffer) -> std::span<const uint8_t>;
auto Bytes(VkBuffer inBuffer) -> std::span<uint8_t>;
auto LiveBufferCount() -> size_t;
}
