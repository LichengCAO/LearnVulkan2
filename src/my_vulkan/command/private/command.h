#pragma once
#include "common.h"
#include "resource/private/vulkan_handle_resource.h"
#include "pipeline/private/render_target_description.h"

class CommandBuffer;

class Command
{
    friend class CommandBuffer;
    friend class RenderGraphInstance;
    friend struct ResourceLifetimeTestProbe;
private:
    std::vector<VulkanDetail::ResourceReference> m_resourceReferences;

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
	auto SetParameters(Parameters inParameters) -> ExternalCommand& { m_parameters = std::move(inParameters); return *this; }
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class BeginRenderPassCommand final : public Command
{
    friend class CommandBuffer;
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
	mutable Parameters m_parameters;
	VulkanDetail::RenderPassSnapshot m_snapshot;
	VulkanDetail::ResolvedFramebufferDesc m_framebuffer;
	mutable bool m_prepared = false;
	void _Prepare() const;

	void _SetDescription(const Parameters& inParameters, const VulkanDetail::RenderPassSnapshot& inSnapshot,
		VulkanDetail::ResolvedFramebufferDesc inFramebuffer)
	{
		m_parameters = inParameters;
		m_snapshot = inSnapshot;
		m_framebuffer = std::move(inFramebuffer);
		m_prepared = false;
	}
public:
	virtual auto Record(VkCommandBuffer inVkCommandBuffer) const->void override;
};

class NextSubpassCommand final : public Command
{
private:
	VkSubpassContents m_contents = VK_SUBPASS_CONTENTS_INLINE;

public:
	auto SetContents(VkSubpassContents inContents)->NextSubpassCommand&
	{
		m_contents = inContents;
		return *this;
	}
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
	std::vector<std::vector<VkAccelerationStructureGeometryKHR>> m_geometries;

public:
	auto SetParameters(const Parameters& inParameters) -> BuildAccelerationStructuresKHRCommand&;
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

// Owned snapshots used by CommandBuffer member commands.
class BindVertexBuffersCommand final : public Command
{
public:
    uint32_t firstBinding = 0;
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> offsets;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class BindIndexBufferCommand final : public Command
{
public:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class SetViewportCommand final : public Command
{
public:
    uint32_t index = 0;
    VkViewport viewport{};
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class SetScissorCommand final : public Command
{
public:
    uint32_t index = 0;
    VkRect2D scissor{};
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DrawCommand final : public Command
{
public:
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DrawIndexedCommand final : public Command
{
public:
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DrawIndirectCommand final : public Command
{
public:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint32_t drawCount = 1;
    uint32_t stride = sizeof(VkDrawIndirectCommand);
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DrawIndexedIndirectCommand final : public Command
{
public:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint32_t drawCount = 1;
    uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DispatchCommand final : public Command
{
public:
    uint32_t x = 1;
    uint32_t y = 1;
    uint32_t z = 1;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class DispatchIndirectCommand final : public Command
{
public:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class PushConstantsCommand final : public Command
{
public:
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderStageFlags stages = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> data;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class UpdateBufferCommand final : public Command
{
public:
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    std::vector<uint8_t> data;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class CopyImageCommand final : public Command
{
public:
    VkImage src = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage dst = VK_NULL_HANDLE;
    VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageCopy> regions;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class CopyImageToBufferCommand final : public Command
{
public:
    VkImage src = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkBuffer dst = VK_NULL_HANDLE;
    std::vector<VkBufferImageCopy> regions;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class ResolveImageCommand final : public Command
{
public:
    VkImage src = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage dst = VK_NULL_HANDLE;
    VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkImageResolve> regions;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class ClearDepthStencilImageCommand final : public Command
{
public:
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkClearDepthStencilValue value{};
    VkImageSubresourceRange range{};
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};

class TraceRaysKHRCommand final : public Command
{
public:
    std::array<VkStridedDeviceAddressRegionKHR, 4> regions{};
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    void Record(VkCommandBuffer inVkCommandBuffer) const override;
};
