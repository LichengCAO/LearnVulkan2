#pragma once
#include "command.h"

class RenderPass;
class Framebuffer;

namespace CommandFramework
{

class CommandBuffer final
{
public:
	using CommandIndex = uint32_t;

	struct RenderPassScope final
	{
		const RenderPass* renderPass = nullptr;
		const Framebuffer* framebuffer = nullptr;
		VkRect2D renderArea{};
		uint32_t subpassIndex = 0;
	};

private:
	std::vector<const Command*> m_commands;
	std::vector<CommandScopeType> m_scopeStack;
	std::optional<RenderPassScope> m_renderPassScope;
	bool m_closed = false;

public:
	CommandBuffer() = default;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	~CommandBuffer() = default;

	auto Reset()->void;
	auto Close()->void;
	auto IsClosed() const->bool { return m_closed; };

	auto AddCommand(const Command* inCommand)->CommandBuffer&;
	auto AddCommands(const Command* const* inCommands, uint32_t inCount)->CommandBuffer&;
	auto GetCommands() const->const std::vector<const Command*>& { return m_commands; };
	auto GetCommandCount() const->uint32_t { return static_cast<uint32_t>(m_commands.size()); };
	auto IsEmpty() const->bool { return m_commands.empty(); };

	auto BeginScope(CommandScopeType inScopeType)->CommandBuffer&;
	auto EndScope(CommandScopeType inScopeType)->CommandBuffer&;
	auto GetCurrentScopeType() const->CommandScopeType;
	auto GetScopeDepth() const->uint32_t { return static_cast<uint32_t>(m_scopeStack.size()); };

	auto BeginRenderPassScope(const RenderPassScope& inScope)->CommandBuffer&;
	auto EndRenderPassScope()->CommandBuffer&;
	auto IsInRenderPassScope() const->bool { return m_renderPassScope.has_value(); };
	auto GetRenderPassScope() const->const std::optional<RenderPassScope>& { return m_renderPassScope; };
};

}
