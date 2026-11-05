#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/render_context/private/frame_slot.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/resource/image.h"
#include "my_vulkan/pipeline/render_pass_desc.h"
#include "my_vulkan/pipeline/framebuffer_desc.h"
#include "my_vulkan/allocator/private/render_target_cache.h"
#include "my_vulkan/device.h"
#include <concepts>
#include <cstring>
#include "my_vulkan/resource/private/buffer_storage.h"

struct CommandBufferTestProbe
{
    static void ClearBorrowedBuffer(Buffer& buffer) { buffer.m_storage.reset(); }
    static void SetBorrowedBuffer(Buffer& buffer, VkBuffer handle, void* mapped = nullptr)
    {
        buffer.m_storage = std::make_unique<VulkanDetail::BufferStorage>();
        buffer.m_storage->buffer = handle;
        buffer.m_storage->allocation.m_mappedMemory = mapped;
    }
    template<typename T>
    static auto Handle(uintptr_t inValue) -> T { return reinterpret_cast<T>(inValue); }

    struct Resources
    {
        Buffer src, dst;
        Image image, otherImage;
        RenderPassDesc pass;
        FramebufferDesc framebuffer;
        PipelineLayout layout;
        ShaderBindingTable table;
        GraphicsShaderProgram graphics;
        ComputeShaderProgram compute;
        RayTracingShaderProgram ray;
        BottomLevelAccelStruct bottomSrc, bottomDst;
        TopLevelAccelStruct topSrc, topDst;

        Resources()
        {
            SetBorrowedBuffer(src, Handle<VkBuffer>(101));
            SetBorrowedBuffer(dst, Handle<VkBuffer>(102));
            src.m_bufferInformation.size = dst.m_bufferInformation.size = 1024 * 1024;
            image.m_resource = MakeIntrusive<VulkanDetail::ImageResource>(Handle<VkImage>(201), nullptr);
            otherImage.m_resource = MakeIntrusive<VulkanDetail::ImageResource>(Handle<VkImage>(202), nullptr);
            image.m_imageInformation.mipLevels = otherImage.m_imageInformation.mipLevels = 8;
            image.m_imageInformation.arrayLayers = otherImage.m_imageInformation.arrayLayers = 6;
            VkClearValue clear{};
            clear.color.float32[0] = 0.25f;
            AttachmentDescription attachment;
            attachment.CustomizeFormat(VK_FORMAT_R8G8B8A8_UNORM, clear);
            pass.AddAttachment("color", attachment);
            SubpassDescription subpass;
            subpass.AddColorAttachment(0, "color");
            pass.AddSubpass("first", subpass);
            pass.AddSubpass("second", subpass);
            pass.Finalize();
            framebuffer.m_attachments.emplace("color", FramebufferDesc::Attachment{
                42, Handle<VkImageView>(304), { 4096, 4096, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1 });
            framebuffer.m_extent = VkExtent2D{ 4096, 4096 };
            framebuffer.m_layers = 1;
            layout.m_vkPipelineLayout = Handle<VkPipelineLayout>(303);
            table.m_uptrBuffer = std::make_unique<Buffer>();
            SetBorrowedBuffer(*table.m_uptrBuffer, Handle<VkBuffer>(104));
            table.m_uptrBuffer->m_bufferInformation.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            table.m_tables[0] = { 2, 0, 0, 64 };
            table.m_tables[1] = { 2, 0, 256, 64 };
            table.m_tables[2] = { 1, 0, 512, 64 };
            bottomSrc.m_vkAccelStruct = Handle<VkAccelerationStructureKHR>(401);
            bottomDst.m_vkAccelStruct = Handle<VkAccelerationStructureKHR>(402);
            topSrc.m_vkAccelStruct = Handle<VkAccelerationStructureKHR>(403);
            topDst.m_vkAccelStruct = Handle<VkAccelerationStructureKHR>(404);
        }

        ~Resources()
        {
            ClearBorrowedBuffer(src); ClearBorrowedBuffer(dst);
            layout.m_vkPipelineLayout = VK_NULL_HANDLE;
            ClearBorrowedBuffer(*table.m_uptrBuffer);
            table.m_uptrBuffer.reset();
            bottomSrc.m_vkAccelStruct = bottomDst.m_vkAccelStruct = VK_NULL_HANDLE;
            topSrc.m_vkAccelStruct = topDst.m_vkAccelStruct = VK_NULL_HANDLE;
        }
    };

    struct NativeRenderTargetHooks
    {
        uint32_t renderPassCreates = 0;
        uint32_t framebufferCreates = 0;
        PFN_vkCreateRenderPass createRenderPass = vkCreateRenderPass;
        PFN_vkDestroyRenderPass destroyRenderPass = vkDestroyRenderPass;
        PFN_vkCreateFramebuffer createFramebuffer = vkCreateFramebuffer;
        PFN_vkDestroyFramebuffer destroyFramebuffer = vkDestroyFramebuffer;
        VkDevice device = VK_NULL_HANDLE;
        std::unique_ptr<VulkanDetail::RenderTargetCache> cache;

        NativeRenderTargetHooks()
        {
            auto& instance = MyDevice::GetInstance();
            device = instance.vkDevice;
            cache = std::move(instance.m_uptrRenderTargetCache);
            instance.vkDevice = Handle<VkDevice>(1);
            instance.m_uptrRenderTargetCache = std::make_unique<VulkanDetail::RenderTargetCache>();
            instance.m_uptrRenderTargetCache->Create(instance.vkDevice);
            activeRenderTargetHooks = this;
            vkCreateRenderPass = _CreateRenderPass;
            vkDestroyRenderPass = _DestroyRenderPass;
            vkCreateFramebuffer = _CreateFramebuffer;
            vkDestroyFramebuffer = _DestroyFramebuffer;
        }
        NativeRenderTargetHooks(const NativeRenderTargetHooks&) = delete;
        auto operator=(const NativeRenderTargetHooks&) -> NativeRenderTargetHooks& = delete;
        ~NativeRenderTargetHooks()
        {
            auto& instance = MyDevice::GetInstance();
            instance.m_uptrRenderTargetCache.reset();
            instance.m_uptrRenderTargetCache = std::move(cache);
            instance.vkDevice = device;
            vkCreateRenderPass = createRenderPass;
            vkDestroyRenderPass = destroyRenderPass;
            vkCreateFramebuffer = createFramebuffer;
            vkDestroyFramebuffer = destroyFramebuffer;
            activeRenderTargetHooks = nullptr;
        }
    };
    inline static NativeRenderTargetHooks* activeRenderTargetHooks = nullptr;

    static auto VKAPI_PTR _CreateRenderPass(VkDevice inDevice, const VkRenderPassCreateInfo* inInfo,
        const VkAllocationCallbacks*, VkRenderPass* outRenderPass) -> VkResult
    {
        CHECK_TRUE(inDevice == Handle<VkDevice>(1) && inInfo->attachmentCount == 1 && inInfo->subpassCount == 2,
            "Native preparation must preserve the finalized attachment and subpass descriptions!");
        ++activeRenderTargetHooks->renderPassCreates;
        *outRenderPass = Handle<VkRenderPass>(301);
        return VK_SUCCESS;
    }
    static void VKAPI_PTR _DestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*) {}
    static auto VKAPI_PTR _CreateFramebuffer(VkDevice inDevice, const VkFramebufferCreateInfo* inInfo,
        const VkAllocationCallbacks*, VkFramebuffer* outFramebuffer) -> VkResult
    {
        CHECK_TRUE(inDevice == Handle<VkDevice>(1) && inInfo->renderPass == Handle<VkRenderPass>(301) &&
            inInfo->attachmentCount == 1 && inInfo->pAttachments[0] == Handle<VkImageView>(304) &&
            inInfo->width == 4096 && inInfo->height == 4096 && inInfo->layers == 1,
            "Native preparation must preserve the borrowed framebuffer attachment snapshot!");
        ++activeRenderTargetHooks->framebufferCreates;
        *outFramebuffer = Handle<VkFramebuffer>(302);
        return VK_SUCCESS;
    }
    static void VKAPI_PTR _DestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*) {}

    static auto Count(const CommandBuffer& inBuffer) -> size_t { return inBuffer.m_commands.size(); }
    struct ExternalBuffer
    {
        Buffer buffer;
        ExternalBuffer(uintptr_t inHandle, VkDeviceSize inSize, VkBufferUsageFlags inUsage,
            void* inMapped = nullptr, VkMemoryPropertyFlags inMemory = 0)
        {
            SetBorrowedBuffer(buffer, Handle<VkBuffer>(inHandle), inMapped);
            buffer.m_bufferInformation.size = inSize;
            buffer.m_bufferInformation.usage = inUsage;
            buffer.m_bufferInformation.memoryProperty = inMemory;
        }
        ~ExternalBuffer()
        {
            ClearBorrowedBuffer(buffer);
        }
    };
    static void PrepareBottom(BottomLevelAccelStruct& inResource,
        const BottomLevelAccelStructCreateInfo& inInfo, VkDeviceSize inBuildScratch = 512,
        VkDeviceSize inUpdateScratch = 128)
    {
        inResource.m_layout = BottomLevelAccelStruct::_Describe(inInfo, false);
        inResource.m_flags = inResource.m_layout.buildGeometryInfo.flags;
        inResource.m_buildSizes.accelerationStructureSize = 2048;
        inResource.m_buildSizes.buildScratchSize = inBuildScratch;
        inResource.m_buildSizes.updateScratchSize = inUpdateScratch;
        inResource.m_scratchAlignment = 256;
        inResource.m_accelStructReference = 32768;
    }
    static void PrepareTop(TopLevelAccelStruct& inResource, const TopLevelAccelStructCreateInfo& inInfo)
    {
        inResource.m_layout = TopLevelAccelStruct::_Describe(inInfo);
        inResource.m_flags = inResource.m_layout.buildGeometryInfo.flags;
        inResource.m_buildSizes.accelerationStructureSize = 2048;
        inResource.m_buildSizes.buildScratchSize = 512;
        inResource.m_buildSizes.updateScratchSize = 128;
        inResource.m_scratchAlignment = 256;
    }
    static void SetBlasAddress(BottomLevelAccelStruct& inResource, VkDeviceAddress inAddress)
    {
        inResource.m_accelStructReference = inAddress;
    }
    static void RecordLast(const CommandBuffer& inBuffer)
    {
        inBuffer.m_commands.back()->Record(Handle<VkCommandBuffer>(1));
    }
    static void PrepareRendering(CommandBuffer& inBuffer)
    {
        inBuffer._PrepareRenderPassCommands();
    }
    static void UseGraphics(CommandBuffer& inBuffer, Resources& inResources)
    {
        inBuffer.m_graphicsProgram = &inResources.graphics;
    }
    static void UseCompute(CommandBuffer& inBuffer, Resources& inResources)
    {
        inBuffer.m_computeProgram = &inResources.compute;
    }
    static void UseRay(CommandBuffer& inBuffer, Resources& inResources)
    {
        inBuffer.m_rayTracingProgram = &inResources.ray;
    }
    static void ChangeSbt(Resources& inResources) { inResources.table.m_tables[0].offset = 1024; }
    static void Append(CommandBuffer& inBuffer, CommandBuffer&& inOther) { inBuffer._Append(std::move(inOther)); }
    static void AppendBorrowedCommands(CommandBuffer& inBuffer, const Command* const* inCommands, size_t inCount)
    {
        inBuffer._AppendCommands(inCommands, inCount);
    }
    static auto BatchSizes(const std::vector<CommandBuffer>& inBuffers) -> std::vector<size_t>
    {
        std::vector<size_t> sizes;
        for (const auto& batch : FrameSlot::_BuildRecordingBatches(inBuffers))
            sizes.push_back(batch.commands.size());
        return sizes;
    }
    template<typename T>
    static void Record(const T& inBuffer)
    {
        for (const Command* command : inBuffer.m_commands)
            command->Record(reinterpret_cast<VkCommandBuffer>(uintptr_t{ 1 }));
    }
};

