#include "my_vulkan/device.h"
#include "clas_buffer_test_fixture.h"
#include "my_vulkan/render_context/private/submission_manager.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <utility>

namespace
{
using namespace std::chrono_literals;

template<class T>
auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }

struct NativeState
{
    int createdPools = 0;
    int destroyedPools = 0;
    int allocatedCommands = 0;
    int createdFences = 0;
    int destroyedFences = 0;
    int submitted = 0;
    bool failAllocation = false;
    bool failSubmission = false;
    bool loseDeviceOnSubmit = false;
    bool failWait = false;
    std::deque<VkResult> statusResults;
    std::atomic_bool pending = false;
    std::promise<void>* enteredWait = nullptr;
    std::shared_future<void> releaseWait;
    std::promise<void>* enteredPresent = nullptr;
    std::future<void> releasePresent;
    std::atomic_bool presenting = false;
    std::atomic_bool overlappingSubmit = false;
};
NativeState* hooks = nullptr;

auto VKAPI_PTR CreatePool(VkDevice device, const VkCommandPoolCreateInfo* info,
    const VkAllocationCallbacks*, VkCommandPool* outPool) -> VkResult
{
    CHECK_TRUE(device == Handle<VkDevice>(1) && info->queueFamilyIndex == 7,
        "Default execution must create its pool on the graphicsAndCompute family!");
    ++hooks->createdPools;
    *outPool = Handle<VkCommandPool>(20);
    return VK_SUCCESS;
}

void VKAPI_PTR DestroyPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks*)
{
    CHECK_TRUE(device == Handle<VkDevice>(1) && pool == Handle<VkCommandPool>(20) && !hooks->pending,
        "Inline pool destruction must use its original Device after GPU completion!");
    ++hooks->destroyedPools;
}

auto VKAPI_PTR Allocate(VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* outCommand) -> VkResult
{
    CHECK_TRUE(info->commandPool == Handle<VkCommandPool>(20) && info->commandBufferCount == 1 &&
        info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY, "Expected one primary inline command buffer!");
    if (std::exchange(hooks->failAllocation, false))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++hooks->allocatedCommands;
    *outCommand = Handle<VkCommandBuffer>(21);
    return VK_SUCCESS;
}

auto VKAPI_PTR CreateFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* fence) -> VkResult
{
    ++hooks->createdFences;
    *fence = Handle<VkFence>(22);
    return VK_SUCCESS;
}

void VKAPI_PTR DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks*)
{
    CHECK_TRUE(device == Handle<VkDevice>(1) && fence == Handle<VkFence>(22) && !hooks->pending,
        "Inline fence destruction must use its original Device after GPU completion!");
    ++hooks->destroyedFences;
}

auto VKAPI_PTR ResetPool(VkDevice, VkCommandPool, VkCommandPoolResetFlags) -> VkResult
{
    CHECK_TRUE(!hooks->pending, "Inline command pool reset before completion!");
    return VK_SUCCESS;
}
auto VKAPI_PTR ResetFences(VkDevice, uint32_t, const VkFence*) -> VkResult
{
    CHECK_TRUE(!hooks->pending, "Inline fence reset before completion!");
    return VK_SUCCESS;
}
auto VKAPI_PTR Begin(VkCommandBuffer command, const VkCommandBufferBeginInfo*) -> VkResult
{
    CHECK_TRUE(!hooks->pending && command == Handle<VkCommandBuffer>(21), "Wrong or still pending command buffer!");
    return VK_SUCCESS;
}
auto VKAPI_PTR End(VkCommandBuffer) -> VkResult { return VK_SUCCESS; }

auto VKAPI_PTR Submit(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence) -> VkResult
{
    if (fence == VK_NULL_HANDLE)
    {
        if (queue == Handle<VkQueue>(10) && hooks->presenting)
            hooks->overlappingSubmit = true;
        return VK_SUCCESS; // Explicit submission.
    }
    CHECK_TRUE(queue == Handle<VkQueue>(10) && count == 1 && submits->commandBufferInfoCount == 1 &&
        submits->pCommandBufferInfos[0].commandBuffer == Handle<VkCommandBuffer>(21) && fence == Handle<VkFence>(22),
        "Expected the recorded buffer on the COMPUTE queue with the private fence!");
    if (std::exchange(hooks->failSubmission, false))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    CHECK_TRUE(!hooks->pending.exchange(true), "Overlapping inline submissions reused a pending fence!");
    ++hooks->submitted;
    if (std::exchange(hooks->loseDeviceOnSubmit, false)) return VK_ERROR_DEVICE_LOST;
    return VK_SUCCESS;
}

