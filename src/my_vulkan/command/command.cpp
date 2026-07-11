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
