#include "common.h"
#include "my_vulkan/device.h"
#include "my_vulkan/command/command_pool.h"
#include "my_vulkan/render_context/private/frame_slot.h"
#include <atomic>
#include <thread>
#include <type_traits>
#include <mutex>

static_assert(!std::is_copy_constructible_v<CommandPool> && !std::is_copy_assignable_v<CommandPool>);
static_assert(!std::is_move_constructible_v<CommandPool> && !std::is_move_assignable_v<CommandPool>);
static_assert(std::is_nothrow_destructible_v<CommandPool>);
template<class T> concept HasPublicDestroy = requires(T& value) { value.Destroy(); };
static_assert(!HasPublicDestroy<CommandPool>);

namespace { VkResult createResult = VK_SUCCESS; }

struct CommandPoolTestProbe
{
    static void RetryFramePool()
    {
        FrameSlot slot;
        bool failed = false;
        try { slot._GetCommandPool(QueueFamilyType::GRAPHICS, 0); }
        catch (const std::exception&) { failed = true; }
        CHECK_TRUE(failed && slot.m_commandPools[0][0] == nullptr, "Failed frame pool must not be published!");
        createResult = VK_SUCCESS;
        auto& pool = slot._GetCommandPool(QueueFamilyType::GRAPHICS, 0);
        CHECK_TRUE(pool.GetVkCommandPool() != VK_NULL_HANDLE && &pool == &slot._GetCommandPool(QueueFamilyType::GRAPHICS, 0),
            "Same frame slot must retry and retain its successful pool!");
    }
};

namespace
{
template<class T> auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }
std::atomic_uint creates{0}, destroys{0}, frees{0}, waits{0}, allocations{0};
VkResult resetResult = VK_SUCCESS;
std::mutex nativeMutex;
std::unordered_map<VkCommandPool, std::unordered_map<VkCommandBuffer, VkCommandBufferLevel>> livePools;
auto VKAPI_PTR Create(VkDevice device, const VkCommandPoolCreateInfo* info, const VkAllocationCallbacks*, VkCommandPool* pool) -> VkResult
{
    CHECK_TRUE(device == Handle<VkDevice>(1) && info->queueFamilyIndex == 3, "Unexpected pool creation inputs!");
    const auto id = ++creates;
    if (createResult != VK_SUCCESS) return createResult;
    *pool = Handle<VkCommandPool>(100 + id);
    std::lock_guard lock(nativeMutex);
    CHECK_TRUE(livePools.emplace(*pool, std::unordered_map<VkCommandBuffer, VkCommandBufferLevel>{}).second, "Pool handle must be unique!");
    return VK_SUCCESS;
}
void VKAPI_PTR Destroy(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks*)
{
    CHECK_TRUE(device == Handle<VkDevice>(1), "Destruction must use creating device!");
    std::lock_guard lock(nativeMutex);
    CHECK_TRUE(livePools.erase(pool) == 1, "Pool must be destroyed exactly once!");
    ++destroys;
}
auto VKAPI_PTR Allocate(VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* buffer) -> VkResult
{
    CHECK_TRUE(device == Handle<VkDevice>(1) && info->commandBufferCount == 1, "Unexpected allocation inputs!");
    std::lock_guard lock(nativeMutex);
    CHECK_TRUE(livePools.contains(info->commandPool), "Allocation requires a live owning pool!");
    *buffer = Handle<VkCommandBuffer>(1000 + ++allocations);
    livePools.at(info->commandPool).emplace(*buffer, info->level);
    return VK_SUCCESS;
}
void VKAPI_PTR Free(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*) { ++frees; }
auto VKAPI_PTR Reset(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags) -> VkResult
{
    std::lock_guard lock(nativeMutex);
    CHECK_TRUE(device == Handle<VkDevice>(1) && livePools.contains(pool), "Reset requires live pool and device!");
    return resetResult;
}
auto VKAPI_PTR Wait(VkDevice) -> VkResult { ++waits; return VK_SUCCESS; }
template<class F> void Fails(F&& function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    CHECK_TRUE(failed, "Expected command pool operation to fail!");
}
struct Environment
{
    VkDevice device = MyDevice::GetInstance().vkDevice;
    decltype(MyDevice::GetInstance().queueFamilyIndices) families = MyDevice::GetInstance().queueFamilyIndices;
    PFN_vkCreateCommandPool create = vkCreateCommandPool;
    PFN_vkDestroyCommandPool destroy = vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers allocate = vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers free = vkFreeCommandBuffers;
    PFN_vkResetCommandPool reset = vkResetCommandPool;
    PFN_vkDeviceWaitIdle wait = vkDeviceWaitIdle;
    Environment()
    {
        creates = destroys = frees = waits = allocations = 0;
        createResult = resetResult = VK_SUCCESS;
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(1);
        MyDevice::GetInstance().queueFamilyIndices.graphicsFamily = 3;
        vkCreateCommandPool = Create; vkDestroyCommandPool = Destroy;
        vkAllocateCommandBuffers = Allocate; vkFreeCommandBuffers = Free;
        vkResetCommandPool = Reset; vkDeviceWaitIdle = Wait;
    }
    ~Environment()
    {
        MyDevice::GetInstance().vkDevice = device;
        MyDevice::GetInstance().queueFamilyIndices = families;
        vkCreateCommandPool = create; vkDestroyCommandPool = destroy;
        vkAllocateCommandBuffers = allocate; vkFreeCommandBuffers = free;
        vkResetCommandPool = reset; vkDeviceWaitIdle = wait;
    }
};
}

