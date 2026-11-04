#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/descriptor_set_layout_allocator.h"
#include "my_vulkan/resource/descriptor.h"
#include "my_vulkan/resource/private/dynamic_descriptor_pool.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <type_traits>
#include <cstdlib>
#include <exception>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

auto RunDescriptorPoolFatalCase(std::string_view argument) -> int;

static_assert(!std::is_copy_constructible_v<DescriptorSet>);
static_assert(std::is_nothrow_move_constructible_v<DescriptorSet>);
static_assert(std::is_nothrow_move_assignable_v<DescriptorSet>);
static_assert(std::is_nothrow_destructible_v<DescriptorSet>);

struct DescriptorPoolTestProbe
{
    static auto ImageViewForTest(std::function<void(VkImageView)> destroy) -> ImageView
    {
        ImageView view;
        view.m_resource = MakeIntrusive<VulkanDetail::ImageViewResource>(
            reinterpret_cast<VkImageView>(uintptr_t{9000}), std::move(destroy));
        return view;
    }
    static auto Exchange(std::unique_ptr<DescriptorSetLayoutAllocator> replacement)
        -> std::unique_ptr<DescriptorSetLayoutAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrDescriptorSetLayoutAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(replacement);
        return previous;
    }
};

namespace
{
template<class T> auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }
struct Page { uint32_t live = 0; VkDescriptorPoolCreateFlags flags = 0; };
struct NativeState
{
    uintptr_t next = 10;
    uint32_t creates = 0, destroys = 0, allocations = 0, frees = 0, updates = 0, resets = 0, freeAttempts = 0;
    uint32_t limit = 2;
    VkResult createResult = VK_SUCCESS, allocateResult = VK_SUCCESS;
    VkResult freeResult = VK_SUCCESS, resetResult = VK_SUCCESS;
    uint32_t failResetCall = 0;
    bool throwUpdate = false;
    std::atomic_bool overlap{false};
    std::mutex mutex;
    std::atomic_uint active{0};
    std::unordered_map<VkDescriptorPool, Page> pages;
    std::unordered_map<VkDescriptorSet, VkDescriptorPool> sets;
    uint32_t maxStorage = 0;
    VkImageLayout expectedLayouts[2] = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};
