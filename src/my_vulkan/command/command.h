#pragma once
#include "common.h"

namespace CommandFramework
{

class CommandBuffer;

enum class CommandScopeType
{
	NONE,
	RENDER_PASS,
	COMPUTE_PASS,
	TRANSFER_PASS,
	DEBUG_LABEL,
};

class CommandRecordContext final
{
private:
	VkCommandBuffer m_vkCommandBuffer = VK_NULL_HANDLE;
	uint32_t m_threadIndex = 0;
	uint32_t m_frameIndex = 0;
	CommandScopeType m_scopeType = CommandScopeType::NONE;

public:
	CommandRecordContext() = default;
	CommandRecordContext(
		VkCommandBuffer inVkCommandBuffer,
		uint32_t inThreadIndex,
		uint32_t inFrameIndex,
		CommandScopeType inScopeType = CommandScopeType::NONE);

	auto Reset(
		VkCommandBuffer inVkCommandBuffer,
		uint32_t inThreadIndex,
		uint32_t inFrameIndex,
		CommandScopeType inScopeType = CommandScopeType::NONE)->void;

	auto GetVkCommandBuffer() const->VkCommandBuffer { return m_vkCommandBuffer; };
	auto GetThreadIndex() const->uint32_t { return m_threadIndex; };
	auto GetFrameIndex() const->uint32_t { return m_frameIndex; };
	auto GetScopeType() const->CommandScopeType { return m_scopeType; };
	auto SetScopeType(CommandScopeType inScopeType)->void { m_scopeType = inScopeType; };
};

class Command
{
public:
	Command() = default;
	Command(const Command&) = delete;
	Command& operator=(const Command&) = delete;
	virtual ~Command() = default;

	auto AppendTo(CommandBuffer* inoutCommandBuffer)->void;
	virtual auto GetScopeType() const->CommandScopeType { return CommandScopeType::NONE; };
	virtual auto Record(CommandRecordContext* inoutContext) const->void = 0;
};

}
