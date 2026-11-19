#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/allocator/private/sampler_allocator.h"
#include <array>
#include <atomic>
#include <barrier>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>

static_assert(std::is_nothrow_copy_constructible_v<Sampler>);
static_assert(std::is_nothrow_copy_assignable_v<Sampler>);
static_assert(std::is_nothrow_move_constructible_v<Sampler>);
static_assert(std::is_nothrow_move_assignable_v<Sampler>);
static_assert(std::is_trivially_destructible_v<Sampler>);

struct SamplerCreationTestProbe
{
    static auto Exchange(std::unique_ptr<SamplerAllocator> inReplacement) -> std::unique_ptr<SamplerAllocator>
    {
        auto& allocator = MyDevice::GetInstance().m_uptrSamplerAllocator;
        auto previous = std::move(allocator);
        allocator = std::move(inReplacement);
        return previous;
    }
};

namespace
{
template<class T> auto Handle(uintptr_t inValue) -> T { return reinterpret_cast<T>(inValue); }

struct NativeState
{
    std::mutex mutex;
    const std::thread::id apiThread = std::this_thread::get_id();
    std::atomic_uint workerCalls{0};
    uintptr_t next = 100;
    uint32_t attempts = 0, creates = 0, destroys = 0;
    VkResult result = VK_SUCCESS;
    VkSampler coalescedHandle = VK_NULL_HANDLE;
    std::unordered_map<VkSampler, VkSamplerCreateInfo> live;
    std::unordered_map<VkSampler, uint32_t> nativeReferences;
};
NativeState* state = nullptr;

auto VKAPI_PTR CreateSampler(VkDevice inDevice, const VkSamplerCreateInfo* inInfo,
    const VkAllocationCallbacks*, VkSampler* outSampler) -> VkResult
{
    if (std::this_thread::get_id() != state->apiThread) ++state->workerCalls;
    std::lock_guard lock(state->mutex);
    ++state->attempts;
    CHECK_TRUE(inDevice == Handle<VkDevice>(1), "Sampler creation must use the active device!");
    CHECK_TRUE(inInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && inInfo->pNext == nullptr,
        "Sampler creation must pass normalized native parameters!");
    if (state->result != VK_SUCCESS) return state->result;
    *outSampler = state->coalescedHandle != VK_NULL_HANDLE
        ? state->coalescedHandle : Handle<VkSampler>(state->next++);
    state->live.emplace(*outSampler, *inInfo);
    ++state->nativeReferences[*outSampler];
    ++state->creates;
    return VK_SUCCESS;
}

void VKAPI_PTR DestroySampler(VkDevice inDevice, VkSampler inSampler, const VkAllocationCallbacks*)
{
    if (std::this_thread::get_id() != state->apiThread) ++state->workerCalls;
    std::lock_guard lock(state->mutex);
    CHECK_TRUE(inDevice == Handle<VkDevice>(1), "Sampler destruction must use its live device!");
    auto& references = state->nativeReferences.at(inSampler);
    CHECK_TRUE(references != 0, "Native creation references must not be released twice!");
    if (--references == 0)
    {
        state->nativeReferences.erase(inSampler);
        CHECK_TRUE(state->live.erase(inSampler) == 1, "The final native reference must destroy its sampler!");
    }
    ++state->destroys;
}

struct Environment
{
    NativeState native;
    VkDevice previousDevice = MyDevice::GetInstance().vkDevice;
    PFN_vkCreateSampler previousCreate = vkCreateSampler;
    PFN_vkDestroySampler previousDestroy = vkDestroySampler;
    std::unique_ptr<SamplerAllocator> previousAllocator;

    Environment()
    {
        state = &native;
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(1);
        vkCreateSampler = CreateSampler;
        vkDestroySampler = DestroySampler;
        auto allocator = std::make_unique<SamplerAllocator>();
        allocator->Create();
        previousAllocator = SamplerCreationTestProbe::Exchange(std::move(allocator));
    }

    void DestroyCache()
    {
        auto allocator = SamplerCreationTestProbe::Exchange({});
        if (!allocator) return;
        allocator->Destroy();
        allocator->Destroy(); // Repeated cleanup and the subsequent destructor must be harmless.
    }