auto VKAPI_PTR Wait(VkDevice, uint32_t count, const VkFence* fences, VkBool32, uint64_t timeout) -> VkResult
{
    CHECK_TRUE(count == 1 && *fences == Handle<VkFence>(22) && timeout == UINT64_MAX &&
        hooks->pending, "Waited for work that was never submitted!");
    if (std::exchange(hooks->failWait, false))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (auto* entered = std::exchange(hooks->enteredWait, nullptr))
    {
        entered->set_value();
        CHECK_TRUE(hooks->releaseWait.wait_for(8s) == std::future_status::ready, "Inline test gate timed out!");
    }
    hooks->pending = false;
    return VK_SUCCESS;
}

auto VKAPI_PTR FenceStatus(VkDevice, VkFence fence) -> VkResult
{
    CHECK_TRUE(fence == Handle<VkFence>(22) && hooks->pending, "Checked the wrong pending fence!");
    if (!hooks->statusResults.empty())
    {
        const VkResult result = hooks->statusResults.front();
        hooks->statusResults.pop_front();
        return result;
    }
    hooks->pending = false;
    return VK_SUCCESS;
}
auto VKAPI_PTR Idle(VkDevice) -> VkResult { return VK_SUCCESS; }

auto VKAPI_PTR Present(VkQueue queue, const VkPresentInfoKHR* info) -> VkResult
{
    CHECK_TRUE(queue == Handle<VkQueue>(10) && info->swapchainCount == 1 &&
        *info->pSwapchains == Handle<VkSwapchainKHR>(30) && *info->pImageIndices == 0 &&
        info->waitSemaphoreCount == 1 && *info->pWaitSemaphores == Handle<VkSemaphore>(31),
        "Expected the swapchain image to present on the aliased native queue!");
    hooks->presenting = true;
    hooks->enteredPresent->set_value();
    const auto released = hooks->releasePresent.wait_for(8s);
    hooks->presenting = false;
    CHECK_TRUE(released == std::future_status::ready, "Presentation test gate timed out!");
    return VK_SUCCESS;
}

void VKAPI_PTR DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks*) {}

template<class Fn>
struct ScopedFunction
{
    Fn& target;
    Fn original;
    ScopedFunction(Fn& inTarget, Fn replacement) : target(inTarget), original(std::exchange(target, replacement)) {}
    ~ScopedFunction() { target = original; }
};
}

struct InlineExecutionTestProbe
{
    struct Scope
    {
        NativeState state;
        MyDevice& device = MyDevice::GetInstance();
        VkDevice oldDevice = std::exchange(device.vkDevice, Handle<VkDevice>(1));
        VkQueue graphics = std::exchange(device.m_vkGraphicsQueue, Handle<VkQueue>(10));
        VkQueue compute = std::exchange(device.m_vkComputeQueue, Handle<VkQueue>(10));
        VkQueue transfer = std::exchange(device.m_vkTransferQueue, Handle<VkQueue>(11));
        VkQueue present = std::exchange(device.m_vkPresentQueue, Handle<VkQueue>(12));
        std::optional<uint32_t> family = std::exchange(device.queueFamilyIndices.graphicsAndComputeFamily, 7u);
        ScopedFunction<PFN_vkCreateCommandPool> createPool{vkCreateCommandPool, CreatePool};
        ScopedFunction<PFN_vkDestroyCommandPool> destroyPool{vkDestroyCommandPool, DestroyPool};
        ScopedFunction<PFN_vkAllocateCommandBuffers> allocate{vkAllocateCommandBuffers, Allocate};
        ScopedFunction<PFN_vkCreateFence> createFence{vkCreateFence, CreateFence};
        ScopedFunction<PFN_vkDestroyFence> destroyFence{vkDestroyFence, DestroyFence};
        ScopedFunction<PFN_vkResetCommandPool> resetPool{vkResetCommandPool, ResetPool};
        ScopedFunction<PFN_vkResetFences> resetFence{vkResetFences, ResetFences};
        ScopedFunction<PFN_vkBeginCommandBuffer> begin{vkBeginCommandBuffer, Begin};
        ScopedFunction<PFN_vkEndCommandBuffer> end{vkEndCommandBuffer, End};
        ScopedFunction<PFN_vkQueueSubmit2> submit{vkQueueSubmit2, Submit};
        ScopedFunction<PFN_vkWaitForFences> wait{vkWaitForFences, Wait};
        ScopedFunction<PFN_vkGetFenceStatus> status{vkGetFenceStatus, FenceStatus};
        ScopedFunction<PFN_vkDeviceWaitIdle> idle{vkDeviceWaitIdle, Idle};

