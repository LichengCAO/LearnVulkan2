#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/descriptor_set_layout_allocator.h"
#include "my_vulkan/resource/descriptor.h"
#include "my_vulkan/resource/private/dynamic_descriptor_pool.h"
#include <atomic>
#include <barrier>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
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
    uint32_t layoutAttempts = 0, layoutCreates = 0, layoutDestroys = 0;
    VkResult layoutResult = VK_SUCCESS;
    const std::thread::id layoutApiThread = std::this_thread::get_id();
    std::atomic_uint layoutWorkerCalls{0};
    std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSetLayoutBinding>> layouts;
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
auto VKAPI_PTR CreateLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* output) -> VkResult
{
    if (std::this_thread::get_id() != state->layoutApiThread) ++state->layoutWorkerCalls;
    std::lock_guard lock(state->mutex);
    ++state->layoutAttempts;
    CHECK_TRUE(device == Handle<VkDevice>(1) && info->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        "Layout creation must use its live Device and initialized native info!");
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    if (info->bindingCount != 0) bindings.assign(info->pBindings, info->pBindings + info->bindingCount);
    for (size_t index = 1; index < bindings.size(); ++index)
        CHECK_TRUE(bindings[index - 1].binding < bindings[index].binding, "Native layout bindings must be sorted!");
    if (state->layoutResult != VK_SUCCESS) return state->layoutResult;
    *output = Handle<VkDescriptorSetLayout>(state->next++);
    state->layouts.emplace(*output, std::move(bindings));
    ++state->layoutCreates;
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyLayout(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks*)
{
    if (std::this_thread::get_id() != state->layoutApiThread) ++state->layoutWorkerCalls;
    std::lock_guard lock(state->mutex);
    CHECK_TRUE(device == Handle<VkDevice>(1) && state->layouts.erase(layout) == 1,
        "Each cached native layout must be destroyed exactly once with its owning Device!");
    ++state->layoutDestroys;
}
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
{
    NativeCall call;
    std::lock_guard lock(state->mutex);
    std::erase_if(state->sets, [pool](const auto& entry) -> bool { return entry.second == pool; });
    ++state->destroys; state->pages.erase(pool);
}
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
    std::lock_guard lock(state->mutex);
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
        if (allocator) allocator->Destroy();
        allocator.reset();
        MyDevice::GetInstance().vkDevice = previousDevice;
        vkCreateDescriptorSetLayout = previousLayout; vkDestroyDescriptorSetLayout = previousDestroyLayout;
        vkCreateDescriptorPool = previousCreate; vkDestroyDescriptorPool = previousDestroy;
        vkAllocateDescriptorSets = previousAllocate; vkFreeDescriptorSets = previousFree;
        vkResetDescriptorPool = previousReset; vkUpdateDescriptorSets = previousUpdate;
        state = nullptr;
    }
    void DestroyLayoutCache()
    {
        auto allocator = DescriptorPoolTestProbe::Exchange({});
        if (!allocator) return;
        allocator->Destroy();
        allocator->Destroy();
    }
};
template<class F> void ExpectFailure(F&& function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    CHECK_TRUE(failed, "Descriptor operation should reject this request!");
}
void TestLayoutCacheAndBorrowedMetadata()
{
    DescriptorSetLayout surviving;
    {
        Environment environment;
        auto& device = MyDevice::GetInstance();
        VkDescriptorSetLayout handle = VK_NULL_HANDLE;
        {
            DescriptorSetLayoutCreateInfo info;
            info.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
            info.SetBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
            DescriptorSetLayout first;
            device.GetOrCreateDescriptorSetLayout(first, info);
            handle = first.GetVkDescriptorSetLayout();
            const auto& native = state->layouts.at(handle);
            CHECK_TRUE(native.size() == 2 && native[0].binding == 1 && native[1].binding == 4 && native[1].descriptorCount == 2,
                "Device must sort and preserve sparse native layout bindings!");
            surviving = first;
            DescriptorSetLayout copied(first);
            DescriptorSetLayout moved(std::move(copied));
            DescriptorSetLayout assigned;
            assigned = std::move(moved);
            first = {};
            copied = {};
            CHECK_TRUE(assigned.GetVkDescriptorSetLayout() == handle &&
                assigned.GetVkDescriptorSetLayoutBinding(4).descriptorCount == 2 && state->layoutDestroys == 0,
                "Layout copy, move and clear must preserve other values without touching native lifetime!");
            DescriptorSetLayoutCreateInfo reordered;
            reordered.SetBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
            reordered.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
            DescriptorSetLayout same;
            device.GetOrCreateDescriptorSetLayout(same, reordered);
            CHECK_TRUE(same.GetVkDescriptorSetLayout() == handle && state->layoutAttempts == 1,
                "Equivalent layout descriptions must share the cached handle regardless of setting order!");
            for (int variant = 0; variant < 4; ++variant)
            {
                auto changed = info;
                if (variant == 0) changed.SetBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
                if (variant == 1) changed.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 2);
                if (variant == 2) changed.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3);
                if (variant == 3) changed.SetBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
                DescriptorSetLayout distinct;
                device.GetOrCreateDescriptorSetLayout(distinct, changed);
                CHECK_TRUE(distinct.GetVkDescriptorSetLayout() != handle, "Each layout binding field must participate in cache identity!");
            }
            info.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 7);
        }
        CHECK_TRUE(surviving.GetVkDescriptorSetLayout() == handle &&
            surviving.GetVkDescriptorSetLayoutBinding(4).descriptorCount == 2 &&
            surviving.GetVkDescriptorSetLayoutBinding(4).stageFlags == VK_SHADER_STAGE_COMPUTE_BIT,
            "A layout must retain its binding snapshot after input mutation or destruction!");
        surviving = {};
        DescriptorSetLayoutCreateInfo original;
        original.SetBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
        original.SetBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
        device.GetOrCreateDescriptorSetLayout(surviving, original);
        CHECK_TRUE(surviving.GetVkDescriptorSetLayout() == handle && state->layoutCreates == 5 && state->layoutDestroys == 0,
            "The cache must survive destruction of every previous layout wrapper!");
        DescriptorSetLayout empty, sameEmpty;
        device.GetOrCreateDescriptorSetLayout(empty, {});
        device.GetOrCreateDescriptorSetLayout(sameEmpty, {});
        CHECK_TRUE(empty.GetVkDescriptorSetLayout() != VK_NULL_HANDLE && empty.GetVkDescriptorSetLayout() == sameEmpty.GetVkDescriptorSetLayout() &&
            state->layouts.at(empty.GetVkDescriptorSetLayout()).empty(), "Empty layouts must be valid and cached!");
        environment.DestroyLayoutCache();
        CHECK_TRUE(state->layoutCreates == 6 && state->layoutDestroys == 6 && state->layouts.empty(),
            "Cache destruction and repeated cleanup must destroy each native layout exactly once!");
        CHECK_TRUE(surviving.GetVkDescriptorSetLayout() == handle && surviving.GetVkDescriptorSetLayoutBinding(4).descriptorCount == 2,
            "Cache destruction must not mutate borrowed wrapper values!");
    }
    surviving = {};
}
void TestLayoutDeviceGuardsAndRetry()
{
    Environment environment;
    auto& device = MyDevice::GetInstance();
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout;
    device.vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(layout, info); });
    device.vkDevice = Handle<VkDevice>(1);
    auto allocator = DescriptorPoolTestProbe::Exchange({});
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(layout, info); });
    static_cast<void>(DescriptorPoolTestProbe::Exchange(std::move(allocator)));
    auto previous = DescriptorPoolTestProbe::Exchange(std::make_unique<DescriptorSetLayoutAllocator>());
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(layout, info); });
    static_cast<void>(DescriptorPoolTestProbe::Exchange(std::move(previous)));
    CHECK_TRUE(layout.GetVkDescriptorSetLayout() == VK_NULL_HANDLE && state->layoutAttempts == 0,
        "An unavailable Device or allocator must reject creation before native calls!");
    state->layoutResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(layout, info); });
    ExpectFailure([&] { static_cast<void>(layout.GetVkDescriptorSetLayoutBinding(2)); });
    CHECK_TRUE(layout.GetVkDescriptorSetLayout() == VK_NULL_HANDLE && state->layouts.empty() && state->layoutDestroys == 0,
        "Native failure must leave the output and its metadata empty without a cached native object!");
    state->layoutResult = VK_SUCCESS;
    device.GetOrCreateDescriptorSetLayout(layout, info);
    const auto handle = layout.GetVkDescriptorSetLayout();
    state->layoutResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const auto calls = state->layoutAttempts;
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(layout, {}); });
    CHECK_TRUE(layout.GetVkDescriptorSetLayout() == handle && layout.GetVkDescriptorSetLayoutBinding(2).descriptorCount == 1 &&
        state->layoutAttempts == calls, "Duplicate creation must preserve the existing layout before native calls!");
    DescriptorSetLayout cached;
    device.GetOrCreateDescriptorSetLayout(cached, info);
    CHECK_TRUE(cached.GetVkDescriptorSetLayout() == handle && state->layoutAttempts == calls,
        "A cache hit must not enter a failing native creation path!");
    auto changed = info;
    changed.SetBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2);
    DescriptorSetLayout retry;
    ExpectFailure([&] { device.GetOrCreateDescriptorSetLayout(retry, changed); });
    state->layoutResult = VK_SUCCESS;
    device.GetOrCreateDescriptorSetLayout(retry, changed);
    CHECK_TRUE(retry.GetVkDescriptorSetLayout() != handle && retry.GetVkDescriptorSetLayoutBinding(2).descriptorCount == 2,
        "A failed new description must be retryable without corrupting old cache entries!");
}
void TestLayoutValueOperationsDuringCacheCreation()
{
    Environment environment;
    auto& device = MyDevice::GetInstance();
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout source;
    device.GetOrCreateDescriptorSetLayout(source, info);
    const auto handle = source.GetVkDescriptorSetLayout();
    std::barrier start(5);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&]
    {
        start.arrive_and_wait();
        for (int j = 0; j < 100; ++j)
        {
            DescriptorSetLayout copy(source);
            DescriptorSetLayout moved(std::move(copy));
            CHECK_TRUE(moved.GetVkDescriptorSetLayout() == handle && moved.GetVkDescriptorSetLayoutBinding(0).descriptorCount == 1,
                "Independent wrapper operations must preserve their binding metadata!");
            moved = {};
        }
    });
    start.arrive_and_wait();
    for (uint32_t count = 2; count <= 21; ++count)
    {
        info.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, count);
        DescriptorSetLayout created;
        device.GetOrCreateDescriptorSetLayout(created, info);
    }
    for (auto& thread : threads) thread.join();
    CHECK_TRUE(state->layoutCreates == 21 && state->layoutDestroys == 0 && state->layoutWorkerCalls == 0,
        "Only the externally serialized cache owner may call native layout APIs; wrapper operations remain host-only!");
}
void TestOutstandingAllocationCleanup()
{
    Environment environment;
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, {});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    for (int i = 0; i < 3; ++i) static_cast<void>(pool->Allocate(layout, {}));
    CHECK_TRUE(state->freeAttempts == 0 && state->destroys == 0 && state->sets.size() == 3,
        "The pool must retain allocations even when callers discard their handles!");
    pool.reset();
    CHECK_TRUE(state->destroys == state->creates && state->sets.empty() && state->freeAttempts == 0,
        "Pool destruction must reclaim abandoned allocations without individual frees!");
}
void TestReferenceCountedPoolLifetime()
{
    static_assert(std::is_base_of_v<RefCounted, DescriptorPool>);
    VkDescriptorSet dangling = VK_NULL_HANDLE;
    {
        Environment environment;
        DescriptorSetLayoutCreateInfo info;
        info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
        DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
        uint32_t destroyedViews = 0;
        auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView)
        {
            CHECK_TRUE(state->pages.empty(), "Destroy native pools before releasing their retained views!");
            ++destroyedViews;
        });
        DescriptorSetDesc description; description.SetImage(0, view);
        auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
        auto otherOwner = pool;
        dangling = pool->Allocate(layout, description);
        const auto handle = dangling;
        description = {}; view.Reset();
        pool.reset();
        CHECK_TRUE(state->destroys == 0 && destroyedViews == 0, "An independent owner must keep the pool and its views alive!");
        otherOwner.reset();
        CHECK_TRUE(state->destroys == 1 && destroyedViews == 1 && state->sets.empty() && state->pages.empty(),
            "A borrowed set must not retain its pool; final owner release reclaims remaining allocations!");
        CHECK_TRUE(state->freeAttempts == 0 && dangling == handle,
            "Pool destruction invalidates borrowed handles without individual frees or clearing caller variables!");
    }
}
void TestExplicitFree()
{
    Environment environment;
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
    uint32_t destroyedViews = 0;
    auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
    DescriptorSetDesc description; description.SetImage(0, view);
    auto first = MakeIntrusive<DescriptorPool>();
    auto second = MakeIntrusive<DescriptorPool>(); first->Create(); second->Create();
    VkDescriptorSet empty = VK_NULL_HANDLE;
    first->Free(empty);
    auto uncreated = MakeIntrusive<DescriptorPool>();
    uncreated->Free(empty);
    auto set = first->Allocate(layout, description);
    auto independent = first->Allocate(layout, description);
    CHECK_TRUE(set != independent, "User allocations must not deduplicate descriptions!");
    first->Free(independent);
    description = {}; view.Reset();
    const auto handle = set;
    const auto attempts = state->freeAttempts;
    ExpectFailure([&] { second->Free(set); });
    ExpectFailure([&] { uncreated->Free(set); });
    auto unknown = Handle<VkDescriptorSet>(99999);
    ExpectFailure([&] { first->Free(unknown); });
    CHECK_TRUE(state->freeAttempts == attempts && set == handle && destroyedViews == 0,
        "A foreign or uncreated pool must reject Free before native calls without changing the set!");
    CHECK_TRUE(unknown == Handle<VkDescriptorSet>(99999), "An unregistered handle must be rejected without changing it!");
    state->freeResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    ExpectFailure([&] { first->Free(set); });
    CHECK_TRUE(state->freeAttempts == attempts + 1 && state->sets.contains(handle) &&
        set == handle && destroyedViews == 0,
        "Failed explicit Free must retain the allocation, handle and image views for retry!");
    state->freeResult = VK_SUCCESS;
    auto oldCopy = set;
    first->Free(set);
    CHECK_TRUE(set == VK_NULL_HANDLE && state->frees == 2 && state->sets.empty() && destroyedViews == 1,
        "Successful Free must release its allocation and dependencies and clear the set!");
    first->Free(set);
    CHECK_TRUE(state->freeAttempts == attempts + 2, "Free on a null handle must be a native no-op!");
    // No intervening allocation can reuse this handle value.
    ExpectFailure([&] { first->Free(oldCopy); });
    CHECK_TRUE(oldCopy == handle && state->freeAttempts == attempts + 2,
        "An old handle copy must be rejected before native Free when its allocation is no longer tracked!");
}
void TestGrowthAndFailures()
{
    Environment environment;
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, {});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    std::vector<VkDescriptorSet> sets;
    for (int i = 0; i < 5; ++i) sets.push_back(pool->Allocate(layout, {}));
    CHECK_TRUE(state->creates == 3, "Full descriptor pages must roll over!");
    for (auto& set : sets) pool->Free(set);
    sets.clear();
    const auto creates = state->creates;
    auto reused = pool->Allocate(layout, {}); pool->Free(reused);
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
    DescriptorSetLayout large; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(large, largeInfo);
    auto growing = MakeIntrusive<DescriptorPool>(); growing->Create();
    auto largeSet = growing->Allocate(large, {});
    CHECK_TRUE(state->maxStorage >= 5000, "Native budget must fit an oversized layout!");
    growing->Free(largeSet); growing.reset();

    DescriptorSetLayoutCreateInfo overflowingInfo;
    overflowingInfo.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, UINT32_MAX);
    overflowingInfo.SetBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1);
    DescriptorSetLayout overflowing; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(overflowing, overflowingInfo);
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
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
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
    ExpectFailure([&] { static_cast<void>(dynamic.GetOrAllocateTransient(layout, description)); });
    CHECK_TRUE(state->frees == frees, "Dynamic failure must not individually free native sets!");
    state->throwUpdate = false;
    const auto afterFailedPublication = state->allocations;
    const auto updatesAfterFailure = state->updates;
    const auto first = dynamic.GetOrAllocateTransient(layout, description);
    CHECK_TRUE(state->allocations == afterFailedPublication + 1 && state->updates == updatesAfterFailure + 1,
        "A failed dynamic write must not publish a cache entry!");
    for (const auto& [handle, page] : state->pages)
        CHECK_TRUE((page.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0,
            "Dynamic pools must omit individual-free flags!");
    DescriptorSetDesc reordered; reordered.SetImage(1, view); reordered.SetImage(0, view);
    const auto allocations = state->allocations;
    CHECK_TRUE(dynamic.GetOrAllocateTransient(layout, reordered) == first && state->allocations == allocations,
        "Equivalent dynamic descriptions must share a cached set!");
    for (uint32_t i = 0; i < 5; ++i)
    {
        DescriptorSetDesc distinct;
        distinct.SetImage(0, view, static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_GENERAL + i));
        state->expectedLayouts[0] = static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_GENERAL + i);
        static_cast<void>(dynamic.GetOrAllocateTransient(layout, distinct));
    }
    state->expectedLayouts[0] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    CHECK_TRUE(state->creates >= 4, "Dynamic sets must also roll over full native pages!");
    const auto creates = state->creates;
    dynamic.Reset();
    static_cast<void>(dynamic.GetOrAllocateTransient(layout, description));
    CHECK_TRUE(state->creates == creates && state->frees == frees, "Reset must reuse dynamic pages without individual frees!");
    dynamic.Destroy();
    auto retaining = MakeIntrusive<DescriptorPool>(); retaining->Create();
    auto retained = retaining->Allocate(layout, description);
    view.Reset(); description = {}; reordered = {};
    CHECK_TRUE(destroyedViews == 0, "Pool allocation records must retain image views independently of descriptions!");
    retaining->Free(retained);
    CHECK_TRUE(destroyedViews == 1, "Freeing the last set must release its retained image view!");
    retaining.reset();
}
void TestExternallySynchronizedPoolAccess()
{
    Environment environment;
    state->limit = 1000;
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, {});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    std::mutex poolMutex;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&, owner = pool]
    {
        for (int j = 0; j < 100; ++j)
        {
            VkDescriptorSet set = VK_NULL_HANDLE;
            {
                std::lock_guard lock(poolMutex);
                set = owner->Allocate(layout, {});
            }
            std::this_thread::yield();
            {
                std::lock_guard lock(poolMutex);
                owner->Free(set);
            }
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK_TRUE(!state->overlap && state->allocations == 400 && state->frees == 400 && state->sets.empty(),
        "Caller serialization must protect allocation and release through the same pool!");
    pool.reset();
    CHECK_TRUE(state->creates == 1 && state->destroys == 1, "Serialized access must reuse and finally destroy its native page!");
}
void TestIndependentPoolsOnDifferentThreads()
{
    Environment environment;
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
    uint32_t destroyedViews = 0;
    auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
    DescriptorSetDesc description; description.SetImage(0, view);
    std::barrier start(4);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&]
    {
        start.arrive_and_wait();
        auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
        for (int j = 0; j < 100; ++j)
        {
            auto set = pool->Allocate(layout, description);
            pool->Free(set);
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK_TRUE(state->creates == 4 && state->destroys == 4 && state->pages.empty(),
        "Independent pools must each own and destroy their native pages on their calling thread!");
    CHECK_TRUE(state->allocations == 400 && state->updates == 400 && state->frees == 400 && state->sets.empty(),
        "Independent pools must allocate, write and return every set to its source pool!");
    CHECK_TRUE(destroyedViews == 0, "Worker releases must preserve the externally retained image view!");
    description = {}; view.Reset();
    CHECK_TRUE(destroyedViews == 1, "The shared view must be released after all worker references disappear!");
}
void TestPoolOwnershipHandoff()
{
    Environment environment;
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, {});
    auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
    auto set = pool->Allocate(layout, {});
    std::thread worker([&, owner = std::move(pool), set = std::exchange(set, VK_NULL_HANDLE)]() mutable
    {
        CHECK_TRUE(set != VK_NULL_HANDLE && state->destroys == 0, "Explicit owner handoff must keep the borrowed set usable!");
        owner->Free(set);
        owner.reset();
    });
    worker.join();
    CHECK_TRUE(set == VK_NULL_HANDLE && state->frees == 1 && state->destroys == 1 && state->pages.empty() && state->sets.empty(),
        "A pool may be explicitly freed and destroyed on another thread after exclusive ownership handoff!");
}
void TestPartialDynamicResetFailure()
{
    Environment environment;
    state->limit = 1;
    DescriptorSetLayoutCreateInfo info;
    info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
    uint32_t destroyedViews = 0;
    VulkanDetail::DynamicDescriptorPool dynamic; dynamic.Create();
    // The empty description is deliberately cached so failure must reject a cache hit too.
    static_cast<void>(dynamic.GetOrAllocateTransient(layout, {}));
    auto view = DescriptorPoolTestProbe::ImageViewForTest([&](VkImageView) { ++destroyedViews; });
    DescriptorSetDesc description; description.SetImage(0, view);
    static_cast<void>(dynamic.GetOrAllocateTransient(layout, description));
    description.SetImage(0, view, VK_IMAGE_LAYOUT_GENERAL);
    state->expectedLayouts[0] = VK_IMAGE_LAYOUT_GENERAL;
    static_cast<void>(dynamic.GetOrAllocateTransient(layout, description));
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
    ExpectFailure([&] { static_cast<void>(dynamic.GetOrAllocateTransient(layout, {})); });
    CHECK_TRUE(state->allocations == allocations && state->creates == creates,
        "Failed reset must reject cached and new allocations before native calls!");
    dynamic.Destroy(); dynamic.Create();
    static_cast<void>(dynamic.GetOrAllocateTransient(layout, {}));
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
    ExpectFatalChild("--descriptor-fatal-rollback");
}
}
auto RunDescriptorPoolFatalCase(std::string_view argument) -> int
{
    if (argument != "--descriptor-fatal-rollback") return -1;
    std::set_terminate(FatalTerminationHandler);
    try
    {
        Environment environment;
        DescriptorSetLayoutCreateInfo info;
        info.SetBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
        DescriptorSetLayout layout; MyDevice::GetInstance().GetOrCreateDescriptorSetLayout(layout, info);
        auto view = DescriptorPoolTestProbe::ImageViewForTest([](VkImageView) {});
        DescriptorSetDesc description; description.SetImage(0, view);
        auto pool = MakeIntrusive<DescriptorPool>(); pool->Create();
        state->throwUpdate = true;
        state->freeResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        auto set = pool->Allocate(layout, description);
        // Returning normally or throwing is a failed death test.
        std::_Exit(UnexpectedFatalExitCode);
    }
    catch (...) { std::_Exit(UnexpectedFatalExitCode); }
}
void RunDescriptorPoolTests()
{
    TestLayoutCacheAndBorrowedMetadata();
    TestLayoutDeviceGuardsAndRetry();
    TestLayoutValueOperationsDuringCacheCreation();
    TestOutstandingAllocationCleanup();
    TestReferenceCountedPoolLifetime();
    TestExplicitFree();
    TestGrowthAndFailures();
    TestDynamicAndRollback();
    TestExternallySynchronizedPoolAccess();
    TestIndependentPoolsOnDifferentThreads();
    TestPoolOwnershipHandoff();
    TestPartialDynamicResetFailure();
    TestFatalContracts();
}
