#pragma once
#include "my_vulkan/resource/buffer.h"
#include <span>

namespace ClasBufferTestFixture
{
void Run(void (*inTest)());
void FailNextReferenceBufferCreation();
auto Bytes(const Buffer& inBuffer) -> std::span<const uint8_t>;
auto LiveBufferCount() -> size_t;
}