namespace
{
    struct CapturedViewport
    {
        uint32_t index = 0;
        VkViewport viewport{};
        uint32_t calls = 0;
    };
    CapturedViewport viewportCapture;

    void VKAPI_PTR CaptureViewport(VkCommandBuffer, uint32_t inFirst, uint32_t inCount, const VkViewport* inViewports)
    {
        CHECK_TRUE(inCount == 1, "SetViewport must record one viewport!");
        viewportCapture.index = inFirst;
        viewportCapture.viewport = inViewports[0];
        ++viewportCapture.calls;
    }

    template<typename T>
    void TestFlattenedViewportIsDeferred()
    {
        if constexpr (requires(T& buffer) { { buffer.SetViewport(1.f, 2.f, 30.f, 40.f) } -> std::same_as<void>; })
        {
            const auto previous = vkCmdSetViewport;
            vkCmdSetViewport = CaptureViewport;
            try
            {
                T buffer;
                viewportCapture = {};
                buffer.SetViewport(1.f, 2.f, 30.f, -40.f, 0.25f, 0.75f, 3);
                CHECK_TRUE(viewportCapture.calls == 0, "Enqueuing a viewport must not call Vulkan!");
                T moved(std::move(buffer));
                CommandBufferTestProbe::Record(moved);
                CHECK_TRUE(viewportCapture.calls == 1 && viewportCapture.index == 3,
                    "Viewport index must survive moving the command buffer!");
                const auto& viewport = viewportCapture.viewport;
                CHECK_TRUE(viewport.x == 1.f && viewport.y == 2.f && viewport.width == 30.f &&
                    viewport.height == -40.f && viewport.minDepth == 0.25f && viewport.maxDepth == 0.75f,
                    "Viewport fields must preserve the supplied scalar parameters!");
            }
            catch (...)
            {
                vkCmdSetViewport = previous;
                throw;
            }
            vkCmdSetViewport = previous;
        }
        else
            CHECK_TRUE(false, "Missing flattened void CommandBuffer::SetViewport interface!");
    }
}


namespace
{
    template<typename T>
    struct ScopedFunction
    {
        T& target;
        T previous;
        ScopedFunction(T& inTarget, T inReplacement) : target(inTarget), previous(inTarget) { target = inReplacement; }
        ~ScopedFunction() { target = previous; }
        ScopedFunction(const ScopedFunction&) = delete;
        auto operator=(const ScopedFunction&) -> ScopedFunction& = delete;
    };

    struct Capture
    {
        std::vector<std::string> order;
        uint32_t index = 0;
        VkRect2D rect{};
        VkBuffer srcBuffer{}, dstBuffer{};
        VkBufferCopy bufferCopy{};
        std::vector<uint8_t> bytes;
        VkPipelineLayout layout{};
        VkShaderStageFlags stages = 0;
        uint32_t offset = 0;
        std::vector<VkClearColorValue> colors;
        std::vector<VkImageSubresourceRange> ranges;
        VkClearDepthStencilValue depth{};
        std::vector<VkRenderPassBeginInfo> begins;
        std::vector<std::vector<VkClearValue>> beginValues;
        std::vector<VkImageCopy> imageCopies;
        std::vector<VkBufferImageCopy> bufferImageCopies;
        std::vector<VkBufferImageCopy> uploads;
        std::vector<VkImageBlit> blits;
        std::vector<VkImageResolve> resolves;
        VkImage srcImage{}, dstImage{};
        VkImageLayout srcLayout{}, dstLayout{};
        VkFilter filter{};
        std::array<uint32_t, 5> arguments{};
        int32_t vertexOffset = 0;
        std::vector<VkBuffer> vertexBuffers;
        std::vector<VkDeviceSize> vertexOffsets;
        VkIndexType indexType{};
        uint32_t fillValue = 0;
        VkDeviceSize byteOffset = 0, byteSize = 0;
        std::array<VkStridedDeviceAddressRegionKHR, 4> sbt{};
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds;
        std::vector<std::vector<VkAccelerationStructureGeometryKHR>> geometries;
        std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRanges;
        std::vector<VkCopyAccelerationStructureInfoKHR> asCopies;
    };
    Capture capture;

