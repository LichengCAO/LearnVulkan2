#include "my_vulkan/resource/buffer.h"
#include "my_vulkan/resource/image.h"
#include "my_vulkan/resource/acceleration_structure.h"
#include "my_vulkan/pipeline/ray_tracing_shader_program.h"
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
    static auto SwapRtProperties(VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties)
        -> VkPhysicalDeviceRayTracingPipelinePropertiesKHR
    { return std::exchange(MyDevice::GetInstance().m_cachedRayTracingProperties, properties); }
    static auto Groups() -> RayTracingShaderGroupSet
    {
        RayTracingShaderGroupSet groups;
        groups.m_groups.emplace("ray", RayTracingShaderGroupSet::GroupData{ShaderTableType::RayGeneration, {1,2,3,4}});
        groups.m_groups.emplace("miss", RayTracingShaderGroupSet::GroupData{ShaderTableType::Miss, {5,6,7,8}});
        return groups;
    }
    static auto SwapExtent(VkExtent2D extent) -> VkExtent2D
    { return std::exchange(MyDevice::GetInstance().m_swapchain.extent, extent); }
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
static_assert(!HasDestroy<BottomLevelAccelStruct> && !std::is_copy_constructible_v<BottomLevelAccelStruct> &&
    !std::is_move_constructible_v<BottomLevelAccelStruct> && !std::is_copy_assignable_v<BottomLevelAccelStruct> &&
    !std::is_move_assignable_v<BottomLevelAccelStruct> && std::is_nothrow_destructible_v<BottomLevelAccelStruct>);
static_assert(!std::is_copy_constructible_v<BufferView> && !std::is_move_constructible_v<BufferView> &&
    !std::is_copy_assignable_v<BufferView> && !std::is_move_assignable_v<BufferView>);
static_assert(std::is_nothrow_move_constructible_v<VulkanDetail::MemoryAllocation> &&
    std::is_nothrow_move_assignable_v<VulkanDetail::MemoryAllocation>);

