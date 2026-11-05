#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/render_target_cache.h"
#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/pipeline/framebuffer_desc.h"
#include "my_vulkan/resource/descriptor.h"
#include "my_vulkan/resource/private/image_storage.h"
#include <algorithm>
#include <barrier>
#include <thread>
#include <type_traits>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

static_assert(std::is_nothrow_copy_constructible_v<ImageView>);
static_assert(std::is_nothrow_move_constructible_v<ImageView>);
static_assert(!std::is_copy_constructible_v<Image>);
static_assert(!std::is_copy_constructible_v<RenderPassDesc>);
static_assert(std::is_same_v<decltype(std::declval<const Image&>().View()), ImageView>);
static_assert(std::is_same_v<decltype(std::declval<ImageCreateInfo&>().SetUsage(0)), void>);
static_assert(std::is_same_v<decltype(std::declval<GraphicsPipelineStateInfo&>().Reset()), void>);
static_assert(noexcept(std::declval<ImageView&>().Reset()));
static_assert(noexcept(std::declval<const ImageView&>().GetResourceId()));
static_assert(std::is_same_v<decltype(std::declval<MyDevice&>().GetOrCreateFramebuffer(
    std::declval<const RenderPassDesc&>(), std::declval<const FramebufferDesc&>())), VkFramebuffer>);

struct ResourceLifetimeTestProbe
{
    template<typename T>
    static auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }
    static void SetImage(Image& inImage, uintptr_t handle, uint32_t width, uint32_t height,
        uint32_t mips, uint32_t layers, VkFormat format, VkImageUsageFlags usage,
        VkSampleCountFlagBits samples)
    {
        CHECK_TRUE(!inImage.IsValid(), "Image fixture must start with an empty owner!");
        auto storage = std::make_unique<VulkanDetail::ImageStorage>();
        storage->device = MyDevice::GetInstance().vkDevice;
        storage->image = Handle<VkImage>(handle);
        storage->ownsNative = true;
        inImage.m_resource = MakeIntrusive<VulkanDetail::ImageResource>(std::move(storage));
        auto& info = inImage.m_imageInformation;
        info.width = width; info.height = height; info.depth = 1;
        info.mipLevels = mips; info.arrayLayers = layers;
        info.format = format; info.usage = usage; info.samples = samples;
    }
    static auto ExchangeAllocator(std::unique_ptr<VulkanDetail::RenderTargetCache> replacement)
        -> std::unique_ptr<VulkanDetail::RenderTargetCache>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrRenderTargetCache;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static void ClearAllocator() { MyDevice::GetInstance().m_uptrRenderTargetCache->Destroy(); }
    static void ImportSwapchain(Image& inImage, uintptr_t handle,
        const IntrusivePtr<VulkanDetail::SwapchainResource>& owner)
    {
        inImage._ImportSwapchain(Handle<VkImage>(handle), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_FORMAT_R8G8B8A8_UNORM, {64, 32}, owner);
    }
    static auto Snapshot(const RenderPassDesc& pass) -> VulkanDetail::RenderPassSnapshot { return pass._GetSnapshot(); }
    static auto CacheSize(const Image& inImage) -> size_t { return inImage.m_imageViews.size(); }
    static void Append(CommandBuffer& destination, CommandBuffer&& source) { destination._Append(std::move(source)); }
    static void Record(const CommandBuffer& commands)
    {
        for (const Command* command : commands.m_commands) command->Record(Handle<VkCommandBuffer>(1));
    }
    static void Prepare(CommandBuffer& commands) { commands._PrepareRenderPassCommands(); }
};