    template<typename F>
    void ExpectRejected(CommandBuffer& inBuffer, F&& inCall)
    {
        const auto before = CommandBufferTestProbe::Count(inBuffer);
        bool threw = false;
        try { inCall(); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK_TRUE(threw, "Invalid command must be rejected!");
        CHECK_TRUE(CommandBufferTestProbe::Count(inBuffer) == before, "Rejected command must not modify the stream!");
    }

    void VKAPI_PTR CaptureScissor(VkCommandBuffer, uint32_t inFirst, uint32_t inCount, const VkRect2D* inRects)
    {
        CHECK_TRUE(inCount == 1, "One scissor expected!");
        capture.order.push_back("scissor");
        capture.index = inFirst;
        capture.rect = inRects[0];
    }
    void VKAPI_PTR CaptureCopyBuffer(VkCommandBuffer, VkBuffer inSrc, VkBuffer inDst,
        uint32_t inCount, const VkBufferCopy* inRegions)
    {
        CHECK_TRUE(inCount == 1, "One copy region expected!");
        capture.order.push_back("copy");
        capture.srcBuffer = inSrc;
        capture.dstBuffer = inDst;
        capture.bufferCopy = inRegions[0];
    }
    void VKAPI_PTR CaptureUpdate(VkCommandBuffer, VkBuffer inDst, VkDeviceSize inOffset,
        VkDeviceSize inSize, const void* inData)
    {
        capture.order.push_back("update");
        capture.dstBuffer = inDst;
        capture.byteOffset = inOffset;
        const auto* bytes = static_cast<const uint8_t*>(inData);
        capture.bytes.assign(bytes, bytes + static_cast<size_t>(inSize));
    }
    void VKAPI_PTR CapturePush(VkCommandBuffer, VkPipelineLayout inLayout, VkShaderStageFlags inStages,
        uint32_t inOffset, uint32_t inSize, const void* inData)
    {
        capture.order.push_back("push");
        capture.layout = inLayout;
        capture.stages = inStages;
        capture.offset = inOffset;
        const auto* bytes = static_cast<const uint8_t*>(inData);
        capture.bytes.assign(bytes, bytes + inSize);
    }
    void VKAPI_PTR CaptureColor(VkCommandBuffer, VkImage inImage, VkImageLayout inLayout,
        const VkClearColorValue* inColor, uint32_t inCount, const VkImageSubresourceRange* inRanges)
    {
        CHECK_TRUE(inCount == 1, "One image clear range expected!");
        capture.order.push_back("color");
        capture.dstImage = inImage;
        capture.dstLayout = inLayout;
        capture.colors.push_back(*inColor);
        capture.ranges.push_back(inRanges[0]);
    }
    void VKAPI_PTR CaptureDepth(VkCommandBuffer, VkImage, VkImageLayout,
        const VkClearDepthStencilValue* inDepth, uint32_t inCount, const VkImageSubresourceRange* inRanges)
    {
        CHECK_TRUE(inCount == 1, "One depth clear range expected!");
        capture.order.push_back("depth");
        capture.depth = *inDepth;
        capture.ranges.push_back(inRanges[0]);
    }
    void VKAPI_PTR CaptureBegin(VkCommandBuffer, const VkRenderPassBeginInfo* inInfo, VkSubpassContents inContents)
    {
        CHECK_TRUE(inContents == VK_SUBPASS_CONTENTS_INLINE, "Inline render pass expected!");
        capture.order.push_back("begin");
        capture.begins.push_back(*inInfo);
        capture.beginValues.emplace_back(inInfo->pClearValues, inInfo->pClearValues + inInfo->clearValueCount);
    }
    void VKAPI_PTR CaptureEnd(VkCommandBuffer) { capture.order.push_back("end"); }
    void VKAPI_PTR CaptureNext(VkCommandBuffer, VkSubpassContents) { capture.order.push_back("next"); }
    void VKAPI_PTR CaptureVertex(VkCommandBuffer, uint32_t inFirst, uint32_t inCount,
        const VkBuffer* inBuffers, const VkDeviceSize* inOffsets)
    {
        capture.order.push_back("vertex");
        capture.index = inFirst;
        capture.vertexBuffers.assign(inBuffers, inBuffers + inCount);
        capture.vertexOffsets.assign(inOffsets, inOffsets + inCount);
    }
    void VKAPI_PTR CaptureIndex(VkCommandBuffer, VkBuffer inBuffer, VkDeviceSize inOffset, VkIndexType inType)
    {
        capture.order.push_back("index");
        capture.srcBuffer = inBuffer;
        capture.byteOffset = inOffset;
        capture.indexType = inType;
    }
    void VKAPI_PTR CaptureDraw(VkCommandBuffer, uint32_t inCount, uint32_t inInstances, uint32_t inFirst, uint32_t inInstance)
    {
        capture.order.push_back("draw");
        capture.arguments = { inCount, inInstances, inFirst, inInstance, 0 };
    }
    void VKAPI_PTR CaptureDrawIndexed(VkCommandBuffer, uint32_t inCount, uint32_t inInstances, uint32_t inFirst,
        int32_t inVertexOffset, uint32_t inInstance)
    {
        capture.order.push_back("indexed");
        capture.arguments = { inCount, inInstances, inFirst, inInstance, 0 };
        capture.vertexOffset = inVertexOffset;
    }
    void VKAPI_PTR CaptureDrawIndirect(VkCommandBuffer, VkBuffer inBuffer, VkDeviceSize inOffset,
        uint32_t inCount, uint32_t inStride)
    {
        capture.order.push_back("indirect");
        capture.srcBuffer = inBuffer;
        capture.byteOffset = inOffset;
        capture.arguments = { inCount, inStride, 0, 0, 0 };
    }
    void VKAPI_PTR CaptureDrawIndexedIndirect(VkCommandBuffer b, VkBuffer inBuffer, VkDeviceSize inOffset,
        uint32_t inCount, uint32_t inStride)
    {
        CaptureDrawIndirect(b, inBuffer, inOffset, inCount, inStride);
        capture.order.back() = "indexedIndirect";
    }
    void VKAPI_PTR CaptureDispatch(VkCommandBuffer, uint32_t inX, uint32_t inY, uint32_t inZ)
    {
        capture.order.push_back("dispatch");
        capture.arguments = { inX, inY, inZ, 0, 0 };
    }
    void VKAPI_PTR CaptureDispatchIndirect(VkCommandBuffer, VkBuffer inBuffer, VkDeviceSize inOffset)
    {
        capture.order.push_back("dispatchIndirect");
        capture.srcBuffer = inBuffer;
        capture.byteOffset = inOffset;
    }
    void VKAPI_PTR CaptureFill(VkCommandBuffer, VkBuffer inBuffer, VkDeviceSize inOffset, VkDeviceSize inSize, uint32_t inValue)
    {
        capture.order.push_back("fill");
        capture.dstBuffer = inBuffer;
        capture.byteOffset = inOffset;
        capture.byteSize = inSize;
        capture.fillValue = inValue;
    }
    void VKAPI_PTR CaptureImageCopy(VkCommandBuffer, VkImage inSrc, VkImageLayout inSrcLayout,
        VkImage inDst, VkImageLayout inDstLayout, uint32_t inCount, const VkImageCopy* inRegions)
    {
        capture.order.push_back("imageCopy");
        capture.srcImage = inSrc; capture.dstImage = inDst;
        capture.srcLayout = inSrcLayout; capture.dstLayout = inDstLayout;
        capture.imageCopies.assign(inRegions, inRegions + inCount);
    }
    void VKAPI_PTR CaptureBufferToImage(VkCommandBuffer, VkBuffer inSrc, VkImage inDst,
        VkImageLayout inLayout, uint32_t inCount, const VkBufferImageCopy* inRegions)
    {
        capture.order.push_back("bufferToImage");
        capture.srcBuffer = inSrc; capture.dstImage = inDst; capture.dstLayout = inLayout;
        capture.uploads.assign(inRegions, inRegions + inCount);
    }
    void VKAPI_PTR CaptureImageToBuffer(VkCommandBuffer, VkImage inSrc, VkImageLayout inLayout,
        VkBuffer inDst, uint32_t inCount, const VkBufferImageCopy* inRegions)
    {
        capture.order.push_back("imageToBuffer");
        capture.srcImage = inSrc; capture.dstBuffer = inDst; capture.srcLayout = inLayout;
        capture.bufferImageCopies.assign(inRegions, inRegions + inCount);
    }
    void VKAPI_PTR CaptureBlit(VkCommandBuffer, VkImage inSrc, VkImageLayout inSrcLayout,
        VkImage inDst, VkImageLayout inDstLayout, uint32_t inCount, const VkImageBlit* inRegions, VkFilter inFilter)
    {
        capture.order.push_back("blit");
        capture.srcImage = inSrc; capture.dstImage = inDst;
        capture.srcLayout = inSrcLayout; capture.dstLayout = inDstLayout;
        capture.blits.assign(inRegions, inRegions + inCount);
        capture.filter = inFilter;
    }
    void VKAPI_PTR CaptureResolve(VkCommandBuffer, VkImage inSrc, VkImageLayout inSrcLayout,
        VkImage inDst, VkImageLayout inDstLayout, uint32_t inCount, const VkImageResolve* inRegions)
    {
        capture.order.push_back("resolve");
        capture.srcImage = inSrc; capture.dstImage = inDst;
        capture.srcLayout = inSrcLayout; capture.dstLayout = inDstLayout;
        capture.resolves.assign(inRegions, inRegions + inCount);
    }
    auto VKAPI_PTR CaptureAddress(VkDevice, const VkBufferDeviceAddressInfo*) -> VkDeviceAddress { return 4096; }
    void VKAPI_PTR CaptureTrace(VkCommandBuffer, const VkStridedDeviceAddressRegionKHR* inRaygen,
        const VkStridedDeviceAddressRegionKHR* inMiss, const VkStridedDeviceAddressRegionKHR* inHit,
        const VkStridedDeviceAddressRegionKHR* inCallable, uint32_t inWidth, uint32_t inHeight, uint32_t inDepth)
    {
        capture.order.push_back("trace");
        capture.sbt = { *inRaygen, *inMiss, *inHit, *inCallable };
        capture.arguments = { inWidth, inHeight, inDepth, 0, 0 };
    }
    void VKAPI_PTR CaptureBuild(VkCommandBuffer, uint32_t inCount,
        const VkAccelerationStructureBuildGeometryInfoKHR* inInfos,
        const VkAccelerationStructureBuildRangeInfoKHR* const* inRanges)
    {
        capture.order.push_back("build");
        capture.builds.insert(capture.builds.end(), inInfos, inInfos + inCount);
        for (uint32_t i = 0; i < inCount; ++i)
        {
            capture.geometries.emplace_back(inInfos[i].pGeometries, inInfos[i].pGeometries + inInfos[i].geometryCount);
            capture.buildRanges.emplace_back(inRanges[i], inRanges[i] + inInfos[i].geometryCount);
        }
    }
    void VKAPI_PTR CaptureAsCopy(VkCommandBuffer, const VkCopyAccelerationStructureInfoKHR* inInfo)
    {
        capture.order.push_back("asCopy");
        capture.asCopies.push_back(*inInfo);
    }
}


namespace
{
    void TestScalarRegionsAndOwnedBytes()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction scissor(vkCmdSetScissor, &CaptureScissor);
        ScopedFunction copy(vkCmdCopyBuffer, &CaptureCopyBuffer);
        ScopedFunction update(vkCmdUpdateBuffer, &CaptureUpdate);
        capture = {};
        CommandBuffer buffer;
        buffer.SetScissor(7, 9, 320, 240, 2);
        buffer.CopyBuffer(resources.src, resources.dst, 12, 36, 64);
        std::array<uint8_t, 8> data{ 1, 2, 3, 4, 5, 6, 7, 8 };
        buffer.UpdateBuffer(resources.dst, 20, data.size(), data.data());
        data.fill(0);
        CHECK_TRUE(capture.order.empty(), "Commands must be deferred!");
        CommandBuffer moved;
        CommandBufferTestProbe::Append(moved, std::move(buffer));
        CHECK_TRUE(CommandBufferTestProbe::Count(buffer) == 0, "Append must consume the source stream!");
        CommandBufferTestProbe::Record(moved);
        CHECK_TRUE(capture.order == std::vector<std::string>({ "scissor", "copy", "update" }), "Command order changed!");
        CHECK_TRUE(capture.index == 2 && capture.rect.offset.x == 7 && capture.rect.offset.y == 9 &&
            capture.rect.extent.width == 320 && capture.rect.extent.height == 240, "Scissor mapping is wrong!");
        CHECK_TRUE(capture.srcBuffer == CommandBufferTestProbe::Handle<VkBuffer>(101) &&
            capture.dstBuffer == CommandBufferTestProbe::Handle<VkBuffer>(102) &&
            capture.bufferCopy.srcOffset == 12 && capture.bufferCopy.dstOffset == 36 &&
            capture.bufferCopy.size == 64, "CopyBuffer scalar mapping is wrong!");
        CHECK_TRUE(capture.byteOffset == 20 && capture.bytes == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6, 7, 8 }),
            "UpdateBuffer must own its input bytes!");
    }

    void TestPushConstantSnapshotAndValidation()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction push(vkCmdPushConstants, &CapturePush);
        capture = {};
        CommandBuffer buffer;
        std::array<uint8_t, 4> data{ 9, 8, 7, 6 };
        buffer.PushConstants(resources.layout, VK_SHADER_STAGE_COMPUTE_BIT, 12, 4, data.data());
        data.fill(0);
        ExpectRejected(buffer, [&] { buffer.PushConstants(resources.layout, VK_SHADER_STAGE_COMPUTE_BIT, 1, 4, data.data()); });
        ExpectRejected(buffer, [&] { buffer.PushConstants(resources.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 3, data.data()); });
        ExpectRejected(buffer, [&] { buffer.PushConstants(resources.layout, 0, 0, 4, data.data()); });
        ExpectRejected(buffer, [&] { buffer.PushConstants(resources.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, nullptr); });
        CommandBufferTestProbe::Record(buffer);
        CHECK_TRUE(capture.layout == CommandBufferTestProbe::Handle<VkPipelineLayout>(303) &&
            capture.stages == VK_SHADER_STAGE_COMPUTE_BIT && capture.offset == 12 &&
            capture.bytes == std::vector<uint8_t>({ 9, 8, 7, 6 }), "PushConstants must snapshot bytes and layout!");
    }

    void TestImageClearTypesAndRanges()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction color(vkCmdClearColorImage, &CaptureColor);
        ScopedFunction depth(vkCmdClearDepthStencilImage, &CaptureDepth);
        capture = {};
        CommandBuffer buffer;
        buffer.ClearColorImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, 0.25f, 0.5f, 0.75f, 1.f);
        buffer.ClearColorImage(resources.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            int32_t{-1}, int32_t{-2}, int32_t{3}, int32_t{4}, 1, 2, 3, 2);
        buffer.ClearColorImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, 1u, 2u, 0xFFFFFFFFu, 4u);
        buffer.ClearDepthStencilImage(resources.otherImage, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0.125f, 29, 2, 3, 1, 4);
        ExpectRejected(buffer, [&] { buffer.ClearColorImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, 0.f, 0.f, 0.f, 1.f, 8); });
        ExpectRejected(buffer, [&] { buffer.ClearDepthStencilImage(resources.otherImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT); });
        CommandBufferTestProbe::Record(buffer);
        CHECK_TRUE(capture.colors.size() == 3 && capture.colors[0].float32[0] == 0.25f &&
            capture.colors[0].float32[1] == 0.5f && capture.colors[0].float32[2] == 0.75f &&
            capture.colors[0].float32[3] == 1.f && capture.colors[1].int32[0] == -1 &&
            capture.colors[1].int32[1] == -2 && capture.colors[1].int32[2] == 3 && capture.colors[1].int32[3] == 4 &&
            capture.colors[2].uint32[0] == 1 && capture.colors[2].uint32[1] == 2 &&
            capture.colors[2].uint32[2] == 0xFFFFFFFFu && capture.colors[2].uint32[3] == 4,
            "Color clear overload selected the wrong representation!");
        CHECK_TRUE(capture.ranges[0].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
            capture.ranges[0].baseMipLevel == 0 && capture.ranges[0].levelCount == VK_REMAINING_MIP_LEVELS &&
            capture.ranges[0].baseArrayLayer == 0 && capture.ranges[0].layerCount == VK_REMAINING_ARRAY_LAYERS,
            "Default image clear range is wrong!");
        CHECK_TRUE(capture.ranges[1].baseMipLevel == 1 && capture.ranges[1].levelCount == 2 &&
            capture.ranges[1].baseArrayLayer == 3 && capture.ranges[1].layerCount == 2, "Explicit color range is wrong!");
        CHECK_TRUE(capture.depth.depth == 0.125f && capture.depth.stencil == 29 &&
            capture.ranges[3].baseMipLevel == 2 && capture.ranges[3].levelCount == 3 &&
            capture.ranges[3].baseArrayLayer == 1 && capture.ranges[3].layerCount == 4 &&
            capture.ranges[3].aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
            "Depth/stencil clear mapping is wrong!");
        buffer.ClearDepthStencilImage(resources.otherImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_DEPTH_BIT);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.depth.depth == 1.f && capture.depth.stencil == 0 &&
            capture.ranges.back().levelCount == VK_REMAINING_MIP_LEVELS &&
            capture.ranges.back().layerCount == VK_REMAINING_ARRAY_LAYERS,
            "Depth/stencil clear defaults are wrong!");
    }

    void TestRenderPassScalars()
    {
        CommandBufferTestProbe::NativeRenderTargetHooks native;
        ScopedFunction begin(vkCmdBeginRenderPass, &CaptureBegin);
        ScopedFunction next(vkCmdNextSubpass, &CaptureNext);
        ScopedFunction end(vkCmdEndRenderPass, &CaptureEnd);
        capture = {};
        CommandBuffer buffer;
        {
            CommandBufferTestProbe::Resources resources;
            buffer.BeginRenderPass(resources.pass, resources.framebuffer, 3, 4, 100, 200);
            ExpectRejected(buffer, [&] { buffer.BeginRenderPass(resources.pass, resources.framebuffer, 0, 0, 1, 1); });
            buffer.NextSubpass();
            ExpectRejected(buffer, [&] { buffer.NextSubpass(); });
            buffer.EndRenderPass();
            std::array<VkClearValue, 1> overrides{};
            overrides[0].depthStencil = { 0.125f, 23 };
            buffer.BeginRenderPass(resources.pass, resources.framebuffer, 8, 9, 20, 30, overrides);
            overrides[0].depthStencil = {};
            resources.framebuffer.Reset();
            buffer.EndRenderPass();
        }
        CHECK_TRUE(native.renderPassCreates == 0 && native.framebufferCreates == 0 && capture.order.empty(),
            "Finalization and render-pass enqueue must make no native rendering calls!");
        CommandBuffer moved(std::move(buffer));
        CommandBuffer appended;
        CommandBufferTestProbe::Append(appended, std::move(moved));
        CHECK_TRUE(native.renderPassCreates == 0 && native.framebufferCreates == 0,
            "Moving and appending render-pass descriptions must not create native resources!");
        CommandBufferTestProbe::PrepareRendering(appended);
        CHECK_TRUE(native.renderPassCreates == 1 && native.framebufferCreates == 1 && capture.order.empty(),
            "Preparation must create and cache native rendering resources without recording commands!");
        CommandBufferTestProbe::PrepareRendering(appended);
        CHECK_TRUE(native.renderPassCreates == 1 && native.framebufferCreates == 1,
            "Repeated preparation must reuse the native rendering cache!");
        CommandBufferTestProbe::Record(appended);
        CHECK_TRUE(capture.begins.size() == 2 && capture.begins[0].renderPass == CommandBufferTestProbe::Handle<VkRenderPass>(301) &&
            capture.begins[0].framebuffer == CommandBufferTestProbe::Handle<VkFramebuffer>(302) &&
            capture.begins[0].renderArea.offset.x == 3 && capture.begins[0].renderArea.offset.y == 4 &&
            capture.begins[0].renderArea.extent.width == 100 && capture.begins[0].renderArea.extent.height == 200,
            "Prepared render-pass handles and rectangle mapping are wrong!");
        CHECK_TRUE(capture.beginValues[0][0].color.float32[0] == 0.25f &&
            capture.beginValues[1][0].depthStencil.depth == 0.125f &&
            capture.beginValues[1][0].depthStencil.stencil == 23, "RenderPass clear defaults/overrides must be copied!");
        CHECK_TRUE(capture.order == std::vector<std::string>({ "begin", "next", "end", "begin", "end" }),
            "RenderPass command ordering changed!");
        CHECK_TRUE(native.renderPassCreates == 1 && native.framebufferCreates == 1,
            "Recording prepared rendering commands must perform no native creation!");
    }

    void TestDrawDispatchAndBindings()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction vertex(vkCmdBindVertexBuffers, &CaptureVertex);
        ScopedFunction index(vkCmdBindIndexBuffer, &CaptureIndex);
        ScopedFunction draw(vkCmdDraw, &CaptureDraw);
        ScopedFunction indexed(vkCmdDrawIndexed, &CaptureDrawIndexed);
        ScopedFunction indirect(vkCmdDrawIndirect, &CaptureDrawIndirect);
        ScopedFunction indexedIndirect(vkCmdDrawIndexedIndirect, &CaptureDrawIndexedIndirect);
        ScopedFunction dispatch(vkCmdDispatch, &CaptureDispatch);
        ScopedFunction dispatchIndirect(vkCmdDispatchIndirect, &CaptureDispatchIndirect);
        capture = {};
        CommandBuffer buffer;
        std::array<const Buffer*, 2> inputs{ &resources.src, &resources.dst };
        std::array<VkDeviceSize, 2> offsets{ 16, 32 };
        buffer.BindVertexBuffers(3, inputs, offsets);
        inputs.fill(nullptr);
        offsets.fill(0);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.index == 3 && capture.vertexBuffers == std::vector<VkBuffer>({
            CommandBufferTestProbe::Handle<VkBuffer>(101), CommandBufferTestProbe::Handle<VkBuffer>(102) }) &&
            capture.vertexOffsets == std::vector<VkDeviceSize>({ 16, 32 }), "Vertex bindings must own input arrays!");
        buffer.BindIndexBuffer(resources.src, 24, VK_INDEX_TYPE_UINT16);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.byteOffset == 24 && capture.indexType == VK_INDEX_TYPE_UINT16, "Index binding mapping is wrong!");
        ExpectRejected(buffer, [&] { buffer.Draw(3); });
        ExpectRejected(buffer, [&] { buffer.Dispatch(1); });
        buffer.BeginRenderPass(resources.pass, resources.framebuffer, 0, 0, 64, 64);
        ExpectRejected(buffer, [&] { buffer.DrawIndexed(3); });
        CommandBufferTestProbe::UseGraphics(buffer, resources);
        buffer.Draw(9, 2, 3, 4);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments == std::to_array<uint32_t>({ 9, 2, 3, 4, 0 }), "Draw fields are wrong!");
        buffer.Draw(3);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments == std::to_array<uint32_t>({ 3, 1, 0, 0, 0 }), "Draw defaults are wrong!");
        buffer.DrawIndexed(11, 3, 4, -5, 6);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments == std::to_array<uint32_t>({ 11, 3, 4, 6, 0 }) &&
            capture.vertexOffset == -5, "DrawIndexed signed vertex offset is wrong!");
        buffer.DrawIndirect(resources.src, 16, 2, 32);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments[0] == 2 && capture.arguments[1] == 32 && capture.byteOffset == 16,
            "DrawIndirect fields are wrong!");
        buffer.DrawIndexedIndirect(resources.src, 20);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.order.back() == "indexedIndirect" && capture.arguments[0] == 1 &&
            capture.arguments[1] == 20 && capture.byteOffset == 20, "DrawIndexedIndirect defaults are wrong!");
        ExpectRejected(buffer, [&] { buffer.DrawIndirect(resources.src, 2); });
        ExpectRejected(buffer, [&] { buffer.Dispatch(1); });
        ExpectRejected(buffer, [&] { buffer.CopyBuffer(resources.src, resources.dst, 0, 0, 4); });
        buffer.NextSubpass();
        ExpectRejected(buffer, [&] { buffer.Draw(3); });
        buffer.EndRenderPass();
        CommandBufferTestProbe::UseCompute(buffer, resources);
        buffer.Dispatch(7);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments == std::to_array<uint32_t>({ 7, 1, 1, 0, 0 }), "Dispatch defaults are wrong!");
        buffer.Dispatch(2, 3, 4);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.arguments == std::to_array<uint32_t>({ 2, 3, 4, 0, 0 }), "Explicit dispatch dimensions are wrong!");
        buffer.DispatchIndirect(resources.src, 40);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.order.back() == "dispatchIndirect" && capture.byteOffset == 40,
            "DispatchIndirect fields are wrong!");
    }

    void TestImageTransferSnapshotsAndFill()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction copy(vkCmdCopyImage, &CaptureImageCopy);
        ScopedFunction toImage(vkCmdCopyBufferToImage, &CaptureBufferToImage);
        ScopedFunction toBuffer(vkCmdCopyImageToBuffer, &CaptureImageToBuffer);
        ScopedFunction blit(vkCmdBlitImage, &CaptureBlit);
        ScopedFunction resolve(vkCmdResolveImage, &CaptureResolve);
        ScopedFunction fill(vkCmdFillBuffer, &CaptureFill);
        capture = {};
        CommandBuffer buffer;
        std::array<VkImageCopy, 1> regions{};
        regions[0].srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 2, 1, 3 };
        regions[0].dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 1, 2, 3 };
        regions[0].srcOffset = { 4, 5, 6 };
        regions[0].dstOffset = { 7, 8, 9 };
        regions[0].extent = { 10, 11, 12 };
        buffer.CopyImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, resources.otherImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions);
        regions[0] = {};
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.srcImage == CommandBufferTestProbe::Handle<VkImage>(201) &&
            capture.dstImage == CommandBufferTestProbe::Handle<VkImage>(202) &&
            capture.srcLayout == VK_IMAGE_LAYOUT_GENERAL && capture.dstLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            capture.imageCopies[0].srcSubresource.mipLevel == 2 && capture.imageCopies[0].dstSubresource.baseArrayLayer == 2 &&
            capture.imageCopies[0].srcOffset.z == 6 && capture.imageCopies[0].dstOffset.x == 7 &&
            capture.imageCopies[0].extent.height == 11, "CopyImage must own region arrays!");
        std::array<VkBufferImageCopy, 1> bufferRegions{};
        bufferRegions[0].bufferOffset = 44;
        bufferRegions[0].imageExtent = { 3, 4, 5 };
        buffer.CopyBufferToImage(resources.src, resources.otherImage, VK_IMAGE_LAYOUT_GENERAL, bufferRegions);
        buffer.CopyImageToBuffer(resources.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resources.dst, bufferRegions);
        bufferRegions[0] = {};
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.srcImage == CommandBufferTestProbe::Handle<VkImage>(201) &&
            capture.dstBuffer == CommandBufferTestProbe::Handle<VkBuffer>(102) &&
            capture.bufferImageCopies[0].bufferOffset == 44 && capture.bufferImageCopies[0].imageExtent.depth == 5,
            "CopyImageToBuffer must own region arrays!");
        std::array<VkImageBlit, 1> blits{};
        blits[0].srcOffsets[1] = { 9, 8, 7 };
        buffer.BlitImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, resources.otherImage,
            VK_IMAGE_LAYOUT_GENERAL, blits, VK_FILTER_NEAREST);
        blits[0] = {};
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.blits[0].srcOffsets[1].z == 7 && capture.filter == VK_FILTER_NEAREST, "Blit region/filter changed!");
        std::array<VkImageResolve, 1> resolves{};
        resolves[0].extent = { 8, 6, 1 };
        buffer.ResolveImage(resources.image, VK_IMAGE_LAYOUT_GENERAL, resources.otherImage, VK_IMAGE_LAYOUT_GENERAL, resolves);
        resolves[0] = {};
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.resolves[0].extent.width == 8 && capture.resolves[0].extent.height == 6, "Resolve region changed!");
        buffer.FillBuffer(resources.dst, 8, VK_WHOLE_SIZE, 0x12345678);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.byteOffset == 8 && capture.byteSize == VK_WHOLE_SIZE &&
            capture.fillValue == 0x12345678, "FillBuffer must preserve VK_WHOLE_SIZE and the data word!");
        capture = {};
        CommandBufferTestProbe::Record(buffer);
        CHECK_TRUE(capture.order == std::vector<std::string>({
            "imageCopy", "bufferToImage", "imageToBuffer", "blit", "resolve", "fill" }) &&
            capture.uploads.size() == 1 && capture.uploads[0].bufferOffset == 44 &&
            capture.uploads[0].imageExtent.width == 3 && capture.uploads[0].imageExtent.height == 4 &&
            capture.uploads[0].imageExtent.depth == 5, "CopyBufferToImage must snapshot its regions and preserve ordering!");
        ExpectRejected(buffer, [&] { buffer.CopyImage(resources.image, VK_IMAGE_LAYOUT_GENERAL,
            resources.otherImage, VK_IMAGE_LAYOUT_GENERAL, {}); });
        ExpectRejected(buffer, [&] { buffer.CopyBuffer(resources.src, resources.dst, 1024 * 1024, 0, 1); });
        ExpectRejected(buffer, [&] { buffer.FillBuffer(resources.dst, 1, 4, 0); });
        ExpectRejected(buffer, [&] { buffer.UpdateBuffer(resources.dst, 0, 65540, "data"); });
    }
}


