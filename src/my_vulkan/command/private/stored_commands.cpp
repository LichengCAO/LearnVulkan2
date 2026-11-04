#include "command.h"

void BindVertexBuffersCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdBindVertexBuffers(inVkCommandBuffer, firstBinding, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
}

void BindIndexBufferCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdBindIndexBuffer(inVkCommandBuffer, buffer, offset, indexType);
}

void SetViewportCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdSetViewport(inVkCommandBuffer, index, 1, &viewport);
}

void SetScissorCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdSetScissor(inVkCommandBuffer, index, 1, &scissor);
}

void DrawCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDraw(inVkCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void DrawIndexedCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDrawIndexed(inVkCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DrawIndirectCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDrawIndirect(inVkCommandBuffer, buffer, offset, drawCount, stride);
}

void DrawIndexedIndirectCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDrawIndexedIndirect(inVkCommandBuffer, buffer, offset, drawCount, stride);
}

void DispatchCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDispatch(inVkCommandBuffer, x, y, z);
}

void DispatchIndirectCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdDispatchIndirect(inVkCommandBuffer, buffer, offset);
}

void PushConstantsCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdPushConstants(inVkCommandBuffer, layout, stages, offset, static_cast<uint32_t>(data.size()), data.data());
}

void UpdateBufferCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdUpdateBuffer(inVkCommandBuffer, buffer, offset, data.size(), data.data());
}

void CopyImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdCopyImage(inVkCommandBuffer, src, srcLayout, dst, dstLayout, static_cast<uint32_t>(regions.size()), regions.data());
}

void CopyImageToBufferCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdCopyImageToBuffer(inVkCommandBuffer, src, srcLayout, dst, static_cast<uint32_t>(regions.size()), regions.data());
}

void ResolveImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdResolveImage(inVkCommandBuffer, src, srcLayout, dst, dstLayout, static_cast<uint32_t>(regions.size()), regions.data());
}

void ClearDepthStencilImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdClearDepthStencilImage(inVkCommandBuffer, image, layout, &value, 1, &range);
}

void TraceRaysKHRCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(vkCmdTraceRaysKHR != nullptr, "Ray tracing commands are unavailable!");
    vkCmdTraceRaysKHR(inVkCommandBuffer, &regions[0], &regions[1], &regions[2], &regions[3], width, height, depth);
}
