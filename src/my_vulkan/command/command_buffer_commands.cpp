#include "command_buffer.h"
#include "resource/buffer.h"
#include "resource/image.h"
#include "resource/acceleration_structure.h"
#include "pipeline/render_pass.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    auto BufferHandle(const Buffer& inBuffer) -> VkBuffer
    {
        const auto handle = inBuffer.GetVkBuffer();
        CHECK_TRUE(handle != VK_NULL_HANDLE, "Buffer was not created!");
        return handle;
    }

    auto ImageHandle(const Image& inImage) -> VkImage
    {
        const auto handle = inImage.GetVkImage();
        CHECK_TRUE(handle != VK_NULL_HANDLE, "Image was not created!");
        return handle;
    }

    void ValidateBufferRange(const Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize)
    {
        BufferHandle(inBuffer);
        const auto capacity = inBuffer.GetBufferInformation().size;
        CHECK_TRUE(inSize > 0 && inOffset <= capacity && inSize <= capacity - inOffset,
            "Buffer range is out of bounds!");
    }

    void ValidateRect(int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight)
    {
        CHECK_TRUE(inX >= 0 && inY >= 0, "Rectangle offset must not be negative!");
        CHECK_TRUE(uint64_t(inX) + inWidth <= std::numeric_limits<int32_t>::max() &&
            uint64_t(inY) + inHeight <= std::numeric_limits<int32_t>::max(), "Rectangle extent overflows!");
    }

    template<typename T>
    void ValidateRegions(std::span<const T> inRegions)
    {
        CHECK_TRUE(!inRegions.empty() && inRegions.size() <= std::numeric_limits<uint32_t>::max(),
            "Invalid region count!");
    }

    auto ImageRange(const Image& inImage, VkImageAspectFlags inAspect, uint32_t inBaseMip,
        uint32_t inMipCount, uint32_t inBaseLayer, uint32_t inLayerCount) -> VkImageSubresourceRange
    {
        ImageHandle(inImage);
        const auto& info = inImage.GetImageInformation();
        CHECK_TRUE(inBaseMip < info.mipLevels && inBaseLayer < info.arrayLayers, "Image range is out of bounds!");
        CHECK_TRUE(inMipCount != 0 && (inMipCount == VK_REMAINING_MIP_LEVELS ||
            inMipCount <= info.mipLevels - inBaseMip), "Invalid mip range!");
        CHECK_TRUE(inLayerCount != 0 && (inLayerCount == VK_REMAINING_ARRAY_LAYERS ||
            inLayerCount <= info.arrayLayers - inBaseLayer), "Invalid array layer range!");
        return { inAspect, inBaseMip, inMipCount, inBaseLayer, inLayerCount };
    }

    void ValidateIndirect(const Buffer& inBuffer, VkDeviceSize inOffset, uint32_t inCount,
        uint32_t inStride, uint32_t inCommandSize)
    {
        BufferHandle(inBuffer);
        CHECK_TRUE(inOffset % 4 == 0, "Indirect argument offset must be four-byte aligned!");
        if (inCount == 0)
            return;
        CHECK_TRUE(inCount == 1 || (inStride >= inCommandSize && inStride % 4 == 0),
            "Invalid indirect argument stride!");
        ValidateBufferRange(inBuffer, inOffset, VkDeviceSize(inCount - 1) * inStride + inCommandSize);
    }
}

void CommandBuffer::_AppendStateCommand(std::unique_ptr<Command> inCommand)
{
    _AppendOwnedCommand(std::move(inCommand));
    m_requiresSingleVkCommandBuffer = true;
}

void CommandBuffer::_RequireOutsideRenderPass() const
{
    CHECK_TRUE(std::holds_alternative<std::monostate>(m_renderingScopeState),
        "Command requires no active render pass!");
}

void CommandBuffer::_RequireInlineRenderPass() const
{
    const auto* scope = std::get_if<LegacyRenderPassState>(&m_renderingScopeState);
    CHECK_TRUE(scope && scope->contents == VK_SUBPASS_CONTENTS_INLINE,
        "Command requires an inline render pass!");
}