namespace
{
#if defined(_MSC_VER) && defined(_DEBUG)
thread_local bool failNextFramebufferCacheAllocation = false;
auto __cdecl FailFramebufferCacheAllocation(int type, void*, size_t size, int block, long,
    const unsigned char*, int) -> int
{
    if (type == _HOOK_ALLOC && block != _CRT_BLOCK && size != sizeof(std::_Container_proxy) &&
        failNextFramebufferCacheAllocation)
    {
        failNextFramebufferCacheAllocation = false;
        return 0;
    }
    return 1;
}
struct HostAllocationFailureScope final
{
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(FailFramebufferCacheAllocation);
    ~HostAllocationFailureScope()
    {
        failNextFramebufferCacheAllocation = false;
        _CrtSetAllocHook(previous);
    }
};
#endif
struct NativeState
{
    uintptr_t nextView = 1000, nextPass = 2000, nextFramebuffer = 3000;
    uint32_t imageCreates = 0, viewCreates = 0, framebufferCreates = 0, passCreates = 0;
    bool failImage = false, failView = false, failPass = false, failFramebuffer = false;
    bool failFramebufferPublication = false;
    std::set<VkRenderPass> livePasses;
    std::set<VkFramebuffer> liveFramebuffers;
    std::vector<uintptr_t> destroyed;
    std::vector<VkImageView> framebufferAttachments;
    VkFramebufferCreateInfo framebufferInfo{};
    VkRenderPass beginPass = VK_NULL_HANDLE;
    VkFramebuffer beginFramebuffer = VK_NULL_HANDLE;
    uint32_t beginClearCount = 0, beginCalls = 0, endCalls = 0;
};
NativeState* nativeState = nullptr;

auto VKAPI_PTR CreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*) -> VkResult
{
    ++nativeState->imageCreates;
    CHECK_TRUE(nativeState->failImage, "A successful image test must avoid real memory allocation!");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
void VKAPI_PTR DestroyImage(VkDevice, VkImage value, const VkAllocationCallbacks*)
{
    nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value));
}
auto VKAPI_PTR CreateView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* output) -> VkResult
{
    ++nativeState->viewCreates;
    if (nativeState->failView) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = ResourceLifetimeTestProbe::Handle<VkImageView>(nativeState->nextView++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyView(VkDevice, VkImageView value, const VkAllocationCallbacks*)
{
    nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value));
}
auto VKAPI_PTR CreatePass(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass* output) -> VkResult
{
    ++nativeState->passCreates;
    if (nativeState->failPass) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = ResourceLifetimeTestProbe::Handle<VkRenderPass>(nativeState->nextPass++);
    nativeState->livePasses.insert(*output);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyPass(VkDevice, VkRenderPass value, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->livePasses.erase(value) == 1, "Each cached native render pass must be destroyed exactly once!");
    nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value));
}
auto VKAPI_PTR CreateFramebuffer(VkDevice, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks*, VkFramebuffer* output) -> VkResult
{
    ++nativeState->framebufferCreates;
    nativeState->framebufferInfo = *info;
    nativeState->framebufferInfo.pAttachments = nullptr;
    nativeState->framebufferAttachments.clear();
    for (uint32_t index = 0; index < info->attachmentCount; ++index)
        nativeState->framebufferAttachments.push_back(info->pAttachments[index]);
    if (nativeState->failFramebuffer) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = ResourceLifetimeTestProbe::Handle<VkFramebuffer>(nativeState->nextFramebuffer++);
    nativeState->liveFramebuffers.insert(*output);
#if defined(_MSC_VER) && defined(_DEBUG)
    failNextFramebufferCacheAllocation = nativeState->failFramebufferPublication;
#endif
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyFramebuffer(VkDevice, VkFramebuffer value, const VkAllocationCallbacks*)
{
    CHECK_TRUE(nativeState->liveFramebuffers.erase(value) == 1, "Each cached native framebuffer must be destroyed exactly once!");
    nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value));
}
void VKAPI_PTR BeginPass(VkCommandBuffer, const VkRenderPassBeginInfo* info, VkSubpassContents)
{
    nativeState->beginPass = info->renderPass;
    nativeState->beginFramebuffer = info->framebuffer;
    nativeState->beginClearCount = info->clearValueCount;
    ++nativeState->beginCalls;
}
void VKAPI_PTR EndPass(VkCommandBuffer) { ++nativeState->endCalls; }

// Declare this before tested owners so native cleanup always occurs under the hooks.
struct NativeEnvironment
{
    NativeState state;
    PFN_vkCreateImage previousImage = vkCreateImage;
    PFN_vkDestroyImage previousDestroyImage = vkDestroyImage;
    PFN_vkCreateImageView previousView = vkCreateImageView;
    PFN_vkDestroyImageView previousDestroyView = vkDestroyImageView;
    PFN_vkCreateRenderPass previousPass = vkCreateRenderPass;
    PFN_vkDestroyRenderPass previousDestroyPass = vkDestroyRenderPass;
    PFN_vkCreateFramebuffer previousFramebuffer = vkCreateFramebuffer;
    PFN_vkDestroyFramebuffer previousDestroyFramebuffer = vkDestroyFramebuffer;
    PFN_vkCmdBeginRenderPass previousBegin = vkCmdBeginRenderPass;
    PFN_vkCmdEndRenderPass previousEnd = vkCmdEndRenderPass;
    std::unique_ptr<VulkanDetail::RenderTargetCache> previousAllocator;
    VkDevice previousDevice = MyDevice::GetInstance().vkDevice;
    NativeEnvironment()
    {
        nativeState = &state;
        vkCreateImage = CreateImage; vkDestroyImage = DestroyImage;
        vkCreateImageView = CreateView; vkDestroyImageView = DestroyView;
        vkCreateRenderPass = CreatePass; vkDestroyRenderPass = DestroyPass;
        vkCreateFramebuffer = CreateFramebuffer; vkDestroyFramebuffer = DestroyFramebuffer;
        vkCmdBeginRenderPass = BeginPass; vkCmdEndRenderPass = EndPass;
        MyDevice::GetInstance().vkDevice = ResourceLifetimeTestProbe::Handle<VkDevice>(1);
        auto allocator = std::make_unique<VulkanDetail::RenderTargetCache>();
        allocator->Create(ResourceLifetimeTestProbe::Handle<VkDevice>(1));
        previousAllocator = ResourceLifetimeTestProbe::ExchangeAllocator(std::move(allocator));
    }
    ~NativeEnvironment()
    {
        auto allocator = ResourceLifetimeTestProbe::ExchangeAllocator(std::move(previousAllocator));
        allocator.reset();
        MyDevice::GetInstance().vkDevice = previousDevice;
        vkCreateImage = previousImage; vkDestroyImage = previousDestroyImage;
        vkCreateImageView = previousView; vkDestroyImageView = previousDestroyView;
        vkCreateRenderPass = previousPass; vkDestroyRenderPass = previousDestroyPass;
        vkCreateFramebuffer = previousFramebuffer; vkDestroyFramebuffer = previousDestroyFramebuffer;
        vkCmdBeginRenderPass = previousBegin; vkCmdEndRenderPass = previousEnd;
        nativeState = nullptr;
    }
    NativeEnvironment(const NativeEnvironment&) = delete;
    auto operator=(const NativeEnvironment&) -> NativeEnvironment& = delete;
};

