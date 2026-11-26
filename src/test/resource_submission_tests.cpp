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
        VkResult nextSubmit = VK_SUCCESS;
        VkResult nextWait = VK_SUCCESS;
        VkResult nextReset = VK_SUCCESS;
        uintptr_t nextFence = 100;
        uintptr_t nextSemaphore = 200;
        size_t waitCount = 0;
        size_t submitCount = 0;
        size_t failAtSubmitCount = 0;
        size_t statusCount = 0;
        size_t resetCount = 0;
        std::vector<VkCommandBuffer> lastCommands;
        std::vector<VkSemaphore> lastWaits, lastSignals;
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
        ++activeHooks->resetCount;
        if (const auto result = std::exchange(activeHooks->nextReset, VK_SUCCESS); result != VK_SUCCESS)
            return result;
        for (uint32_t index = 0; index < inCount; ++index)
            activeHooks->signaled.erase(inFences[index]);
        return VK_SUCCESS;
    }

    static VKAPI_ATTR void VKAPI_CALL _DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}

    static VKAPI_ATTR auto VKAPI_CALL _CreateSemaphore(VkDevice, const VkSemaphoreCreateInfo*,
        const VkAllocationCallbacks*, VkSemaphore* semaphore) -> VkResult
    {
        *semaphore = reinterpret_cast<VkSemaphore>(++activeHooks->nextSemaphore);
        return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL _DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks*) {}

    static VKAPI_ATTR auto VKAPI_CALL _QueueSubmit(VkQueue, uint32_t inCount,
        const VkSubmitInfo2* inSubmits, VkFence) -> VkResult
    {
        _CheckAlive();
        CHECK_TRUE(inCount == 1 && inSubmits != nullptr, "Expected one native queue submission!");
        activeHooks->lastCommands.clear();
        for (uint32_t index = 0; index < inSubmits->commandBufferInfoCount; ++index)
            activeHooks->lastCommands.push_back(inSubmits->pCommandBufferInfos[index].commandBuffer);
        activeHooks->lastWaits.clear();
        activeHooks->lastSignals.clear();
        for (uint32_t index = 0; index < inSubmits->waitSemaphoreInfoCount; ++index)
            activeHooks->lastWaits.push_back(inSubmits->pWaitSemaphoreInfos[index].semaphore);
        for (uint32_t index = 0; index < inSubmits->signalSemaphoreInfoCount; ++index)
            activeHooks->lastSignals.push_back(inSubmits->pSignalSemaphoreInfos[index].semaphore);
        ++activeHooks->submitCount;
        if (activeHooks->submitCount == activeHooks->failAtSubmitCount)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        return std::exchange(activeHooks->nextSubmit, VK_SUCCESS);
    }

    static VKAPI_ATTR auto VKAPI_CALL _WaitForFences(VkDevice, uint32_t inCount,
        const VkFence* inFences, VkBool32, uint64_t inTimeout) -> VkResult
    {
        _CheckAlive();
        ++activeHooks->waitCount;
        if (const VkResult result = std::exchange(activeHooks->nextWait, VK_SUCCESS); result != VK_SUCCESS)
            return result;
        for (uint32_t index = 0; index < inCount; ++index)
        {
            if (inTimeout == 0 && !activeHooks->signaled.contains(inFences[index]))
                return VK_TIMEOUT;
        }
        return VK_SUCCESS;
    }

    static VKAPI_ATTR auto VKAPI_CALL _GetFenceStatus(VkDevice, VkFence fence) -> VkResult
    {
        _CheckAlive();
        if (++activeHooks->statusCount == 1)
            return VK_NOT_READY;
        activeHooks->signaled.insert(fence);
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
        PFN_vkCreateSemaphore createSemaphore = vkCreateSemaphore;
        PFN_vkDestroySemaphore destroySemaphore = vkDestroySemaphore;
        PFN_vkQueueSubmit2 queueSubmit = vkQueueSubmit2;
        PFN_vkWaitForFences waitForFences = vkWaitForFences;
        PFN_vkGetFenceStatus getFenceStatus = vkGetFenceStatus;
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
            vkCreateSemaphore = _CreateSemaphore;
            vkDestroySemaphore = _DestroySemaphore;
            vkQueueSubmit2 = _QueueSubmit;
            vkWaitForFences = _WaitForFences;
            vkGetFenceStatus = _GetFenceStatus;
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
            vkCreateSemaphore = createSemaphore;
            vkDestroySemaphore = destroySemaphore;
            vkQueueSubmit2 = queueSubmit;
            vkWaitForFences = waitForFences;
            vkGetFenceStatus = getFenceStatus;
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

    static void ForeignFenceCannotConsumeMatchingVersion()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto first = _Context();
        auto second = _Context();
        HostFence firstFence, secondFence;
        first->StartFrame();
        second->StartFrame();
        first->SubmitQueue(QueueFamilyType::GRAPHICS, SubmissionSyncInfo{}.SetFence(firstFence));
        second->SubmitQueue(QueueFamilyType::GRAPHICS, SubmissionSyncInfo{}.SetFence(secondFence));
        second->Wait(secondFence);
        bool rejected = false;
        try { second->Poll(firstFence); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK_TRUE(rejected && !first->Poll(firstFence),
            "Another context's completed submission version must not consume an unrelated fence!");
        first->Wait(firstFence);
        first->EndFrame();
        second->EndFrame();
    }

    static void PartiallySubmittedFrameCanEndOnRetry()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        context->StartFrame();
        hooks.state.failAtSubmitCount = 2;
        bool failed = false;
        try { context->EndFrame(); }
        catch (const std::runtime_error&) { failed = true; }
        CHECK_TRUE(failed && hooks.state.submitCount == 2, "The second queue marker must fail in this fixture!");
        context->SubmitQueue(QueueFamilyType::GRAPHICS, {});
        context->EndFrame();
        CHECK_TRUE(hooks.state.submitCount == 6 && hooks.state.waitCount == 1,
            "Retry must cover work submitted after an earlier queue's successful marker!");
    }

    static void CrossQueueCompletionRequiresProducerFenceSignal()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        HostFence producer, consumer;
        bool producerCallback = false;
        producer.AddCallback([&] { producerCallback = true; });
        context->StartFrame();
        QueueDependency dependency;
        context->SubmitQueue(QueueFamilyType::GRAPHICS,
            SubmissionSyncInfo{}.AddSignalQueueDependency(dependency).SetFence(producer));
        context->SubmitQueue(QueueFamilyType::COMPUTE,
            SubmissionSyncInfo{}.AddWaitQueueDependency(dependency).SetFence(consumer));
        context->Wait(consumer);
        CHECK_TRUE(!producerCallback && !context->Poll(producer),
            "A downstream queue completion must preserve an unsignaled producer fence!");
        hooks.state.nextReset = VK_ERROR_OUT_OF_HOST_MEMORY;
        bool failed = false;
        try { context->Wait(producer); }
        catch (const std::runtime_error&) { failed = true; }
        CHECK_TRUE(failed && !producerCallback, "A failed fence recycle must preserve its completion record!");
        context->Wait(producer);
        CHECK_TRUE(producerCallback, "Retry must reclaim the producer fence and run its callback!");
        context->EndFrame();
    }

    static void DependencyWaitRetryAndResignal()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        auto& manager = *context->m_uptrCommandQueueManager;
        QueueDependency dependency;
        HostFence completed;
        context->StartFrame();
        context->SubmitQueue(QueueFamilyType::GRAPHICS, SubmissionSyncInfo{}.AddSignalQueueDependency(dependency));
        const auto firstSignal = hooks.state.lastSignals;
        hooks.state.nextSubmit = VK_ERROR_OUT_OF_HOST_MEMORY;
        bool failed = false;
        try { context->SubmitQueue(QueueFamilyType::COMPUTE, SubmissionSyncInfo{}.AddWaitQueueDependency(dependency)); }
        catch (const std::runtime_error&) { failed = true; }
        const auto& compute = manager.m_queueStates[1];
        const bool preserved = failed && hooks.state.submitCount == 2 && hooks.state.lastWaits == firstSignal &&
            compute.tailFrontier.GetVersion(1) == 0 && compute.retiredSemaphores.empty();

        context->SubmitQueue(QueueFamilyType::COMPUTE,
            SubmissionSyncInfo{}.AddWaitQueueDependency(dependency).AddSignalQueueDependency(dependency));
        const auto secondSignal = hooks.state.lastSignals;
        const bool replaced = firstSignal.size() == 1 && secondSignal.size() == 1 && firstSignal != secondSignal &&
            hooks.state.lastWaits == firstSignal && compute.retiredSemaphores.size() == 1 &&
            compute.retiredSemaphores.front().semaphore == firstSignal.front() &&
            compute.retiredSemaphores.front().consumerVersion == 1;

        context->SubmitQueue(QueueFamilyType::TRANSFER, SubmissionSyncInfo{}.AddWaitQueueDependency(dependency).SetFence(completed));
        const auto& transfer = manager.m_queueStates[2];
        CHECK_TRUE(preserved && replaced && hooks.state.lastWaits == secondSignal && hooks.state.lastSignals.empty() &&
            transfer.retiredSemaphores.size() == 1 && transfer.retiredSemaphores.front().semaphore == secondSignal.front() &&
            transfer.retiredSemaphores.front().consumerVersion == 1 && manager.m_dependencySemaphores.empty(),
            "A failed native wait must leave the pending signal and frontier intact for an ordered wait-and-resignal chain!");
        context->Wait(completed);
        CHECK_TRUE(compute.retiredSemaphores.empty() && transfer.retiredSemaphores.empty(),
            "Completing the final wait must reclaim the consumed semaphores!");
        context->EndFrame();
    }

    static void SubmittedFenceOwnsOneShotCallbacks()
    {
        int destroyed = 0, callbacks = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        context->StartFrame();
        bool rejected = false;
        {
            HostFence temporary;
            temporary.AddCallback([&] { ++callbacks; });
            context->SubmitQueue(QueueFamilyType::GRAPHICS, SubmissionSyncInfo{}.SetFence(temporary));
            try { temporary.AddCallback([&] { ++callbacks; }); }
            catch (const std::runtime_error&) { rejected = true; }
        }
        CHECK_TRUE(rejected && callbacks == 0, "An in-flight fence must reject new callbacks and preserve the submitted one!");
        HostFence later;
        context->SubmitQueue(QueueFamilyType::GRAPHICS, SubmissionSyncInfo{}.SetFence(later));
        context->Wait(later);
        CHECK_TRUE(context->Poll(later) && callbacks == 1,
            "Completion must run the submitted callback exactly once after its public fence is destroyed!");
        context->EndFrame();
    }

    static void UniversalQueueProvidesTransferFallback()
    {
        const auto getFamilies = vkGetPhysicalDeviceQueueFamilyProperties;
        const auto getSupport = vkGetPhysicalDeviceSurfaceSupportKHR;
        vkGetPhysicalDeviceQueueFamilyProperties = [](VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* families)
        {
            *count = 1;
            if (families) {
                families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
                families[0].queueCount = 1;
            }
        };
        vkGetPhysicalDeviceSurfaceSupportKHR = [](VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32* present)
        {
            *present = VK_TRUE;
            return VK_SUCCESS;
        };
        auto families = MyDevice::GetInstance()._GetQueueFamilyIndices(VK_NULL_HANDLE);
        vkGetPhysicalDeviceQueueFamilyProperties = getFamilies;
        vkGetPhysicalDeviceSurfaceSupportKHR = getSupport;
        CHECK_TRUE(families.isComplete() && families.transferFamily == 0,
            "A graphics/compute queue must provide transfer operations without an explicit transfer bit!");
    }

    static void RecordingAndSubmitFailuresPreserveOtherCommands()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        struct NativeRecordingScope
        {
            PFN_vkCreateCommandPool create = vkCreateCommandPool;
            PFN_vkDestroyCommandPool destroy = vkDestroyCommandPool;
            PFN_vkAllocateCommandBuffers allocate = vkAllocateCommandBuffers;
            PFN_vkBeginCommandBuffer begin = vkBeginCommandBuffer;
            PFN_vkEndCommandBuffer end = vkEndCommandBuffer;
            std::optional<uint32_t>& family;
            std::optional<uint32_t> previous;

            explicit NativeRecordingScope(std::optional<uint32_t>& queueFamily)
                : family(queueFamily), previous(std::exchange(family, 0u))
            {
                vkCreateCommandPool = [](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* pool)
                    { *pool = reinterpret_cast<VkCommandPool>(uintptr_t(7)); return VK_SUCCESS; };
                vkDestroyCommandPool = [](VkDevice, VkCommandPool, const VkAllocationCallbacks*) {};
                vkAllocateCommandBuffers = [](VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* buffer)
                {
                    static std::atomic<uintptr_t> next = 30;
                    *buffer = reinterpret_cast<VkCommandBuffer>(++next);
                    return VK_SUCCESS;
                };
                vkBeginCommandBuffer = [](VkCommandBuffer, const VkCommandBufferBeginInfo*) { return VK_SUCCESS; };
                vkEndCommandBuffer = [](VkCommandBuffer) { return VK_SUCCESS; };
                MyTaskScheduler::GetInstance().Create();
            }
            ~NativeRecordingScope()
            {
                MyTaskScheduler::GetInstance().Destroy();
                family = previous;
                vkEndCommandBuffer = end;
                vkBeginCommandBuffer = begin;
                vkAllocateCommandBuffers = allocate;
                vkDestroyCommandPool = destroy;
                vkCreateCommandPool = create;
            }
        } recording(MyDevice::GetInstance().queueFamilyIndices.transferFamily);
        auto context = _Context();
        context->StartFrame();
        VkCommandBuffer first = VK_NULL_HANDLE, last = VK_NULL_HANDLE;
        auto commit = [&](std::function<void(VkCommandBuffer)> record)
        {
            std::vector<CommandBuffer> buffers(1);
            buffers.front().AppendCommand(std::move(record));
            context->CommitCommandsToQueue(QueueFamilyType::TRANSFER, std::move(buffers));
        };
        commit([&](VkCommandBuffer buffer) { first = buffer; });
        commit([](VkCommandBuffer) { throw std::runtime_error("recording failed"); });
        commit([&](VkCommandBuffer buffer) { last = buffer; });
        bool recordingFailed = false;
        try { context->SubmitQueue(QueueFamilyType::TRANSFER, {}); }
        catch (const std::runtime_error& error) { recordingFailed = std::string_view(error.what()) == "recording failed"; }
        CHECK_TRUE(recordingFailed && hooks.state.submitCount == 0,
            "A recording error must reach the caller before GPU submission!");
        hooks.state.nextSubmit = VK_ERROR_OUT_OF_HOST_MEMORY;
        bool submitFailed = false;
        try { context->SubmitQueue(QueueFamilyType::TRANSFER, {}); }
        catch (const std::runtime_error&) { submitFailed = true; }
        const std::vector<VkCommandBuffer> expected{first, last};
        CHECK_TRUE(submitFailed && first && last && hooks.state.lastCommands == expected,
            "The failed recording must leave the other tickets ordered for retry!");
        context->SubmitQueue(QueueFamilyType::TRANSFER, {});
        CHECK_TRUE(hooks.state.lastCommands == expected, "Retry must submit every original native command buffer!");
        context->EndFrame();
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
        hooks.state.nextSubmit = VK_ERROR_OUT_OF_HOST_MEMORY;
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

    static void FailedImmediateWaitRetiresNativeWork()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        hooks.state.nextWait = VK_ERROR_OUT_OF_HOST_MEMORY;
        bool failed = false;
        try
        {
            context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        CHECK_TRUE(failed && hooks.state.submitCount == 1 && hooks.state.waitCount == 1 && hooks.state.statusCount == 2,
            "An immediate fence wait error must preserve the error and resolve native pending work before unwinding!");
        context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, {});
        CHECK_TRUE(destroyed == 1, "A later wait must reclaim the retired submission's resources!");
        hooks.state.expectAlive = false;
    }

    static void LostImmediateSubmitRetiresNativeWork()
    {
        int destroyed = 0;
        HookScope hooks(destroyed);
        auto context = _Context();
        hooks.state.nextSubmit = VK_ERROR_DEVICE_LOST;
        hooks.state.nextWait = VK_ERROR_DEVICE_LOST;
        bool failed = false;
        try
        {
            context->ExecuteCommandsAndWait(QueueFamilyType::TRANSFER, _Buffers(_Resource(destroyed)));
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        CHECK_TRUE(failed && hooks.state.submitCount == 1 && hooks.state.waitCount == 1 &&
            hooks.state.resetCount == 0 && destroyed == 1,
            "Device loss during an immediate submit must retire native work before releasing resources or reusing fences!");
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
    ResourceSubmissionTestProbe::ForeignFenceCannotConsumeMatchingVersion();
    ResourceSubmissionTestProbe::PartiallySubmittedFrameCanEndOnRetry();
    ResourceSubmissionTestProbe::CrossQueueCompletionRequiresProducerFenceSignal();
    ResourceSubmissionTestProbe::DependencyWaitRetryAndResignal();
    ResourceSubmissionTestProbe::SubmittedFenceOwnsOneShotCallbacks();
    ResourceSubmissionTestProbe::UniversalQueueProvidesTransferFallback();
    ResourceSubmissionTestProbe::RecordingAndSubmitFailuresPreserveOtherCommands();
    ResourceSubmissionTestProbe::ExplicitKeepAliveWaitsForSubmissionCompletion();
    ResourceSubmissionTestProbe::FailedImmediateSubmitReleasesResources();
    ResourceSubmissionTestProbe::LostImmediateSubmitRetiresNativeWork();
    ResourceSubmissionTestProbe::FailedImmediateWaitRetiresNativeWork();
}
