#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/render_pass_allocator.h"
#include "my_vulkan/command/command_buffer.h"
#include "my_vulkan/resource/descriptor.h"
#include <algorithm>
#include <type_traits>

static_assert(std::is_nothrow_copy_constructible_v<ImageView>);
static_assert(std::is_nothrow_move_constructible_v<ImageView>);
static_assert(std::is_nothrow_copy_constructible_v<Framebuffer>);
static_assert(std::is_nothrow_move_constructible_v<Framebuffer>);
static_assert(!std::is_copy_constructible_v<Image>);
static_assert(!std::is_copy_constructible_v<RenderPass>);
static_assert(std::is_same_v<decltype(std::declval<const Image&>().View()), ImageView>);
static_assert(std::is_same_v<decltype(std::declval<ImageCreateInfo&>().SetUsage(0)), void>);
static_assert(std::is_same_v<decltype(std::declval<GraphicsPipelineStateInfo&>().Reset()), void>);
static_assert(noexcept(std::declval<ImageView&>().Reset()));
static_assert(noexcept(std::declval<Framebuffer&>().Reset()));

struct ResourceLifetimeTestProbe
{
    template<typename T>
    static auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }
    static void SetImage(Image& image, uintptr_t handle, uint32_t width, uint32_t height,
        uint32_t mips, uint32_t layers, VkFormat format, VkImageUsageFlags usage,
        VkSampleCountFlagBits samples, std::function<void(VkImage)> destroy)
    {
        image.Destroy();
        image.m_resource = MakeIntrusive<VulkanDetail::ImageResource>(Handle<VkImage>(handle), std::move(destroy));
        auto& info = image.m_imageInformation;
        info.width = width; info.height = height; info.depth = 1;
        info.mipLevels = mips; info.arrayLayers = layers;
        info.format = format; info.usage = usage; info.samples = samples;
    }
    static auto ExchangeAllocator(std::unique_ptr<RenderPassAllocator> replacement) -> std::unique_ptr<RenderPassAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrRenderPassAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
    static void ClearAllocator() { MyDevice::GetInstance().m_uptrRenderPassAllocator->Destroy(); }
    static void ImportSwapchain(Image& image, uintptr_t handle,
        const IntrusivePtr<VulkanDetail::SwapchainResource>& owner)
    {
        image._ImportSwapchain(Handle<VkImage>(handle), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_FORMAT_R8G8B8A8_UNORM, {64, 32}, owner);
    }
    static auto Snapshot(const RenderPass& pass) -> VulkanDetail::RenderPassSnapshot { return pass._GetSnapshot(); }
    static auto CacheSize(const RenderPass& pass) -> size_t { return pass.m_framebuffers.size(); }
    static auto CacheSize(const Image& image) -> size_t { return image.m_imageViews.size(); }
    static void Append(CommandBuffer& destination, CommandBuffer&& source) { destination._Append(std::move(source)); }
    static void Record(const CommandBuffer& commands)
    {
        for (const Command* command : commands.m_commands) command->Record(Handle<VkCommandBuffer>(1));
    }
};