    ~Environment()
    {
        auto allocator = SamplerCreationTestProbe::Exchange(std::move(previousAllocator));
        allocator.reset();
        MyDevice::GetInstance().vkDevice = previousDevice;
        vkCreateSampler = previousCreate;
        vkDestroySampler = previousDestroy;
        state = nullptr;
    }
};

template<class F> void ExpectFailure(F&& inFunction)
{
    bool failed = false;
    try { inFunction(); } catch (const std::exception&) { failed = true; }
    CHECK_TRUE(failed, "Sampler creation should reject this request!");
}

void TestBorrowedValuesAndCacheLifetime()
{
    Sampler surviving;
    {
        Environment environment;
        auto& device = MyDevice::GetInstance();
        { Sampler empty; }
        CHECK_TRUE(state->attempts == 0 && state->destroys == 0, "Empty sampler destruction must not call Vulkan!");
        const SamplerCreateInfo info;
        VkSampler handle = VK_NULL_HANDLE;
        {
            Sampler first;
            device.GetOrCreateSampler(first, info);
            handle = first.GetVkSampler();
            CHECK_TRUE(handle != VK_NULL_HANDLE && state->creates == 1, "Device creation must initialize the sampler!");
            const auto& nativeInfo = state->live.at(handle);
            CHECK_TRUE(nativeInfo.minFilter == VK_FILTER_LINEAR && nativeInfo.addressModeU == VK_SAMPLER_ADDRESS_MODE_REPEAT,
                "Device creation must preserve the supplied sampler parameters!");
            Sampler copied(first);
            Sampler assigned;
            assigned = first;
            Sampler moved(std::move(copied));
            Sampler moveAssigned;
            moveAssigned = std::move(assigned);
            CHECK_TRUE(copied.GetVkSampler() == handle && moved.GetVkSampler() == handle &&
                assigned.GetVkSampler() == handle && moveAssigned.GetVkSampler() == handle,
                "Copy and move must copy only the borrowed handle and preserve the move source!");
            first = {};
            copied = {};
            surviving = moved;
            CHECK_TRUE(first.GetVkSampler() == VK_NULL_HANDLE && copied.GetVkSampler() == VK_NULL_HANDLE &&
                state->attempts == 1 && state->destroys == 0,
                "Clearing borrowed values must not allocate or release native samplers!");
        }
        CHECK_TRUE(state->destroys == 0 && state->live.contains(handle),
            "Wrapper destruction must leave the native sampler in the device cache!");
        surviving = {};
        {
            Sampler cached;
            device.GetOrCreateSampler(cached, info);
            CHECK_TRUE(cached.GetVkSampler() == handle && state->attempts == 1,
                "The cache must retain its sampler after every previous wrapper disappears!");
            Sampler distinct;
            SamplerCreateInfo distinctInfo;
            distinctInfo.CustomizeAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            device.GetOrCreateSampler(distinct, distinctInfo);
            CHECK_TRUE(distinct.GetVkSampler() != handle && state->creates == 2 &&
                state->live.at(distinct.GetVkSampler()).addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                "Different sampler state must create a separate correctly initialized cache entry!");
            surviving = cached;
        }
        environment.DestroyCache();
        CHECK_TRUE(state->destroys == 2 && state->live.empty() && state->nativeReferences.empty(),
            "Cache destruction must reclaim every successful native creation exactly once!");
        // Borrowed values may still exist after invalidation, but must not be used with Vulkan.
        device.vkDevice = VK_NULL_HANDLE;
        Sampler staleCopy = surviving;
        staleCopy = {};
        CHECK_TRUE(state->destroys == 2, "Clearing a stale borrowed value must not access an unavailable device!");
    }
    surviving = {}; // No device or allocator access after the test environment has gone away.
}

void TestRejectedAndFailedCreation()
{
    Environment environment;
    auto& device = MyDevice::GetInstance();
    const SamplerCreateInfo info;
    Sampler sampler;
    auto allocator = SamplerCreationTestProbe::Exchange({});
    ExpectFailure([&] { device.GetOrCreateSampler(sampler, info); });
    CHECK_TRUE(sampler.GetVkSampler() == VK_NULL_HANDLE && state->attempts == 0,
        "Missing allocator must fail before native creation!");
    static_cast<void>(SamplerCreationTestProbe::Exchange(std::move(allocator)));
    device.vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { device.GetOrCreateSampler(sampler, info); });
    CHECK_TRUE(sampler.GetVkSampler() == VK_NULL_HANDLE && state->attempts == 0,
        "Missing device must fail before native creation!");
    device.vkDevice = Handle<VkDevice>(1);
    state->result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ExpectFailure([&] { device.GetOrCreateSampler(sampler, info); });
        CHECK_TRUE(sampler.GetVkSampler() == VK_NULL_HANDLE && state->live.empty() && state->destroys == 0,
            "Failed creation must leave the target empty without publishing a cached allocation!");
    }
    state->result = VK_SUCCESS;
    device.GetOrCreateSampler(sampler, info);
    const auto handle = sampler.GetVkSampler();
    const auto attempts = state->attempts;
    ExpectFailure([&] { device.GetOrCreateSampler(sampler, info); });
    CHECK_TRUE(sampler.GetVkSampler() == handle && state->attempts == attempts,
        "Repeated creation must preserve the target and fail before consulting the native path!");
    state->result = VK_ERROR_OUT_OF_HOST_MEMORY;
    Sampler cached;
    device.GetOrCreateSampler(cached, info);
    CHECK_TRUE(cached.GetVkSampler() == handle && state->attempts == attempts,
        "A cache hit must not attempt native creation even while native creation would fail!");
    SamplerCreateInfo distinct;
    distinct.CustomizeAddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    Sampler retry;
    ExpectFailure([&] { device.GetOrCreateSampler(retry, distinct); });
    CHECK_TRUE(retry.GetVkSampler() == VK_NULL_HANDLE && state->live.size() == 1 && state->live.contains(handle),
        "A failed cache miss must preserve existing cached samplers!");
    state->result = VK_SUCCESS;
    device.GetOrCreateSampler(retry, distinct);
    CHECK_TRUE(state->creates == 2 && state->destroys == 0, "Retry must publish the new sampler without releasing existing entries!");
    environment.DestroyCache();
    CHECK_TRUE(state->destroys == 2 && state->live.empty(), "Cache destruction must clean up successful retry results!");
}