template<typename Function>
void ExpectFailure(Function&& function)
{
    bool failed = false;
    try { function(); } catch (const std::runtime_error&) { failed = true; }
    CHECK_TRUE(failed, "Invalid resource operation must fail!");
}
auto WasDestroyed(uintptr_t handle) -> bool
{
    return std::find(nativeState->destroyed.begin(), nativeState->destroyed.end(), handle) != nativeState->destroyed.end();
}
void FakeImage(Image& inImage, uintptr_t handle, uint32_t width = 128, uint32_t height = 64,
    uint32_t mips = 4, uint32_t layers = 4, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
{
    ResourceLifetimeTestProbe::SetImage(inImage, handle, width, height, mips, layers, format, usage, samples);
}
auto SingleMip(uint32_t mip = 0, uint32_t baseLayer = 0, uint32_t layers = VK_REMAINING_ARRAY_LAYERS) -> ImageViewInfo
{
    ImageViewInfo info;
    info.CustomizeMipLevels(mip, 1);
    info.CustomizeArrayLayers(baseLayer, layers);
    return info;
}
void CreateColorPass(RenderPassDesc& info, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
    VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_CLEAR, uint32_t attachmentCount = 1)
{
    SubpassDescription subpass;
    for (uint32_t index = 0; index < attachmentCount; ++index)
    {
        AttachmentDescription attachment;
        VkClearValue clear{};
        clear.color.float32[0] = 0.25f;
        attachment.CustomizeFormat(format, clear);
        attachment.CustomizeLoadOperation(load);
        const auto name = std::to_string(index);
        info.AddAttachment(name, attachment);
        subpass.AddColorAttachment(index, name);
    }
    info.AddSubpass("main", subpass);
    info.Finalize();
}

void TestNativeResourceCopiesAndParentReleaseOrder()
{
    using Resource = VulkanDetail::VulkanHandleResource<uint64_t>;
    std::vector<uint64_t> destroyed;
    auto parent = MakeIntrusive<Resource>(11, [&destroyed](uint64_t handle) { destroyed.push_back(handle); });
    std::vector<VulkanDetail::ResourceReference> dependencies{ parent };
    auto child = MakeIntrusive<Resource>(22, [&destroyed](uint64_t handle) { destroyed.push_back(handle); },
        std::move(dependencies));
    auto copy = child;
    CHECK_TRUE(copy->GetHandle() == 22, "A native resource copy must preserve its handle!");
    parent.reset();
    child.reset();
    CHECK_TRUE(destroyed.empty(), "A surviving child copy must retain its native parent!");
    copy.reset();
    CHECK_TRUE(destroyed == std::vector<uint64_t>({22, 11}), "Native children must be destroyed before parent dependencies, exactly once!");
}

void TestConcurrentImageViewResourceIdentityAllocation()
{
    constexpr size_t workerCount = 8;
    constexpr size_t resourcesPerWorker = 64;
    std::vector<uint64_t> identities(workerCount * resourcesPerWorker);
    std::vector<std::exception_ptr> failures(workerCount);
    std::barrier start(static_cast<ptrdiff_t>(workerCount));
    std::vector<std::thread> workers;
    for (size_t worker = 0; worker < workerCount; ++worker)
        workers.emplace_back([&, worker]
        {
            start.arrive_and_wait();
            try
            {
                for (size_t index = 0; index < resourcesPerWorker; ++index)
                {
                    auto resource = MakeIntrusive<VulkanDetail::ImageViewResource>(
                        ResourceLifetimeTestProbe::Handle<VkImageView>(1), std::function<void(VkImageView)>{});
                    identities[worker * resourcesPerWorker + index] = resource->GetResourceId();
                }
            }
            catch (...) { failures[worker] = std::current_exception(); }
        });
    for (auto& worker : workers) worker.join();
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    std::sort(identities.begin(), identities.end());
    CHECK_TRUE(identities.front() != 0 && std::adjacent_find(identities.begin(), identities.end()) == identities.end(),
        "Independent resources created concurrently must receive unique nonzero identities even with identical native handles!");
}

void TestImageViewCacheSnapshotsAndFailures()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 10);
    auto view = image->View(SingleMip(1, 1, 3));
    auto copy = image->View(SingleMip(1, 1));
    CHECK_TRUE(view == copy && environment.state.viewCreates == 1, "Resolved image view ranges must share the strong cache!");
    CHECK_TRUE(view.GetResourceId() != 0 && view.GetResourceId() == copy.GetResourceId(),
        "ImageView copies and exact cache hits must retain the same nonzero native resource identity!");
    {
        ImageView empty;
        CHECK_TRUE(empty.GetResourceId() == 0, "An empty ImageView must have no resource identity!");
        auto moved = std::move(copy);
        CHECK_TRUE(moved.GetResourceId() == view.GetResourceId() && copy.GetResourceId() == 0,
            "Move construction must transfer ImageView identity and clear the source identity!");
        copy = moved;
        empty = std::move(moved);
        CHECK_TRUE(copy.GetResourceId() == view.GetResourceId() && empty.GetResourceId() == view.GetResourceId() &&
            moved.GetResourceId() == 0, "Copy and move assignment must preserve the resource identity!");
        empty.Reset();
        CHECK_TRUE(empty.GetResourceId() == 0 && copy.GetResourceId() == view.GetResourceId(),
            "Reset must clear only the reset wrapper's identity!");
    }
    const auto oldHandle = reinterpret_cast<uintptr_t>(view.GetVkImageView());
    CHECK_TRUE(view.GetImageViewInformation().width == 128 && view.GetViewSize().width == 64 &&
        view.GetViewSize().height == 32 && view.GetImageViewInformation().layerCount == 3,
        "View snapshots must preserve mip-zero size and resolve selected mip/layer dimensions!");
    image->ClearViewCache();
    CHECK_TRUE(!WasDestroyed(oldHandle) && !WasDestroyed(10), "Clearing the cache must preserve external view owners!");
    auto replacement = image->View(SingleMip(1, 1, 3));
    CHECK_TRUE(!(replacement == view) && replacement.GetResourceId() != view.GetResourceId(),
        "A cleared cache must create a new native view version and resource identity!");
    const auto cacheSize = ResourceLifetimeTestProbe::CacheSize(*image);
    const auto createCount = environment.state.viewCreates;
    for (const auto info : {SingleMip(0, 0, 0), SingleMip(0, UINT32_MAX, 1), SingleMip(UINT32_MAX), SingleMip(0, 1, UINT32_MAX - 1)})
        ExpectFailure([&] { (void)image->View(info); });
    ImageViewInfo zeroMips;
    zeroMips.CustomizeMipLevels(0, 0);
    ExpectFailure([&] { (void)image->View(zeroMips); });
    ImageViewInfo overflowMips;
    overflowMips.CustomizeMipLevels(1, UINT32_MAX - 1);
    ExpectFailure([&] { (void)image->View(overflowMips); });
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(*image) == cacheSize && environment.state.viewCreates == createCount &&
        image->View(SingleMip(1, 1, 3)) == replacement, "Rejected ranges must leave the image cache and current version unchanged!");
    environment.state.failView = true;
    ExpectFailure([&] { (void)image->View(SingleMip(2)); });
    environment.state.failView = false;
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(*image) == cacheSize, "Failed native view creation must not enter the cache!");
    ImageCreateInfo createInfo;
    createInfo.CustomizeSize2D(16, 16);
    createInfo.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
    const auto imageCreates = environment.state.imageCreates;
    ExpectFailure([&] { image->Create(createInfo); });
    CHECK_TRUE(image->GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(10) &&
        image->View(SingleMip(1, 1, 3)) == replacement && environment.state.imageCreates == imageCreates,
        "Repeated image creation must reject before native work and preserve the image and cache!");
    image.reset();
    auto rebuilt = std::make_unique<Image>();
    FakeImage(*rebuilt, 11, 32, 16, 1, 1);
    CHECK_TRUE(view.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(10) &&
        view.GetImageViewInformation().width == 128 && view.GetImageViewInformation().baseMipLevel == 1 &&
        rebuilt->GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(11),
        "A new image owner must not mutate surviving view snapshots!");
    rebuilt.reset();
    CHECK_TRUE(!image && !WasDestroyed(10), "Image destruction must preserve surviving native view versions!");
    copy.Reset();
    replacement.Reset();
    CHECK_TRUE(!WasDestroyed(10), "The final external view must retain its native image!");
    view.Reset();
    CHECK_TRUE(WasDestroyed(oldHandle) && WasDestroyed(10) && !view.IsValid() && view.GetVkImage() == VK_NULL_HANDLE &&
        view.GetResourceId() == 0,
        "Reset must release the final view and then its image parent!");
}