namespace
{
    template<typename Info, typename Tlas>
    void TestEmptyInstanceUploadNeedsNoDeviceResources()
    {
        if constexpr (requires(Info& info, Buffer& destination) {
            info.SetInstances({});
            Tlas::WriteInstances(info, destination, VkDeviceSize{ 0 });
        })
        {
            Info info;
            info.SetInstances({});
            Buffer destination;
            Tlas::WriteInstances(info, destination, 0);
        }
        else
            CHECK_TRUE(false, "Missing CPU instance description and static host upload interface!");
    }

    constexpr VkBufferUsageFlags asInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VkBufferUsageFlags asScratchUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    uint32_t asAddressQueries = 0;
    uint32_t asSizeQueries = 0;
    uint32_t asPropertyQueries = 0;
    VkAccelerationStructureBuildGeometryInfoKHR queriedBuild{};
    std::vector<VkAccelerationStructureGeometryKHR> queriedGeometries;
    std::vector<uint32_t> queriedPrimitiveCounts;

    auto VKAPI_PTR CaptureAsAddress(VkDevice, const VkBufferDeviceAddressInfo* inInfo) -> VkDeviceAddress
    {
        ++asAddressQueries;
        if (inInfo->buffer == CommandBufferTestProbe::Handle<VkBuffer>(102)) return 8192;
        if (inInfo->buffer == CommandBufferTestProbe::Handle<VkBuffer>(103)) return 12288;
        if (inInfo->buffer == CommandBufferTestProbe::Handle<VkBuffer>(104)) return 16384;
        return 4096;
    }
    void VKAPI_PTR CaptureAsProperties(VkPhysicalDevice, VkPhysicalDeviceProperties2* inProperties)
    {
        ++asPropertyQueries;
        auto* properties = static_cast<VkPhysicalDeviceAccelerationStructurePropertiesKHR*>(inProperties->pNext);
        CHECK_TRUE(properties && properties->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
            "AS creation must query acceleration structure properties!");
        properties->maxGeometryCount = 16;
        properties->maxPrimitiveCount = 1000000;
        properties->maxInstanceCount = 1024;
        properties->minAccelerationStructureScratchOffsetAlignment = 256;
    }
    void VKAPI_PTR CaptureAsFormat(VkPhysicalDevice, VkFormat, VkFormatProperties* outProperties)
    {
        *outProperties = {};
        outProperties->bufferFeatures = VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR;
    }
    struct SizeQueryStop : std::runtime_error { SizeQueryStop() : std::runtime_error("Stop before GPU allocation") {} };
    void VKAPI_PTR CaptureAsSizes(VkDevice, VkAccelerationStructureBuildTypeKHR inType,
        const VkAccelerationStructureBuildGeometryInfoKHR* inInfo, const uint32_t* inCounts,
        VkAccelerationStructureBuildSizesInfoKHR*)
    {
        ++asSizeQueries;
        CHECK_TRUE(inType == VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, "Only device builds were requested!");
        queriedBuild = *inInfo;
        queriedGeometries.assign(inInfo->pGeometries, inInfo->pGeometries + inInfo->geometryCount);
        queriedPrimitiveCounts.assign(inCounts, inCounts + inInfo->geometryCount);
        throw SizeQueryStop{};
    }