        Scope() { hooks = &state; }
        ~Scope()
        {
            device.vkDevice = oldDevice;
            device._DestroyInlineExecution();
            hooks = nullptr;
            device.m_vkGraphicsQueue = graphics;
            device.m_vkComputeQueue = compute;
            device.m_vkTransferQueue = transfer;
            device.m_vkPresentQueue = present;
            device.queueFamilyIndices.graphicsAndComputeFamily = family;
        }
    };

    static void QueueLocks()
    {
        Scope scope;
        SubmissionManager graphics;
        SubmissionManager transfer;
        auto& device = scope.device;
        auto held = device._AcquireQueueHostLock(device.m_vkComputeQueue);
        std::promise<void> started;
        auto shared = std::async(std::launch::async, [&] {
            started.set_value();
            graphics.Submit(QueueFamilyType::GRAPHICS, {});
        });
        started.get_future().wait();
        auto distinct = std::async(std::launch::async, [&] { transfer.Submit(QueueFamilyType::TRANSFER, {}); });
        const bool distinctFinished = distinct.wait_for(3s) == std::future_status::ready;
        const bool sharedBlocked = shared.wait_for(100ms) == std::future_status::timeout;
        held = {};
        shared.get();
        distinct.get();
        CHECK_TRUE(distinctFinished && sharedBlocked, "Aliased COMPUTE/GRAPHICS submissions must serialize while TRANSFER runs independently!");

        auto present = device._AcquireQueueHostLock(device.m_vkPresentQueue);
        auto idle = std::async(std::launch::async, [&] { device.WaitIdle(); });
        const bool idleBlocked = idle.wait_for(100ms) == std::future_status::timeout;
        present = {};
        idle.get();
        CHECK_TRUE(idleBlocked, "Device idle must also exclude a present-only queue!");
    }

    static void PresentationAndSubmissionShareNativeQueueLock()
    {
        Scope scope;
        auto& device = scope.device;
        const ScopedFunction presentQueue(device.m_vkPresentQueue, device.m_vkGraphicsQueue);
        const ScopedFunction swapchain(device.vkSwapchain, Handle<VkSwapchainKHR>(30));
        const ScopedFunction<PFN_vkQueuePresentKHR> nativePresent(vkQueuePresentKHR, Present);
        const ScopedFunction<PFN_vkDestroySemaphore> destroySemaphore(vkDestroySemaphore, DestroySemaphore);
        auto restoreImages = [previous = std::move(device.m_uptrSwapchainImages)](MyDevice* owner) mutable {
            owner->m_uptrSwapchainImages = std::move(previous);
        };
        const std::unique_ptr<MyDevice, decltype(restoreImages)> imageScope(&device, std::move(restoreImages));
        auto ownedImage = std::unique_ptr<SwapchainImage>(new SwapchainImage(0));
        auto& image = *ownedImage;
        image.m_renderFinishedSemaphore = Handle<VkSemaphore>(31);
        image._SetAcquireSemaphore(Handle<VkSemaphore>(32));
        image._CommitAcquireWait();
        device.m_uptrSwapchainImages.push_back(std::move(ownedImage));

        SubmissionManager graphics;
        SubmissionManager transfer;
        std::promise<void> entered;
        std::promise<void> release;
        scope.state.enteredPresent = &entered;
        scope.state.releasePresent = release.get_future();
        auto presenting = std::async(std::launch::async, [&] { device.PresentSwapchainImage(image); });
        const bool presentEntered = entered.get_future().wait_for(3s) == std::future_status::ready;
        std::promise<void> started;
        auto aliased = std::async(std::launch::async, [&] {
            started.set_value();
            graphics.Submit(QueueFamilyType::GRAPHICS, {});
        });
        const bool aliasStarted = started.get_future().wait_for(3s) == std::future_status::ready;
        auto distinct = std::async(std::launch::async, [&] { transfer.Submit(QueueFamilyType::TRANSFER, {}); });
        const bool distinctFinished = distinct.wait_for(3s) == std::future_status::ready;
        const bool aliasBlocked = aliased.wait_for(100ms) == std::future_status::timeout;
        release.set_value();
        presenting.get();
        aliased.get();
        distinct.get();
        CHECK_TRUE(presentEntered && aliasStarted && distinctFinished && aliasBlocked && !scope.state.overlappingSubmit,
            "Submit must not overlap a present on the same native queue, while an independent queue can submit!");
    }

