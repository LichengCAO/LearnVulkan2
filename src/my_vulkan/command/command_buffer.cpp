#include "command_buffer.h"

auto CommandBuffer::_AppendCommand(const Command* inCommand)->void
{
	CHECK_TRUE(inCommand != nullptr, "No command to append!");
	m_commands.push_back(inCommand);
}

auto CommandBuffer::_AppendOwnedCommand(std::unique_ptr<Command> inCommand)->void
{
	CHECK_TRUE(inCommand != nullptr, "No command to append!");
	const Command* command = inCommand.get();
	m_ownedCommands.push_back(std::move(inCommand));
	_AppendCommand(command);
}

auto CommandBuffer::_Append(CommandBuffer&& inCommandBuffer)->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<std::monostate>(inCommandBuffer.m_renderingScopeState),
		"Cannot append a command buffer with an active rendering scope!");
	if (!std::holds_alternative<std::monostate>(m_renderingScopeState))
	{
		CHECK_TRUE(
			!inCommandBuffer.m_hasRenderingCommands,
			"Cannot append nested rendering commands into an active rendering scope!");
	}

	m_commands.insert(
		m_commands.end(),
		inCommandBuffer.m_commands.begin(),
		inCommandBuffer.m_commands.end());
	for (std::unique_ptr<Command>& command : inCommandBuffer.m_ownedCommands)
	{
		m_ownedCommands.push_back(std::move(command));
	}
	m_hasRenderingCommands = m_hasRenderingCommands || inCommandBuffer.m_hasRenderingCommands;

	inCommandBuffer.m_commands.clear();
	inCommandBuffer.m_ownedCommands.clear();
	inCommandBuffer.m_hasRenderingCommands = false;
	return *this;
}

auto CommandBuffer::BeginRenderPass(
	const BeginRenderPassCommand::Parameters& inParameters,
	uint32_t inSubpassCount)->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<std::monostate>(m_renderingScopeState),
		"A rendering scope is already active!");
	CHECK_TRUE(inParameters.renderPass != VK_NULL_HANDLE, "Invalid render pass!");
	CHECK_TRUE(inParameters.framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");
	CHECK_TRUE(inSubpassCount > 0, "Render pass must contain at least one subpass!");

	auto command = std::make_unique<BeginRenderPassCommand>();
	command->SetParameters(inParameters);
	_AppendOwnedCommand(std::move(command));
	m_hasRenderingCommands = true;

	m_renderingScopeState = LegacyRenderPassState{
		inParameters.renderPass,
		inParameters.framebuffer,
		0,
		inSubpassCount,
		inParameters.contents};
	return *this;
}

auto CommandBuffer::NextSubpass(VkSubpassContents inContents)->CommandBuffer&
{
	LegacyRenderPassState* state = std::get_if<LegacyRenderPassState>(&m_renderingScopeState);
	CHECK_TRUE(state != nullptr, "NextSubpass requires an active render pass!");
	CHECK_TRUE(
		state->currentSubpass + 1 < state->subpassCount,
		"No next subpass exists!");

	auto command = std::make_unique<NextSubpassCommand>();
	command->SetContents(inContents);
	_AppendOwnedCommand(std::move(command));

	++state->currentSubpass;
	state->contents = inContents;
	return *this;
}

auto CommandBuffer::EndRenderPass()->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<LegacyRenderPassState>(m_renderingScopeState),
		"EndRenderPass requires an active render pass!");

	_AppendOwnedCommand(std::make_unique<EndRenderPassCommand>());
	m_renderingScopeState = std::monostate{};
	return *this;
}

auto CommandBuffer::AddCommands(const Command* const* inCommands, size_t inCount)->CommandBuffer&
{
	if (inCount == 0)
	{
		return *this;
	}

	CHECK_TRUE(inCommands != nullptr, "No commands!");
	for (size_t commandIndex = 0; commandIndex < inCount; ++commandIndex)
	{
		const Command* command = inCommands[commandIndex];
		CHECK_TRUE(command != nullptr, "No command to append!");
		CHECK_TRUE(
			dynamic_cast<const BeginRenderPassCommand*>(command) == nullptr &&
			dynamic_cast<const NextSubpassCommand*>(command) == nullptr &&
			dynamic_cast<const EndRenderPassCommand*>(command) == nullptr,
			"Rendering scope commands must be appended through CommandBuffer members!");
	}
	for (size_t commandIndex = 0; commandIndex < inCount; ++commandIndex)
	{
		_AppendCommand(inCommands[commandIndex]);
	}
	return *this;
}