void TestFramebufferInferenceValidationAndFailure()
{
    NativeEnvironment environment;
    Image first, second;
    FakeImage(first, 20, 128, 64, 4, 4);
    FakeImage(second, 21, 100, 100, 1, 2);
    RenderPassDesc pass;
    CreateColorPass(pass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR, 2);
    FramebufferDesc info;
    info.SetImageView("0", first.View(SingleMip(1, 1, 3)));
    info.SetImageView("1", second.View(SingleMip()));
    auto& device = MyDevice::GetInstance();
    const auto framebuffer = device.GetOrCreateFramebuffer(pass, info);
    CHECK_TRUE(framebuffer != VK_NULL_HANDLE && environment.state.framebufferInfo.width == 64 &&
        environment.state.framebufferInfo.height == 32 && environment.state.framebufferInfo.layers == 2,
        "Framebuffer inference must use minimum selected-mip dimensions and attachment layer counts!");
    CHECK_TRUE(device.GetOrCreateFramebuffer(pass, info) == framebuffer && environment.state.framebufferCreates == 1,
        "Equivalent framebuffer requests must share the Device cache!");
    const auto creates = environment.state.framebufferCreates;
    const auto rejectView = [&](const ImageView& view)
    {
        FramebufferDesc invalid = info;
        invalid.SetImageView("0", view);
        ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, invalid); });
    };
    rejectView(first.View());
    Image wrongUsage, wrongFormat, wrongSamples;
    FakeImage(wrongUsage, 22, 128, 64, 1, 4, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    FakeImage(wrongFormat, 23, 128, 64, 1, 4, VK_FORMAT_B8G8R8A8_UNORM);
    FakeImage(wrongSamples, 24, 128, 64, 1, 4, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SAMPLE_COUNT_4_BIT);
    rejectView(wrongUsage.View(SingleMip()));
    rejectView(wrongFormat.View(SingleMip()));
    rejectView(wrongSamples.View(SingleMip()));
    FramebufferDesc oversized = info;
    oversized.CustomizeSize(65, 32, 2);
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, oversized); });
    oversized = info;
    oversized.CustomizeSize(64, 32, 3);
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, oversized); });
    CHECK_TRUE(environment.state.framebufferCreates == creates && device.GetOrCreateFramebuffer(pass, info) == framebuffer,
        "Framebuffer validation failures must not call Vulkan or change the cache!");
    FramebufferDesc smaller = info;
    smaller.CustomizeSize(16, 16, 1);
    environment.state.failFramebuffer = true;
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, smaller); });
    environment.state.failFramebuffer = false;
    CHECK_TRUE(device.GetOrCreateFramebuffer(pass, info) == framebuffer && environment.state.framebufferCreates == creates + 1,
        "Failed native framebuffer creation must preserve cached versions!");
    const auto smallerFramebuffer = device.GetOrCreateFramebuffer(pass, smaller);
    CHECK_TRUE(smallerFramebuffer != framebuffer && environment.state.framebufferCreates == creates + 2 &&
        environment.state.framebufferInfo.width == 16 && environment.state.framebufferInfo.height == 16 &&
        environment.state.framebufferInfo.layers == 1,
        "Retry must preserve an explicit framebuffer extent and publish it only after native success!");
    FramebufferDesc shorter = info;
    shorter.CustomizeSize(16, 15, 1);
    FramebufferDesc wider = info;
    wider.CustomizeSize(17, 16, 1);
    FramebufferDesc layered = smaller;
    layered.CustomizeSize(16, 16, 2);
    CHECK_TRUE(device.GetOrCreateFramebuffer(pass, shorter) != smallerFramebuffer &&
        device.GetOrCreateFramebuffer(pass, wider) != smallerFramebuffer &&
        device.GetOrCreateFramebuffer(pass, layered) != smallerFramebuffer,
        "Framebuffer cache identity must include width, height and layer count!");
    RenderPassDesc loadPass;
    CreateColorPass(loadPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD, 2);
    CHECK_TRUE(device.GetOrCreateFramebuffer(loadPass, info) != framebuffer,
        "Framebuffer cache identity must include its exact native render pass!");
    RenderPassDesc emptyPass;
    CreateColorPass(emptyPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR, 0);
    FramebufferDesc emptyInfo;
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(emptyPass, emptyInfo); });
    emptyInfo.CustomizeSize(17, 19, 2);
    CHECK_TRUE(device.GetOrCreateFramebuffer(emptyPass, emptyInfo) != VK_NULL_HANDLE &&
        environment.state.framebufferInfo.width == 17 && environment.state.framebufferInfo.height == 19 &&
        environment.state.framebufferInfo.layers == 2,
        "An attachmentless framebuffer must support explicit dimensions and layers!");
}