#ifdef _DEBUG
thread_local bool failPublication = false;
thread_local int publicationCountdown = 0;
auto FailPublication(int kind, void*, size_t size, int block, long, const unsigned char*, int) -> int
{
    if (block == _CRT_BLOCK || size == sizeof(std::_Container_proxy)) return 1;
    if (kind == _HOOK_ALLOC && failPublication && publicationCountdown-- <= 0) { failPublication = false; return 0; }
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
    std::unordered_map<VkImage, VkImageCreateInfo> images;
    std::unordered_map<VkImage, Binding> imageBindings;
    std::unordered_map<VkImageView, VkImage> imageViews;
    std::unordered_map<VkAccelerationStructureKHR, VkBuffer> accelerationStructures;
    uint32_t asProperties = 0, asFormats = 0, asSizes = 0, asCreates = 0, asAddresses = 0, asDestroys = 0;
    bool failAsCreate = false, zeroAsAddress = false, zeroAsSize = false, badAsFormat = false, badAsLimits = false;
    bool checkAsSnapshot = false, checkTlasSnapshot = false, nullAsCreate = false, badTlasLimits = false, zeroAsAlignment = false;
    uint32_t bufferAddresses = 0;
    uint32_t imageCreates = 0, imageDestroys = 0, imageViewCreates = 0, imageViewDestroys = 0;
    bool failImage = false, failImageView = false, failImagePublication = false;
    bool failImageCachePublication = false;
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
    for (const auto& [handle, owner] : active->accelerationStructures)
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
    for (const auto& [image, binding] : active->imageBindings)
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
auto VKAPI_PTR CreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* out) -> VkResult
{
    std::lock_guard lock(active->mutex);
    CHECK_TRUE(device == active->expectedDevice);
    ++active->imageCreates;
    if (active->failImage) { *out = Handle<VkImage>(99999); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *out = Handle<VkImage>(active->next++); active->images.emplace(*out, *info); return VK_SUCCESS;
}
void VKAPI_PTR DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks*)
{
    const bool alive = !active->requireAllocationOnDestroy || BufferAllocationTestProbe::AllocationCount() != 0;
    std::lock_guard lock(active->mutex);
    if (!alive || device != active->expectedDevice || active->images.erase(image) != 1) active->badOrder = true;
    for (const auto& [view, owner] : active->imageViews) if (owner == image) active->badOrder = true;
    active->imageBindings.erase(image); ++active->imageDestroys;
}
void VKAPI_PTR ImageRequirements(VkDevice, VkImage, VkMemoryRequirements* out) { *out = {256, 64, 1}; }
auto VKAPI_PTR BindImage(VkDevice, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) -> VkResult
{
    std::lock_guard lock(active->mutex);
    if (active->failBind) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    active->imageBindings.emplace(image, NativeState::Binding{memory, offset});
#ifdef _DEBUG
    if (active->failImagePublication) failPublication = true;
#endif
    return VK_SUCCESS;
}
auto VKAPI_PTR CreateImageView(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* out) -> VkResult
{
    std::lock_guard lock(active->mutex); CHECK_TRUE(device == active->expectedDevice);
    ++active->imageViewCreates;
    if (active->failImageView) { *out = Handle<VkImageView>(99999); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *out = Handle<VkImageView>(active->next++); active->imageViews.emplace(*out, info->image);
#ifdef _DEBUG
    if (active->failViewPublication || active->failImageCachePublication)
    { publicationCountdown = active->failImageCachePublication ? 1 : 0; failPublication = true; }
#endif
    return VK_SUCCESS;
}
void VKAPI_PTR DestroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks*)
{
    std::lock_guard lock(active->mutex);
    const auto found = active->imageViews.find(view);
    if (device != active->expectedDevice || found == active->imageViews.end() || !active->images.contains(found->second)) active->badOrder = true;
    active->imageViews.erase(view); ++active->imageViewDestroys;
}
void VKAPI_PTR CopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*) {}
auto VKAPI_PTR InstanceProc(VkInstance, const char*) -> PFN_vkVoidFunction { return nullptr; }
auto VKAPI_PTR DeviceProc(VkDevice, const char*) -> PFN_vkVoidFunction { return nullptr; }
auto VKAPI_PTR Address(VkDevice device, const VkBufferDeviceAddressInfo* info) -> VkDeviceAddress
{
    CHECK_TRUE(device == active->expectedDevice, "Device address must use the creating Device!");
    std::lock_guard lock(active->mutex);
    ++active->bufferAddresses;
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

void VKAPI_PTR AsProperties(VkPhysicalDevice, VkPhysicalDeviceProperties2* out)
{
    std::lock_guard lock(active->mutex); ++active->asProperties;
    auto* properties = static_cast<VkPhysicalDeviceAccelerationStructurePropertiesKHR*>(out->pNext);
    properties->minAccelerationStructureScratchOffsetAlignment = active->zeroAsAlignment ? 0 : 256;
    properties->maxGeometryCount = active->badAsLimits ? 0 : 32; properties->maxPrimitiveCount = 1024; properties->maxInstanceCount = active->badTlasLimits ? 0 : 1024;
}
void VKAPI_PTR AsFormats(VkPhysicalDevice, VkFormat, VkFormatProperties* out)
{ std::lock_guard lock(active->mutex); ++active->asFormats; *out = {}; out->bufferFeatures = active->badAsFormat ? 0 : VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR; }
void VKAPI_PTR AsSizes(VkDevice, VkAccelerationStructureBuildTypeKHR,
    const VkAccelerationStructureBuildGeometryInfoKHR* info, const uint32_t* counts, VkAccelerationStructureBuildSizesInfoKHR* out)
{
    std::lock_guard lock(active->mutex); ++active->asSizes;
    if (active->checkAsSnapshot)
    {
        const auto& triangle = info->pGeometries[0].geometry.triangles;
        CHECK_TRUE(info->geometryCount == 1 && info->flags == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR &&
            counts[0] == 2 && triangle.vertexFormat == VK_FORMAT_R16G16B16A16_SFLOAT && triangle.vertexStride == 16 &&
            triangle.maxVertex == 5 && triangle.indexType == VK_INDEX_TYPE_NONE_KHR && triangle.transformData.deviceAddress == 1 &&
            triangle.vertexData.deviceAddress == 0 && active->bufferAddresses == 0,
            "Size queries must use copied geometry layout without resolving input addresses!");
    }
    if (active->checkTlasSnapshot)
        CHECK_TRUE(info->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR && info->geometryCount == 1 &&
            info->flags == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR && counts[0] == 2 &&
            info->pGeometries[0].geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR &&
            info->pGeometries[0].geometry.instances.arrayOfPointers == VK_FALSE &&
            info->pGeometries[0].geometry.instances.data.deviceAddress == 0 && active->bufferAddresses == 0 &&
            active->asAddresses == 0 && active->maps == 0, "TLAS must snapshot host layout without resolving or uploading inputs!");
    out->accelerationStructureSize = active->zeroAsSize ? 0 : 512; out->buildScratchSize = 256; out->updateScratchSize = 128;
}
auto VKAPI_PTR AsCreate(VkDevice device, const VkAccelerationStructureCreateInfoKHR* info,
    const VkAllocationCallbacks*, VkAccelerationStructureKHR* out) -> VkResult
{
    std::lock_guard lock(active->mutex); CHECK_TRUE(device == active->expectedDevice);
    ++active->asCreates;
    if (active->failAsCreate) { *out = Handle<VkAccelerationStructureKHR>(99999); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    if (active->nullAsCreate) { *out = VK_NULL_HANDLE; return VK_SUCCESS; }
    *out = Handle<VkAccelerationStructureKHR>(active->next++);
    active->accelerationStructures.emplace(*out, info->buffer); return VK_SUCCESS;
}
void VKAPI_PTR AsDestroy(VkDevice device, VkAccelerationStructureKHR handle, const VkAllocationCallbacks*)
{
    std::lock_guard lock(active->mutex);
    const auto found = active->accelerationStructures.find(handle);
    if (device != active->expectedDevice || found == active->accelerationStructures.end() || !active->buffers.contains(found->second)) active->badOrder = true;
    active->accelerationStructures.erase(handle); ++active->asDestroys;
}
auto VKAPI_PTR AsAddress(VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR*) -> VkDeviceAddress
{ std::lock_guard lock(active->mutex); ++active->asAddresses; return active->zeroAsAddress ? 0 : 0x800000; }

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
    X(vkDeviceWaitIdle, Wait) X(vkCreateImageView, CreateImageView) X(vkDestroyImageView, DestroyImageView) \
    X(vkGetPhysicalDeviceProperties2, AsProperties) X(vkGetPhysicalDeviceFormatProperties, AsFormats) \
    X(vkGetAccelerationStructureBuildSizesKHR, AsSizes) X(vkCreateAccelerationStructureKHR, AsCreate) \
    X(vkDestroyAccelerationStructureKHR, AsDestroy) X(vkGetAccelerationStructureDeviceAddressKHR, AsAddress)

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
        const bool clean = state.accelerationStructures.empty() && state.buffers.empty() && state.views.empty() && state.images.empty() && state.imageViews.empty() && state.memories.empty() && !state.badOrder && state.waits == 0;
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
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImage image = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImage(hooks.state.expectedDevice, &imageInfo, nullptr, &image));
    const auto before = BufferAllocationTestProbe::AllocationCount();
    {
        auto allocation = allocator->AllocateForVkImage(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == before + 1, "Image allocation record must own VMA allocation!");
        vkDestroyImage(hooks.state.expectedDevice, image, nullptr);
    }
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == before, "Image allocation record must free VMA allocation!");
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

