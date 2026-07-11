#include "command_buffer.h"

auto CommandBuffer::AppendCommands(const PrimaryScope* inPrimaryScope)->CommandBuffer&
{
	CHECK_TRUE(inPrimaryScope != nullptr, "No primary scope!");
	m_scopes.emplace_back(*inPrimaryScope);
	return *this;
}

auto CommandBuffer::AppendRenderPass(const RenderPassScope* inRenderPassScope)->CommandBuffer&
{
	CHECK_TRUE(inRenderPassScope != nullptr, "No render pass scope!");
	CHECK_TRUE(inRenderPassScope->renderPass != VK_NULL_HANDLE, "Invalid render pass!");
	CHECK_TRUE(inRenderPassScope->framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");

	m_scopes.emplace_back(*inRenderPassScope);

	return *this;
}