void TestFramebufferCreationDependencies()
{
    NativeEnvironment environment;
    auto& device = MyDevice::GetInstance();
    RenderPassDesc pass;
    pass.AddSubpass("main", SubpassDescription{});
    FramebufferDesc info;
    info.CustomizeSize(16, 16);
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, info); });
    pass.Finalize();
    const auto nativeDevice = device.vkDevice;
    device.vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, info); });
    device.vkDevice = nativeDevice;
    auto cache = ResourceLifetimeTestProbe::ExchangeAllocator({});
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, info); });
    ResourceLifetimeTestProbe::ExchangeAllocator(std::make_unique<VulkanDetail::RenderTargetCache>());
    ExpectFailure([&] { (void)device.GetOrCreateFramebuffer(pass, info); });
    ResourceLifetimeTestProbe::ExchangeAllocator(std::move(cache));
    CHECK_TRUE(environment.state.passCreates == 0 && environment.state.framebufferCreates == 0 && pass.IsFinalized(),
        "Unfinalized descriptions and unavailable Device cache dependencies must fail before native render target creation!");
    CHECK_TRUE(device.GetOrCreateFramebuffer(pass, info) != VK_NULL_HANDLE && environment.state.passCreates == 1 &&
        environment.state.framebufferCreates == 1,
        "Restoring dependencies must allow the same framebuffer description to be retried!");
}

void TestRenderTargetCacheTeardownReleasesAllNativeObjects()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 90, 32, 32, 1, 1);
    RenderPassDesc pass, loadPass;
    CreateColorPass(pass);
    CreateColorPass(loadPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD);
    FramebufferDesc info;
    info.SetImageView("0", image->View(SingleMip()));
    FramebufferDesc smaller = info;
    smaller.CustomizeSize(16, 16);
    auto& device = MyDevice::GetInstance();
    (void)device.GetOrCreateFramebuffer(pass, info);
    (void)device.GetOrCreateFramebuffer(pass, smaller);
    (void)device.GetOrCreateFramebuffer(loadPass, info);
    CHECK_TRUE(environment.state.livePasses.size() == 2 && environment.state.liveFramebuffers.size() == 3,
        "Teardown coverage must begin with multiple native render passes and framebuffers!");
    ResourceLifetimeTestProbe::ClearAllocator();
    CHECK_TRUE(environment.state.livePasses.empty() && environment.state.liveFramebuffers.empty() &&
        environment.state.destroyed.size() == 5,
        "Explicit Device cache teardown must release every cached native render target exactly once!");
    const auto& destroyed = environment.state.destroyed;
    CHECK_TRUE(std::all_of(destroyed.begin(), destroyed.begin() + 3,
        [](uintptr_t handle) { return handle >= 3000 && handle < 4000; }) &&
        std::all_of(destroyed.begin() + 3, destroyed.end(),
        [](uintptr_t handle) { return handle >= 2000 && handle < 3000; }),
        "Cache teardown must destroy all native framebuffers before destroying any native render pass!");
    ResourceLifetimeTestProbe::ClearAllocator();
    CHECK_TRUE(environment.state.destroyed.size() == 5, "Repeated cache teardown must not release native handles twice!");
}