NativeState* state = nullptr;
struct NativeCall
{
    NativeCall() { if (state->active.fetch_add(1) != 0) state->overlap = true; std::this_thread::yield(); }
    ~NativeCall() { --state->active; }
};
auto VKAPI_PTR CreateLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* output) -> VkResult
{ *output = Handle<VkDescriptorSetLayout>(state->next++); return VK_SUCCESS; }
void VKAPI_PTR DestroyLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {}
auto VKAPI_PTR CreatePool(VkDevice, const VkDescriptorPoolCreateInfo* info, const VkAllocationCallbacks*, VkDescriptorPool* output) -> VkResult
{
    std::lock_guard lock(state->mutex);
    ++state->creates;
    if (state->createResult != VK_SUCCESS) return state->createResult;
    for (uint32_t i = 0; i < info->poolSizeCount; ++i)
        if (info->pPoolSizes[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            state->maxStorage = (std::max)(state->maxStorage, info->pPoolSizes[i].descriptorCount);
    *output = Handle<VkDescriptorPool>(state->next++);
    state->pages.emplace(*output, Page{0, info->flags});
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyPool(VkDevice, VkDescriptorPool pool, const VkAllocationCallbacks*)
{ ++state->destroys; state->pages.erase(pool); }
auto VKAPI_PTR Allocate(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) -> VkResult
{
    NativeCall call;
    std::lock_guard lock(state->mutex);
    ++state->allocations;
    if (state->allocateResult != VK_SUCCESS) return state->allocateResult;
    auto& page = state->pages.at(info->descriptorPool);
    if (page.live >= state->limit) return VK_ERROR_OUT_OF_POOL_MEMORY;
    ++page.live;
    *output = Handle<VkDescriptorSet>(state->next++);
    state->sets.emplace(*output, info->descriptorPool);
    return VK_SUCCESS;
}
auto VKAPI_PTR Free(VkDevice, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* sets) -> VkResult
{
    NativeCall call;
    std::lock_guard lock(state->mutex);
    ++state->freeAttempts;
    if (state->freeResult != VK_SUCCESS) return state->freeResult;
    CHECK_TRUE((state->pages.at(pool).flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) != 0, "Individual free requires public pool flags!");
    for (uint32_t i = 0; i < count; ++i)
    {
        CHECK_TRUE(state->sets.at(sets[i]) == pool, "Set must return to its source pool!");
        state->sets.erase(sets[i]); --state->pages.at(pool).live; ++state->frees;
    }
    return VK_SUCCESS;
}
auto VKAPI_PTR Reset(VkDevice, VkDescriptorPool pool, VkDescriptorPoolResetFlags) -> VkResult
{
    ++state->resets;
    if (state->resets == state->failResetCall) return state->resetResult;
    state->pages.at(pool).live = 0;
    return VK_SUCCESS;
}
void VKAPI_PTR Update(VkDevice, uint32_t count, const VkWriteDescriptorSet* writes, uint32_t copyCount, const VkCopyDescriptorSet*)
{
    ++state->updates;
    CHECK_TRUE(count != 0 && writes != nullptr && copyCount == 0, "Descriptor update must publish its write array!");
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& write = writes[i];
        CHECK_TRUE(state->sets.contains(write.dstSet), "Writes must target a successfully allocated set!");
        CHECK_TRUE(write.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && write.descriptorCount == 1,
            "Each description item must produce one initialized write!");
        CHECK_TRUE(write.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && write.dstBinding < 2 && write.dstArrayElement == 0,
            "Sampled-image bindings must preserve their destination and type!");
        CHECK_TRUE(write.pImageInfo != nullptr && write.pBufferInfo == nullptr && write.pTexelBufferView == nullptr,
            "Sampled-image writes must point to live image-info storage!");
        CHECK_TRUE(write.pImageInfo->imageView == Handle<VkImageView>(9000) && write.pImageInfo->sampler == VK_NULL_HANDLE,
            "Sampled-image payload must preserve the view and omit a sampler!");
        CHECK_TRUE(write.pImageInfo->imageLayout == state->expectedLayouts[write.dstBinding],
            "Sampled-image payload must preserve the supplied layout!");
        if (i != 0)
            CHECK_TRUE(write.dstSet == writes[0].dstSet && write.pImageInfo != writes[i - 1].pImageInfo,
                "Each write must keep its own image-info storage and target the same set!");
    }
    if (state->throwUpdate) throw std::bad_alloc();
}
struct Environment
{
    NativeState native;
    VkDevice previousDevice = MyDevice::GetInstance().vkDevice;
    PFN_vkCreateDescriptorSetLayout previousLayout = vkCreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout previousDestroyLayout = vkDestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool previousCreate = vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool previousDestroy = vkDestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets previousAllocate = vkAllocateDescriptorSets;
    PFN_vkFreeDescriptorSets previousFree = vkFreeDescriptorSets;
    PFN_vkResetDescriptorPool previousReset = vkResetDescriptorPool;
    PFN_vkUpdateDescriptorSets previousUpdate = vkUpdateDescriptorSets;
    std::unique_ptr<DescriptorSetLayoutAllocator> previousAllocator;
    Environment()
    {
        state = &native;
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(1);
        vkCreateDescriptorSetLayout = CreateLayout; vkDestroyDescriptorSetLayout = DestroyLayout;
        vkCreateDescriptorPool = CreatePool; vkDestroyDescriptorPool = DestroyPool;
        vkAllocateDescriptorSets = Allocate; vkFreeDescriptorSets = Free;
        vkResetDescriptorPool = Reset; vkUpdateDescriptorSets = Update;
        auto allocator = std::make_unique<DescriptorSetLayoutAllocator>();
        allocator->Create(Handle<VkDevice>(1));
        previousAllocator = DescriptorPoolTestProbe::Exchange(std::move(allocator));
    }
    ~Environment()
    {
        auto allocator = DescriptorPoolTestProbe::Exchange(std::move(previousAllocator));
        allocator->Destroy(); allocator.reset();
        MyDevice::GetInstance().vkDevice = previousDevice;
        vkCreateDescriptorSetLayout = previousLayout; vkDestroyDescriptorSetLayout = previousDestroyLayout;
        vkCreateDescriptorPool = previousCreate; vkDestroyDescriptorPool = previousDestroy;
        vkAllocateDescriptorSets = previousAllocate; vkFreeDescriptorSets = previousFree;
        vkResetDescriptorPool = previousReset; vkUpdateDescriptorSets = previousUpdate;
        state = nullptr;
    }
};
template<class F> void ExpectFailure(F&& function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    CHECK_TRUE(failed, "Descriptor operation should reject this request!");
}
template<class T = DescriptorPool> void TestReferenceCountedPoolLifetime()
{
    if constexpr (!std::is_base_of_v<RefCounted, T>)
    {
        CHECK_TRUE(false, "DescriptorPool must support reference-counted ownership!");
    }
    else
    {
        Environment environment;
        DescriptorSetLayoutCreateInfo info;
        info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
        DescriptorSetLayout layout; layout.Create(info);
        uint32_t destroyedViews = 0;
        auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
        DescriptorSetDesc description; description.SetImage(0, view);
        auto pool = MakeIntrusive<T>(); pool->Create();
        auto otherOwner = pool;
        auto first = pool->Allocate(layout, description);
        auto second = pool->Allocate(layout, description);
        auto destinationPool = MakeIntrusive<T>(); destinationPool->Create();
        auto destination = destinationPool->Allocate(layout, description);
        destinationPool.reset();
        description = {}; view.Reset();
        pool.reset();
        CHECK_TRUE(state->destroys == 0, "An independent pool owner must keep native pages alive!");
        otherOwner.reset();
        CHECK_TRUE(state->destroys == 0 && destroyedViews == 0,
            "Live sets must retain their pool and image views after external owners release it!");
        const auto handle = first.GetVkDescriptorSet();
        DescriptorSet moved(std::move(first));
        CHECK_TRUE(!first.IsValid() && moved.GetVkDescriptorSet() == handle,
            "Moving a set must retain its pool after external owners release it!");
        destination = std::move(moved);
        CHECK_TRUE(!moved.IsValid() && state->frees == 1 && state->destroys == 1 && destroyedViews == 0,
            "Move assignment must free its previous set while preserving the source pool!");
        second = {};
        CHECK_TRUE(state->frees == 2 && state->destroys == 1 && destroyedViews == 0,
            "Releasing one set must preserve the pool and views retained by another set!");
        destination = {};
        CHECK_TRUE(state->frees == 3 && state->destroys == 2 && state->pages.empty() && state->sets.empty(),
            "Releasing the last set must free it before destroying its pool exactly once!");
        CHECK_TRUE(destroyedViews == 1, "Releasing the last set must release its retained image view!");
    }
}
void TestOwnership()
{
    Environment environment;
    DescriptorSetLayout layout; layout.Create({});
    auto first = MakeIntrusive<DescriptorPool>();
    auto second = MakeIntrusive<DescriptorPool>(); first->Create(); second->Create();
    DescriptorSet empty;
    CHECK_TRUE(!empty.IsValid() && empty.GetVkDescriptorSet() == VK_NULL_HANDLE, "Default set must be empty!");
    {
        auto a = first->Allocate(layout, {});
        auto b = second->Allocate(layout, {});
        auto handle = a.GetVkDescriptorSet();
        DescriptorSet moved(std::move(a));
        CHECK_TRUE(!a.IsValid() && moved.GetVkDescriptorSet() == handle, "Move must transfer the native set!");
        moved = std::move(moved);
        CHECK_TRUE(moved.IsValid(), "Self move must preserve ownership!");
        b = std::move(moved);
        CHECK_TRUE(state->frees == 1 && !moved.IsValid(), "Move assignment must free its previous set!");
        CHECK_TRUE(state->destroys == 0, "Pool owners must retain native pages while sets are alive!");
        b = {};
        CHECK_TRUE(state->frees == 2, "Assigning empty must free the set exactly once!");
        auto scoped = first->Allocate(layout, {});
    }
    CHECK_TRUE(state->frees == 3, "Scope exit must free the native set!");
    auto additionalOwner = first;
    first.reset(); second.reset();
    CHECK_TRUE(state->destroys == 1, "An additional pool owner must retain native pages even without live sets!");
    auto retained = additionalOwner->Allocate(layout, {});
    additionalOwner.reset();
    CHECK_TRUE(state->destroys == 1, "A set allocated through a remaining owner must retain its pool!");
    retained = {};
    CHECK_TRUE(state->frees == 4 && state->destroys == 2 && state->pages.empty() && state->sets.empty(),
        "The final reference must free its set before destroying native pages exactly once!");
}
void TestGrowthAndFailures()
{
    Environment environment;
    DescriptorSetLayout layout; layout.Create({});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    std::vector<DescriptorSet> sets;
    for (int i = 0; i < 5; ++i) sets.push_back(pool->Allocate(layout, {}));
    CHECK_TRUE(state->creates == 3, "Full descriptor pages must roll over!");
    sets.clear();
    const auto creates = state->creates;
    auto reused = pool->Allocate(layout, {}); reused = {};
    CHECK_TRUE(state->creates == creates, "Freed capacity must be reusable!");
    state->allocateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const auto allocations = state->allocations;
    ExpectFailure([&] { auto ignored = pool->Allocate(layout, {}); });
    CHECK_TRUE(state->allocations == allocations + 1 && state->creates == creates, "Non-capacity errors must propagate without retry!");
    state->allocateResult = VK_ERROR_FRAGMENTED_POOL;
    const auto before = state->creates;
    ExpectFailure([&] { auto ignored = pool->Allocate(layout, {}); });
    CHECK_TRUE(state->creates <= before + 1, "Capacity failures must create at most one fresh page!");
    state->allocateResult = VK_SUCCESS;
    pool.reset();

    auto creationFailure = MakeIntrusive<DescriptorPool>(); creationFailure->Create();
    state->createResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    ExpectFailure([&] { auto ignored = creationFailure->Allocate(layout, {}); });
    state->createResult = VK_SUCCESS;
    creationFailure.reset();

    DescriptorSetLayoutCreateInfo largeInfo;
    largeInfo.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 5000);
    DescriptorSetLayout large; large.Create(largeInfo);
    auto growing = MakeIntrusive<DescriptorPool>(); growing->Create();
    auto largeSet = growing->Allocate(large, {});
    CHECK_TRUE(state->maxStorage >= 5000, "Native budget must fit an oversized layout!");
    largeSet = {}; growing.reset();

    DescriptorSetLayoutCreateInfo overflowingInfo;
    overflowingInfo.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, UINT32_MAX);
    overflowingInfo.SetBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1);
    DescriptorSetLayout overflowing; overflowing.Create(overflowingInfo);
    auto checked = MakeIntrusive<DescriptorPool>(); checked->Create();
    const auto callsBefore = state->creates;
    ExpectFailure([&] { auto ignored = checked->Allocate(overflowing, {}); });
    CHECK_TRUE(state->creates == callsBefore, "Overflowing descriptor counts must fail before native pool creation!");
    DescriptorSetLayout invalid;
    const auto allocateBefore = state->allocations;
    ExpectFailure([&] { auto ignored = checked->Allocate(invalid, {}); });
    DescriptorSetDesc invalidDescription; invalidDescription.SetImage(0, {});
    ExpectFailure([&] { auto ignored = checked->Allocate(layout, invalidDescription); });
    CHECK_TRUE(state->allocations == allocateBefore, "Invalid layout or writes must fail before native allocation!");
    checked.reset();
}
void TestDynamicAndRollback()
{
    Environment environment;
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    info.SetBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout; layout.Create(info);
    uint32_t destroyedViews = 0;
    auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
    DescriptorSetDesc description;
    description.SetImage(0, view); description.SetImage(1, view);
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    state->throwUpdate = true;
    ExpectFailure([&] { auto ignored = pool->Allocate(layout, description); });
    CHECK_TRUE(state->frees == 1, "Failed public publication must free its allocated native set!");
    state->throwUpdate = false;
    pool.reset();
    VulkanDetail::DynamicDescriptorPool dynamic; dynamic.Create();
    state->throwUpdate = true;
    const auto frees = state->frees;
    ExpectFailure([&] { static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, description)); });
    CHECK_TRUE(state->frees == frees, "Dynamic failure must not individually free native sets!");
    state->throwUpdate = false;
    const auto afterFailedPublication = state->allocations;
    const auto updatesAfterFailure = state->updates;
    const auto first = dynamic.GetOrAllocateDescriptorSet(layout, description);
    CHECK_TRUE(state->allocations == afterFailedPublication + 1 && state->updates == updatesAfterFailure + 1,
        "A failed dynamic write must not publish a cache entry!");
    for (const auto& [handle, page] : state->pages)
        CHECK_TRUE((page.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0,
            "Dynamic pools must omit individual-free flags!");
    DescriptorSetDesc reordered; reordered.SetImage(1, view); reordered.SetImage(0, view);
    const auto allocations = state->allocations;
    CHECK_TRUE(dynamic.GetOrAllocateDescriptorSet(layout, reordered) == first && state->allocations == allocations,
        "Equivalent dynamic descriptions must share a cached set!");
    for (uint32_t i = 0; i < 5; ++i)
    {
        DescriptorSetDesc distinct;
        distinct.SetImage(0, view, static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_GENERAL + i));
        state->expectedLayouts[0] = static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_GENERAL + i);
        static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, distinct));
    }
    state->expectedLayouts[0] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    CHECK_TRUE(state->creates >= 4, "Dynamic sets must also roll over full native pages!");
    const auto creates = state->creates;
    dynamic.Reset();
    static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, description));
    CHECK_TRUE(state->creates == creates && state->frees == frees, "Reset must reuse dynamic pages without individual frees!");
    dynamic.Destroy();
    auto retaining = MakeIntrusive<DescriptorPool>(); retaining->Create();
    auto retained = retaining->Allocate(layout, description);
    view.Reset(); description = {}; reordered = {};
    CHECK_TRUE(destroyedViews == 0, "Live public sets must retain image views independently of descriptions!");
    retained = {};
    CHECK_TRUE(destroyedViews == 1, "Freeing the last set must release its retained image view!");
    retaining.reset();
}
void TestConcurrentOwnership()
{
    Environment environment;
    state->limit = 1000;
    DescriptorSetLayout layout; layout.Create({});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&]
    { for (int j = 0; j < 100; ++j) { auto set = pool->Allocate(layout, {}); } });
    for (auto& thread : threads) thread.join();
    CHECK_TRUE(!state->overlap && state->frees == 400, "Public native allocation and free must be synchronized!");
    pool.reset();
}
void TestPartialDynamicResetFailure()
{
    Environment environment;
    state->limit = 1;
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout; layout.Create(info);
    uint32_t destroyedViews = 0;
    VulkanDetail::DynamicDescriptorPool dynamic; dynamic.Create();
    // The empty description is deliberately cached so failure must reject a cache hit too.
    static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, {}));
    auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
    DescriptorSetDesc description; description.SetImage(0, view);
    static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, description));
    description.SetImage(0, view, VK_IMAGE_LAYOUT_GENERAL);
    state->expectedLayouts[0] = VK_IMAGE_LAYOUT_GENERAL;
    static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, description));
    CHECK_TRUE(state->creates == 3, "Partial reset regression needs multiple native pages!");
    description = {}; view.Reset();
    CHECK_TRUE(destroyedViews == 0, "Dynamic cache must retain the view before reset!");
    state->resetResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    state->failResetCall = state->resets + 2;
    ExpectFailure([&] { dynamic.Reset(); });
    CHECK_TRUE(state->resets == 2 && destroyedViews == 1,
        "A partial native reset must clear all cached descriptions and retained views!");
    const auto allocations = state->allocations;
    const auto creates = state->creates;
    ExpectFailure([&] { static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, {})); });
    CHECK_TRUE(state->allocations == allocations && state->creates == creates,
        "Failed reset must reject cached and new allocations before native calls!");
    dynamic.Destroy(); dynamic.Create();
    static_cast<void>(dynamic.GetOrAllocateDescriptorSet(layout, {}));
    CHECK_TRUE(state->allocations == allocations + 1, "Destroy/Create must recover from a partial reset failure!");
    dynamic.Destroy();
}
constexpr int FatalExitCode = 87;
constexpr int UnexpectedFatalExitCode = 88;
void FatalTerminationHandler() noexcept
{
    const bool expected = state != nullptr && state->freeAttempts == 1 && state->freeResult != VK_SUCCESS;
    std::_Exit(expected ? FatalExitCode : UnexpectedFatalExitCode);
}
void ExpectFatalChild(std::string_view argument)
{
#ifdef _WIN32
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    CHECK_TRUE(length != 0 && length < executable.size(), "Cannot locate death-test executable!");
    executable.resize(length);
    std::wstring command = L"\"" + executable + L"\" ";
    command.append(argument.begin(), argument.end());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    CHECK_TRUE(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "Cannot launch descriptor death test!");
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = 0;
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, UnexpectedFatalExitCode);
    const BOOL readExit = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CHECK_TRUE(wait == WAIT_OBJECT_0 && readExit && exitCode == FatalExitCode,
        "Descriptor fatal contract must terminate through the expected native path!");