    static void ConcurrentExecution()
    {
        Scope scope;
        auto& device = scope.device;
        SubmissionManager graphics;
        std::promise<void> entered;
        auto enteredFuture = entered.get_future();
        std::promise<void> release;
        scope.state.enteredWait = &entered;
        scope.state.releaseWait = release.get_future().share();
        std::promise<void> recorded;
        auto held = device._AcquireQueueHostLock(device.m_vkGraphicsQueue);
        auto first = std::async(std::launch::async, [&] {
            device._ExecuteInlineAndWait([&](VkCommandBuffer command) {
                CHECK_TRUE(command == Handle<VkCommandBuffer>(21), "Unexpected command buffer during inline recording!");
                recorded.set_value();
            });
        });
        const bool recordedWhileLocked = recorded.get_future().wait_for(3s) == std::future_status::ready;
        const bool submitBlocked = enteredFuture.wait_for(100ms) == std::future_status::timeout;
        held = {};
        const bool waiting = enteredFuture.wait_for(3s) == std::future_status::ready;
        auto explicitSubmit = std::async(std::launch::async, [&] { graphics.Submit(QueueFamilyType::GRAPHICS, {}); });
        std::atomic_bool recordedSecond = false;
        auto second = std::async(std::launch::async, [&] {
            device._ExecuteInlineAndWait([&](VkCommandBuffer) { recordedSecond = true; });
        });
        const bool explicitFinished = explicitSubmit.wait_for(3s) == std::future_status::ready;
        const bool secondBlocked = second.wait_for(100ms) == std::future_status::timeout && !recordedSecond;
        release.set_value();
        first.get();
        second.get();
        explicitSubmit.get();
        CHECK_TRUE(recordedWhileLocked && submitBlocked && waiting && explicitFinished && secondBlocked &&
            recordedSecond && !scope.state.pending && scope.state.submitted == 2 && scope.state.createdPools == 1 &&
            scope.state.allocatedCommands == 1 && scope.state.createdFences == 1,
            "Default executor must serialize reuse across workers, releasing the queue guard during recording and fence waits!");
    }

    static void FailureAndRetry()
    {
        Scope scope;
        auto& device = scope.device;
        auto execute = [&](bool failRecord) {
            device._ExecuteInlineAndWait([&](VkCommandBuffer) {
                if (failRecord) throw std::runtime_error("Injected recording failure");
            });
        };
        auto expectFailure = [&](bool failRecord) {
            bool failed = false;
            try { execute(failRecord); }
            catch (const std::runtime_error&) { failed = true; }
            CHECK_TRUE(failed && !scope.state.pending, "An inline failure must propagate with no submitted work outstanding!");
        };
        scope.state.failAllocation = true;
        expectFailure(false);
        expectFailure(true);
        scope.state.failSubmission = true;
        expectFailure(false);
        scope.state.failWait = true;
        scope.state.statusResults = {VK_NOT_READY, VK_ERROR_OUT_OF_HOST_MEMORY, VK_NOT_READY};
        expectFailure(false);
        CHECK_TRUE(scope.state.statusResults.empty(), "Fence recovery stopped before establishing completion!");
        execute(false);
        device.vkDevice = VK_NULL_HANDLE;
        device._DestroyInlineExecution();
        CHECK_TRUE(scope.state.submitted == 2 && scope.state.destroyedPools == scope.state.createdPools &&
            scope.state.destroyedFences == scope.state.createdFences,
            "Partial initialization and successful retries must release each owned handle on the saved Device!");
    }

    static void DeviceLostDuringSubmit()
    {
        Scope scope;
        scope.state.loseDeviceOnSubmit = true;
        bool failed = false;
        try { scope.device._ExecuteInlineAndWait([](VkCommandBuffer) {}); }
        catch (const std::runtime_error&) { failed = true; }
        // Let the fake GPU complete for teardown even if the caller observed a premature return.
        const bool pendingAtReturn = scope.state.pending.exchange(false);
        CHECK_TRUE(failed && scope.state.submitted == 1 && !pendingAtReturn,
            "Device-lost submission must wait for its pending fence before propagating failure!");
    }
};

void ClasBufferTestFixture::WithInlineExecutor(void (*inTest)())
{
    InlineExecutionTestProbe::Scope execution;
    inTest();
}

void RunInlineExecutionTests()
{
    InlineExecutionTestProbe::QueueLocks();
    InlineExecutionTestProbe::PresentationAndSubmissionShareNativeQueueLock();
    InlineExecutionTestProbe::FailureAndRetry();
    InlineExecutionTestProbe::DeviceLostDuringSubmit();
    InlineExecutionTestProbe::ConcurrentExecution();
}