void CommandBuffer::_RequireGraphicsProgram() const
{
    _RequireInlineRenderPass();
    CHECK_TRUE(m_graphicsProgram != nullptr, "Bind a graphics program before drawing!");
}

void CommandBuffer::BeginRenderPass(const RenderPass& inRenderPass, const Framebuffer& inFramebuffer,
    int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight, VkSubpassContents inContents)
{
    BeginRenderPass(inRenderPass, inFramebuffer, inX, inY, inWidth, inHeight,
        inRenderPass.GetClearValues(), inContents);
}

void CommandBuffer::BeginRenderPass(const RenderPass& inRenderPass, const Framebuffer& inFramebuffer,
    int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight,
    std::span<const VkClearValue> inClearValues, VkSubpassContents inContents)
{
    ValidateRect(inX, inY, inWidth, inHeight);
    CHECK_TRUE(inWidth > 0 && inHeight > 0, "Render area must not be empty!");
    CHECK_TRUE(inClearValues.size() <= std::numeric_limits<uint32_t>::max(), "Too many clear values!");
    BeginRenderPassCommand::Parameters parameters;
    parameters.renderPass = inRenderPass.GetVkRenderPass();
    parameters.framebuffer = inFramebuffer.GetVkFramebuffer();
    parameters.renderArea = { { inX, inY }, { inWidth, inHeight } };
    parameters.clearValues.assign(inClearValues.begin(), inClearValues.end());
    parameters.contents = inContents;
    _BeginRenderPass(parameters, inRenderPass.GetSubpassCount());
}