void TestRenderPassSnapshotsCompatibilityAndBorrowedCommands()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 30, 80, 40, 1, 1);
    auto clearPass = std::make_unique<RenderPassDesc>();
    auto loadPass = std::make_unique<RenderPassDesc>();
    auto differentPass = std::make_unique<RenderPassDesc>();
    CreateColorPass(*clearPass);
    CreateColorPass(*loadPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD);
    CreateColorPass(*differentPass, VK_FORMAT_B8G8R8A8_UNORM);
    auto snapshot = ResourceLifetimeTestProbe::Snapshot(*clearPass);
    GraphicsPipelineStateInfo graphicsState;
    graphicsState.SetRenderPassSubpass(*clearPass);
    auto stateCopy = graphicsState;
    graphicsState.Reset();
    CHECK_TRUE(snapshot.IsCompatibleWith(ResourceLifetimeTestProbe::Snapshot(*loadPass)) &&
        !snapshot.IsCompatibleWith(ResourceLifetimeTestProbe::Snapshot(*differentPass)),
        "Render pass compatibility must accept CLEAR/LOAD variants and reject changed attachment formats!");
    FramebufferDesc info;
    info.SetImageView("0", image->View(SingleMip()));
    CommandBuffer commands;
    commands.BeginRenderPass(*clearPass, info, 0, 0, 80, 40);
    commands.EndRenderPass();
    CHECK_TRUE(environment.state.passCreates == 0 && environment.state.framebufferCreates == 0,
        "Queued render pass commands and Graphics state must copy CPU metadata without native creation!");
    CommandBuffer moved(std::move(commands));
    CommandBuffer appended;
    ResourceLifetimeTestProbe::Append(appended, std::move(moved));
    AttachmentDescription extra;
    extra.CustomizeFormat(VK_FORMAT_R16G16B16A16_SFLOAT, VkClearValue{});
    clearPass->AddAttachment("later", extra);
    clearPass.reset(); info.Reset(); loadPass.reset(); differentPass.reset();
    CHECK_TRUE(snapshot.GetSubpassCount() == 1 && snapshot.GetClearValues().size() == 1,
        "CPU snapshots must retain immutable metadata after source mutation and destruction!");
    ResourceLifetimeTestProbe::Prepare(appended);
    const auto passHandle = ResourceLifetimeTestProbe::Handle<VkRenderPass>(environment.state.nextPass - 1);
    const auto framebufferHandle = ResourceLifetimeTestProbe::Handle<VkFramebuffer>(environment.state.nextFramebuffer - 1);
    ResourceLifetimeTestProbe::Record(appended);
    CHECK_TRUE(environment.state.beginCalls == 1 && environment.state.endCalls == 1 && environment.state.beginPass == passHandle &&
        environment.state.beginFramebuffer == framebufferHandle && environment.state.beginClearCount == 1,
        "Moved and appended commands must resolve borrowed attachments using their immutable CPU snapshots!");
    appended = {};
    CHECK_TRUE(!WasDestroyed(reinterpret_cast<uintptr_t>(framebufferHandle)) && !WasDestroyed(reinterpret_cast<uintptr_t>(passHandle)),
        "Discarding queued commands must leave Device-cached native render targets alive!");
    ResourceLifetimeTestProbe::ClearAllocator();
    CHECK_TRUE(WasDestroyed(reinterpret_cast<uintptr_t>(framebufferHandle)) && WasDestroyed(reinterpret_cast<uintptr_t>(passHandle)) &&
        snapshot.GetSubpassCount() == 1 && snapshot.GetClearValues().size() == 1,
        "Cache teardown must release native objects independently of surviving CPU snapshots and Graphics state!");
    const auto destroyedCount = environment.state.destroyed.size();
    snapshot = {}; stateCopy.Reset(); ResourceLifetimeTestProbe::ClearAllocator();
    CHECK_TRUE(environment.state.destroyed.size() == destroyedCount,
        "Snapshot release and repeated cache teardown must not release native objects twice!");
    const auto& destroyed = environment.state.destroyed;
    CHECK_TRUE(std::find(destroyed.begin(), destroyed.end(), reinterpret_cast<uintptr_t>(framebufferHandle)) <
        std::find(destroyed.begin(), destroyed.end(), reinterpret_cast<uintptr_t>(passHandle)),
        "Device cache teardown must destroy native framebuffers before native render passes!");
}

void TestDescriptorBindingsOwnTemporaryAndCopiedViews()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 40, 32, 32, 1, 1);
    DescriptorSetDesc binding;
    binding.SetImage(0, image->View(SingleMip()));
    const auto viewHandle = reinterpret_cast<uintptr_t>(image->View(SingleMip()).GetVkImageView());
    auto copy = binding;
    ShaderResources named;
    named.SetImage("texture", image->View(SingleMip()));
    auto namedCopy = named;
    binding = {}; named.Clear(); image.reset();
    CHECK_TRUE(!WasDestroyed(viewHandle) && !WasDestroyed(40), "Descriptor copies must own temporary image-view values!");
    copy = {};
    CHECK_TRUE(!WasDestroyed(viewHandle), "Shader resource copies must independently retain their view!");
    namedCopy.Clear();
    CHECK_TRUE(WasDestroyed(viewHandle) && WasDestroyed(40), "Releasing descriptor snapshots must release their view and image!");
}

void TestFramebufferDoesNotRetainAttachment()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 70, 32, 32, 1, 1);
    RenderPassDesc pass;
    CreateColorPass(pass);
    FramebufferDesc info;
    auto view = image->View(SingleMip());
    const auto viewHandle = reinterpret_cast<uintptr_t>(view.GetVkImageView());
    info.SetImageView("0", view);
    auto copiedInfo = info;
    const auto framebuffer = MyDevice::GetInstance().GetOrCreateFramebuffer(pass, info);
    CommandBuffer commands;
    commands.BeginRenderPass(pass, info, 0, 0, 32, 32);
    commands.EndRenderPass();
    view.Reset();
    image.reset();
    CHECK_TRUE(WasDestroyed(viewHandle) && WasDestroyed(70) &&
        !WasDestroyed(reinterpret_cast<uintptr_t>(framebuffer)),
        "Framebuffer descriptions, copies, native cache entries and queued commands must not retain borrowed attachments!");
    // Discard borrowed commands without preparation or recording after their owners die.
    commands = {}; info.Reset(); copiedInfo.Reset();
}