#else
    const pid_t child = fork();
    CHECK_TRUE(child >= 0, "Cannot fork descriptor death test!");
    if (child == 0) std::_Exit(RunDescriptorPoolFatalCase(argument));
    int status = 0;
    CHECK_TRUE(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == FatalExitCode,
        "Descriptor fatal contract must terminate through the expected native path!");
#endif
}
void TestFatalContracts()
{
    ExpectFatalChild("--descriptor-fatal-free");
}
}
auto RunDescriptorPoolFatalCase(std::string_view argument) -> int
{
    if (argument != "--descriptor-fatal-free") return -1;
    std::set_terminate(FatalTerminationHandler);
    try
    {
        Environment environment;
        DescriptorSetLayout layout; layout.Create({});
        auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
        auto set = pool->Allocate(layout, {});
        state->freeResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        set = {};
        // Returning normally or throwing is a failed death test.
        std::_Exit(UnexpectedFatalExitCode);
    }
    catch (...) { std::_Exit(UnexpectedFatalExitCode); }
}
void RunDescriptorPoolTests()
{
    TestReferenceCountedPoolLifetime();
    TestOwnership();
    TestGrowthAndFailures();
    TestDynamicAndRollback();
    TestConcurrentOwnership();
    TestPartialDynamicResetFailure();
    TestFatalContracts();
}
