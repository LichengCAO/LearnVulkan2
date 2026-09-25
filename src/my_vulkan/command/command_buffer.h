#pragma once
#include "common.h"
#include "command.h"
#include <variant>

class CommandBuffer final
{
	friend class RenderGraphInstance;
	friend struct RenderGraphTestProbe;
	friend class CommandQueue;
	friend class FrameContext;

private:
	struct LegacyRenderPassState final
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		uint32_t currentSubpass = 0;
		uint32_t subpassCount = 0;
		VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
	};

	struct DynamicRenderingState final
	{
	};

	using RenderingScopeState = std::variant<
		std::monostate,
		LegacyRenderPassState,
		DynamicRenderingState>;

private:
	std::vector<const Command*> m_commands;
	std::vector<std::unique_ptr<Command>> m_ownedCommands;
	RenderingScopeState m_renderingScopeState;
	bool m_hasRenderingCommands = false;

private:
	auto _AppendCommand(const Command* inCommand)->void;
	auto _AppendOwnedCommand(std::unique_ptr<Command> inCommand)->void;
	auto _Append(CommandBuffer&& inCommandBuffer)->CommandBuffer&;

public:
	CommandBuffer() = default;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) noexcept = default;
	CommandBuffer& operator=(CommandBuffer&&) noexcept = default;
	~CommandBuffer() = default;

	auto BeginRenderPass(
		const BeginRenderPassCommand::Parameters& inParameters,
		uint32_t inSubpassCount = 1)->CommandBuffer&;
	auto NextSubpass(
		VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE)->CommandBuffer&;
	auto EndRenderPass()->CommandBuffer&;

	// Commands are borrowed and must remain alive until this command buffer is enqueued.
	auto AddCommands(const Command* const* inCommands, size_t inCount)->CommandBuffer&;
};