void CommandBuffer::BindVertexBuffers(uint32_t inFirstBinding, std::span<const Buffer* const> inBuffers,
    std::span<const VkDeviceSize> inOffsets)
{
    ValidateRegions(inBuffers);
    CHECK_TRUE(inBuffers.size() == inOffsets.size(), "Vertex buffer/offset count mismatch!");
    CHECK_TRUE(inBuffers.size() <= uint64_t(std::numeric_limits<uint32_t>::max()) - inFirstBinding,
        "Vertex binding range overflows!");
    auto command = std::make_unique<BindVertexBuffersCommand>();
    command->firstBinding = inFirstBinding;
    command->offsets.assign(inOffsets.begin(), inOffsets.end());
    for (size_t i = 0; i < inBuffers.size(); ++i)
    {
        CHECK_TRUE(inBuffers[i] != nullptr, "Missing vertex buffer!");
        ValidateBufferRange(*inBuffers[i], inOffsets[i], 1);
        command->buffers.push_back(BufferHandle(*inBuffers[i]));
    }
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::BindIndexBuffer(const Buffer& inBuffer, VkDeviceSize inOffset, VkIndexType inIndexType)
{
    uint32_t alignment = 0;
    switch (inIndexType)
    {
    case VK_INDEX_TYPE_UINT16: alignment = 2; break;
    case VK_INDEX_TYPE_UINT32: alignment = 4; break;
    case VK_INDEX_TYPE_UINT8_EXT: alignment = 1; break;
    default: CHECK_TRUE(false, "Invalid index type!");
    }
    CHECK_TRUE(inOffset % alignment == 0, "Invalid index buffer alignment!");
    ValidateBufferRange(inBuffer, inOffset, alignment);
    auto command = std::make_unique<BindIndexBufferCommand>();
    command->buffer = BufferHandle(inBuffer);
    command->offset = inOffset;
    command->indexType = inIndexType;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::SetViewport(float inX, float inY, float inWidth, float inHeight,
    float inMinDepth, float inMaxDepth, uint32_t inViewportIndex)
{
    CHECK_TRUE(std::isfinite(inX) && std::isfinite(inY) && std::isfinite(inWidth) && std::isfinite(inHeight) &&
        inWidth > 0 && inHeight != 0, "Invalid viewport dimensions!");
    CHECK_TRUE(inMinDepth >= 0 && inMinDepth <= 1 && inMaxDepth >= 0 && inMaxDepth <= 1,
        "Viewport depth must be in [0, 1]!");
    auto command = std::make_unique<SetViewportCommand>();
    command->index = inViewportIndex;
    command->viewport = { inX, inY, inWidth, inHeight, inMinDepth, inMaxDepth };
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::SetScissor(int32_t inX, int32_t inY, uint32_t inWidth, uint32_t inHeight,
    uint32_t inScissorIndex)
{
    ValidateRect(inX, inY, inWidth, inHeight);
    auto command = std::make_unique<SetScissorCommand>();
    command->index = inScissorIndex;
    command->scissor = { { inX, inY }, { inWidth, inHeight } };
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::Draw(uint32_t inVertexCount, uint32_t inInstanceCount,
    uint32_t inFirstVertex, uint32_t inFirstInstance)
{
    _RequireGraphicsProgram();
    auto command = std::make_unique<DrawCommand>();
    command->vertexCount = inVertexCount;
    command->instanceCount = inInstanceCount;
    command->firstVertex = inFirstVertex;
    command->firstInstance = inFirstInstance;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::DrawIndexed(uint32_t inIndexCount, uint32_t inInstanceCount,
    uint32_t inFirstIndex, int32_t inVertexOffset, uint32_t inFirstInstance)
{
    _RequireGraphicsProgram();
    auto command = std::make_unique<DrawIndexedCommand>();
    command->indexCount = inIndexCount;
    command->instanceCount = inInstanceCount;
    command->firstIndex = inFirstIndex;
    command->vertexOffset = inVertexOffset;
    command->firstInstance = inFirstInstance;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::DrawIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset,
    uint32_t inDrawCount, uint32_t inStride)
{
    _RequireGraphicsProgram();
    ValidateIndirect(inArgumentBuffer, inOffset, inDrawCount, inStride, sizeof(VkDrawIndirectCommand));
    auto command = std::make_unique<DrawIndirectCommand>();
    command->buffer = BufferHandle(inArgumentBuffer);
    command->offset = inOffset;
    command->drawCount = inDrawCount;
    command->stride = inStride;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::DrawIndexedIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset,
    uint32_t inDrawCount, uint32_t inStride)
{
    _RequireGraphicsProgram();
    ValidateIndirect(inArgumentBuffer, inOffset, inDrawCount, inStride, sizeof(VkDrawIndexedIndirectCommand));
    auto command = std::make_unique<DrawIndexedIndirectCommand>();
    command->buffer = BufferHandle(inArgumentBuffer);
    command->offset = inOffset;
    command->drawCount = inDrawCount;
    command->stride = inStride;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::Dispatch(uint32_t inGroupCountX, uint32_t inGroupCountY, uint32_t inGroupCountZ)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(m_computeProgram != nullptr, "Bind a compute program before dispatching!");
    auto command = std::make_unique<DispatchCommand>();
    command->x = inGroupCountX;
    command->y = inGroupCountY;
    command->z = inGroupCountZ;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::DispatchIndirect(const Buffer& inArgumentBuffer, VkDeviceSize inOffset)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(m_computeProgram != nullptr, "Bind a compute program before dispatching!");
    ValidateIndirect(inArgumentBuffer, inOffset, 1, 0, sizeof(VkDispatchIndirectCommand));
    auto command = std::make_unique<DispatchIndirectCommand>();
    command->buffer = BufferHandle(inArgumentBuffer);
    command->offset = inOffset;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::PushConstants(const PipelineLayout& inLayout, VkShaderStageFlags inStageFlags,
    uint32_t inOffset, uint32_t inSize, const void* inData)
{
    CHECK_TRUE(inLayout.GetVkPipelineLayout() != VK_NULL_HANDLE, "Pipeline layout was not created!");
    CHECK_TRUE(inStageFlags != 0 && inData != nullptr && inSize > 0 &&
        inOffset % 4 == 0 && inSize % 4 == 0 &&
        inSize <= std::numeric_limits<uint32_t>::max() - inOffset, "Invalid push constant data!");
    auto command = std::make_unique<PushConstantsCommand>();
    command->layout = inLayout.GetVkPipelineLayout();
    command->stages = inStageFlags;
    command->offset = inOffset;
    const auto* bytes = static_cast<const uint8_t*>(inData);
    command->data.assign(bytes, bytes + inSize);
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::CopyBuffer(const Buffer& inSrcBuffer, Buffer& inDstBuffer,
    VkDeviceSize inSrcOffset, VkDeviceSize inDstOffset, VkDeviceSize inSize)
{
    _RequireOutsideRenderPass();
    ValidateBufferRange(inSrcBuffer, inSrcOffset, inSize);
    ValidateBufferRange(inDstBuffer, inDstOffset, inSize);
    auto command = std::make_unique<CopyBufferCommand>();
    CopyBufferCommand::Parameters parameters;
    parameters.srcBuffer = BufferHandle(inSrcBuffer);
    parameters.dstBuffer = BufferHandle(inDstBuffer);
    parameters.regions.push_back({ inSrcOffset, inDstOffset, inSize });
    command->SetParameters(parameters);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::FillBuffer(Buffer& inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize, uint32_t inValue)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(inOffset % 4 == 0 && (inSize == VK_WHOLE_SIZE || inSize % 4 == 0),
        "Fill buffer range must be four-byte aligned!");
    if (inSize == VK_WHOLE_SIZE)
    {
        const auto capacity = inBuffer.GetBufferInformation().size;
        CHECK_TRUE(inOffset < capacity, "Buffer range is out of bounds!");
        ValidateBufferRange(inBuffer, inOffset, capacity - inOffset);
    }
    else
        ValidateBufferRange(inBuffer, inOffset, inSize);
    auto command = std::make_unique<FillBufferCommand>();
    command->SetParameters({ BufferHandle(inBuffer), inOffset, inSize, inValue });
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::UpdateBuffer(Buffer& inBuffer, VkDeviceSize inOffset,
    VkDeviceSize inSize, const void* inData)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(inData != nullptr && inOffset % 4 == 0 && inSize % 4 == 0 && inSize <= 65536,
        "Update buffer requires aligned data of at most 65536 bytes!");
    ValidateBufferRange(inBuffer, inOffset, inSize);
    auto command = std::make_unique<UpdateBufferCommand>();
    command->buffer = BufferHandle(inBuffer);
    command->offset = inOffset;
    const auto* bytes = static_cast<const uint8_t*>(inData);
    command->data.assign(bytes, bytes + static_cast<size_t>(inSize));
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::CopyImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
    Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageCopy> inRegions)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inRegions);
    auto command = std::make_unique<CopyImageCommand>();
    command->src = ImageHandle(inSrcImage);
    command->srcLayout = inSrcLayout;
    command->dst = ImageHandle(inDstImage);
    command->dstLayout = inDstLayout;
    command->regions.assign(inRegions.begin(), inRegions.end());
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::CopyBufferToImage(const Buffer& inSrcBuffer, Image& inDstImage,
    VkImageLayout inDstLayout, std::span<const VkBufferImageCopy> inRegions)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inRegions);
    CopyBufferToImageCommand::Parameters parameters;
    parameters.srcBuffer = BufferHandle(inSrcBuffer);
    parameters.dstImage = ImageHandle(inDstImage);
    parameters.dstLayout = inDstLayout;
    parameters.regions.assign(inRegions.begin(), inRegions.end());
    auto command = std::make_unique<CopyBufferToImageCommand>();
    command->SetParameters(parameters);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::CopyImageToBuffer(const Image& inSrcImage, VkImageLayout inSrcLayout,
    Buffer& inDstBuffer, std::span<const VkBufferImageCopy> inRegions)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inRegions);
    auto command = std::make_unique<CopyImageToBufferCommand>();
    command->src = ImageHandle(inSrcImage);
    command->srcLayout = inSrcLayout;
    command->dst = BufferHandle(inDstBuffer);
    command->regions.assign(inRegions.begin(), inRegions.end());
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::BlitImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
    Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageBlit> inRegions, VkFilter inFilter)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inRegions);
    BlitImageCommand::Parameters parameters;
    parameters.srcImage = ImageHandle(inSrcImage);
    parameters.srcLayout = inSrcLayout;
    parameters.dstImage = ImageHandle(inDstImage);
    parameters.dstLayout = inDstLayout;
    parameters.regions.assign(inRegions.begin(), inRegions.end());
    parameters.filter = inFilter;
    auto command = std::make_unique<BlitImageCommand>();
    command->SetParameters(parameters);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::ResolveImage(const Image& inSrcImage, VkImageLayout inSrcLayout,
    Image& inDstImage, VkImageLayout inDstLayout, std::span<const VkImageResolve> inRegions)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inRegions);
    auto command = std::make_unique<ResolveImageCommand>();
    command->src = ImageHandle(inSrcImage);
    command->srcLayout = inSrcLayout;
    command->dst = ImageHandle(inDstImage);
    command->dstLayout = inDstLayout;
    command->regions.assign(inRegions.begin(), inRegions.end());
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::_ClearColorImage(Image& inImage, VkImageLayout inLayout, const VkClearColorValue& inColor,
    uint32_t inBaseMipLevel, uint32_t inLevelCount, uint32_t inBaseArrayLayer, uint32_t inLayerCount)
{
    _RequireOutsideRenderPass();
    const auto range = ImageRange(inImage, VK_IMAGE_ASPECT_COLOR_BIT,
        inBaseMipLevel, inLevelCount, inBaseArrayLayer, inLayerCount);
    auto command = std::make_unique<ClearColorImageCommand>();
    ClearColorImageCommand::Parameters parameters;
    parameters.image = ImageHandle(inImage);
    parameters.imageLayout = inLayout;
    parameters.clearColor = inColor;
    parameters.ranges.push_back(range);
    command->SetParameters(parameters);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::ClearDepthStencilImage(Image& inImage, VkImageLayout inLayout, VkImageAspectFlags inAspectMask,
    float inDepth, uint32_t inStencil, uint32_t inBaseMipLevel, uint32_t inLevelCount,
    uint32_t inBaseArrayLayer, uint32_t inLayerCount)
{
    _RequireOutsideRenderPass();
    constexpr auto aspects = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    CHECK_TRUE(inAspectMask != 0 && (inAspectMask & ~aspects) == 0, "Invalid depth/stencil aspects!");
    CHECK_TRUE(inDepth >= 0.f && inDepth <= 1.f, "Depth clear value must be in [0, 1]!");
    auto command = std::make_unique<ClearDepthStencilImageCommand>();
    command->image = ImageHandle(inImage);
    command->layout = inLayout;
    command->value = { inDepth, inStencil };
    command->range = ImageRange(inImage, inAspectMask,
        inBaseMipLevel, inLevelCount, inBaseArrayLayer, inLayerCount);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::TraceRaysKHR(const ShaderBindingTable& inTable, uint32_t inWidth,
    uint32_t inHeight, uint32_t inDepth, uint32_t inRayGenerationIndex)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(m_rayTracingProgram != nullptr, "Bind a ray tracing program before tracing!");
    CHECK_TRUE(inWidth > 0 && inHeight > 0 && inDepth > 0, "Invalid ray tracing dimensions!");
    auto command = std::make_unique<TraceRaysKHRCommand>();
    command->regions = {
        inTable.GetShaderTableRegion(ShaderTableType::RayGeneration, inRayGenerationIndex),
        inTable.GetShaderTableRegion(ShaderTableType::Miss),
        inTable.GetShaderTableRegion(ShaderTableType::Hit),
        inTable.GetShaderTableRegion(ShaderTableType::Callable) };
    command->width = inWidth;
    command->height = inHeight;
    command->depth = inDepth;
    _AppendStateCommand(std::move(command));
}

void CommandBuffer::BuildAccelerationStructuresKHR(std::span<const VkAccelStructBuildInfo> inBuildInfos)
{
    _RequireOutsideRenderPass();
    ValidateRegions(inBuildInfos);
    BuildAccelerationStructuresKHRCommand::Parameters parameters;
    parameters.buildInfos.reserve(inBuildInfos.size());
    parameters.buildRanges.reserve(inBuildInfos.size());
    for (const auto& build : inBuildInfos)
    {
        CHECK_TRUE(build.buildGeometryInfo.geometryCount == build.geometries.size() &&
            build.geometries.size() == build.buildRangesInfos.size(), "Geometry/range count mismatch!");
        CHECK_TRUE(build.buildGeometryInfo.dstAccelerationStructure != VK_NULL_HANDLE,
            "Invalid destination acceleration structure!");
        auto info = build.buildGeometryInfo;
        // The existing BuildHelper wrapper's owned vectors are the source of truth.
        info.pGeometries = build.geometries.data();
        info.ppGeometries = nullptr;
        parameters.buildInfos.push_back(info);
        parameters.buildRanges.push_back(build.buildRangesInfos);
    }
    auto command = std::make_unique<BuildAccelerationStructuresKHRCommand>();
    command->SetParameters(parameters);
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::_CopyAccelerationStructureKHR(VkAccelerationStructureKHR inSrc,
    VkAccelerationStructureKHR inDst, VkCopyAccelerationStructureModeKHR inMode)
{
    _RequireOutsideRenderPass();
    CHECK_TRUE(inSrc != VK_NULL_HANDLE && inDst != VK_NULL_HANDLE, "Acceleration structure was not created!");
    CHECK_TRUE(inMode == VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR ||
        inMode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR, "Invalid acceleration structure copy mode!");
    auto command = std::make_unique<CopyAccelerationStructureKHRCommand>();
    command->SetParameters({ inSrc, inDst, inMode, nullptr });
    _AppendOwnedCommand(std::move(command));
}

void CommandBuffer::CopyAccelerationStructureKHR(const BottomLevelAccelStruct& inSrc,
    BottomLevelAccelStruct& inDst, VkCopyAccelerationStructureModeKHR inMode)
{
    _CopyAccelerationStructureKHR(inSrc.GetVkAccelerationStructure(), inDst.GetVkAccelerationStructure(), inMode);
}

void CommandBuffer::CopyAccelerationStructureKHR(const TopLevelAccelStruct& inSrc,
    TopLevelAccelStruct& inDst, VkCopyAccelerationStructureModeKHR inMode)
{
    _CopyAccelerationStructureKHR(inSrc.GetVkAccelerationStructure(), inDst.GetVkAccelerationStructure(), inMode);
}

void CommandBuffer::ClearColorImage(Image& inImage, VkImageLayout inLayout,
    float inR, float inG, float inB, float inA,
    uint32_t inBaseMipLevel, uint32_t inLevelCount, uint32_t inBaseArrayLayer, uint32_t inLayerCount)
{
    VkClearColorValue color{};
    color.float32[0] = inR;
    color.float32[1] = inG;
    color.float32[2] = inB;
    color.float32[3] = inA;
    _ClearColorImage(inImage, inLayout, color, inBaseMipLevel, inLevelCount, inBaseArrayLayer, inLayerCount);
}

void CommandBuffer::ClearColorImage(Image& inImage, VkImageLayout inLayout,
    int32_t inR, int32_t inG, int32_t inB, int32_t inA,
    uint32_t inBaseMipLevel, uint32_t inLevelCount, uint32_t inBaseArrayLayer, uint32_t inLayerCount)
{
    VkClearColorValue color{};
    color.int32[0] = inR;
    color.int32[1] = inG;
    color.int32[2] = inB;
    color.int32[3] = inA;
    _ClearColorImage(inImage, inLayout, color, inBaseMipLevel, inLevelCount, inBaseArrayLayer, inLayerCount);
}

void CommandBuffer::ClearColorImage(Image& inImage, VkImageLayout inLayout,
    uint32_t inR, uint32_t inG, uint32_t inB, uint32_t inA,
    uint32_t inBaseMipLevel, uint32_t inLevelCount, uint32_t inBaseArrayLayer, uint32_t inLayerCount)
{
    VkClearColorValue color{};
    color.uint32[0] = inR;
    color.uint32[1] = inG;
    color.uint32[2] = inB;
    color.uint32[3] = inA;
    _ClearColorImage(inImage, inLayout, color, inBaseMipLevel, inLevelCount, inBaseArrayLayer, inLayerCount);
}