void TestFramebufferIdentitySurvivesNativeHandleReuse()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 71, 32, 32, 1, 1);
    RenderPassDesc pass;
    CreateColorPass(pass);
    auto oldView = image->View(SingleMip());
    const auto rawView = reinterpret_cast<uintptr_t>(oldView.GetVkImageView());
    const auto oldId = oldView.GetResourceId();
    FramebufferDesc oldInfo;
    oldInfo.SetImageView("0", oldView);
    const auto oldFramebuffer = MyDevice::GetInstance().GetOrCreateFramebuffer(pass, oldInfo);
    oldInfo.Reset(); oldView.Reset(); image.reset();
    image = std::make_unique<Image>();
    FakeImage(*image, 72, 32, 32, 1, 1);
    environment.state.nextView = rawView;
    auto newView = image->View(SingleMip());
    FramebufferDesc newInfo;
    newInfo.SetImageView("0", newView);
    const auto newFramebuffer = MyDevice::GetInstance().GetOrCreateFramebuffer(pass, newInfo);
    CHECK_TRUE(newView.GetVkImageView() == ResourceLifetimeTestProbe::Handle<VkImageView>(rawView) &&
        newView.GetResourceId() != 0 && newView.GetResourceId() != oldId && newFramebuffer != oldFramebuffer &&
        environment.state.framebufferCreates == 2,
        "A new native resource instance must miss stale framebuffer cache entries even when Vulkan reuses its numeric handle!");
}

void TestExplicitCommandKeepAliveRetainsAttachments()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 73, 32, 32, 1, 1);
    auto view = image->View(SingleMip());
    const auto viewHandle = reinterpret_cast<uintptr_t>(view.GetVkImageView());
    RenderPassDesc pass;
    CreateColorPass(pass);
    FramebufferDesc info;
    info.SetImageView("0", view);
    CommandBuffer commands;
    commands.KeepAlive(view);
    commands.BeginRenderPass(pass, info, 0, 0, 32, 32);
    commands.EndRenderPass();
    view.Reset(); info.Reset(); image.reset();
    CHECK_TRUE(!WasDestroyed(viewHandle) && !WasDestroyed(73),
        "Explicit ImageView KeepAlive must retain its native image and view while commands own it!");
    ResourceLifetimeTestProbe::Prepare(commands);
    ResourceLifetimeTestProbe::Record(commands);
    CHECK_TRUE(environment.state.beginCalls == 1,
        "Explicitly retained borrowed attachments must remain valid through preparation and recording!");
    commands = {};
    CHECK_TRUE(WasDestroyed(viewHandle) && WasDestroyed(73),
        "Discarding commands must release their explicit attachment ownership!");

    image = std::make_unique<Image>();
    FakeImage(*image, 74, 16, 16, 1, 1);
    CommandBuffer imageOwner;
    imageOwner.KeepAlive(*image);
    image.reset();
    CHECK_TRUE(!WasDestroyed(74), "Explicit Image KeepAlive must retain its native image!");
    imageOwner = {};
    CHECK_TRUE(WasDestroyed(74), "Discarding explicit Image KeepAlive must release the native image!");
}

void TestFramebufferPublicationFailureAndRetry()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 75, 32, 32, 1, 1);
    RenderPassDesc pass;
    CreateColorPass(pass);
    FramebufferDesc info;
    info.SetImageView("0", image->View(SingleMip()));
    auto& device = MyDevice::GetInstance();
    const auto existing = device.GetOrCreateFramebuffer(pass, info);
    FramebufferDesc smaller = info;
    smaller.CustomizeSize(16, 16);
    environment.state.failFramebufferPublication = true;
    bool failed = false;
    {
        HostAllocationFailureScope failureScope;
        try { (void)device.GetOrCreateFramebuffer(pass, smaller); }
        catch (const std::bad_alloc&) { failed = true; }
    }
    const auto unpublished = environment.state.nextFramebuffer - 1;
    CHECK_TRUE(failed && WasDestroyed(unpublished) && !WasDestroyed(reinterpret_cast<uintptr_t>(existing)) &&
        environment.state.passCreates == 1 && environment.state.framebufferCreates == 2,
        "Framebuffer map publication failure must destroy only its new handle and retain the cached render pass and framebuffer!");
    environment.state.failFramebufferPublication = false;
    const auto retried = device.GetOrCreateFramebuffer(pass, smaller);
    CHECK_TRUE(retried != existing && device.GetOrCreateFramebuffer(pass, smaller) == retried &&
        device.GetOrCreateFramebuffer(pass, info) == existing && environment.state.passCreates == 1 &&
        environment.state.framebufferCreates == 3,
        "Framebuffer publication failure must leave both retry and earlier cache hits usable!");
#endif
}

