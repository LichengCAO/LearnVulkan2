#include "command.h"

auto BeginRenderPassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
	CHECK_TRUE(m_parameters.renderPass != VK_NULL_HANDLE, "Invalid render pass!");
	CHECK_TRUE(m_parameters.framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");

	VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	beginInfo.pNext = m_parameters.next;
	beginInfo.renderPass = m_parameters.renderPass;
	beginInfo.framebuffer = m_parameters.framebuffer;
	beginInfo.renderArea = m_parameters.renderArea;
	beginInfo.clearValueCount = static_cast<uint32_t>(m_parameters.clearValues.size());
	beginInfo.pClearValues = m_parameters.clearValues.data();

	vkCmdBeginRenderPass(inVkCommandBuffer, &beginInfo, m_parameters.contents);
}

auto EndRenderPassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

	vkCmdEndRenderPass(inVkCommandBuffer);
}

auto PipelineBarrierCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

	const bool hasMemoryBarriers = !m_parameters.memoryBarriers.empty();
	const bool hasBufferBarriers = !m_parameters.bufferBarriers.empty();
	const bool hasImageBarriers = !m_parameters.imageBarriers.empty();
	if (!hasMemoryBarriers && !hasBufferBarriers && !hasImageBarriers)
	{
		return;
	}

	CHECK_TRUE(m_parameters.srcStageMask != 0, "Invalid source pipeline stage mask!");
	CHECK_TRUE(m_parameters.dstStageMask != 0, "Invalid destination pipeline stage mask!");

	vkCmdPipelineBarrier(
		inVkCommandBuffer,
		m_parameters.srcStageMask,
		m_parameters.dstStageMask,
		m_parameters.flags,
		static_cast<uint32_t>(m_parameters.memoryBarriers.size()),
		m_parameters.memoryBarriers.empty() ? nullptr : m_parameters.memoryBarriers.data(),
		static_cast<uint32_t>(m_parameters.bufferBarriers.size()),
		m_parameters.bufferBarriers.empty() ? nullptr : m_parameters.bufferBarriers.data(),
		static_cast<uint32_t>(m_parameters.imageBarriers.size()),
		m_parameters.imageBarriers.empty() ? nullptr : m_parameters.imageBarriers.data());
}