auto ImageInfo() -> ImageCreateInfo
{
    ImageCreateInfo info; info.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT); info.CustomizeSize2D(16, 8);
    info.CustomizeMipLevels(3); info.CustomizeArrayLayers(2); return info;
}

void TestImageValidationAndRollback()
{
    static_assert(!HasDestroy<Image> && !std::is_move_constructible_v<Image> && std::is_nothrow_destructible_v<Image>);
    Hooks hooks;
    Image image;
    CHECK_TRUE(!image.IsValid() && image.GetVkImage() == VK_NULL_HANDLE);
    ExpectFailure([&] { image.Create(ImageCreateInfo{}); });
    auto invalid = ImageInfo(); invalid.CustomizeSize2D(0, 8); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeMipLevels(6); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeMipLevels(0); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeArrayLayers(0); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeSampleCount(VK_SAMPLE_COUNT_4_BIT); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeSize3D(8, 8, 8); ExpectFailure([&] { image.Create(invalid); });
    invalid = ImageInfo(); invalid.CustomizeSampleCount(static_cast<VkSampleCountFlagBits>(3)); ExpectFailure([&] { image.Create(invalid); });
    auto allocator = BufferAllocationTestProbe::TakeAllocator();
    ExpectFailure([&] { image.Create(ImageInfo()); });
    BufferAllocationTestProbe::PutAllocator(std::move(allocator));
    MyDevice::GetInstance().vkDevice = VK_NULL_HANDLE; ExpectFailure([&] { image.Create(ImageInfo()); });
    MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
    CHECK_TRUE(hooks.state.imageCreates == 0, "Invalid Image inputs must reject before native calls!");
    for (auto flag : {&hooks.state.failImage, &hooks.state.failAllocate, &hooks.state.failBind})
    {
        *flag = true; ExpectFailure([&] { image.Create(ImageInfo()); }); *flag = false;
        CHECK_TRUE(!image.IsValid() && image.GetImageInformation().width == 0 && hooks.state.images.empty() && BufferAllocationTestProbe::AllocationCount() == 0);
    }
#ifdef _DEBUG
    const auto previous = _CrtSetAllocHook(FailPublication);
    hooks.state.failImagePublication = true;
    ExpectFailure([&] { image.Create(ImageInfo()); });
    failPublication = false; hooks.state.failImagePublication = false; _CrtSetAllocHook(previous);
    CHECK_TRUE(!image.IsValid() && hooks.state.images.empty() && BufferAllocationTestProbe::AllocationCount() == 0,
        "Image wrapper allocation failure must roll back bound native storage!");
#endif
    image.Create(ImageInfo()); auto view = image.View();
    const auto native = image.GetVkImage(); const auto calls = hooks.state.imageCreates;
    ExpectFailure([&] { image.Create(ImageInfo()); });
    CHECK_TRUE(image.GetVkImage() == native && image.GetImageInformation().width == 16 && image.View() == view && calls == hooks.state.imageCreates);
}

void TestImageViewsAndOwners()
{
    Hooks hooks; hooks.state.requireAllocationOnDestroy = true;
    ImageView retained;
    {
        auto image = std::make_unique<Image>(); image->Create(ImageInfo());
        retained = image->View(); const auto identity = retained.GetResourceId();
        auto copy = retained; CHECK_TRUE(copy.GetResourceId() == identity);
        ImageViewInfo slice; slice.CustomizeMipLevels(1, 1); slice.CustomizeArrayLayers(1, 1);
        hooks.state.failImageView = true; ExpectFailure([&] { image->View(slice); }); hooks.state.failImageView = false;
#ifdef _DEBUG
        const auto previous = _CrtSetAllocHook(FailPublication); hooks.state.failViewPublication = true;
        ExpectFailure([&] { image->View(slice); });
        failPublication = false; hooks.state.failViewPublication = false; _CrtSetAllocHook(previous);
        const auto cacheHook = _CrtSetAllocHook(FailPublication); hooks.state.failImageCachePublication = true;
        ExpectFailure([&] { image->View(slice); });
        failPublication = false; publicationCountdown = 0; hooks.state.failImageCachePublication = false; _CrtSetAllocHook(cacheHook);
#endif
        CHECK_TRUE(image->View() == retained, "Failed view publication must preserve cached entries!");
        auto sub = image->View(slice); CHECK_TRUE(sub.GetViewSize().width == 8 && sub.GetViewSize().height == 4 && sub.GetImageSubresourceRange().layerCount == 1);
        image->ClearViewCache(); CHECK_TRUE(retained.IsValid());
        auto recreated = image->View(); CHECK_TRUE(recreated.GetResourceId() != identity);
        image.reset(); CHECK_TRUE(hooks.state.imageDestroys == 0 && BufferAllocationTestProbe::AllocationCount() == 1);
        Image rebuilt; rebuilt.Create(ImageInfo()); CHECK_TRUE(rebuilt.GetVkImage() != retained.GetVkImage());
    }
    CHECK_TRUE(hooks.state.imageDestroys == 1 && BufferAllocationTestProbe::AllocationCount() == 1);
    MyDevice::GetInstance().vkDevice = Handle<VkDevice>(9);
    retained.Reset(); CHECK_TRUE(hooks.state.imageDestroys == 2 && BufferAllocationTestProbe::AllocationCount() == 0);
    MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
    {
        Image one; auto info = ImageInfo(); info.CustomizeSize1D(16); info.CustomizeArrayLayers(1); one.Create(info);
        CHECK_TRUE(one.View().GetImageViewInformation().type == VK_IMAGE_VIEW_TYPE_1D);
        Image three; info.CustomizeSize3D(16, 8, 4); three.Create(info);
        CHECK_TRUE(three.View().GetImageViewInformation().type == VK_IMAGE_VIEW_TYPE_3D);
        Image multisample; info.CustomizeSize2D(8, 8); info.CustomizeMipLevels(1); info.CustomizeSampleCount(VK_SAMPLE_COUNT_4_BIT); multisample.Create(info);
        CHECK_TRUE(multisample.GetImageInformation().samples == VK_SAMPLE_COUNT_4_BIT);
    }
    const auto oldExtent = BufferAllocationTestProbe::SwapExtent({32, 24});
    {
        Image image; ImageCreateInfo info; info.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT); image.Create(info);
        CHECK_TRUE(image.GetImageSize().width == 32 && image.GetImageSize().height == 24);
    }
    BufferAllocationTestProbe::SwapExtent({0, 0});
    {
        Image explicitImage; explicitImage.Create(ImageInfo());
        Image defaultImage; ImageCreateInfo info; info.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
        ExpectFailure([&] { defaultImage.Create(info); });
    }
    BufferAllocationTestProbe::SwapExtent(oldExtent);
}

