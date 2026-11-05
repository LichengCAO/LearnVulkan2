#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/render_context/device_context.h"
#include "my_vulkan/render_context/private/frame_slot.h"
#include "my_vulkan/pipeline/render_pass_desc.h"
#include "my_vulkan/pipeline/framebuffer_desc.h"
#include "my_vulkan/allocator/private/render_target_cache.h"
#include "my_vulkan/resource/private/image_storage.h"

#include <set>
#include <utility>

struct ResourceSubmissionTestProbe
{
    struct HookState
    {
        int* destroyed = nullptr;
        bool expectAlive = true;
        bool failNextSubmit = false;
        uintptr_t nextFence = 100;
        size_t waitCount = 0;
        size_t submitCount = 0;
        std::set<VkFence> signaled;
    };

    inline static HookState* activeHooks = nullptr;
    inline static size_t renderPassPreparationCalls = 0;

    struct PreparationStop final : std::runtime_error
    {
        PreparationStop() : std::runtime_error("Stop before native render-pass allocation") {}
    };

    static auto VKAPI_PTR _StopRenderPassPreparation(VkDevice inDevice,
        const VkRenderPassCreateInfo* inInfo, const VkAllocationCallbacks*, VkRenderPass*) -> VkResult
    {
        CHECK_TRUE(inDevice == reinterpret_cast<VkDevice>(uintptr_t(1)) &&
            inInfo->attachmentCount == 1 && inInfo->subpassCount == 1,
            "Frame recording must prepare the queued rendering description on the active Device!");
        ++renderPassPreparationCalls;
        throw PreparationStop{};
    }

    struct RenderPreparationScope
    {
        PFN_vkCreateRenderPass createRenderPass = vkCreateRenderPass;
        std::unique_ptr<VulkanDetail::RenderTargetCache> cache;

        RenderPreparationScope()
        {
            auto& instance = MyDevice::GetInstance();
            cache = std::move(instance.m_uptrRenderTargetCache);
            instance.m_uptrRenderTargetCache = std::make_unique<VulkanDetail::RenderTargetCache>();
            instance.m_uptrRenderTargetCache->Create(instance.vkDevice);
            renderPassPreparationCalls = 0;
            vkCreateRenderPass = _StopRenderPassPreparation;
        }
        RenderPreparationScope(const RenderPreparationScope&) = delete;
        auto operator=(const RenderPreparationScope&) -> RenderPreparationScope& = delete;
        ~RenderPreparationScope()
        {
            auto& instance = MyDevice::GetInstance();
            instance.m_uptrRenderTargetCache.reset();
            instance.m_uptrRenderTargetCache = std::move(cache);
            vkCreateRenderPass = createRenderPass;
        }
    };

    static void _CheckAlive()
    {
        if (activeHooks->expectAlive)
            CHECK_TRUE(*activeHooks->destroyed == 0, "Resource was released before native GPU completion!");
    }

    static VKAPI_ATTR auto VKAPI_CALL _CreateFence(VkDevice, const VkFenceCreateInfo*,
        const VkAllocationCallbacks*, VkFence* outFence) -> VkResult
    {
        *outFence = reinterpret_cast<VkFence>(++activeHooks->nextFence);
        return VK_SUCCESS;
    }

    static VKAPI_ATTR auto VKAPI_CALL _ResetFences(VkDevice, uint32_t inCount,
        const VkFence* inFences) -> VkResult
    {
        for (uint32_t index = 0; index < inCount; ++index)
            activeHooks->signaled.erase(inFences[index]);
        return VK_SUCCESS;
    }

