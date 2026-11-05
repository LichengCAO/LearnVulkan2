#pragma once
#include "common.h"
#include "private/command.h"
#include <span>
#include <variant>
#include "pipeline/graphics_shader_program.h"
#include "pipeline/compute_shader_program.h"
#include "pipeline/ray_tracing_shader_program.h"
#include "common_enums.h"

class Buffer;
class Image;
class ImageView;
class BottomLevelAccelStruct;
class TopLevelAccelStruct;
class BottomLevelAccelStructCreateInfo;
class TopLevelAccelStructCreateInfo;
struct VkAccelStructBuildInfo;
namespace VulkanDetail { class DynamicDescriptorPool; }

// Member commands own CPU parameters; image commands retain their image versions.
// Rendering attachments are borrowed through preparation, recording and GPU use.
// Other GPU resources remain caller-owned. Callbacks own their captures;
// any captured references must remain valid until recording completes.
class CommandBuffer final
{
    friend class RenderGraphInstance;
    friend struct RenderGraphTestProbe;
    friend class FrameSlot;
    friend struct ShaderBindingTestProbe;
    friend struct CommandBufferTestProbe;
    friend struct ResourceLifetimeTestProbe;
    friend struct ResourceSubmissionTestProbe;

private:
    struct LegacyRenderPassState final
    {
        uint32_t currentSubpass = 0;
        uint32_t subpassCount = 0;
        VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE;
        VulkanDetail::RenderPassSnapshot snapshot;
    };
    struct DynamicRenderingState final {};
    using RenderingScopeState = std::variant<std::monostate, LegacyRenderPassState, DynamicRenderingState>;

    std::vector<const Command*> m_commands;
    std::vector<std::unique_ptr<Command>> m_ownedCommands;
    std::vector<VulkanDetail::ResourceReference> m_resourceReferences;
    RenderingScopeState m_renderingScopeState;
    bool m_hasRenderingCommands = false;
    bool m_hasProgramBindings = false;
    bool m_requiresSingleVkCommandBuffer = false;
    GraphicsShaderProgram* m_graphicsProgram = nullptr;
    ComputeShaderProgram* m_computeProgram = nullptr;
    RayTracingShaderProgram* m_rayTracingProgram = nullptr;
    struct BindingCommand;

    void _PrepareCommands(VulkanDetail::DynamicDescriptorPool& inAllocator, QueueFamilyType inQueue);
    void _PrepareRenderPassCommands();
    void _BindShaderResources(const PipelineLayout& inLayout, VkPipelineBindPoint inBindPoint,
        const ShaderResources& inResources);
    void _AppendCommand(const Command& inCommand);
    // Internal RenderGraph commands are borrowed until recording completes.
    void _AppendCommands(const Command* const* inCommands, size_t inCount);
    void _AppendOwnedCommand(std::unique_ptr<Command> inCommand);
    void _AppendStateCommand(std::unique_ptr<Command> inCommand);
    void _Append(CommandBuffer&& inCommandBuffer);
    void _BeginRenderPass(const BeginRenderPassCommand::Parameters& inParameters,
        const VulkanDetail::RenderPassSnapshot& inSnapshot, VulkanDetail::ResolvedFramebufferDesc inFramebuffer);
    void _RequireOutsideRenderPass() const;
    void _RequireInlineRenderPass() const;
    void _RequireGraphicsProgram() const;
    void _ClearColorImage(Image& inImage, VkImageLayout inLayout, const VkClearColorValue& inColor,
        uint32_t inBaseMipLevel, uint32_t inLevelCount, uint32_t inBaseArrayLayer, uint32_t inLayerCount);
    void _CopyAccelerationStructureKHR(VkAccelerationStructureKHR inSrc, VkAccelerationStructureKHR inDst,
        VkCopyAccelerationStructureModeKHR inMode);

public:
    CommandBuffer() = default;
    CommandBuffer(const CommandBuffer&) = delete;
    auto operator=(const CommandBuffer&) -> CommandBuffer& = delete;
    CommandBuffer(CommandBuffer&& inOther) noexcept;
    auto operator=(CommandBuffer&& inOther) noexcept -> CommandBuffer&;
    ~CommandBuffer() = default;

    void BeginRenderPass(const RenderPassDesc& inRenderPass, const FramebufferDesc& inFramebuffer,
        int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight,
        VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE);
    void BeginRenderPass(const RenderPassDesc& inRenderPass, const FramebufferDesc& inFramebuffer,
        int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight,
        std::span<const VkClearValue> inClearValues,
        VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE);
    void NextSubpass(VkSubpassContents inContents = VK_SUBPASS_CONTENTS_INLINE);
    void EndRenderPass();

    void BindGraphicsProgram(GraphicsShaderProgram& inProgram, const GraphicsPipelineStateInfo& inStateInfo = {});
    void BindComputeProgram(ComputeShaderProgram& inProgram);
    void BindRayTracingProgram(RayTracingShaderProgram& inProgram);
    void BindGraphicsShaderResource(const ShaderResources& inResources);
    void BindComputeShaderResource(const ShaderResources& inResources);
    void BindRayTracingShaderResource(const ShaderResources& inResources);

    void BindVertexBuffers(uint32_t inFirstBinding, std::span<const Buffer* const> inBuffers,
        std::span<const VkDeviceSize> inOffsets);
    void BindIndexBuffer(const Buffer& inBuffer, VkDeviceSize inOffset, VkIndexType inIndexType);
    void SetViewport(float inX, float inY, float inWidth, float inHeight,
        float inMinDepth = 0.0f, float inMaxDepth = 1.0f, uint32_t inViewportIndex = 0);
    void SetScissor(int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight,
        uint32_t inScissorIndex = 0);

