#pragma once
#include "common.h"

class CommandBuffer;

class Command
{
public:
	Command() = default;
	Command(const Command&) = delete;
	Command& operator=(const Command&) = delete;
	virtual ~Command() = default;

	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void = 0;
};

class BeginCommandsCommand final : public Command
{
public:
	struct Parameters final
	{
		VkCommandBufferUsageFlags flags = 0;
		std::optional<VkCommandBufferInheritanceInfo> inheritanceInfo;
		const void* next = nullptr;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->BeginCommandsCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class EndCommandsCommand final : public Command
{
public:
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class ExternalCommand final : public Command
{
public:
	struct Parameters final
	{
		std::function<void(VkCommandBuffer)> record;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->ExternalCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class BeginRenderPassCommand final : public Command
{
public:
	struct Parameters final
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkRect2D renderArea{};
		std::vector<VkClearValue> clearValues;
		VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
		const void* next = nullptr;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->BeginRenderPassCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class EndRenderPassCommand final : public Command
{
public:
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class PipelineBarrierCommand final : public Command
{
public:
	struct Parameters final
	{
		VkPipelineStageFlags srcStageMask = 0;
		VkPipelineStageFlags dstStageMask = 0;
		std::vector<VkMemoryBarrier> memoryBarriers;
		std::vector<VkBufferMemoryBarrier> bufferBarriers;
		std::vector<VkImageMemoryBarrier> imageBarriers;
		VkDependencyFlags flags = 0;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->PipelineBarrierCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class ClearColorImageCommand final : public Command
{
public:
	struct Parameters final
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkClearColorValue clearColor{};
		std::vector<VkImageSubresourceRange> ranges;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->ClearColorImageCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class FillBufferCommand final : public Command
{
public:
	struct Parameters final
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
		VkDeviceSize size = VK_WHOLE_SIZE;
		uint32_t value = 0;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->FillBufferCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class BlitImageCommand final : public Command
{
public:
	struct Parameters final
	{
		VkImage srcImage = VK_NULL_HANDLE;
		VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImage dstImage = VK_NULL_HANDLE;
		VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		std::vector<VkImageBlit> regions;
		VkFilter filter = VK_FILTER_LINEAR;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->BlitImageCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class CopyBufferToImageCommand final : public Command
{
public:
	struct Parameters final
	{
		VkBuffer srcBuffer = VK_NULL_HANDLE;
		VkImage dstImage = VK_NULL_HANDLE;
		VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		std::vector<VkBufferImageCopy> regions;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->CopyBufferToImageCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class CopyBufferCommand final : public Command
{
public:
	struct Parameters final
	{
		VkBuffer srcBuffer = VK_NULL_HANDLE;
		VkBuffer dstBuffer = VK_NULL_HANDLE;
		std::vector<VkBufferCopy> regions;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->CopyBufferCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class BuildAccelerationStructuresKHRCommand final : public Command
{
public:
	struct Parameters final
	{
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
		std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRanges;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->BuildAccelerationStructuresKHRCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class WriteAccelerationStructuresPropertiesKHRCommand final : public Command
{
public:
	struct Parameters final
	{
		std::vector<VkAccelerationStructureKHR> accelerationStructures;
		VkQueryType queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
		VkQueryPool queryPool = VK_NULL_HANDLE;
		uint32_t firstQuery = 0;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->WriteAccelerationStructuresPropertiesKHRCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class CopyAccelerationStructureKHRCommand final : public Command
{
public:
	struct Parameters final
	{
		VkAccelerationStructureKHR src = VK_NULL_HANDLE;
		VkAccelerationStructureKHR dst = VK_NULL_HANDLE;
		VkCopyAccelerationStructureModeKHR mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
		const void* next = nullptr;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->CopyAccelerationStructureKHRCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class ExecuteCommandsCommand final : public Command
{
public:
	struct Parameters final
	{
		std::vector<VkCommandBuffer> commandBuffers;
	};

private:
	Parameters m_parameters;

public:
	auto SetParameters(const Parameters& inParameters)->ExecuteCommandsCommand& { m_parameters = inParameters; return *this; };
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};