    static VKAPI_ATTR void VKAPI_CALL _DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}

    static VKAPI_ATTR auto VKAPI_CALL _QueueSubmit(VkQueue, uint32_t inCount,
        const VkSubmitInfo2* inSubmits, VkFence) -> VkResult
    {
        _CheckAlive();
        CHECK_TRUE(inCount == 1 && inSubmits != nullptr, "Expected one native queue submission!");
        ++activeHooks->submitCount;
        if (std::exchange(activeHooks->failNextSubmit, false))
            return VK_ERROR_DEVICE_LOST;
        return VK_SUCCESS;
    }

    static VKAPI_ATTR auto VKAPI_CALL _WaitForFences(VkDevice, uint32_t inCount,
        const VkFence* inFences, VkBool32, uint64_t inTimeout) -> VkResult
    {
        _CheckAlive();
        ++activeHooks->waitCount;
        for (uint32_t index = 0; index < inCount; ++index)
        {
            if (inTimeout == 0 && !activeHooks->signaled.contains(inFences[index]))
                return VK_TIMEOUT;
        }
        return VK_SUCCESS;
    }

    static VKAPI_ATTR auto VKAPI_CALL _DeviceWaitIdle(VkDevice) -> VkResult { return VK_SUCCESS; }

    static VKAPI_ATTR void VKAPI_CALL _DestroyImage(VkDevice inDevice, VkImage inImage, const VkAllocationCallbacks*)
    {
        CHECK_TRUE(inDevice == reinterpret_cast<VkDevice>(uintptr_t(1)) &&
            inImage == reinterpret_cast<VkImage>(uintptr_t(101)), "Unexpected retained image destruction!");
        ++*activeHooks->destroyed;
    }

    struct HookScope
    {
        HookState state;
        PFN_vkCreateFence createFence = vkCreateFence;
        PFN_vkResetFences resetFences = vkResetFences;
        PFN_vkDestroyFence destroyFence = vkDestroyFence;
        PFN_vkQueueSubmit2 queueSubmit = vkQueueSubmit2;
        PFN_vkWaitForFences waitForFences = vkWaitForFences;
        PFN_vkDeviceWaitIdle deviceWaitIdle = vkDeviceWaitIdle;
        PFN_vkDestroyImage destroyImage = vkDestroyImage;
        VkDevice device = VK_NULL_HANDLE;
        std::array<VkQueue, 3> queues{};

        explicit HookScope(int& inDestroyed)
        {
            auto& instance = MyDevice::GetInstance();
            device = instance.vkDevice;
            queues = {instance.m_vkGraphicsQueue, instance.m_vkComputeQueue, instance.m_vkTransferQueue};
            instance.vkDevice = reinterpret_cast<VkDevice>(uintptr_t(1));
            instance.m_vkGraphicsQueue = reinterpret_cast<VkQueue>(uintptr_t(11));
            instance.m_vkComputeQueue = reinterpret_cast<VkQueue>(uintptr_t(12));
            instance.m_vkTransferQueue = reinterpret_cast<VkQueue>(uintptr_t(13));
            state.destroyed = &inDestroyed;
            activeHooks = &state;
            vkCreateFence = _CreateFence;
            vkResetFences = _ResetFences;
            vkDestroyFence = _DestroyFence;
            vkQueueSubmit2 = _QueueSubmit;
            vkWaitForFences = _WaitForFences;
            vkDeviceWaitIdle = _DeviceWaitIdle;
            vkDestroyImage = _DestroyImage;
        }

        HookScope(const HookScope&) = delete;
        auto operator=(const HookScope&) -> HookScope& = delete;

        ~HookScope()
        {
            auto& instance = MyDevice::GetInstance();
            instance.vkDevice = device;
            instance.m_vkGraphicsQueue = queues[0];
            instance.m_vkComputeQueue = queues[1];
            instance.m_vkTransferQueue = queues[2];
            vkCreateFence = createFence;
            vkResetFences = resetFences;
            vkDestroyFence = destroyFence;
            vkQueueSubmit2 = queueSubmit;
            vkWaitForFences = waitForFences;
            vkDeviceWaitIdle = deviceWaitIdle;
            vkDestroyImage = destroyImage;
            activeHooks = nullptr;
        }
    };

    static auto _Resource(int& inDestroyed) -> VulkanDetail::ResourceReference
    {
        return MakeIntrusive<VulkanDetail::VulkanHandleResource<uint64_t>>(99,
            [&inDestroyed](uint64_t) { ++inDestroyed; });
    }

    static auto _Buffers(VulkanDetail::ResourceReference inResource) -> std::vector<CommandBuffer>
    {
        // A resource-only buffer exercises the real recording ownership handoff
        // without needing a worker scheduler or an actual Vulkan command pool.
        std::vector<CommandBuffer> buffers(1);
        buffers.front().m_resourceReferences.push_back(std::move(inResource));
        return buffers;
    }

    static auto _ExplicitKeepAliveBuffers(int& inDestroyed, bool inView) -> std::vector<CommandBuffer>
    {
        Image image;
        CHECK_TRUE(activeHooks && activeHooks->destroyed == &inDestroyed,
            "Explicit KeepAlive fixture requires active native lifetime hooks!");
        auto storage = std::make_unique<VulkanDetail::ImageStorage>();
        storage->device = MyDevice::GetInstance().vkDevice;
        storage->image = reinterpret_cast<VkImage>(uintptr_t(101));
        storage->ownsNative = true;
        image.m_resource = MakeIntrusive<VulkanDetail::ImageResource>(std::move(storage));
        std::vector<CommandBuffer> buffers(1);
        if (inView)
        {
            ImageView view;
            view.m_resource = MakeIntrusive<VulkanDetail::ImageViewResource>(
                reinterpret_cast<VkImageView>(uintptr_t(102)), nullptr,
                std::vector<VulkanDetail::ResourceReference>{image.m_resource});
            buffers.front().KeepAlive(view);
        }
        else buffers.front().KeepAlive(image);
        return buffers;
    }

    static auto _Context() -> std::unique_ptr<DeviceContext>
    {
        auto context = std::make_unique<DeviceContext>(1);
        // Frame waits below still use real submission-manager code. Suppress its
        // destructor's global device drain, as the test uses an uninitialized device.
        context->m_uptrCommandQueueManager->m_created = false;
        return context;
    }

    static void RecordingPayloadOutlivesCpuSources()
    {
        int destroyed = 0;
        auto slot = std::unique_ptr<FrameSlot>(new FrameSlot());
        auto ticket = slot->DispatchRecording(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
        CHECK_TRUE(destroyed == 0, "Recording must retain resource-only command buffers!");
        auto payload = slot->TakeRecordingResult(ticket);
        CHECK_TRUE(slot->m_recordTasks.empty() && payload.resourceReferences.size() == 1,
            "Taking a recording result must consume its CPU task and transfer its references!");
        slot.reset();
        CHECK_TRUE(destroyed == 0, "Recorded resource references must survive destruction of CPU recording sources!");
        payload.resourceReferences.clear();
        CHECK_TRUE(destroyed == 1, "An abandoned recording payload must release its resources exactly once!");
    }

    static void RenderingPreparationRunsWithoutProgramBindings()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        RenderPreparationScope preparation;
        std::vector<CommandBuffer> buffers(1);
        {
            RenderPassDesc pass;
            AttachmentDescription attachment;
            attachment.CustomizeFormat(VK_FORMAT_R8G8B8A8_UNORM, VkClearValue{});
            pass.AddAttachment("color", attachment);
            SubpassDescription subpass;
            subpass.AddColorAttachment(0, "color");
            pass.AddSubpass("main", subpass);
            pass.Finalize();
            FramebufferDesc framebuffer;
            framebuffer.m_attachments.emplace("color", FramebufferDesc::Attachment{
                42, reinterpret_cast<VkImageView>(uintptr_t(304)), { 64, 64, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1 });
            buffers.front().BeginRenderPass(pass, framebuffer, 0, 0, 64, 64);
            buffers.front().EndRenderPass();
        }
        CHECK_TRUE(renderPassPreparationCalls == 0,
            "Host finalization and rendering enqueue must defer native creation until frame preparation!");
        auto slot = std::unique_ptr<FrameSlot>(new FrameSlot());
        bool reachedPreparation = false;
        try
        {
            slot->DispatchRecording(QueueFamilyType::GRAPHICS, std::move(buffers));
        }
        catch (const PreparationStop&)
        {
            reachedPreparation = true;
        }
        CHECK_TRUE(reachedPreparation && renderPassPreparationCalls == 1,
            "FrameSlot must prepare rendering commands even when no Program is bound!");
        CHECK_TRUE(slot->m_recordTasks.empty() && !slot->m_dynamicDescriptorPool && hooks.state.submitCount == 0,
            "A preparation failure must not publish recording work, allocate descriptors, or submit GPU work!");
    }

    static void AbandonedRecordingReleasesResources()
    {
        int destroyed = 0;
        {
            auto slot = std::unique_ptr<FrameSlot>(new FrameSlot());
            slot->DispatchRecording(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
            CHECK_TRUE(destroyed == 0, "An unconsumed recording ticket must retain its resources!");
        }
        CHECK_TRUE(destroyed == 1, "Abandoning an unsubmitted recording must release its resources!");
    }

    static void DeferredResourcesWaitForEveryFrameQueue()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        context->StartFrame();
        context->CommitCommandsToQueue(QueueFamilyType::GRAPHICS, _Buffers(_Resource(destroyed)));
        context->SubmitQueue(QueueFamilyType::GRAPHICS, {});
        CHECK_TRUE(destroyed == 0, "Deferred submission must transfer recording references to frame completion!");
        context->EndFrame();
        CHECK_TRUE(hooks.state.submitCount == 4, "Expected resource submission and three frame completion markers!");

        auto& slot = *context->m_frameContexts.front();
        auto& manager = *context->m_uptrCommandQueueManager;
        auto& graphics = slot.GetCompletionFence(QueueFamilyType::GRAPHICS);
        hooks.state.signaled.insert(manager._FindHostFenceVkFence(graphics));
        CHECK_TRUE(context->Poll(graphics), "The signaled graphics queue must complete!");
        CHECK_TRUE(!context->Poll(slot.GetCompletionFence(QueueFamilyType::COMPUTE)),
            "An unsignaled compute queue must remain incomplete!");
        CHECK_TRUE(!context->Poll(slot.GetCompletionFence(QueueFamilyType::TRANSFER)),
            "An unsignaled transfer queue must remain incomplete!");
        CHECK_TRUE(destroyed == 0, "One completed queue must not release resources retained by the whole frame!");

        context->StartFrame();
        CHECK_TRUE(hooks.state.waitCount == 5 && destroyed == 1,
            "Frame reuse must wait for both remaining queues before releasing resources exactly once!");
        hooks.state.expectAlive = false;
        context->EndFrame();
    }

    static void ImmediateResourcesWaitForFenceCallback()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
        CHECK_TRUE(hooks.state.submitCount == 1 && hooks.state.waitCount == 1 && destroyed == 1,
            "Immediate resources must survive native submission/wait and release in fence completion!");
        hooks.state.expectAlive = false;
    }

    static void ExplicitKeepAliveWaitsForSubmissionCompletion()
    {
        for (const bool view : { false, true })
        {
            int destroyed = 0;
            HookScope hooks(destroyed);
            auto context = _Context();
            context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, _ExplicitKeepAliveBuffers(destroyed, view));
            CHECK_TRUE(hooks.state.submitCount == 1 && hooks.state.waitCount == 1 && destroyed == 1,
                "Explicit Image/ImageView KeepAlive must survive owner destruction, submission and waiting, then release at completion!");
            hooks.state.expectAlive = false;
        }
    }

    static void FailedImmediateSubmitReleasesResources()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        hooks.state.failNextSubmit = true;
        bool failed = false;
        try
        {
            context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        CHECK_TRUE(failed && hooks.state.submitCount == 1 && hooks.state.waitCount == 0 && destroyed == 1,
            "A failed immediate submission must release its unsubmitted resource callback without waiting!");
        CHECK_TRUE(context->m_uptrCommandQueueManager->m_queueStates[2].pendingHostFences.empty(),
            "A failed submission must not leave a pending fence callback!");
        hooks.state.expectAlive = false;
    }
};

void RunResourceSubmissionTests()
{
    ResourceSubmissionTestProbe::RenderingPreparationRunsWithoutProgramBindings();
    ResourceSubmissionTestProbe::RecordingPayloadOutlivesCpuSources();
    ResourceSubmissionTestProbe::AbandonedRecordingReleasesResources();
    ResourceSubmissionTestProbe::DeferredResourcesWaitForEveryFrameQueue();
    ResourceSubmissionTestProbe::ImmediateResourcesWaitForFenceCallback();
    ResourceSubmissionTestProbe::ExplicitKeepAliveWaitsForSubmissionCompletion();
    ResourceSubmissionTestProbe::FailedImmediateSubmitReleasesResources();
}