    void Draw(uint32_t inVertexCount, uint32_t inInstanceCount = 1,
        uint32_t inFirstVertex = 0, uint32_t inFirstInstance = 0);
    void DrawIndexed(uint32_t inIndexCount, uint32_t inInstanceCount = 1,
        uint32_t inFirstIndex = 0, int32_t inVertexOffset = 0, uint32_t inFirstInstance = 0);
    void DrawIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset, uint32_t inDrawCount = 1,
        uint32_t inStride = sizeof(VkDrawIndirectCommand));
    void DrawIndexedIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset, uint32_t inDrawCount = 1,
        uint32_t inStride = sizeof(VkDrawIndexedIndirectCommand));

    void Dispatch(uint32_t inGroupCountX, uint32_t inGroupCountY = 1, uint32_t inGroupCountZ = 1);
    void DispatchIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset);
    void PushConstants(const PipelineLayout& inLayout, VkShaderStageFlags inStageFlags,
        uint32_t inOffset, uint32_t inSize, const void* inData);

    void CopyBuffer(const Buffer& inSrcBuffer, Buffer& inDstBuffer,
        VkDeviceSize inSrcOffset, VkDeviceSize inDstOffset, VkDeviceSize inSize);
    void FillBuffer(Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize, uint32_t inValue);
    void UpdateBuffer(Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize, const void* inData);
    void CopyImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
        Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageCopy> inRegions);
    void CopyBufferToImage(const Buffer& inSrcBuffer, Image& inDstImage,
        VkImageLayout inDstLayout, std::span<const VkBufferImageCopy> inRegions);
    void CopyImageToBuffer(const Image& inSrcImage, VkImageLayout inSrcLayout,
        Buffer& inDstBuffer, std::span<const VkBufferImageCopy> inRegions);
    void BlitImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
        Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageBlit> inRegions,
        VkFilter inFilter = VK_FILTER_LINEAR);
    void ResolveImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
        Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageResolve> inRegions);
    void ClearColorImage(Image& inImage, VkImageLayout inLayout,
        float inR, float inG, float inB, float inA,
        uint32_t inBaseMipLevel = 0, uint32_t inLevelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t inBaseArrayLayer = 0, uint32_t inLayerCount = VK_REMAINING_ARRAY_LAYERS);
    void ClearColorImage(Image& inImage, VkImageLayout inLayout,
        int32_t inR, int32_t inG, int32_t inB, int32_t inA,
        uint32_t inBaseMipLevel = 0, uint32_t inLevelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t inBaseArrayLayer = 0, uint32_t inLayerCount = VK_REMAINING_ARRAY_LAYERS);
    void ClearColorImage(Image& inImage, VkImageLayout inLayout,
        uint32_t inR, uint32_t inG, uint32_t inB, uint32_t inA,
        uint32_t inBaseMipLevel = 0, uint32_t inLevelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t inBaseArrayLayer = 0, uint32_t inLayerCount = VK_REMAINING_ARRAY_LAYERS);
    void ClearDepthStencilImage(Image& inImage, VkImageLayout inLayout, VkImageAspectFlags inAspectMask,
        float inDepth = 1.0f, uint32_t inStencil = 0,
        uint32_t inBaseMipLevel = 0, uint32_t inLevelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t inBaseArrayLayer = 0, uint32_t inLayerCount = VK_REMAINING_ARRAY_LAYERS);

    void TraceRaysKHR(const ShaderBindingTable& inTable, uint32_t inWidth,
        uint32_t inHeight = 1, uint32_t inDepth = 1, uint32_t inRayGenerationIndex = 0);
    void BuildAccelerationStructuresKHR(std::span<const VkAccelStructBuildInfo> inBuildInfos);
    // Snapshots descriptions only. External buffers/contents must survive GPU use.
    void BuildAccelerationStructureKHR(BottomLevelAccelStruct& inDst,
        const BottomLevelAccelStructCreateInfo& inInfo, Buffer& inScratchBuffer,
        VkDeviceSize inScratchOffset = 0);
    void BuildAccelerationStructureKHR(TopLevelAccelStruct& inDst,
        const TopLevelAccelStructCreateInfo& inInfo, const Buffer& inInstanceBuffer,
        VkDeviceSize inInstanceOffset, Buffer& inScratchBuffer, VkDeviceSize inScratchOffset = 0);
    void UpdateAccelerationStructureKHR(const BottomLevelAccelStruct& inSrc, BottomLevelAccelStruct& inDst,
        const BottomLevelAccelStructCreateInfo& inInfo, Buffer& inScratchBuffer,
        VkDeviceSize inScratchOffset = 0);
    void UpdateAccelerationStructureKHR(const TopLevelAccelStruct& inSrc, TopLevelAccelStruct& inDst,
        const TopLevelAccelStructCreateInfo& inInfo, const Buffer& inInstanceBuffer,
        VkDeviceSize inInstanceOffset, Buffer& inScratchBuffer, VkDeviceSize inScratchOffset = 0);
    void CopyAccelerationStructureKHR(const BottomLevelAccelStruct& inSrc, BottomLevelAccelStruct& inDst,
        VkCopyAccelerationStructureModeKHR inMode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR);
    void CopyAccelerationStructureKHR(const TopLevelAccelStruct& inSrc, TopLevelAccelStruct& inDst,
        VkCopyAccelerationStructureModeKHR inMode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR);

    // Executes later during recording, in stream order. The callback must preserve
    // the tracked RenderPass lifecycle and Program/ShaderResource bindings.
    // This logical buffer remains one native command buffer.
    void AppendCommand(std::function<void(VkCommandBuffer)> inRecord);
    void KeepAlive(const Image& inImage);
    void KeepAlive(const ImageView& inImageView);
};