void TestConcurrentFramebufferCacheRequests()
{
    NativeEnvironment environment;
    auto image = std::make_unique<Image>();
    FakeImage(*image, 76, 32, 32, 1, 1);
    RenderPassDesc pass;
    CreateColorPass(pass);
    FramebufferDesc info;
    info.SetImageView("0", image->View(SingleMip()));
    std::vector<VkFramebuffer> handles(8, VK_NULL_HANDLE);
    std::vector<std::exception_ptr> failures(handles.size());
    std::vector<std::thread> workers;
    for (size_t index = 0; index < handles.size(); ++index)
        workers.emplace_back([&, index]
        {
            try { handles[index] = MyDevice::GetInstance().GetOrCreateFramebuffer(pass, info); }
            catch (...) { failures[index] = std::current_exception(); }
        });
    for (auto& worker : workers) worker.join();
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    for (const auto handle : handles)
        CHECK_TRUE(handle != VK_NULL_HANDLE && handle == handles.front(),
            "Concurrent equivalent framebuffer requests must return one cached native handle!");
    CHECK_TRUE(environment.state.passCreates == 1 && environment.state.framebufferCreates == 1,
        "Concurrent framebuffer lookup must create exactly one native framebuffer and render pass per key!");
}

void TestSwapchainViewsRetainTheirOriginalGeneration()
{
    NativeEnvironment environment;
    int oldDestroyed = 0, newDestroyed = 0;
    auto oldGeneration = MakeIntrusive<VulkanDetail::SwapchainResource>(
        ResourceLifetimeTestProbe::Handle<VkSwapchainKHR>(51),
        [&oldDestroyed](VkSwapchainKHR) { ++oldDestroyed; });
    auto image = std::make_unique<Image>();
    ResourceLifetimeTestProbe::ImportSwapchain(*image, 52, oldGeneration);
    auto oldView = image->View();
    const auto viewCreates = environment.state.viewCreates;
    ExpectFailure([&] { ResourceLifetimeTestProbe::ImportSwapchain(*image, 53, oldGeneration); });
    CHECK_TRUE(image->GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(52) &&
        image->GetImageInformation().isSwapchainImage && image->GetImageSize().width == 64 &&
        image->View() == oldView && environment.state.viewCreates == viewCreates &&
        ResourceLifetimeTestProbe::CacheSize(*image) == 1,
        "Duplicate swapchain import must preserve the original resource, information and cached views!");
    oldGeneration.reset();
    image.reset();
    CHECK_TRUE(oldDestroyed == 0, "A retained view must retain the old swapchain generation!");

    auto newGeneration = MakeIntrusive<VulkanDetail::SwapchainResource>(
        ResourceLifetimeTestProbe::Handle<VkSwapchainKHR>(61),
        [&newDestroyed](VkSwapchainKHR) { ++newDestroyed; });
    image = std::make_unique<Image>();
    ResourceLifetimeTestProbe::ImportSwapchain(*image, 62, newGeneration);
    auto newView = image->View();
    newGeneration.reset();
    image.reset();
    CHECK_TRUE(oldView.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(52) &&
        newView.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(62),
        "Rebuilding imported images must preserve separate native generations!");
    oldView.Reset();
    CHECK_TRUE(oldDestroyed == 1 && newDestroyed == 0,
        "Releasing the old view must release only its own swapchain generation!");
    newView.Reset();
    CHECK_TRUE(newDestroyed == 1, "The new swapchain generation must also release exactly once!");
    CHECK_TRUE(!WasDestroyed(52) && !WasDestroyed(53) && !WasDestroyed(62) &&
        environment.state.imageCreates == 0,
        "Imported swapchain images must stay borrowed and never be created or destroyed individually!");
}

void TestBorrowedRenderTargetsDoNotRetainSwapchainGeneration()
{
    NativeEnvironment environment;
    int generationDestroys = 0;
    auto generation = MakeIntrusive<VulkanDetail::SwapchainResource>(
        ResourceLifetimeTestProbe::Handle<VkSwapchainKHR>(81),
        [&generationDestroys](VkSwapchainKHR) { ++generationDestroys; });
    auto image = std::make_unique<Image>();
    ResourceLifetimeTestProbe::ImportSwapchain(*image, 82, generation);
    auto view = image->View();
    RenderPassDesc pass;
    CreateColorPass(pass);
    FramebufferDesc info;
    info.SetImageView("0", view);
    const auto framebuffer = MyDevice::GetInstance().GetOrCreateFramebuffer(pass, info);
    CommandBuffer commands;
    commands.BeginRenderPass(pass, info, 0, 0, 64, 32);
    commands.EndRenderPass();
    generation.reset(); view.Reset(); image.reset();
    CHECK_TRUE(generationDestroys == 1 && !WasDestroyed(reinterpret_cast<uintptr_t>(framebuffer)),
        "Borrowed framebuffer metadata, native cache entries and queued commands must not retain a swapchain generation!");
    commands = {}; info.Reset();
}
}

void RunResourceLifetimeTests()
{
    TestFramebufferDoesNotRetainAttachment();
    TestNativeResourceCopiesAndParentReleaseOrder();
    TestConcurrentImageViewResourceIdentityAllocation();
    TestImageViewCacheSnapshotsAndFailures();
    TestFramebufferInferenceValidationAndFailure();
    TestFramebufferCreationDependencies();
    TestRenderTargetCacheTeardownReleasesAllNativeObjects();
    TestRenderPassSnapshotsCompatibilityAndBorrowedCommands();
    TestFramebufferIdentitySurvivesNativeHandleReuse();
    TestExplicitCommandKeepAliveRetainsAttachments();
    TestFramebufferPublicationFailureAndRetry();
    TestConcurrentFramebufferCacheRequests();
    TestDescriptorBindingsOwnTemporaryAndCopiedViews();
    TestSwapchainViewsRetainTheirOriginalGeneration();
    TestBorrowedRenderTargetsDoNotRetainSwapchainGeneration();
}