void TestImageThreads()
{
    Hooks hooks; std::mutex mutex; std::vector<std::exception_ptr> failures; std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&]
    {
        try { for (int n = 0; n < 16; ++n) { Image image; image.Create(ImageInfo()); auto view = image.View(); auto copy = view; image.ClearViewCache(); CHECK_TRUE(view == copy); } }
        catch (...) { std::lock_guard lock(mutex); failures.push_back(std::current_exception()); }
    });
    for (auto& worker : workers) worker.join();
    if (!failures.empty()) std::rethrow_exception(failures.front());
    auto image = std::make_unique<Image>(); ImageView view;
    std::thread create([&] { try { image->Create(ImageInfo()); view = image->View(); } catch (...) { failures.push_back(std::current_exception()); } }); create.join();
    if (!failures.empty()) std::rethrow_exception(failures.front());
    auto copy = view; image.reset();
    std::thread first([value = std::move(copy)]() mutable { value.Reset(); });
    std::thread second([value = std::move(view)]() mutable { value.Reset(); }); first.join(); second.join();
    CHECK_TRUE(BufferAllocationTestProbe::AllocationCount() == 0 && hooks.state.images.empty());
}
}

void RunBufferAllocationTests()
{
    static_assert(!HasDestroy<TopLevelAccelStruct> && !std::is_move_constructible_v<TopLevelAccelStruct> &&
        !std::is_copy_constructible_v<TopLevelAccelStruct> && std::is_nothrow_destructible_v<TopLevelAccelStruct>);
    static_assert(!HasDestroy<ShaderBindingTable> && !std::is_move_constructible_v<ShaderBindingTable> &&
        !std::is_copy_constructible_v<ShaderBindingTable> && std::is_nothrow_destructible_v<ShaderBindingTable>);
    {
        Hooks hooks; TopLevelAccelStructCreateInfo info;
        std::array<AccelStructInstance, 2> instances{};
        info.SetInstances(instances); info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        instances[0].mask = 0x100; instances[1].accelStruct = reinterpret_cast<BottomLevelAccelStruct*>(1);
        hooks.state.checkTlasSnapshot = true;
        TopLevelAccelStruct tlas; tlas.Create(info);
        CHECK_TRUE(tlas.GetBuildSizes().accelerationStructureSize == 512 && tlas.GetScratchAlignment() == 256 &&
            info.GetInstanceCount() == 2 && info.GetInstanceBufferSize() == 128);
    }
    {
        Hooks hooks; TopLevelAccelStruct tlas; TopLevelAccelStructCreateInfo info;
        const auto empty = [&]
        {
            CHECK_TRUE(!tlas.IsValid() && tlas.GetVkBuildAccelerationStructureFlags() == 0 &&
                tlas.GetBuildSizes().accelerationStructureSize == 0 && tlas.GetScratchAlignment() == 1 &&
                hooks.state.accelerationStructures.empty() && hooks.state.buffers.empty() &&
                BufferAllocationTestProbe::AllocationCount() == 0, "Failed TLAS creation must stay empty and retryable!");
        };
        auto& device = MyDevice::GetInstance();
        device.vkDevice = VK_NULL_HANDLE; ExpectFailure([&] { tlas.Create(info); }); device.vkDevice = hooks.state.expectedDevice;
        const auto physical = device.vkPhysicalDevice;
        device.vkPhysicalDevice = VK_NULL_HANDLE; ExpectFailure([&] { tlas.Create(info); }); device.vkPhysicalDevice = physical;
        auto allocator = BufferAllocationTestProbe::TakeAllocator(); ExpectFailure([&] { tlas.Create(info); });
        BufferAllocationTestProbe::PutAllocator(std::move(allocator));
#define TLAS_MISSING(name) { const auto saved = name; name = nullptr; ExpectFailure([&] { tlas.Create(info); }); name = saved; empty(); }
        TLAS_MISSING(vkGetPhysicalDeviceProperties2)
        TLAS_MISSING(vkGetAccelerationStructureBuildSizesKHR)
        TLAS_MISSING(vkCreateAccelerationStructureKHR)
        TLAS_MISSING(vkDestroyAccelerationStructureKHR)
#undef TLAS_MISSING
        CHECK_TRUE(hooks.state.asProperties == 0 && hooks.state.asCreates == 0);
        for (auto flag : {&hooks.state.zeroAsSize, &hooks.state.failCreate, &hooks.state.failAllocate,
            &hooks.state.failBind, &hooks.state.failAsCreate, &hooks.state.nullAsCreate})
        { *flag = true; ExpectFailure([&] { tlas.Create(info); }); *flag = false; empty(); }
        AccelStructInstance instance{}; info.SetInstances(std::span(&instance, 1));
        for (auto flag : {&hooks.state.badTlasLimits, &hooks.state.zeroAsAlignment})
        { *flag = true; ExpectFailure([&] { tlas.Create(info); }); *flag = false; empty(); }
#ifdef _DEBUG
        const auto oldHook = _CrtSetAllocHook(FailPublication);
        uint32_t observedFailures = 0;
        try
        {
            for (int countdown = 0; countdown < 3; ++countdown)
            {
                TopLevelAccelStruct candidate; publicationCountdown = countdown; failPublication = true;
                try { candidate.Create(info); }
                catch (const std::bad_alloc&)
                {
                    failPublication = false; ++observedFailures;
                    CHECK_TRUE(!candidate.IsValid() && candidate.GetBuildSizes().accelerationStructureSize == 0 &&
                        candidate.GetScratchAlignment() == 1 && hooks.state.buffers.empty() &&
                        BufferAllocationTestProbe::AllocationCount() == 0);
                }
                failPublication = false;
            }
        }
        catch (...) { failPublication = false; _CrtSetAllocHook(oldHook); throw; }
        _CrtSetAllocHook(oldHook); CHECK_TRUE(observedFailures == 3);
#endif
        info.Reset();
        const auto formats = vkGetPhysicalDeviceFormatProperties; const auto address = vkGetAccelerationStructureDeviceAddressKHR;
        vkGetPhysicalDeviceFormatProperties = nullptr; vkGetAccelerationStructureDeviceAddressKHR = nullptr;
        tlas.Create(info); CHECK_TRUE(tlas.IsValid() && hooks.state.asFormats == 0 && hooks.state.asAddresses == 0);
        vkGetPhysicalDeviceFormatProperties = formats; vkGetAccelerationStructureDeviceAddressKHR = address;
        device.vkDevice = Handle<VkDevice>(9);
    }
    {
        Hooks hooks;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        props.shaderGroupHandleSize = 4; props.shaderGroupHandleAlignment = 8; props.shaderGroupBaseAlignment = 64; props.maxShaderGroupStride = 128;
        const auto oldProperties = BufferAllocationTestProbe::SwapRtProperties(props);
        try
        {
            ShaderBindingTableCreateInfo info;
            auto groups = BufferAllocationTestProbe::Groups();
            info.SetShaderGroupSet(groups).SetTableCapacity(ShaderTableType::RayGeneration, 2, 4)
                .SetTableCapacity(ShaderTableType::Miss, 2, 4);
            groups.Reset();
            ShaderBindingTable table;
            CHECK_TRUE(!table.IsValid());
            for (auto flag : {&hooks.state.failCreate, &hooks.state.failAllocate, &hooks.state.failBind})
            { *flag = true; ExpectFailure([&] { table.Create(info); }); *flag = false; CHECK_TRUE(!table.IsValid() && hooks.state.buffers.empty()); }
            const auto address = vkGetBufferDeviceAddress; vkGetBufferDeviceAddress = nullptr;
            ExpectFailure([&] { table.Create(info); }); vkGetBufferDeviceAddress = address;
            auto allocator = BufferAllocationTestProbe::TakeAllocator(); ExpectFailure([&] { table.Create(info); });
            BufferAllocationTestProbe::PutAllocator(std::move(allocator));
            auto& device = MyDevice::GetInstance();
            device.vkDevice = VK_NULL_HANDLE; ExpectFailure([&] { table.Create(info); }); device.vkDevice = hooks.state.expectedDevice;
            const auto physical = device.vkPhysicalDevice;
            device.vkPhysicalDevice = VK_NULL_HANDLE; ExpectFailure([&] { table.Create(info); }); device.vkPhysicalDevice = physical;
            for (auto member : {&VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleSize,
                &VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleAlignment,
                &VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupBaseAlignment,
                &VkPhysicalDeviceRayTracingPipelinePropertiesKHR::maxShaderGroupStride})
            {
                auto invalidProps = props; invalidProps.*member = 0;
                BufferAllocationTestProbe::SwapRtProperties(invalidProps); ExpectFailure([&] { table.Create(info); });
                BufferAllocationTestProbe::SwapRtProperties(props);
            }
#ifdef _DEBUG
            const auto oldHook = _CrtSetAllocHook(FailPublication); uint32_t observedFailures = 0;
            try
            {
                for (int countdown = 0; countdown < 3; ++countdown)
                {
                    ShaderBindingTable candidate; publicationCountdown = countdown; failPublication = true;
                    try { candidate.Create(info); }
                    catch (const std::bad_alloc&)
                    {
                        failPublication = false; ++observedFailures;
                        CHECK_TRUE(!candidate.IsValid() && hooks.state.buffers.empty() && BufferAllocationTestProbe::AllocationCount() == 0);
                        ExpectFailure([&] { candidate.Commit(); });
                        candidate.Create(info);
                    }
                    failPublication = false;
                }
            }
            catch (...) { failPublication = false; _CrtSetAllocHook(oldHook); throw; }
            _CrtSetAllocHook(oldHook); CHECK_TRUE(observedFailures == 3);
#endif
            ShaderBindingTableCreateInfo invalid;
            ExpectFailure([&] { table.Create(invalid); });
            invalid.SetTableCapacity(ShaderTableType::RayGeneration, 1, SIZE_MAX);
            ExpectFailure([&] { table.Create(invalid); });
            invalid = {}; invalid.SetTableCapacity(ShaderTableType::RayGeneration, 1, 0);
            auto invalidProps = props; invalidProps.shaderGroupHandleAlignment = 3;
            BufferAllocationTestProbe::SwapRtProperties(invalidProps);
            ExpectFailure([&] { table.Create(invalid); }); BufferAllocationTestProbe::SwapRtProperties(props);
            table.Create(info); info = {}; CHECK_TRUE(table.IsValid() && hooks.state.maps == 0);
            const std::array<uint8_t,4> payload{9,10,11,12};
            table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray", payload.data(), payload.size());
            table.SetShaderTableBinding(ShaderTableType::RayGeneration, 1, "ray");
            table.SetShaderTableBinding(ShaderTableType::Miss, 0, "miss", payload.data(), payload.size());
            const auto ray = table.GetShaderTableRegion(ShaderTableType::RayGeneration);
            const auto ray1 = table.GetShaderTableRegion(ShaderTableType::RayGeneration, 1);
            const auto miss = table.GetShaderTableRegion(ShaderTableType::Miss);
            CHECK_TRUE(ray.deviceAddress % 64 == 0 && ray.stride == 64 && ray.size == 64 &&
                ray1.deviceAddress == ray.deviceAddress + 64 && ray1.size == 64 &&
                miss.deviceAddress == ray.deviceAddress + 128 && miss.stride == 8 && miss.size == 16 &&
                table.GetShaderTableRegion(ShaderTableType::Hit).size == 0);
            ExpectFailure([&] { table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "miss"); });
            ExpectFailure([&] { table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "missing"); });
            ExpectFailure([&] { table.SetShaderTableBinding(ShaderTableType::RayGeneration, 2, "ray"); });
            ExpectFailure([&] { table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray", nullptr, 1); });
            ExpectFailure([&] { table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray", payload.data(), 5); });
            ExpectFailure([&] { table.GetShaderTableRegion(ShaderTableType::RayGeneration, 2); });
            ExpectFailure([&] { table.GetShaderTableRegion(ShaderTableType::Miss, 1); });
            const auto creates = hooks.state.creates;
            ExpectFailure([&] { table.Create(ShaderBindingTableCreateInfo{}); });
            CHECK_TRUE(hooks.state.creates == creates && table.GetShaderTableRegion(ShaderTableType::RayGeneration).deviceAddress == ray.deviceAddress);
            hooks.state.failMap = true; ExpectFailure([&] { table.Commit(); }); hooks.state.failMap = false;
            table.Commit();
            const auto buffer = hooks.state.bindings.begin()->second;
            auto& bytes = hooks.state.memories.at(buffer.memory);
            CHECK_TRUE(bytes[buffer.offset] == 1 && bytes[buffer.offset+3] == 4 && bytes[buffer.offset+4] == 9 &&
                bytes[buffer.offset+7] == 12 && bytes[buffer.offset+8] == 0 && bytes[buffer.offset+128] == 5);
            table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray"); table.Commit();
            CHECK_TRUE(bytes[buffer.offset+4] == 0 && bytes[buffer.offset+63] == 0);
        }
        catch (...) { BufferAllocationTestProbe::SwapRtProperties(oldProperties); throw; }
        BufferAllocationTestProbe::SwapRtProperties(oldProperties);
    }
    {
        Hooks hooks;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        props.shaderGroupHandleSize = 4; props.shaderGroupHandleAlignment = 8; props.shaderGroupBaseAlignment = 64; props.maxShaderGroupStride = 128;
        const auto oldProperties = BufferAllocationTestProbe::SwapRtProperties(props);
        TopLevelAccelStructCreateInfo tlasInfo;
        ShaderBindingTableCreateInfo sbtInfo; sbtInfo.SetShaderGroupSet(BufferAllocationTestProbe::Groups())
            .SetTableCapacity(ShaderTableType::RayGeneration, 1, 0);
        std::mutex mutex; std::vector<std::exception_ptr> failures; std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) workers.emplace_back([&]
        {
            try
            {
                for (int n = 0; n < 16; ++n)
                {
                    TopLevelAccelStruct tlas; tlas.Create(tlasInfo);
                    ShaderBindingTable table; table.Create(sbtInfo);
                    table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray"); table.Commit();
                }
            }
            catch (...) { std::lock_guard lock(mutex); failures.push_back(std::current_exception()); }
        });
        for (auto& worker : workers) worker.join();
        auto table = std::make_unique<ShaderBindingTable>();
        std::thread sbtCreator([&]
        {
            try { table->Create(sbtInfo); table->SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "ray"); }
            catch (...) { failures.push_back(std::current_exception()); }
        }); sbtCreator.join();
        if (failures.empty())
        {
            std::thread sbtUser([&]
            {
                try { table->Commit(); CHECK_TRUE(table->GetShaderTableRegion(ShaderTableType::RayGeneration).size == 64); }
                catch (...) { failures.push_back(std::current_exception()); }
            }); sbtUser.join();
        }
        std::thread sbtReleaser([value = std::move(table)]() mutable { value.reset(); }); sbtReleaser.join();
        BufferAllocationTestProbe::SwapRtProperties(oldProperties);
        if (!failures.empty()) std::rethrow_exception(failures.front());
        auto tlas = MakeIntrusive<TopLevelAccelStruct>();
        std::thread creator([&] { try { tlas->Create(tlasInfo); } catch (...) { failures.push_back(std::current_exception()); } }); creator.join();
        if (!failures.empty()) std::rethrow_exception(failures.front());
        auto copy = tlas; tlas = nullptr; CHECK_TRUE(hooks.state.asDestroys == 64);
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(9);
        std::thread releaser([value = std::move(copy)]() mutable { value = nullptr; }); releaser.join();
        MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
        CHECK_TRUE(hooks.state.asDestroys == 65 && hooks.state.accelerationStructures.empty() &&
            hooks.state.buffers.empty() && BufferAllocationTestProbe::AllocationCount() == 0);
    }
    {
        Hooks hooks;
        TopLevelAccelStruct tlas; TopLevelAccelStructCreateInfo info;
        tlas.Create(info);
        const auto handle = tlas.GetVkAccelerationStructure();
        const auto calls = hooks.state.asCreates, queries = hooks.state.asProperties, sizes = hooks.state.asSizes,
            buffers = hooks.state.creates;
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        bool rejected = false;
        try { tlas.Create(info); } catch (const std::exception&) { rejected = true; }
        CHECK_TRUE(rejected && calls == hooks.state.asCreates && queries == hooks.state.asProperties &&
            sizes == hooks.state.asSizes && buffers == hooks.state.creates && handle == tlas.GetVkAccelerationStructure() &&
            tlas.GetBuildSizes().accelerationStructureSize == 512 && tlas.GetBuildSizes().buildScratchSize == 256 &&
            tlas.GetBuildSizes().updateScratchSize == 128 && tlas.GetScratchAlignment() == 256 &&
            tlas.GetVkBuildAccelerationStructureFlags() == 0 && handle != VK_NULL_HANDLE,
            "TLAS must reject duplicate creation before native work!");
    }
    {
        Hooks hooks; Buffer input;
        AccelStructTriangleGeometry geometry; geometry.vertexBuffer = &input; geometry.transformBuffer = &input;
        geometry.vertexFormat = VK_FORMAT_R16G16B16A16_SFLOAT; geometry.vertexStride = 16; geometry.vertexCount = 6; geometry.primitiveCount = 2;
        BottomLevelAccelStructCreateInfo info; info.SetTriangles(std::span(&geometry, 1));
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        geometry.vertexFormat = VK_FORMAT_UNDEFINED; geometry.vertexCount = 0; geometry.primitiveCount = 0;
        hooks.state.checkAsSnapshot = true;
        BottomLevelAccelStruct blas; blas.Create(info);
    }
    // Catch leaked temporary ownership, poisoned native output adoption, and partial publication.
    {
        Hooks hooks;
        Buffer input;
        AccelStructTriangleGeometry geometry; geometry.vertexBuffer = &input; geometry.vertexStride = 12; geometry.vertexCount = 3; geometry.primitiveCount = 1;
        BottomLevelAccelStructCreateInfo info; info.SetTriangles(std::span(&geometry, 1));
        BottomLevelAccelStruct blas;
        const auto empty = [&]
        {
            CHECK_TRUE(!blas.IsValid() && blas.GetVkAccelerationStructure() == VK_NULL_HANDLE && blas.GetDeviceAddress() == 0 &&
                blas.GetVkBuildAccelerationStructureFlags() == 0 && blas.GetBuildSizes().accelerationStructureSize == 0 &&
                blas.GetScratchAlignment() == 1 && hooks.state.accelerationStructures.empty() && hooks.state.buffers.empty() &&
                BufferAllocationTestProbe::AllocationCount() == 0, "Failed BLAS creation must retain default state and release all allocations!");
        };
        empty();
        ExpectFailure([&] { blas.Create(BottomLevelAccelStructCreateInfo{}); });
        auto allocator = BufferAllocationTestProbe::TakeAllocator();
        ExpectFailure([&] { blas.Create(info); });
        BufferAllocationTestProbe::PutAllocator(std::move(allocator));
        auto& device = MyDevice::GetInstance();
        device.vkDevice = VK_NULL_HANDLE; ExpectFailure([&] { blas.Create(info); }); device.vkDevice = hooks.state.expectedDevice;
        const auto physical = device.vkPhysicalDevice;
        device.vkPhysicalDevice = VK_NULL_HANDLE; ExpectFailure([&] { blas.Create(info); }); device.vkPhysicalDevice = physical;
#define MISSING_AS_ENTRY(name) { const auto saved = name; name = nullptr; ExpectFailure([&] { blas.Create(info); }); name = saved; }
        MISSING_AS_ENTRY(vkGetPhysicalDeviceProperties2)
        MISSING_AS_ENTRY(vkGetPhysicalDeviceFormatProperties)
        MISSING_AS_ENTRY(vkGetAccelerationStructureBuildSizesKHR)
        MISSING_AS_ENTRY(vkCreateAccelerationStructureKHR)
        MISSING_AS_ENTRY(vkDestroyAccelerationStructureKHR)
        MISSING_AS_ENTRY(vkGetAccelerationStructureDeviceAddressKHR)
#undef MISSING_AS_ENTRY
        CHECK_TRUE(hooks.state.asProperties == 0 && hooks.state.asSizes == 0 && hooks.state.creates == 0,
            "Unavailable dependencies must reject before native BLAS queries or creation!");
        for (auto flag : { &hooks.state.badAsFormat, &hooks.state.badAsLimits, &hooks.state.zeroAsSize,
            &hooks.state.failCreate, &hooks.state.failAllocate, &hooks.state.failBind, &hooks.state.failAsCreate, &hooks.state.zeroAsAddress })
        {
            *flag = true; ExpectFailure([&] { blas.Create(info); }); *flag = false; empty();
        }
#ifdef _DEBUG
        struct AllocationHookGuard final
        {
            _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(FailPublication);
            ~AllocationHookGuard() noexcept { failPublication = false; _CrtSetAllocHook(previous); }
        } allocationHook;
        uint32_t observedFailures = 0;
        for (int countdown = 0; countdown < 5; ++countdown)
        {
            BottomLevelAccelStruct candidate;
            publicationCountdown = countdown; failPublication = true;
            try { candidate.Create(info); }
            catch (const std::bad_alloc&)
            {
                failPublication = false;
                ++observedFailures;
                CHECK_TRUE(!candidate.IsValid() && candidate.GetVkAccelerationStructure() == VK_NULL_HANDLE &&
                    candidate.GetDeviceAddress() == 0 && candidate.GetVkBuildAccelerationStructureFlags() == 0 &&
                    candidate.GetBuildSizes().accelerationStructureSize == 0 && candidate.GetBuildSizes().buildScratchSize == 0 &&
                    candidate.GetBuildSizes().updateScratchSize == 0 && candidate.GetScratchAlignment() == 1 &&
                    hooks.state.accelerationStructures.empty() && hooks.state.buffers.empty() &&
                    BufferAllocationTestProbe::AllocationCount() == 0,
                    "Host allocation failure must leave the failed BLAS empty before its destructor runs!");
            }
            failPublication = false;
        }
        CHECK_TRUE(observedFailures > 0, "BLAS host allocation coverage must observe injected allocation failures!");
        empty();
#endif
        blas.Create(info);
        CHECK_TRUE(blas.IsValid() && blas.GetDeviceAddress() == 0x800000 && blas.GetBuildSizes().accelerationStructureSize == 512,
            "BLAS must support retry after all native and host failures!");
    }
    // Catch incorrect saved-device release and premature release while strong owners remain.
    {
        Hooks hooks; hooks.state.requireAllocationOnDestroy = true;
        Buffer input;
        AccelStructAabbGeometry geometry; geometry.buffer = &input; geometry.primitiveCount = 1;
        BottomLevelAccelStructCreateInfo info; info.SetAabbs(std::span(&geometry, 1));
        auto blas = MakeIntrusive<BottomLevelAccelStruct>(); blas->Create(info);
        auto copy = blas; blas = nullptr;
        CHECK_TRUE(hooks.state.asDestroys == 0 && BufferAllocationTestProbe::AllocationCount() == 1);
        MyDevice::GetInstance().vkDevice = Handle<VkDevice>(9); copy = nullptr;
        MyDevice::GetInstance().vkDevice = hooks.state.expectedDevice;
        CHECK_TRUE(hooks.state.asDestroys == 1 && hooks.state.destroys == 1 && BufferAllocationTestProbe::AllocationCount() == 0,
            "Final strong release must synchronously destroy AS before Buffer and allocation using saved Device!");
    }
    // Catch accidental affinity/shared mutable allocation records during independent creation.
    {
        Hooks hooks; Buffer input;
        AccelStructAabbGeometry geometry; geometry.buffer = &input; geometry.primitiveCount = 1;
        BottomLevelAccelStructCreateInfo info; info.SetAabbs(std::span(&geometry, 1));
        std::mutex mutex; std::vector<std::exception_ptr> failures; std::vector<std::thread> workers;
        for (int n = 0; n < 4; ++n) workers.emplace_back([&]
        {
            try { for (int iteration = 0; iteration < 16; ++iteration) { BottomLevelAccelStruct blas; blas.Create(info); } }
            catch (...) { std::lock_guard lock(mutex); failures.push_back(std::current_exception()); }
        });
        for (auto& worker : workers) worker.join();
        if (!failures.empty()) std::rethrow_exception(failures.front());
        auto blas = MakeIntrusive<BottomLevelAccelStruct>();
        std::thread create([&] { try { blas->Create(info); } catch (...) { failures.push_back(std::current_exception()); } }); create.join();
        if (!failures.empty()) std::rethrow_exception(failures.front());
        auto copy = blas;
        std::thread releaseFirst([value = std::move(blas)]() mutable { value = nullptr; });
        std::thread releaseLast([value = std::move(copy)]() mutable { value = nullptr; });
        releaseFirst.join(); releaseLast.join();
        CHECK_TRUE(hooks.state.asCreates == 65 && hooks.state.asDestroys == 65 && BufferAllocationTestProbe::AllocationCount() == 0);
    }
    {
        Hooks hooks;
        Buffer input;
        AccelStructTriangleGeometry geometry; geometry.vertexBuffer = &input; geometry.vertexStride = 12; geometry.vertexCount = 3; geometry.primitiveCount = 1;
        BottomLevelAccelStructCreateInfo info; info.SetTriangles(std::span(&geometry, 1));
        BottomLevelAccelStruct blas; blas.Create(info);
        const auto handle = blas.GetVkAccelerationStructure();
        const auto queries = hooks.state.asProperties;
        const auto formats = hooks.state.asFormats, sizes = hooks.state.asSizes, creates = hooks.state.asCreates,
            addresses = hooks.state.asAddresses, buffers = hooks.state.creates;
        const auto address = blas.GetDeviceAddress();
        const auto originalSizes = blas.GetBuildSizes();
        const auto alignment = blas.GetScratchAlignment();
        const auto flags = blas.GetVkBuildAccelerationStructureFlags();
        info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
        bool rejected = false; try { blas.Create(info); } catch (const std::exception&) { rejected = true; }
        CHECK_TRUE(rejected && blas.GetVkAccelerationStructure() == handle && hooks.state.asProperties == queries &&
            hooks.state.asFormats == formats && hooks.state.asSizes == sizes && hooks.state.asCreates == creates &&
            hooks.state.asAddresses == addresses && hooks.state.creates == buffers && blas.GetDeviceAddress() == address &&
            blas.GetVkBuildAccelerationStructureFlags() == flags && blas.GetScratchAlignment() == alignment &&
            blas.GetBuildSizes().accelerationStructureSize == originalSizes.accelerationStructureSize &&
            blas.GetBuildSizes().buildScratchSize == originalSizes.buildScratchSize &&
            blas.GetBuildSizes().updateScratchSize == originalSizes.updateScratchSize,
            "Duplicate BLAS Create must reject before native queries and preserve its original owner!");
    }
    TestImageValidationAndRollback();
    TestImageViewsAndOwners();
    TestImageThreads();
    TestLifecycleAndMove();
    TestValidationAndFailureRetry();
    TestAllocationAlignmentAndUnmap();
    TestViews();
    TestIndependentThreads();
}
