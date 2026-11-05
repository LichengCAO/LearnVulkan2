#include "my_vulkan/resource/buffer.h"
#include "my_vulkan/resource/private/buffer_storage.h"
#include "my_vulkan/device.h"
#include <atomic>
#include <concepts>
#include <mutex>
#include <thread>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

struct BufferAllocationTestProbe
{
    static auto TakeAllocator() -> std::unique_ptr<MemoryAllocator>
    { return std::move(MyDevice::GetInstance().m_uptrMemoryAllocator); }
    static void PutAllocator(std::unique_ptr<MemoryAllocator> allocator)
    { MyDevice::GetInstance().m_uptrMemoryAllocator = std::move(allocator); }
    static auto CreateAllocator() -> std::unique_ptr<MemoryAllocator>
    { return std::unique_ptr<MemoryAllocator>(new MemoryAllocator()); }
    static auto AllocationCount() -> uint32_t
    {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(MyDevice::GetInstance().m_uptrMemoryAllocator->m_vmaAllocator, &stats);
        return stats.total.statistics.allocationCount;
    }
};

namespace
{
template<typename T> auto Handle(uintptr_t value) -> T { return reinterpret_cast<T>(value); }
template<typename F> void ExpectFailure(F&& action)
{
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    CHECK_TRUE(failed, "Expected Buffer operation to fail!");
}
template<typename T> concept HasDestroy = requires(T& value) { value.Destroy(); };
static_assert(!HasDestroy<Buffer> && !std::is_copy_constructible_v<Buffer> &&
    !std::is_move_assignable_v<Buffer> && !std::is_copy_assignable_v<Buffer>);
static_assert(std::is_nothrow_move_constructible_v<Buffer> && std::is_nothrow_destructible_v<Buffer>);
static_assert(!std::is_copy_constructible_v<BufferView> && !std::is_move_constructible_v<BufferView> &&
    !std::is_copy_assignable_v<BufferView> && !std::is_move_assignable_v<BufferView>);
static_assert(std::is_nothrow_move_constructible_v<VulkanDetail::MemoryAllocation> &&
    std::is_nothrow_move_assignable_v<VulkanDetail::MemoryAllocation>);

#ifdef _DEBUG
thread_local bool failPublication = false;
auto FailPublication(int kind, void*, size_t, int, long, const unsigned char*, int) -> int
{
    if (kind == _HOOK_ALLOC && failPublication) { failPublication = false; return 0; }
    return 1;
}
#endif

struct NativeState final
{
    struct Binding { VkDeviceMemory memory; VkDeviceSize offset; };
    std::mutex mutex;
    uintptr_t next = 100;
    std::unordered_map<VkBuffer, VkDeviceSize> buffers;
    std::unordered_map<VkBuffer, Binding> bindings;
    std::unordered_map<VkDeviceMemory, std::vector<uint8_t>> memories;
    std::unordered_map<VkBufferView, VkBuffer> views;
    uint32_t creates = 0, destroys = 0, maps = 0, unmaps = 0, waits = 0;
    uint32_t viewCreates = 0, viewDestroys = 0;
    bool failCreate = false, failAllocate = false, failBind = false, failMap = false, failView = false;
    bool failViewPublication = false;
    bool badOrder = false;
    bool requireAllocationOnDestroy = false, requireUnmappedOnDestroy = false;
    VkDevice expectedDevice = Handle<VkDevice>(1);
};
NativeState* active = nullptr;

auto VKAPI_PTR CreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
    const VkAllocationCallbacks*, VkBuffer* out) -> VkResult
{
    std::lock_guard lock(active->mutex);
    CHECK_TRUE(device == active->expectedDevice, "Buffer must use its creating Device!");
    ++active->creates;
    if (active->failCreate) { *out = Handle<VkBuffer>(99999); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *out = Handle<VkBuffer>(active->next++);
    active->buffers.emplace(*out, info->size);
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks*)
{
    const bool allocationAlive = !active->requireAllocationOnDestroy || BufferAllocationTestProbe::AllocationCount() != 0;
    std::lock_guard lock(active->mutex);
    if (!allocationAlive || (active->requireUnmappedOnDestroy && active->unmaps == 0)) active->badOrder = true;
    if (device != active->expectedDevice || active->buffers.erase(buffer) != 1) active->badOrder = true;
    for (const auto& [view, owner] : active->views)
        if (owner == buffer) active->badOrder = true;
    active->bindings.erase(buffer);
    ++active->destroys;
}
void VKAPI_PTR BufferRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* out)
{
    std::lock_guard lock(active->mutex);
    *out = { active->buffers.at(buffer), 64, 1 };
}
auto VKAPI_PTR AllocateMemory(VkDevice, const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks*, VkDeviceMemory* out) -> VkResult
{
    std::lock_guard lock(active->mutex);
    if (active->failAllocate) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out = Handle<VkDeviceMemory>(active->next++);
    active->memories.emplace(*out, std::vector<uint8_t>(static_cast<size_t>(info->allocationSize)));
    return VK_SUCCESS;
}
void VKAPI_PTR FreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*)
{
    std::lock_guard lock(active->mutex);
    for (const auto& [buffer, binding] : active->bindings)
        if (binding.memory == memory) active->badOrder = true;
    if (active->memories.erase(memory) != 1) active->badOrder = true;
}
auto VKAPI_PTR BindBuffer(VkDevice, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) -> VkResult
{
    std::lock_guard lock(active->mutex);
    if (active->failBind) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    active->bindings.emplace(buffer, NativeState::Binding{memory, offset});
    return VK_SUCCESS;
}
auto VKAPI_PTR MapMemory(VkDevice, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize, VkMemoryMapFlags, void** out) -> VkResult
{
    std::lock_guard lock(active->mutex);
    ++active->maps;
    if (active->failMap) return VK_ERROR_MEMORY_MAP_FAILED;
    *out = active->memories.at(memory).data() + offset;
    return VK_SUCCESS;
}
void VKAPI_PTR UnmapMemory(VkDevice, VkDeviceMemory)
{
    std::lock_guard lock(active->mutex);
    ++active->unmaps;
}
void VKAPI_PTR Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* out)
{
    *out = {};
    out->apiVersion = VK_API_VERSION_1_0;
    out->limits.bufferImageGranularity = 64;
    out->limits.nonCoherentAtomSize = 64;
    out->limits.minMemoryMapAlignment = 64;
    out->limits.maxMemoryAllocationCount = 4096;
}
void VKAPI_PTR MemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* out)
{
    *out = {};
    out->memoryHeapCount = 1;
    out->memoryHeaps[0] = { 1024 * 1024, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
    out->memoryTypeCount = 1;
    out->memoryTypes[0] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0 };
}
auto VKAPI_PTR Flush(VkDevice, uint32_t, const VkMappedMemoryRange*) -> VkResult { return VK_SUCCESS; }
auto VKAPI_PTR CreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*) -> VkResult
{ return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
void VKAPI_PTR DestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) {}
void VKAPI_PTR ImageRequirements(VkDevice, VkImage, VkMemoryRequirements* out) { *out = {256, 64, 1}; }
auto VKAPI_PTR BindImage(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) -> VkResult { return VK_SUCCESS; }
void VKAPI_PTR CopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*) {}
auto VKAPI_PTR InstanceProc(VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; }
auto VKAPI_PTR DeviceProc(VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; }
auto VKAPI_PTR Address(VkDevice device, const VkBufferDeviceAddressInfo* info) -> VkDeviceAddress
{
    CHECK_TRUE(device == active->expectedDevice, "Device address must use the creating Device!");
    std::lock_guard lock(active->mutex);
    const auto binding = active->bindings.at(info->buffer);
    return 0x100000 + binding.offset;
}
auto VKAPI_PTR CreateView(VkDevice device, const VkBufferViewCreateInfo* info,
    const VkAllocationCallbacks*, VkBufferView* out) -> VkResult
{
    std::lock_guard lock(active->mutex);
    CHECK_TRUE(device == active->expectedDevice, "View must use the buffer's creating Device!");
    ++active->viewCreates;
    if (active->failView) { *out = Handle<VkBufferView>(99999); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *out = Handle<VkBufferView>(active->next++);
    active->views.emplace(*out, info->buffer);
#ifdef _DEBUG
    if (active->failViewPublication) failPublication = true;
#endif
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyView(VkDevice device, VkBufferView view, const VkAllocationCallbacks*)
{
    std::lock_guard lock(active->mutex);
    const auto found = active->views.find(view);
    if (device != active->expectedDevice || found == active->views.end() ||
        !active->buffers.contains(found->second)) active->badOrder = true;
    active->views.erase(view);
    ++active->viewDestroys;
}
auto VKAPI_PTR Wait(VkDevice) -> VkResult { ++active->waits; return VK_SUCCESS; }

#define BUFFER_HOOKS(X) \
    X(vkCreateBuffer, CreateBuffer) X(vkDestroyBuffer, DestroyBuffer) \
    X(vkGetBufferMemoryRequirements, BufferRequirements) X(vkAllocateMemory, AllocateMemory) \
    X(vkFreeMemory, FreeMemory) X(vkBindBufferMemory, BindBuffer) X(vkMapMemory, MapMemory) \
    X(vkUnmapMemory, UnmapMemory) X(vkGetPhysicalDeviceProperties, Properties) \
    X(vkGetPhysicalDeviceMemoryProperties, MemoryProperties) X(vkFlushMappedMemoryRanges, Flush) \
    X(vkInvalidateMappedMemoryRanges, Flush) X(vkCreateImage, CreateImage) X(vkDestroyImage, DestroyImage) \
    X(vkGetImageMemoryRequirements, ImageRequirements) X(vkBindImageMemory, BindImage) \
    X(vkCmdCopyBuffer, CopyBuffer) X(vkGetInstanceProcAddr, InstanceProc) X(vkGetDeviceProcAddr, DeviceProc) \
    X(vkGetBufferDeviceAddress, Address) X(vkCreateBufferView, CreateView) X(vkDestroyBufferView, DestroyView) \
    X(vkDeviceWaitIdle, Wait)

struct Hooks final
{
    NativeState state;
    VkDevice device;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    std::unique_ptr<MemoryAllocator> allocator;
#define SAVE(name, impl) decltype(name) old_##name = name;
    BUFFER_HOOKS(SAVE)
#undef SAVE
    Hooks()
    {
        auto& owner = MyDevice::GetInstance();
        device = owner.vkDevice; instance = owner.vkInstance; physicalDevice = owner.vkPhysicalDevice;
        allocator = BufferAllocationTestProbe::TakeAllocator();
        owner.vkDevice = state.expectedDevice;
        owner.vkInstance = Handle<VkInstance>(2);
        owner.vkPhysicalDevice = Handle<VkPhysicalDevice>(3);
        active = &state;
#define INSTALL(name, impl) name = impl;
        BUFFER_HOOKS(INSTALL)
#undef INSTALL
        BufferAllocationTestProbe::PutAllocator(BufferAllocationTestProbe::CreateAllocator());
    }
    ~Hooks() noexcept(false)
    {
        BufferAllocationTestProbe::PutAllocator(nullptr);
        const bool clean = state.buffers.empty() && state.views.empty() && state.memories.empty() && !state.badOrder && state.waits == 0;
        auto& owner = MyDevice::GetInstance();
        BufferAllocationTestProbe::PutAllocator(std::move(allocator));
        owner.vkDevice = device; owner.vkInstance = instance; owner.vkPhysicalDevice = physicalDevice;
#define RESTORE(name, impl) name = old_##name;
        BUFFER_HOOKS(RESTORE)
#undef RESTORE
        active = nullptr;
        CHECK_TRUE(clean, "Buffer ownership must release everything once, in native lifetime order, without waiting!");
    }
};
#undef BUFFER_HOOKS

auto HostInfo() -> BufferCreateInfo
{
    BufferCreateInfo info;
    info.SetBufferSize(256).SetBufferUsage(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT).CustomizeMemoryProperty(
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    return info;
}

void TestLifecycleAndMove()
{
    Buffer empty;
    CHECK_TRUE(!empty.IsValid() && empty.GetVkBuffer() == VK_NULL_HANDLE, "Default Buffer must be empty!");
    Hooks hooks;
    hooks.state.requireAllocationOnDestroy = true;
    hooks.state.requireUnmappedOnDestroy = true;
    {
        auto buffer = MakeIntrusive<Buffer>();
        buffer->Create(HostInfo());
        const uint32_t value = 0x12345678;
        buffer->CopyFromHost(&value, 16, sizeof(value));
        const auto binding = hooks.state.bindings.at(buffer->GetVkBuffer());
        CHECK_TRUE(std::memcmp(hooks.state.memories.at(binding.memory).data() + binding.offset + 16, &value, sizeof(value)) == 0,
            "CopyFromHost must write into the real VMA allocation offset!");
        buffer->CopyFromHost(&value, 32, sizeof(value));
        CHECK_TRUE(hooks.state.maps == 1, "Repeated copies must reuse one mapping!");
        auto retained = buffer;
        buffer = nullptr;
        CHECK_TRUE(hooks.state.destroys == 0, "A remaining strong reference must retain allocation ownership!");
        Buffer moved(std::move(*retained));
        CHECK_TRUE(!retained->IsValid() && retained->GetBufferInformation().size == 0,
            "Move must empty source storage and information!");
        retained = nullptr;
        moved.CopyFromHost(&value, 0, sizeof(value));
        CHECK_TRUE(hooks.state.maps == 1 && hooks.state.unmaps == 0, "Move must transfer the mapping!");
        CHECK_TRUE(moved.GetDeviceAddress() != 0 && moved.GetDescriptorBufferInfo().range == 256,
            "Moved Buffer queries must retain allocation information!");
        // Release must use saved dependencies even if the singleton's native device changes.
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(9);
    }
    MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
    CHECK_TRUE(hooks.state.destroys == 1 && hooks.state.unmaps == 1 && BufferAllocationTestProbe::AllocationCount() == 0,
        "Final release must unmap and free the sole VMA allocation exactly once!");
    hooks.state.requireUnmappedOnDestroy = false;
    {
        auto buffer = MakeIntrusive<Buffer>();
        auto info = HostInfo(); info.CustomizeAlignment(0);
        buffer->Create(info);
        auto retained = buffer;
        buffer = nullptr;
        CHECK_TRUE(hooks.state.destroys == 1, "Final strong-reference ownership must retain the native Buffer!");
        retained = nullptr;
        CHECK_TRUE(hooks.state.destroys == 2 && BufferAllocationTestProbe::AllocationCount() == 0,
            "Dropping the final strong reference must synchronously release the native Buffer and allocation!");
    }
}

void TestValidationAndFailureRetry()
{
    Hooks hooks;
    Buffer buffer;
    ExpectFailure([&] { buffer.Create(BufferCreateInfo{}); });
    auto invalid = HostInfo(); invalid.CustomizeAlignment(3);
    ExpectFailure([&] { buffer.Create(invalid); });
    invalid = HostInfo(); invalid.CustomizeSharingMode(VK_SHARING_MODE_CONCURRENT);
    ExpectFailure([&] { buffer.Create(invalid); });
    auto allocator = BufferAllocationTestProbe::TakeAllocator();
    ExpectFailure([&] { buffer.Create(HostInfo()); });
    BufferAllocationTestProbe::PutAllocator(std::move(allocator));
    MyDevice::GetInstance().vkDevice = VK_NULL_HANDLE;
    ExpectFailure([&] { buffer.Create(HostInfo()); });
    ExpectFailure([] { auto rejected = BufferAllocationTestProbe::CreateAllocator(); });
    MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
    CHECK_TRUE(hooks.state.creates == 0, "Invalid inputs/dependencies must fail before Vulkan creation!");
    for (auto flag : { &hooks.state.failCreate, &hooks.state.failAllocate, &hooks.state.failBind })
    {
        *flag = true;
        ExpectFailure([&] { buffer.Create(HostInfo()); });
        *flag = false;
        CHECK_TRUE(!buffer.IsValid() && buffer.GetBufferInformation().size == 0 &&
            hooks.state.buffers.empty() && BufferAllocationTestProbe::AllocationCount() == 0,
            "Failed native creation/allocation/bind must leave Buffer empty and retryable!");
    }
    buffer.Create(HostInfo());
    const auto native = buffer.GetVkBuffer();
    const auto creates = hooks.state.creates;
    ExpectFailure([&] { buffer.Create(HostInfo()); });
    CHECK_TRUE(buffer.GetVkBuffer() == native && hooks.state.creates == creates,
        "Duplicate creation must preserve the existing Buffer before calling Vulkan!");
    uint32_t value = 42;
    hooks.state.failMap = true;
    ExpectFailure([&] { buffer.CopyFromHost(&value, 0, sizeof(value)); });
    hooks.state.failMap = false;
    buffer.CopyFromHost(&value, 0, sizeof(value));
    buffer.CopyFromHost(&value, 4, sizeof(value));
    CHECK_TRUE(hooks.state.maps == 2, "A failed map must not publish a pointer and must support retry!");
}

void TestAllocationAlignmentAndUnmap()
{
    Hooks hooks;
    Buffer first; first.Create(HostInfo());
    auto info = HostInfo(); info.CustomizeAlignment(512);
    Buffer aligned; aligned.Create(info);
    const auto binding = hooks.state.bindings.at(aligned.GetVkBuffer());
    CHECK_TRUE(binding.offset != 0 && binding.offset % 512 == 0,
        "Real VMA must satisfy custom alignment at a nonzero allocation offset!");
    auto* allocator = MyDevice::GetInstance().GetMemoryAllocator();
    VkBufferCreateInfo nativeInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; nativeInfo.size = 128;
    nativeInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer native = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(hooks.state.expectedDevice, &nativeInfo, nullptr, &native));
    {
        auto allocation = allocator->AllocateForVkBuffer(native, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto* pointer = allocation.Map();
        CHECK_TRUE(pointer == allocation.Map(), "Allocation Map must reuse its cached pointer!");
        allocation.Unmap(); allocation.Unmap();
        CHECK_TRUE(hooks.state.unmaps == 1, "Repeated Unmap must be a no-op!");
        allocation.Map();
        VulkanDetail::MemoryAllocation moved(std::move(allocation));
        CHECK_TRUE(!allocation.IsValid() && moved.IsValid(), "Allocation move must transfer ownership!");
        VulkanDetail::MemoryAllocation assigned;
        assigned.Unmap();
        assigned = std::move(assigned);
        assigned = std::move(moved);
        CHECK_TRUE(!moved.IsValid() && assigned.Map() != nullptr, "Allocation move assignment must retain mapping!");
        const auto mapped = assigned.Map();
        assigned = std::move(assigned);
        CHECK_TRUE(assigned.IsValid() && assigned.Map() == mapped, "Self-move must retain live allocation ownership!");
        assigned.Unmap();
        vkDestroyBuffer(hooks.state.expectedDevice, native, nullptr);
    }
    CHECK_TRUE(hooks.state.maps == 2 && hooks.state.unmaps == 2,
        "Unmap must clear cached mapping so a subsequent Map maps again!");
    const auto image = Handle<VkImage>(777);
    const auto before = BufferAllocationTestProbe::AllocationCount();
    allocator->AllocateForVkImage(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == before + 1, "Legacy Image allocation path must remain intact!");
    allocator->FreeMemory(image);
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == before, "Legacy Image free must remain intact!");
}

void TestViews()
{
    Hooks hooks;
    Buffer buffer; buffer.Create(HostInfo());
    BufferViewInfo info; info.SetFormat(VK_FORMAT_R32_UINT);
    hooks.state.failView = true;
    ExpectFailure([&] { buffer.View(info); });
    hooks.state.failView = false;
#ifdef _DEBUG
    const auto previous = _CrtSetAllocHook(FailPublication);
    hooks.state.failViewPublication = true;
    bool failed = false;
    try { buffer.View(info); } catch (const std::bad_alloc&) { failed = true; }
    failPublication = false;
    hooks.state.failViewPublication = false;
    _CrtSetAllocHook(previous);
    CHECK_TRUE(failed && hooks.state.views.empty() && hooks.state.viewDestroys == 1,
        "Host cache publication failure must destroy the just-created native view!");
#endif
    auto* view = buffer.View(info);
    CHECK_TRUE(view == buffer.View(info) && hooks.state.views.size() == 1, "Identical views must be cached!");
    Buffer moved(std::move(buffer));
    CHECK_TRUE(moved.View(info) == view && !buffer.IsValid(), "Move must preserve borrowed view pointers in the transferred cache!");
    const auto creates = hooks.state.creates;
    ExpectFailure([&] { moved.Create(HostInfo()); });
    CHECK_TRUE(moved.View(info) == view && hooks.state.creates == creates,
        "Duplicate Buffer creation must retain borrowed cached view pointers!");
    MyDevice::GetInstance().vkDevice = Handle<VkDevice>(9);
    CHECK_TRUE(moved.GetDeviceAddress() != 0, "Device address must use stable creating dependencies!");
    // Buffer and views are released before Hooks restores the original singleton.
}

void TestIndependentThreads()
{
    Hooks hooks;
    std::mutex failuresMutex;
    std::vector<std::exception_ptr> failures;
    std::vector<std::thread> workers;
    for (uint32_t worker = 0; worker < 4; ++worker)
        workers.emplace_back([&, worker]
        {
            try
            {
                for (uint32_t iteration = 0; iteration < 16; ++iteration)
                {
                    Buffer buffer; buffer.Create(HostInfo());
                    buffer.CopyFromHost(&worker, 0, sizeof(worker));
                    BufferViewInfo info; info.SetFormat(VK_FORMAT_R32_UINT);
                    CHECK_TRUE(buffer.View(info) == buffer.View(info), "Concurrent independent view caches must remain stable!");
                }
            }
            catch (...) { std::lock_guard lock(failuresMutex); failures.push_back(std::current_exception()); }
        });
    for (auto& worker : workers) worker.join();
    if (!failures.empty()) std::rethrow_exception(failures.front());
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == 0 && hooks.state.buffers.empty() && hooks.state.views.empty(),
        "Independent concurrent Buffer operations must reclaim every resource!");
    auto buffer = std::make_unique<Buffer>();
    std::thread create([&] { try { buffer->Create(HostInfo()); } catch (...) { failures.push_back(std::current_exception()); } });
    create.join();
    if (!failures.empty()) std::rethrow_exception(failures.front());
    std::thread consume([&]
    {
        try { uint32_t value = 7; buffer->CopyFromHost(&value, 0, sizeof(value)); buffer.reset(); }
        catch (...) { failures.push_back(std::current_exception()); }
    });
    consume.join();
    if (!failures.empty()) std::rethrow_exception(failures.front());
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == 0, "Sequential thread handoff must preserve ownership!");
}
}

void RunBufferAllocationTests()
{
    TestLifecycleAndMove();
    TestValidationAndFailureRetry();
    TestAllocationAlignmentAndUnmap();
    TestViews();
    TestIndependentThreads();
}