void RunCommandPoolTests()
{
    Environment environment;
    { CommandPool empty; }
    CHECK_TRUE(creates == 0 && destroys == 0, "Empty pool must do no native work!");
    CommandPoolCreateInfo info;
    {
        CommandPool pool;
        createResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        Fails([&] { pool.Create(info); });
        CHECK_TRUE(pool.GetVkCommandPool() == VK_NULL_HANDLE && pool.GetQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED,
            "Failed Create must preserve empty state!");
        createResult = VK_SUCCESS;
        pool.Create(info);
        const auto original = pool.GetVkCommandPool();
        Fails([&] { pool.Create(info); });
        CHECK_TRUE(pool.GetVkCommandPool() == original, "Duplicate Create must preserve owner!");
        auto primary = pool.AllocateOrGetCommandBuffer();
        auto secondary = pool.AllocateOrGetCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        CHECK_TRUE(livePools.at(original).at(primary) == VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
            livePools.at(original).at(secondary) == VK_COMMAND_BUFFER_LEVEL_SECONDARY, "Both native levels must be forwarded!");
        resetResult = VK_ERROR_OUT_OF_HOST_MEMORY;
        Fails([&] { pool.Reset(); });
        CHECK_TRUE(pool.AllocateOrGetCommandBuffer() != primary, "Failed reset must preserve used queues!");
        resetResult = VK_SUCCESS; pool.Reset();
        CHECK_TRUE(pool.AllocateOrGetCommandBuffer() == primary &&
            pool.AllocateOrGetCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY) == secondary, "Reset must reuse both levels!");
    }
    CHECK_TRUE(destroys == 1 && frees == 0 && waits == 0, "RAII must destroy once without buffer frees or waits!");
    {
        CommandPool pool;
        auto attempts = creates.load();
        MyDevice::GetInstance().vkDevice = VK_NULL_HANDLE;
        Fails([&] { pool.Create(info); });
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(1);
        MyDevice::GetInstance().queueFamilyIndices.graphicsFamily.reset();
        Fails([&] { pool.Create(info); });
        CHECK_TRUE(creates == attempts && pool.GetQueueFamilyIndex() == VK_QUEUE_FAMILY_IGNORED, "Invalid inputs must not reach Vulkan!");
        MyDevice::GetInstance().queueFamilyIndices.graphicsFamily = 3;
    }
    createResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    CommandPoolTestProbe::RetryFramePool();
    createResult = VK_SUCCESS;
    std::array<std::thread, 8> workers;
    for (auto& worker : workers) worker = std::thread([&]
    {
        CommandPool pool; pool.Create(info);
        auto buffer = pool.AllocateOrGetCommandBuffer(); pool.Reset();
        CHECK_TRUE(pool.AllocateOrGetCommandBuffer() == buffer, "Independent pools must support concurrent recording preparation!");
    });
    for (auto& worker : workers) worker.join();
    auto pool = std::make_unique<CommandPool>();
    std::thread creator([&] { pool->Create(info); }); creator.join();
    std::thread releaser([&] { pool->AllocateOrGetCommandBuffer(); pool->Reset(); pool.reset(); }); releaser.join();
    {
        CommandPool savedDevicePool; savedDevicePool.Create(info);
        MyDevice::GetInstance().vkDevice = VK_NULL_HANDLE;
    }
    MyDevice::GetInstance().vkDevice = Handle<VkDevice>(1);
    CHECK_TRUE(destroys == 12 && frees == 0 && waits == 0 && livePools.empty(), "Independent pools and sequential handoff must release synchronously!");
}