namespace
{
struct NativeState
{
    uintptr_t nextView = 1000, nextPass = 2000, nextFramebuffer = 3000;
    uint32_t viewCreates = 0, framebufferCreates = 0;
    bool failImage = false, failView = false, failPass = false, failFramebuffer = false;
    std::vector<uintptr_t> destroyed;
    VkFramebufferCreateInfo framebufferInfo{};
    VkRenderPass beginPass = VK_NULL_HANDLE;
    VkFramebuffer beginFramebuffer = VK_NULL_HANDLE;
    uint32_t beginClearCount = 0, beginCalls = 0, endCalls = 0;
};
NativeState* nativeState = nullptr;

auto VKAPI_PTR CreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*) -> VkResult
{
    CHECK_TRUE(nativeState->failImage, "A successful image test must avoid real memory allocation!");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
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
    if (nativeState->failPass) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = ResourceLifetimeTestProbe::Handle<VkRenderPass>(nativeState->nextPass++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyPass(VkDevice, VkRenderPass value, const VkAllocationCallbacks*)
{
    nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value));
}
auto VKAPI_PTR CreateFramebuffer(VkDevice, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks*, VkFramebuffer* output) -> VkResult
{
    ++nativeState->framebufferCreates;
    nativeState->framebufferInfo = *info;
    nativeState->framebufferInfo.pAttachments = nullptr;
    if (nativeState->failFramebuffer) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *output = ResourceLifetimeTestProbe::Handle<VkFramebuffer>(nativeState->nextFramebuffer++);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyFramebuffer(VkDevice, VkFramebuffer value, const VkAllocationCallbacks*)
{
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
    PFN_vkCreateImageView previousView = vkCreateImageView;
    PFN_vkDestroyImageView previousDestroyView = vkDestroyImageView;
    PFN_vkCreateRenderPass previousPass = vkCreateRenderPass;
    PFN_vkDestroyRenderPass previousDestroyPass = vkDestroyRenderPass;
    PFN_vkCreateFramebuffer previousFramebuffer = vkCreateFramebuffer;
    PFN_vkDestroyFramebuffer previousDestroyFramebuffer = vkDestroyFramebuffer;
    PFN_vkCmdBeginRenderPass previousBegin = vkCmdBeginRenderPass;
    PFN_vkCmdEndRenderPass previousEnd = vkCmdEndRenderPass;
    std::unique_ptr<RenderPassAllocator> previousAllocator;
    NativeEnvironment()
    {
        nativeState = &state;
        vkCreateImage = CreateImage; vkCreateImageView = CreateView; vkDestroyImageView = DestroyView;
        vkCreateRenderPass = CreatePass; vkDestroyRenderPass = DestroyPass;
        vkCreateFramebuffer = CreateFramebuffer; vkDestroyFramebuffer = DestroyFramebuffer;
        vkCmdBeginRenderPass = BeginPass; vkCmdEndRenderPass = EndPass;
        auto allocator = std::make_unique<RenderPassAllocator>();
        allocator->Create(ResourceLifetimeTestProbe::Handle<VkDevice>(1));
        previousAllocator = ResourceLifetimeTestProbe::ExchangeAllocator(std::move(allocator));
    }
    ~NativeEnvironment()
    {
        auto allocator = ResourceLifetimeTestProbe::ExchangeAllocator(std::move(previousAllocator));
        allocator.reset();
        vkCreateImage = previousImage; vkCreateImageView = previousView; vkDestroyImageView = previousDestroyView;
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
void FakeImage(Image& image, uintptr_t handle, uint32_t width = 128, uint32_t height = 64,
    uint32_t mips = 4, uint32_t layers = 4, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
{
    ResourceLifetimeTestProbe::SetImage(image, handle, width, height, mips, layers, format, usage, samples,
        [](VkImage value) { nativeState->destroyed.push_back(reinterpret_cast<uintptr_t>(value)); });
}
auto SingleMip(uint32_t mip = 0, uint32_t baseLayer = 0, uint32_t layers = VK_REMAINING_ARRAY_LAYERS) -> ImageViewInfo
{
    ImageViewInfo info;
    info.CustomizeMipLevels(mip, 1);
    info.CustomizeArrayLayers(baseLayer, layers);
    return info;
}
void CreateColorPass(RenderPass& pass, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
    VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_CLEAR, uint32_t attachmentCount = 1)
{
    RenderPassCreateInfo info;
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
    pass.Create(info);
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

void TestImageViewCacheSnapshotsAndFailures()
{
    NativeEnvironment environment;
    Image image;
    FakeImage(image, 10);
    auto view = image.View(SingleMip(1, 1, 3));
    auto copy = image.View(SingleMip(1, 1));
    CHECK_TRUE(view == copy && environment.state.viewCreates == 1, "Resolved image view ranges must share the strong cache!");
    const auto oldHandle = reinterpret_cast<uintptr_t>(view.GetVkImageView());
    CHECK_TRUE(view.GetImageViewInformation().width == 128 && view.GetViewSize().width == 64 &&
        view.GetViewSize().height == 32 && view.GetImageViewInformation().layerCount == 3,
        "View snapshots must preserve mip-zero size and resolve selected mip/layer dimensions!");
    image.ClearViewCache();
    CHECK_TRUE(!WasDestroyed(oldHandle) && !WasDestroyed(10), "Clearing the cache must preserve external view owners!");
    auto replacement = image.View(SingleMip(1, 1, 3));
    CHECK_TRUE(!(replacement == view), "A cleared cache must create a new native view version!");
    const auto cacheSize = ResourceLifetimeTestProbe::CacheSize(image);
    const auto createCount = environment.state.viewCreates;
    for (const auto info : {SingleMip(0, 0, 0), SingleMip(0, UINT32_MAX, 1), SingleMip(UINT32_MAX), SingleMip(0, 1, UINT32_MAX - 1)})
        ExpectFailure([&] { (void)image.View(info); });
    ImageViewInfo zeroMips;
    zeroMips.CustomizeMipLevels(0, 0);
    ExpectFailure([&] { (void)image.View(zeroMips); });
    ImageViewInfo overflowMips;
    overflowMips.CustomizeMipLevels(1, UINT32_MAX - 1);
    ExpectFailure([&] { (void)image.View(overflowMips); });
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(image) == cacheSize && environment.state.viewCreates == createCount &&
        image.View(SingleMip(1, 1, 3)) == replacement, "Rejected ranges must leave the image cache and current version unchanged!");
    environment.state.failView = true;
    ExpectFailure([&] { (void)image.View(SingleMip(2)); });
    environment.state.failView = false;
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(image) == cacheSize, "Failed native view creation must not enter the cache!");
    environment.state.failImage = true;
    ImageCreateInfo createInfo;
    createInfo.CustomizeSize2D(16, 16);
    createInfo.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
    ExpectFailure([&] { image.Create(createInfo); });
    CHECK_TRUE(image.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(10) &&
        image.View(SingleMip(1, 1, 3)) == replacement, "Failed image replacement must preserve the old native image and cache!");
    FakeImage(image, 11, 32, 16, 1, 1);
    CHECK_TRUE(view.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(10) &&
        view.GetImageViewInformation().width == 128 && view.GetImageViewInformation().baseMipLevel == 1,
        "Replacing an image facade must not mutate surviving view snapshots!");
    image.Destroy();
    CHECK_TRUE(!image.IsValid() && !WasDestroyed(10), "Image destruction must preserve surviving native view versions!");
    copy.Reset();
    replacement.Reset();
    CHECK_TRUE(!WasDestroyed(10), "The final external view must retain its native image!");
    view.Reset();
    CHECK_TRUE(WasDestroyed(oldHandle) && WasDestroyed(10) && !view.IsValid() && view.GetVkImage() == VK_NULL_HANDLE,
        "Reset must release the final view and then its image parent!");
}

void TestFramebufferInferenceValidationAndFailure()
{
    NativeEnvironment environment;
    Image first, second;
    FakeImage(first, 20, 128, 64, 4, 4);
    FakeImage(second, 21, 100, 100, 1, 2);
    RenderPass pass;
    CreateColorPass(pass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR, 2);
    FramebufferInfo info;
    info.SetImageView("0", first.View(SingleMip(1, 1, 3)));
    info.SetImageView("1", second.View(SingleMip()));
    auto framebuffer = pass.GetFramebuffer(info);
    CHECK_TRUE(framebuffer.GetSize().width == 64 && framebuffer.GetSize().height == 32 && framebuffer.GetLayerCount() == 2 &&
        environment.state.framebufferInfo.width == 64 && environment.state.framebufferInfo.layers == 2,
        "Framebuffer inference must use minimum selected-mip dimensions and attachment layer counts!");
    CHECK_TRUE(pass.GetFramebuffer(info) == framebuffer && environment.state.framebufferCreates == 1,
        "Equivalent framebuffer requests must share the strong cache!");
    const auto cacheSize = ResourceLifetimeTestProbe::CacheSize(pass);
    const auto creates = environment.state.framebufferCreates;
    const auto rejectView = [&](const ImageView& view)
    {
        FramebufferInfo invalid = info;
        invalid.SetImageView("0", view);
        ExpectFailure([&] { (void)pass.GetFramebuffer(invalid); });
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
    FramebufferInfo oversized = info;
    oversized.CustomizeSize(65, 32, 2);
    ExpectFailure([&] { (void)pass.GetFramebuffer(oversized); });
    oversized = info;
    oversized.CustomizeSize(64, 32, 3);
    ExpectFailure([&] { (void)pass.GetFramebuffer(oversized); });
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(pass) == cacheSize && environment.state.framebufferCreates == creates,
        "Framebuffer validation failures must not call Vulkan or change the cache!");
    FramebufferInfo smaller = info;
    smaller.CustomizeSize(16, 16, 1);
    environment.state.failFramebuffer = true;
    ExpectFailure([&] { (void)pass.GetFramebuffer(smaller); });
    environment.state.failFramebuffer = false;
    CHECK_TRUE(ResourceLifetimeTestProbe::CacheSize(pass) == cacheSize && pass.GetFramebuffer(info) == framebuffer,
        "Failed native framebuffer creation must preserve cached versions!");
    auto smallerFramebuffer = pass.GetFramebuffer(smaller);
    CHECK_TRUE(smallerFramebuffer.GetSize().width == 16 && smallerFramebuffer.GetLayerCount() == 1,
        "An explicit framebuffer extent must be preserved!");
    const auto oldFramebuffer = reinterpret_cast<uintptr_t>(framebuffer.GetVkFramebuffer());
    pass.ClearFramebufferCache();
    CHECK_TRUE(!WasDestroyed(oldFramebuffer), "Clearing the framebuffer cache must preserve externally held framebuffer values!");
    auto rebuilt = pass.GetFramebuffer(info);
    CHECK_TRUE(!(rebuilt == framebuffer), "A cleared framebuffer cache must create a new native framebuffer version!");
    framebuffer = rebuilt;
    const auto oldPass = pass.GetVkRenderPass();
    environment.state.failPass = true;
    ExpectFailure([&] { CreateColorPass(pass, VK_FORMAT_R16G16B16A16_SFLOAT); });
    environment.state.failPass = false;
    CHECK_TRUE(pass.GetVkRenderPass() == oldPass && pass.GetFramebuffer(info) == framebuffer,
        "Failed render pass replacement must preserve its native version and framebuffer cache!");
    RenderPass emptyPass;
    CreateColorPass(emptyPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR, 0);
    FramebufferInfo emptyInfo;
    ExpectFailure([&] { (void)emptyPass.GetFramebuffer(emptyInfo); });
    emptyInfo.CustomizeSize(17, 19, 2);
    auto emptyFramebuffer = emptyPass.GetFramebuffer(emptyInfo);
    CHECK_TRUE(emptyFramebuffer.GetSize().width == 17 && emptyFramebuffer.GetSize().height == 19 && emptyFramebuffer.GetLayerCount() == 2,
        "An attachmentless framebuffer must support explicit dimensions and layers!");
}

void TestRenderPassSnapshotsCompatibilityAndCommandRetention()
{
    NativeEnvironment environment;
    Image image;
    FakeImage(image, 30, 80, 40, 1, 1);
    RenderPass clearPass, loadPass, differentPass;
    CreateColorPass(clearPass);
    CreateColorPass(loadPass, VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_LOAD);
    CreateColorPass(differentPass, VK_FORMAT_B8G8R8A8_UNORM);
    auto snapshot = ResourceLifetimeTestProbe::Snapshot(clearPass);
    CHECK_TRUE(snapshot.IsCompatibleWith(ResourceLifetimeTestProbe::Snapshot(loadPass)) &&
        !snapshot.IsCompatibleWith(ResourceLifetimeTestProbe::Snapshot(differentPass)),
        "Render pass compatibility must accept CLEAR/LOAD variants and reject changed attachment formats!");
    FramebufferInfo info;
    info.SetImageView("0", image.View(SingleMip()));
    auto framebuffer = clearPass.GetFramebuffer(info);
    CommandBuffer compatible;
    compatible.BeginRenderPass(loadPass, framebuffer, 0, 0, 80, 40);
    compatible.EndRenderPass();
    CommandBuffer incompatible;
    ExpectFailure([&] { incompatible.BeginRenderPass(differentPass, framebuffer, 0, 0, 80, 40); });
    compatible = {};
    const auto passHandle = clearPass.GetVkRenderPass();
    const auto framebufferHandle = framebuffer.GetVkFramebuffer();
    const auto viewHandle = reinterpret_cast<uintptr_t>(image.View(SingleMip()).GetVkImageView());
    clearPass.Destroy();
    CHECK_TRUE(snapshot.GetVkRenderPass() == passHandle && snapshot.GetSubpassCount() == 1 && snapshot.GetClearValues().size() == 1,
        "Snapshots must retain native identity and immutable render pass metadata after facade destruction!");
    CommandBuffer commands;
    commands.BeginRenderPass(framebuffer, 0, 0, 80, 40);
    commands.EndRenderPass();
    CommandBuffer moved(std::move(commands));
    CommandBuffer appended;
    ResourceLifetimeTestProbe::Append(appended, std::move(moved));
    framebuffer.Reset(); info.Reset(); image.Destroy();
    loadPass.Destroy(); differentPass.Destroy(); snapshot = {};
    ResourceLifetimeTestProbe::ClearAllocator();
    CHECK_TRUE(!WasDestroyed(reinterpret_cast<uintptr_t>(framebufferHandle)) && !WasDestroyed(viewHandle) && !WasDestroyed(30),
        "Moved and appended commands must retain native framebuffer, view, and image versions!");
    ResourceLifetimeTestProbe::Record(appended);
    CHECK_TRUE(environment.state.beginCalls == 1 && environment.state.endCalls == 1 && environment.state.beginPass == passHandle &&
        environment.state.beginFramebuffer == framebufferHandle && environment.state.beginClearCount == 1,
        "Implicit framebuffer begin must record retained render pass metadata after its facade is destroyed!");
    appended = {};
    CHECK_TRUE(WasDestroyed(reinterpret_cast<uintptr_t>(framebufferHandle)) && WasDestroyed(viewHandle) && WasDestroyed(30),
        "Releasing command owners must release the final native attachment chain!");
    const auto& destroyed = environment.state.destroyed;
    CHECK_TRUE(std::find(destroyed.begin(), destroyed.end(), reinterpret_cast<uintptr_t>(framebufferHandle)) <
        std::find(destroyed.begin(), destroyed.end(), viewHandle) &&
        std::find(destroyed.begin(), destroyed.end(), viewHandle) < std::find(destroyed.begin(), destroyed.end(), 30),
        "Native framebuffers must die before attachment views and images!");
}

void TestDescriptorBindingsOwnTemporaryAndCopiedViews()
{
    NativeEnvironment environment;
    Image image;
    FakeImage(image, 40, 32, 32, 1, 1);
    DescriptorSetDesc binding;
    binding.SetImage(0, image.View(SingleMip()));
    const auto viewHandle = reinterpret_cast<uintptr_t>(image.View(SingleMip()).GetVkImageView());
    auto copy = binding;
    ShaderResources named;
    named.SetImage("texture", image.View(SingleMip()));
    auto namedCopy = named;
    binding = {}; named.Clear(); image.Destroy();
    CHECK_TRUE(!WasDestroyed(viewHandle) && !WasDestroyed(40), "Descriptor copies must own temporary image-view values!");
    copy = {};
    CHECK_TRUE(!WasDestroyed(viewHandle), "Shader resource copies must independently retain their view!");
    namedCopy.Clear();
    CHECK_TRUE(WasDestroyed(viewHandle) && WasDestroyed(40), "Releasing descriptor snapshots must release their view and image!");
}

void TestSwapchainViewsRetainTheirOriginalGeneration()
{
    NativeEnvironment environment;
    int oldDestroyed = 0, newDestroyed = 0;
    auto oldGeneration = MakeIntrusive<VulkanDetail::SwapchainResource>(
        ResourceLifetimeTestProbe::Handle<VkSwapchainKHR>(51),
        [&oldDestroyed](VkSwapchainKHR) { ++oldDestroyed; });
    Image image;
    ResourceLifetimeTestProbe::ImportSwapchain(image, 52, oldGeneration);
    auto oldView = image.View();
    oldGeneration.reset();
    image.Destroy();
    CHECK_TRUE(oldDestroyed == 0, "A retained view must retain the old swapchain generation!");

    auto newGeneration = MakeIntrusive<VulkanDetail::SwapchainResource>(
        ResourceLifetimeTestProbe::Handle<VkSwapchainKHR>(61),
        [&newDestroyed](VkSwapchainKHR) { ++newDestroyed; });
    ResourceLifetimeTestProbe::ImportSwapchain(image, 62, newGeneration);
    auto newView = image.View();
    newGeneration.reset();
    image.Destroy();
    CHECK_TRUE(oldView.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(52) &&
        newView.GetVkImage() == ResourceLifetimeTestProbe::Handle<VkImage>(62),
        "Rebuilding imported images must preserve separate native generations!");
    oldView.Reset();
    CHECK_TRUE(oldDestroyed == 1 && newDestroyed == 0,
        "Releasing the old view must release only its own swapchain generation!");
    newView.Reset();
    CHECK_TRUE(newDestroyed == 1, "The new swapchain generation must also release exactly once!");
}
}

void RunResourceLifetimeTests()
{
    TestNativeResourceCopiesAndParentReleaseOrder();
    TestImageViewCacheSnapshotsAndFailures();
    TestFramebufferInferenceValidationAndFailure();
    TestRenderPassSnapshotsCompatibilityAndCommandRetention();
    TestDescriptorBindingsOwnTemporaryAndCopiedViews();
    TestSwapchainViewsRetainTheirOriginalGeneration();
}