void TestCoalescedNativeHandles()
{
    Environment environment;
    auto& device = MyDevice::GetInstance();
    const SamplerCreateInfo original;
    SamplerCreateInfo equivalent;
    // Border color is ignored by REPEAT address modes, so a driver may coalesce these state vectors.
    equivalent.CustomizeBorderColor(VK_BORDER_COLOR_INT_OPAQUE_WHITE);
    Sampler first;
    device.GetOrCreateSampler(first, original);
    const auto handle = first.GetVkSampler();
    state->coalescedHandle = handle;
    Sampler second, shared;
    device.GetOrCreateSampler(second, equivalent);
    device.GetOrCreateSampler(shared, equivalent);
    CHECK_TRUE(second.GetVkSampler() == handle && shared.GetVkSampler() == handle && state->creates == 2,
        "Coalesced handles must support both descriptions and subsequent cache hits!");
    first = {}; second = {}; shared = {};
    CHECK_TRUE(state->destroys == 0 && state->nativeReferences.at(handle) == 2,
        "Borrowed wrappers must not release either native creation sharing the handle!");
    environment.DestroyCache();
    CHECK_TRUE(state->destroys == 2 && state->live.empty() && state->nativeReferences.empty(),
        "Cache destruction must balance every native creation, even when handle values are equal!");
}

void TestBorrowedValuesWhileCreating()
{
    Environment environment;
    auto& device = MyDevice::GetInstance();
    constexpr size_t workerCount = 4;
    constexpr size_t rounds = 64;
    Sampler seed;
    device.GetOrCreateSampler(seed, {});
    const auto seedHandle = seed.GetVkSampler();
    std::array<std::vector<Sampler>, workerCount> values;
    for (auto& worker : values) worker.assign(rounds, seed);
    std::atomic_bool valuesValid{true};
    std::barrier phase(static_cast<std::ptrdiff_t>(workerCount + 1));
    std::vector<std::thread> threads;
    for (size_t worker = 0; worker < workerCount; ++worker)
        threads.emplace_back([&, worker]
        {
            for (size_t round = 0; round < rounds; ++round)
            {
                phase.arrive_and_wait();
                {
                    Sampler copied(values[worker][round]);
                    Sampler moved(std::move(copied));
                    if (copied.GetVkSampler() != seedHandle || moved.GetVkSampler() != seedHandle)
                        valuesValid = false;
                    values[worker][round] = {};
                    moved = {};
                }
                phase.arrive_and_wait();
            }
            values[worker].clear();
        });
    for (size_t round = 0; round < rounds; ++round)
    {
        phase.arrive_and_wait();
        {
            Sampler sampler;
            SamplerCreateInfo info;
            info.CustomizeLod(static_cast<float>(round + 1), static_cast<float>(round + 2));
            device.GetOrCreateSampler(sampler, info);
        }
        phase.arrive_and_wait();
    }
    for (auto& thread : threads) thread.join();
    CHECK_TRUE(valuesValid && state->workerCalls == 0,
        "Borrowed value operations on worker threads must preserve handles without entering native paths!");
    CHECK_TRUE(state->creates == rounds + 1 && state->destroys == 0 && state->live.size() == rounds + 1,
        "One serial creator must retain every cache entry while other threads destroy borrowed values!");
    environment.DestroyCache();
    CHECK_TRUE(state->destroys == rounds + 1 && state->live.empty(),
        "Cache cleanup after worker completion must release all retained native creations!");
}
}

void RunSamplerCreationTests()
{
    TestBorrowedValuesAndCacheLifetime();
    TestRejectedAndFailedCreation();
    TestCoalescedNativeHandles();
    TestBorrowedValuesWhileCreating();
}
