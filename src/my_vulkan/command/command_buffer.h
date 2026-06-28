#pragma once
#include "command.h"

class CommandBuffer final
{
public:
	enum class ScopeType
	{
		PRIMARY,
		RENDER_PASS,
		SECONDARY,
	};

	struct Scope final
	{
		ScopeType type = ScopeType::PRIMARY;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		const Command* enterCommand = nullptr;
		const Command* leaveCommand = nullptr;
	};

private:
	std::vector<const Command*> m_commands;
	uint32_t m_commandCount = 0;
	Scope m_scope;

private:
	auto _SetScope(const Scope& inScope)->void;
	auto _ClearScope()->void;
	auto _AppendCommandList(CommandBuffer&& inCommandBuffer)->void;
	auto _AppendPrimaryScope(CommandBuffer&& inCommandBuffer)->void;
	auto _AppendRenderPassScope(CommandBuffer&& inCommandBuffer, const Scope& inScope)->void;
	auto _AppendSecondaryScope(CommandBuffer&& inCommandBuffer, const Scope& inScope)->void;

public:
	CommandBuffer() = default;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) noexcept = default;
	CommandBuffer& operator=(CommandBuffer&&) noexcept = default;
	~CommandBuffer() = default;

	auto RecordCommand(const Command* inCommand)->CommandBuffer&;
	auto AppendScope(CommandBuffer inCommandBuffer, const Scope& inScope)->CommandBuffer&;
	auto GetCommandCount() const->uint32_t { return m_commandCount; };
	auto GetScope() const->const Scope& { return m_scope; };
	auto GetScopeType() const->ScopeType { return m_scope.type; };

	friend class CommandQueue;
};