    void TestHostDescriptionSnapshotAndSizeQuery()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction properties(vkGetPhysicalDeviceProperties2, &CaptureAsProperties);
        ScopedFunction formats(vkGetPhysicalDeviceFormatProperties, &CaptureAsFormat);
        ScopedFunction sizes(vkGetAccelerationStructureBuildSizesKHR, &CaptureAsSizes);
        ScopedFunction addresses(vkGetBufferDeviceAddress, &CaptureAsAddress);
        asAddressQueries = asSizeQueries = asPropertyQueries = 0;
        std::array<AccelStructTriangleGeometry, 1> geometries{};
        geometries[0].vertexBuffer = &resources.src;
        geometries[0].vertexStride = 16;
        geometries[0].vertexCount = 6;
        geometries[0].primitiveCount = 2;
        geometries[0].vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        geometries[0].transformBuffer = &resources.src;
        BottomLevelAccelStructCreateInfo info;
        info.SetTriangles(geometries);
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        geometries[0].vertexFormat = VK_FORMAT_UNDEFINED;
        geometries[0].primitiveCount = 999;
        CHECK_TRUE(asAddressQueries == 0 && asSizeQueries == 0 && asPropertyQueries == 0,
            "CreateInfo setters must only copy CPU descriptions!");
        bool reachedQuery = false;
        try { resources.bottomDst.Create(info); }
        catch (const SizeQueryStop&) { reachedQuery = true; }
        CHECK_TRUE(reachedQuery && asSizeQueries == 1 && asAddressQueries == 0,
            "Host Create must query sizes without resolving Buffer addresses!");
        CHECK_TRUE(queriedBuild.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
            queriedBuild.flags == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR &&
            queriedPrimitiveCounts == std::vector<uint32_t>({ 2 }) &&
            queriedGeometries[0].flags == VK_GEOMETRY_OPAQUE_BIT_KHR &&
            queriedGeometries[0].geometry.triangles.vertexFormat == VK_FORMAT_R16G16B16A16_SFLOAT &&
            queriedGeometries[0].geometry.triangles.maxVertex == 5 &&
            queriedGeometries[0].geometry.triangles.vertexStride == 16 &&
            queriedGeometries[0].geometry.triangles.indexType == VK_INDEX_TYPE_NONE_KHR &&
            queriedGeometries[0].geometry.triangles.transformData.deviceAddress != 0,
            "Size query must preserve the copied format/count/flags and transform presence!");
        CHECK_TRUE(resources.bottomDst.GetVkAccelerationStructure() ==
            CommandBufferTestProbe::Handle<VkAccelerationStructureKHR>(402),
            "A failed host query must preserve an existing AS!");
        info.Reset();
        CommandBuffer commands;
        ExpectRejected(commands, [&] { resources.bottomDst.Create(info); });

