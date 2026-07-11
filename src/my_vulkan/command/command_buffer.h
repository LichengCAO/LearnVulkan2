#pragma once
#include "command.h"
#include "common.h"
#include <variant>

class CommandBuffer final
{
public:
	struct PrimaryScope final
	{
		std::vector<const Command*> commands;
	};

	struct SubpassScope final
	{
		std::vector<const Command*> commands;
	};

	struct RenderPassScope final
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkRect2D renderArea{};
		std::vector<VkClearValue> clearValues;
		VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
		const void* next = nullptr;
		std::vector<SubpassScope> subpassScopes;
	};

	using Scope = std::variant<PrimaryScope, RenderPassScope>;

private:
	std::vector<Scope> m_scopes;

public:
	CommandBuffer() = default;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) noexcept = default;
	CommandBuffer& operator=(CommandBuffer&&) noexcept = default;
	~CommandBuffer() = default;

	auto AppendCommands(const PrimaryScope* inPrimaryScope)->CommandBuffer&;
	auto AppendRenderPass(const RenderPassScope* inRenderPassScope)->CommandBuffer&;

	friend class CommandQueue;
};