        std::array<AccelStructInstance, 2> instances{};
        TopLevelAccelStructCreateInfo topInfo;
        topInfo.SetInstances(instances);
        instances[0].instanceCustomIndex = 0xFFFFFFFF;
        reachedQuery = false;
        try { resources.topDst.Create(topInfo); }
        catch (const SizeQueryStop&) { reachedQuery = true; }
        CHECK_TRUE(reachedQuery && queriedBuild.type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
            queriedBuild.flags == 0 && queriedPrimitiveCounts == std::vector<uint32_t>({ 2 }) &&
            queriedGeometries[0].geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR &&
            topInfo.GetInstanceBufferSize() == 128, "TLAS host query must use the copied instance count!");
    }

    void TestObjectBlasCommandsUseExternalBuffersAndSnapshots()
    {
        CommandBufferTestProbe::Resources resources;
        CommandBufferTestProbe::ExternalBuffer vertices(101, 4096, asInputUsage);
        CommandBufferTestProbe::ExternalBuffer scratch(102, 4096, asScratchUsage);
        CommandBufferTestProbe::ExternalBuffer indices(103, 4096, asInputUsage);
        CommandBufferTestProbe::ExternalBuffer transform(104, 4096, asInputUsage);
        ScopedFunction address(vkGetBufferDeviceAddress, &CaptureAsAddress);
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        ScopedFunction sizeQueries(vkGetAccelerationStructureBuildSizesKHR, &CaptureAsSizes);
        ScopedFunction properties(vkGetPhysicalDeviceProperties2, &CaptureAsProperties);
        asAddressQueries = asSizeQueries = asPropertyQueries = 0;
        AccelStructTriangleGeometry geometry;
        geometry.vertexBuffer = &vertices.buffer;
        geometry.vertexOffset = 64;
        geometry.vertexStride = 16;
        geometry.vertexCount = 7;
        geometry.primitiveCount = 2;
        geometry.vertexFormat = VK_FORMAT_R32G32_SFLOAT;
        geometry.indexBuffer = &indices.buffer;
        geometry.indexOffset = 12;
        geometry.indexType = VK_INDEX_TYPE_UINT16;
        geometry.transformBuffer = &transform.buffer;
        geometry.transformOffset = 16;
        BottomLevelAccelStructCreateInfo info;
        info.SetTriangles(std::span(&geometry, 1));
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        CommandBufferTestProbe::PrepareBottom(resources.bottomSrc, info);
        CommandBufferTestProbe::PrepareBottom(resources.bottomDst, info);
        capture = {};
        CommandBuffer commands;
        commands.BuildAccelerationStructureKHR(resources.bottomDst, info, scratch.buffer, 256);
        commands.UpdateAccelerationStructureKHR(resources.bottomSrc, resources.bottomDst, info, scratch.buffer, 512);
        commands.UpdateAccelerationStructureKHR(resources.bottomDst, resources.bottomDst, info, scratch.buffer);
        CHECK_TRUE(capture.order.empty(), "Object AS commands must be deferred!");
        info.Reset();
        geometry.vertexOffset = 0;
        CommandBuffer moved(std::move(commands));
        CommandBuffer appended;
        CommandBufferTestProbe::Append(appended, std::move(moved));
        const auto addressCount = asAddressQueries;
        CommandBufferTestProbe::Record(appended);
        CHECK_TRUE(capture.order == std::vector<std::string>({ "build", "build", "build" }) &&
            capture.builds.size() == 3 && capture.geometries.size() == 3 &&
            asAddressQueries == addressCount && asSizeQueries == 0 && asPropertyQueries == 0,
            "Recording must preserve order and perform no host preparation!");
        const auto& triangle = capture.geometries[0][0].geometry.triangles;
        CHECK_TRUE(triangle.vertexData.deviceAddress == 4160 && triangle.indexData.deviceAddress == 12300 &&
            triangle.transformData.deviceAddress == 16400 && triangle.vertexStride == 16 &&
            triangle.maxVertex == 6 && triangle.vertexFormat == VK_FORMAT_R32G32_SFLOAT &&
            triangle.indexType == VK_INDEX_TYPE_UINT16 && capture.buildRanges[0][0].primitiveCount == 2 &&
            capture.builds[0].scratchData.deviceAddress == 8448 &&
            capture.builds[0].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR &&
            capture.builds[1].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR &&
            capture.builds[1].scratchData.deviceAddress == 8704 &&
            capture.builds[1].srcAccelerationStructure == resources.bottomSrc.GetVkAccelerationStructure() &&
            capture.builds[2].srcAccelerationStructure == resources.bottomDst.GetVkAccelerationStructure(),
            "BLAS addresses, offsets, format, or build/update mapping is wrong!");
    }

    void TestAsValidationAndScratchSelection()
    {
        CommandBufferTestProbe::Resources resources;
        CommandBufferTestProbe::ExternalBuffer vertices(101, 512, asInputUsage);
        CommandBufferTestProbe::ExternalBuffer scratch(102, 128, asScratchUsage);
        ScopedFunction address(vkGetBufferDeviceAddress, &CaptureAsAddress);
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        AccelStructTriangleGeometry geometry;
        geometry.vertexBuffer = &vertices.buffer;
        geometry.vertexStride = 12;
        geometry.vertexCount = 6;
        geometry.primitiveCount = 2;
        BottomLevelAccelStructCreateInfo info;
        info.SetTriangles(std::span(&geometry, 1));
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        CommandBufferTestProbe::PrepareBottom(resources.bottomSrc, info);
        CommandBufferTestProbe::PrepareBottom(resources.bottomDst, info);
        CommandBuffer commands;
        ExpectRejected(commands, [&] { commands.BuildAccelerationStructureKHR(resources.bottomDst, info, scratch.buffer); });
        commands.UpdateAccelerationStructureKHR(resources.bottomSrc, resources.bottomDst, info, scratch.buffer);
        ExpectRejected(commands, [&] { commands.UpdateAccelerationStructureKHR(
            resources.bottomSrc, resources.bottomDst, info, scratch.buffer, 1); });
        CommandBufferTestProbe::ExternalBuffer wrongUsage(102, 4096, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        ExpectRejected(commands, [&] { commands.BuildAccelerationStructureKHR(resources.bottomDst, info, wrongUsage.buffer); });
        geometry.vertexOffset = 500;
        info.SetTriangles(std::span(&geometry, 1));
        ExpectRejected(commands, [&] { commands.UpdateAccelerationStructureKHR(
            resources.bottomSrc, resources.bottomDst, info, scratch.buffer); });
        geometry.vertexOffset = 0;
        geometry.vertexCount = 0;
        info.SetTriangles(std::span(&geometry, 1));
        ExpectRejected(commands, [&] { commands.UpdateAccelerationStructureKHR(
            resources.bottomSrc, resources.bottomDst, info, scratch.buffer); });
        geometry.vertexCount = 6;
        geometry.flags = 0;
        info.SetTriangles(std::span(&geometry, 1));
        ExpectRejected(commands, [&] { commands.UpdateAccelerationStructureKHR(
            resources.bottomSrc, resources.bottomDst, info, scratch.buffer); });
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        info.SetTriangles(std::span(&geometry, 1));
        info.CustomizeBuildFlags(0);
        CommandBufferTestProbe::PrepareBottom(resources.bottomSrc, info);
        CommandBufferTestProbe::PrepareBottom(resources.bottomDst, info, 0, 0);
        Buffer emptyScratch;
        commands.BuildAccelerationStructureKHR(resources.bottomDst, info, emptyScratch);
        capture = {};
        CommandBufferTestProbe::RecordLast(commands);
        CHECK_TRUE(capture.builds[0].scratchData.deviceAddress == 0,
            "Zero scratch requirements must not require an allocated Buffer!");
        ExpectRejected(commands, [&] { commands.UpdateAccelerationStructureKHR(
            resources.bottomSrc, resources.bottomDst, info, emptyScratch); });
        commands.BeginRenderPass(resources.pass, resources.framebuffer, 0, 0, 8, 8);
        ExpectRejected(commands, [&] { commands.BuildAccelerationStructureKHR(resources.bottomDst, info, emptyScratch); });
        commands.EndRenderPass();
    }

    void TestNonIndexedTrianglesAndAabbMapping()
    {
        CommandBufferTestProbe::Resources resources;
        CommandBufferTestProbe::ExternalBuffer inputs(101, 4096, asInputUsage);
        CommandBufferTestProbe::ExternalBuffer scratch(102, 4096, asScratchUsage);
        ScopedFunction address(vkGetBufferDeviceAddress, &CaptureAsAddress);
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        BottomLevelAccelStructCreateInfo info;
        AccelStructTriangleGeometry triangle;
        triangle.vertexBuffer = &inputs.buffer;
        triangle.vertexOffset = 12;
        triangle.vertexStride = 12;
        triangle.vertexCount = 6;
        triangle.primitiveCount = 2;
        info.SetTriangles(std::span(&triangle, 1));
        CommandBufferTestProbe::PrepareBottom(resources.bottomDst, info);
        CommandBuffer commands;
        commands.BuildAccelerationStructureKHR(resources.bottomDst, info, scratch.buffer);
        AccelStructAabbGeometry aabb;
        aabb.buffer = &inputs.buffer;
        aabb.offset = 8;
        aabb.stride = 32;
        aabb.primitiveCount = 3;
        aabb.flags = 0;
        info.SetAabbs(std::span(&aabb, 1));
        CommandBufferTestProbe::PrepareBottom(resources.bottomDst, info);
        commands.BuildAccelerationStructureKHR(resources.bottomDst, info, scratch.buffer);
        info.Reset();
        capture = {};
        CommandBufferTestProbe::Record(commands);
        CHECK_TRUE(capture.geometries[0][0].geometry.triangles.indexType == VK_INDEX_TYPE_NONE_KHR &&
            capture.geometries[0][0].geometry.triangles.indexData.deviceAddress == 0 &&
            capture.geometries[0][0].geometry.triangles.vertexData.deviceAddress == 4108 &&
            capture.geometries[1][0].geometryType == VK_GEOMETRY_TYPE_AABBS_KHR &&
            capture.geometries[1][0].flags == 0 &&
            capture.geometries[1][0].geometry.aabbs.data.deviceAddress == 4104 &&
            capture.geometries[1][0].geometry.aabbs.stride == 32 &&
            capture.buildRanges[1][0].primitiveCount == 3,
            "Non-indexed triangles or AABB descriptions were not copied/mapped correctly!");
    }

    void TestStaticInstanceUploadIsExplicitAndAtomic()
    {
        CommandBufferTestProbe::Resources resources;
        CommandBufferTestProbe::SetBlasAddress(resources.bottomSrc, 32768);
        std::array<AccelStructInstance, 2> instances{};
        instances[0].accelStruct = &resources.bottomSrc;
        instances[0].transform[3][0] = 7.f;
        instances[0].instanceCustomIndex = 0x123456;
        instances[0].shaderGroupIndexOffset = 0x654321;
        instances[0].mask = 0xAA;
        instances[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
        TopLevelAccelStructCreateInfo info;
        info.SetInstances(instances);
        instances[0].instanceCustomIndex = 0;
        std::array<uint8_t, 256> memory;
        memory.fill(0xCD);
        CommandBufferTestProbe::ExternalBuffer upload(105, memory.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            memory.data(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        TopLevelAccelStruct::WriteInstances(info, upload.buffer, 16);
        VkAccelerationStructureInstanceKHR first{}, second{};
        std::memcpy(&first, memory.data() + 16, sizeof(first));
        std::memcpy(&second, memory.data() + 80, sizeof(second));
        CHECK_TRUE(memory[0] == 0xCD && memory[15] == 0xCD && memory[144] == 0xCD &&
            first.accelerationStructureReference == 32768 && first.instanceCustomIndex == 0x123456 &&
            first.instanceShaderBindingTableRecordOffset == 0x654321 && first.mask == 0xAA &&
            first.flags == VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR &&
            first.transform.matrix[0][0] == 1.f && first.transform.matrix[0][3] == 7.f &&
            second.accelerationStructureReference == 0 && second.mask == 0xFF &&
            second.transform.matrix[1][1] == 1.f, "Static instance encoding, defaults, or upload offset is wrong!");
        const auto previous = memory;
        instances[1].instanceCustomIndex = 0x1000000;
        info.SetInstances(instances);
        CommandBuffer commands;
        ExpectRejected(commands, [&] { TopLevelAccelStruct::WriteInstances(info, upload.buffer, 16); });
        CHECK_TRUE(memory == previous, "A rejected instance list must not partially overwrite the destination!");
        instances[1].instanceCustomIndex = 0;
        info.SetInstances(instances);
        ExpectRejected(commands, [&] { TopLevelAccelStruct::WriteInstances(info, upload.buffer, 200); });
        CommandBufferTestProbe::ExternalBuffer invisible(105, 256, 0, memory.data(), 0);
        ExpectRejected(commands, [&] { TopLevelAccelStruct::WriteInstances(info, invisible.buffer); });
        CommandBufferTestProbe::SetBlasAddress(resources.bottomSrc, 0);
        ExpectRejected(commands, [&] { TopLevelAccelStruct::WriteInstances(info, upload.buffer); });
        CHECK_TRUE(memory == previous, "Host validation must precede all writes!");
    }

    void TestTlasCommandsDoNotUploadAndSnapshotInstanceCount()
    {
        CommandBufferTestProbe::Resources resources;
        std::array<uint8_t, 256> memory;
        memory.fill(0xCD);
        CommandBufferTestProbe::ExternalBuffer instances(101, memory.size(), asInputUsage,
            memory.data(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CommandBufferTestProbe::ExternalBuffer scratch(102, 4096, asScratchUsage);
        ScopedFunction address(vkGetBufferDeviceAddress, &CaptureAsAddress);
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        std::array<AccelStructInstance, 2> descriptions{};
        TopLevelAccelStructCreateInfo info;
        info.SetInstances(descriptions);
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        CommandBufferTestProbe::PrepareTop(resources.topSrc, info);
        CommandBufferTestProbe::PrepareTop(resources.topDst, info);
        CommandBuffer commands;
        commands.BuildAccelerationStructureKHR(resources.topDst, info, instances.buffer, 32, scratch.buffer, 256);
        commands.UpdateAccelerationStructureKHR(resources.topSrc, resources.topDst, info, instances.buffer, 32, scratch.buffer);
        ExpectRejected(commands, [&] { commands.BuildAccelerationStructureKHR(
            resources.topDst, info, instances.buffer, 1, scratch.buffer); });
        ExpectRejected(commands, [&] { commands.BuildAccelerationStructureKHR(
            resources.topDst, info, instances.buffer, 144, scratch.buffer); });
        info.Reset();
        capture = {};
        CommandBufferTestProbe::Record(commands);
        CHECK_TRUE(capture.geometries[0][0].geometry.instances.data.deviceAddress == 4128 &&
            capture.geometries[0][0].geometry.instances.arrayOfPointers == VK_FALSE &&
            capture.buildRanges[0][0].primitiveCount == 2 &&
            capture.builds[0].scratchData.deviceAddress == 8448 &&
            capture.builds[1].srcAccelerationStructure == resources.topSrc.GetVkAccelerationStructure() &&
            capture.builds[1].dstAccelerationStructure == resources.topDst.GetVkAccelerationStructure() &&
            capture.builds[1].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR &&
            std::all_of(memory.begin(), memory.end(), [](uint8_t value) { return value == 0xCD; }),
            "TLAS recording must snapshot count/offsets without uploading or changing external Buffer contents!");
    }

    void TestAccelerationStructureAndSbtSnapshots()
    {
        CommandBufferTestProbe::Resources resources;
        ScopedFunction address(vkGetBufferDeviceAddress, &CaptureAddress);
        ScopedFunction trace(vkCmdTraceRaysKHR, &CaptureTrace);
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        ScopedFunction copy(vkCmdCopyAccelerationStructureKHR, &CaptureAsCopy);
        capture = {};
        CommandBuffer buffer;
        ExpectRejected(buffer, [&] { buffer.TraceRaysKHR(resources.table, 10); });
        CommandBufferTestProbe::UseRay(buffer, resources);
        buffer.TraceRaysKHR(resources.table, 640, 480, 2, 1);
        CommandBufferTestProbe::ChangeSbt(resources);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.sbt[0].deviceAddress == 4160 && capture.sbt[0].stride == 64 && capture.sbt[0].size == 64 &&
            capture.sbt[1].deviceAddress == 4352 && capture.sbt[1].size == 128 &&
            capture.sbt[2].deviceAddress == 4608 && capture.sbt[2].size == 64 &&
            capture.sbt[3].deviceAddress == 0 && capture.sbt[3].size == 0 &&
            capture.arguments[0] == 640 && capture.arguments[1] == 480 && capture.arguments[2] == 2,
            "TraceRays must snapshot the selected raygen record and all SBT regions!");
        std::array<VkAccelStructBuildInfo, 2> builds{};
        for (auto& info : builds)
        {
            info.buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
            info.buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            info.buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            info.buildGeometryInfo.geometryCount = 1;
            info.buildGeometryInfo.dstAccelerationStructure = resources.bottomDst.GetVkAccelerationStructure();
            VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexData.deviceAddress = 8192;
            geometry.geometry.triangles.vertexStride = 12;
            info.geometries.push_back(geometry);
            info.buildRangesInfos.push_back({ 7, 16, 3, 64 });
        }
        builds[1].geometries[0].geometry.triangles.vertexData.deviceAddress = 16384;
        builds[1].buildRangesInfos[0].primitiveCount = 11;
        buffer.BuildAccelerationStructuresKHR(builds);
        builds[0].geometries[0].geometry.triangles.vertexData.deviceAddress = 0;
        builds[0].buildRangesInfos[0].primitiveCount = 0;
        builds[0].geometries.clear();
        builds[0].buildRangesInfos.clear();
        builds[1] = {};
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.builds.size() == 2 && capture.geometries.size() == 2 &&
            capture.geometries[0][0].geometry.triangles.vertexData.deviceAddress == 8192 &&
            capture.geometries[1][0].geometry.triangles.vertexData.deviceAddress == 16384 &&
            capture.buildRanges[0][0].primitiveCount == 7 && capture.buildRanges[0][0].primitiveOffset == 16 &&
            capture.buildRanges[0][0].firstVertex == 3 && capture.buildRanges[0][0].transformOffset == 64 &&
            capture.buildRanges[1][0].primitiveCount == 11, "AS commands must own nested geometry and range arrays!");
        ExpectRejected(buffer, [&] { buffer.BuildAccelerationStructuresKHR(builds); });
        buffer.CopyAccelerationStructureKHR(resources.bottomSrc, resources.bottomDst);
        CommandBufferTestProbe::RecordLast(buffer);
        buffer.CopyAccelerationStructureKHR(resources.topSrc, resources.topDst, VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR);
        CommandBufferTestProbe::RecordLast(buffer);
        CHECK_TRUE(capture.asCopies[0].src == CommandBufferTestProbe::Handle<VkAccelerationStructureKHR>(401) &&
            capture.asCopies[0].dst == CommandBufferTestProbe::Handle<VkAccelerationStructureKHR>(402) &&
            capture.asCopies[0].mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR &&
            capture.asCopies[1].src == CommandBufferTestProbe::Handle<VkAccelerationStructureKHR>(403) &&
            capture.asCopies[1].dst == CommandBufferTestProbe::Handle<VkAccelerationStructureKHR>(404) &&
            capture.asCopies[1].mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR,
            "BLAS/TLAS copy overload mapping is wrong!");
    }

    void TestPointerGeometrySnapshotAndPNextRejection()
    {
        ScopedFunction build(vkCmdBuildAccelerationStructuresKHR, &CaptureBuild);
        capture = {};
        VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.geometry.aabbs = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };
        geometry.geometry.aabbs.data.deviceAddress = 2048;
        geometry.geometry.aabbs.stride = 24;
        const VkAccelerationStructureGeometryKHR* pointer = &geometry;
        BuildAccelerationStructuresKHRCommand::Parameters parameters;
        VkAccelerationStructureBuildGeometryInfoKHR info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        info.geometryCount = 1;
        info.ppGeometries = &pointer;
        parameters.buildInfos.push_back(info);
        parameters.buildRanges.push_back({ { 5, 0, 0, 0 } });
        BuildAccelerationStructuresKHRCommand command;
        command.SetParameters(parameters);
        geometry.geometry.aabbs.data.deviceAddress = 0;
        parameters.buildRanges[0][0].primitiveCount = 0;
        command.Record(CommandBufferTestProbe::Handle<VkCommandBuffer>(1));
        CHECK_TRUE(capture.geometries[0][0].geometry.aabbs.data.deviceAddress == 2048 &&
            capture.buildRanges[0][0].primitiveCount == 5, "Pointer-form geometry must be normalized to owned data!");
        parameters.buildInfos[0].pNext = &geometry;
        bool rejected = false;
        try { command.SetParameters(parameters); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK_TRUE(rejected, "Unsupported AS pNext must be rejected!");
        capture = {};
        command.Record(CommandBufferTestProbe::Handle<VkCommandBuffer>(1));
        CHECK_TRUE(capture.geometries[0][0].geometry.aabbs.data.deviceAddress == 2048,
            "Rejected SetParameters must leave the previous snapshot intact!");
    }

    void TestRecordingBatchBoundaries()
    {
        CommandBufferTestProbe::Resources resources;
        for (const size_t count : { 255u, 256u, 257u })
        {
            std::vector<CommandBuffer> buffers(1);
            for (size_t i = 0; i < count; ++i)
                buffers[0].FillBuffer(resources.dst, 0, 4, 0);
            const auto sizes = CommandBufferTestProbe::BatchSizes(buffers);
            if (count == 255)
                CHECK_TRUE(sizes == std::vector<size_t>({ 255 }), "255 transfer commands should remain one batch!");
            else if (count == 256)
                CHECK_TRUE(sizes == std::vector<size_t>({ 256 }), "256 transfer commands should remain one batch!");
            else
                CHECK_TRUE(sizes == std::vector<size_t>({ 256, 1 }), "257 transfer commands should split at the threshold!");
        }
        std::vector<CommandBuffer> buffers(3);
        buffers[0].FillBuffer(resources.dst, 0, 4, 0);
        CommandBuffer state;
        state.SetViewport(0.f, 0.f, 64.f, 64.f);
        for (size_t i = 0; i < 257; ++i)
            state.FillBuffer(resources.dst, 0, 4, 0);
        CommandBufferTestProbe::Append(buffers[1], std::move(state));
        buffers[2].FillBuffer(resources.dst, 0, 4, 0);
        CHECK_TRUE(CommandBufferTestProbe::BatchSizes(buffers) == std::vector<size_t>({ 1, 258, 1 }),
            "An appended stateful buffer must not split or merge with adjacent buffers!");
        CommandBuffer open;
        open.BeginRenderPass(resources.pass, resources.framebuffer, 0, 0, 64, 64);
        ExpectRejected(buffers[2], [&] { CommandBufferTestProbe::Append(buffers[2], std::move(open)); });
        CHECK_TRUE(CommandBufferTestProbe::Count(open) == 1, "Rejected append must preserve its source!");
        open.EndRenderPass();
    }

    void TestInvalidCommandsDoNotAppend()
    {
        CommandBufferTestProbe::Resources resources;
        CommandBuffer buffer;
        std::array<const Buffer*, 1> inputs{ &resources.src };
        std::array<VkDeviceSize, 1> offsets{ 0 };
        ExpectRejected(buffer, [&] { buffer.BindVertexBuffers(0, inputs, {}); });
        inputs[0] = nullptr;
        ExpectRejected(buffer, [&] { buffer.BindVertexBuffers(0, inputs, offsets); });
        ExpectRejected(buffer, [&] { buffer.BindIndexBuffer(resources.src, 1, VK_INDEX_TYPE_UINT32); });
        ExpectRejected(buffer, [&] { buffer.SetViewport(0.f, 0.f, 0.f, 64.f); });
        ExpectRejected(buffer, [&] { buffer.SetScissor(-1, 0, 64, 64); });
        ExpectRejected(buffer, [&] { buffer.BeginRenderPass(resources.pass, resources.framebuffer, 0, 0, 0, 64); });
        ExpectRejected(buffer, [&] { buffer.BindComputeProgram(resources.compute); });
        ExpectRejected(buffer, [&] { buffer.BindRayTracingProgram(resources.ray); });
        ExpectRejected(buffer, [&] { buffer.BindGraphicsShaderResource({}); });
        ExpectRejected(buffer, [&] { buffer.BindComputeShaderResource({}); });
        ExpectRejected(buffer, [&] { buffer.BindRayTracingShaderResource({}); });
        BeginRenderPassCommand directBegin;
        const Command* direct = &directBegin;
        ExpectRejected(buffer, [&] { CommandBufferTestProbe::AppendBorrowedCommands(buffer, &direct, 1); });
        CommandBufferTestProbe::AppendBorrowedCommands(buffer, nullptr, 0);
        CHECK_TRUE(CommandBufferTestProbe::Count(buffer) == 0, "Empty custom commands must be a no-op!");
    }

    void TestBorrowedTransferBatching()
    {
        CommandBufferTestProbe::Resources resources;
        FillBufferCommand transfer;
        transfer.SetParameters({ resources.dst.GetVkBuffer(), 0, 4, 7 });
        std::array<const Command*, 257> commands;
        commands.fill(&transfer);
        std::vector<CommandBuffer> buffers(1);
        CommandBufferTestProbe::AppendBorrowedCommands(buffers[0], commands.data(), commands.size());
        CHECK_TRUE(CommandBufferTestProbe::BatchSizes(buffers) == std::vector<size_t>({ 256, 1 }),
            "Known borrowed transfer commands should retain threshold batching!");

        struct CustomCommand final : Command
        {
            uint32_t& count;
            explicit CustomCommand(uint32_t& inCount) : count(inCount) {}
            void Record(VkCommandBuffer) const override { ++count; }
        };
        uint32_t calls = 0;
        CustomCommand custom(calls);
        commands.fill(&custom);
        std::vector<CommandBuffer> opaque(1);
        CommandBufferTestProbe::AppendBorrowedCommands(opaque[0], commands.data(), commands.size());
        CHECK_TRUE(CommandBufferTestProbe::BatchSizes(opaque) == std::vector<size_t>({ 257 }),
            "Opaque custom commands may bind state and must remain indivisible!");
        CHECK_TRUE(calls == 0, "Borrowed commands must remain deferred!");
        CommandBufferTestProbe::Record(opaque[0]);
        CHECK_TRUE(calls == 257, "Borrowed custom commands must preserve repetition and ordering!");
    }
}


namespace
{
    template<typename T>
    void TestOwnedAppendCallback()
    {
        if constexpr (requires(T& buffer, std::function<void(VkCommandBuffer)> callback)
            { { buffer.AppendCommand(std::move(callback)) } -> std::same_as<void>; })
        {
            std::weak_ptr<int> lifetime;
            std::vector<int> observations;
            {
                T source;
                {
                    auto value = std::make_shared<int>(27);
                    lifetime = value;
                    std::function<void(VkCommandBuffer)> callback = [value, &observations](VkCommandBuffer inBuffer)
                    {
                        CHECK_TRUE(inBuffer == CommandBufferTestProbe::Handle<VkCommandBuffer>(1),
                            "Callback must receive the native recording buffer!");
                        observations.push_back(*value);
                    };
                    source.AppendCommand(std::move(callback));
                }
                CHECK_TRUE(!lifetime.expired() && observations.empty(),
                    "AppendCommand must own its closure and defer execution!");
                T moved(std::move(source));
                T joined;
                joined.AppendCommand([&observations](VkCommandBuffer) { observations.push_back(1); });
                CommandBufferTestProbe::Append(joined, std::move(moved));
                joined.AppendCommand([&observations](VkCommandBuffer) { observations.push_back(3); });
                ExpectRejected(joined, [&] { joined.AppendCommand({}); });
                CommandBufferTestProbe::Record(joined);
                CHECK_TRUE(observations == std::vector<int>({ 1, 27, 3 }),
                    "Callback commands must preserve capture values and stream order across move/append!");
            }
            CHECK_TRUE(lifetime.expired(), "Destroying the command stream must release owned callback captures!");

            CommandBufferTestProbe::Resources resources;
            std::vector<T> buffers(3);
            buffers[0].FillBuffer(resources.dst, 0, 4, 0);
            std::vector<uint32_t> recorded;
            for (uint32_t i = 0; i < 257; ++i)
                buffers[1].AppendCommand([i, &recorded](VkCommandBuffer) { recorded.push_back(i); });
            buffers[2].FillBuffer(resources.dst, 0, 4, 0);
            CHECK_TRUE(CommandBufferTestProbe::BatchSizes(buffers) == std::vector<size_t>({ 1, 257, 1 }),
                "Opaque callback commands must remain in one native recording buffer!");
            CHECK_TRUE(recorded.empty(), "Callbacks must not run while batching!");
            CommandBufferTestProbe::Record(buffers[1]);
            CHECK_TRUE(recorded.size() == 257 && recorded.front() == 0 && recorded.back() == 256,
                "Callback recording must preserve insertion order!");
        }
        else
            CHECK_TRUE(false, "Missing void AppendCommand(std::function<void(VkCommandBuffer)>) interface!");
    }
}

void RunCommandBufferTests()
{
    {
        CommandBufferTestProbe::ExternalBuffer source(801, 128, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        Buffer moved(std::move(source.buffer));
        const bool emptied = source.buffer.GetBufferInformation().size == 0;
        // The fixture borrowed this handle; remove it before either destructor runs.
        CommandBufferTestProbe::ClearBorrowedBuffer(moved);
        CHECK_TRUE(emptied, "Moving a Buffer must empty the source information!");
    }
    TestEmptyInstanceUploadNeedsNoDeviceResources<TopLevelAccelStructCreateInfo, TopLevelAccelStruct>();
    TestHostDescriptionSnapshotAndSizeQuery();
    TestObjectBlasCommandsUseExternalBuffersAndSnapshots();
    TestAsValidationAndScratchSelection();
    TestNonIndexedTrianglesAndAabbMapping();
    TestStaticInstanceUploadIsExplicitAndAtomic();
    TestTlasCommandsDoNotUploadAndSnapshotInstanceCount();
    TestFlattenedViewportIsDeferred<CommandBuffer>();
    TestOwnedAppendCallback<CommandBuffer>();
    TestScalarRegionsAndOwnedBytes();
    TestPushConstantSnapshotAndValidation();
    TestImageClearTypesAndRanges();
    TestRenderPassScalars();
    TestDrawDispatchAndBindings();
    TestImageTransferSnapshotsAndFill();
    TestAccelerationStructureAndSbtSnapshots();
    TestPointerGeometrySnapshotAndPNextRejection();
    TestRecordingBatchBoundaries();
    TestInvalidCommandsDoNotAppend();
    TestBorrowedTransferBatching();
}
