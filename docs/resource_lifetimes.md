# Resource ownership and rendering descriptions

Native images belong to `VulkanDetail::ImageResource`, held through
`IntrusivePtr<T>`. Native image views and swapchain generations use
`VulkanDetail::VulkanHandleResource<T>`, whose destructor releases the native
object before releasing parent dependencies. This ownership does not insert
synchronization or wait for GPU work. Native render passes and framebuffers
instead belong solely to the Device's private render-target cache.

## Buffer ownership

`Buffer` retains synchronous public `Create(const BufferCreateInfo&)` and its
`RefCounted` base. It has no public `Destroy()` or release alias. Its `noexcept`
destructor releases owned resources; use scope or an external owner's `reset()`
for early release. Other strong references keep the Buffer alive. `IsValid()`
reports whether this Buffer owns a native buffer. Copying and assignment are
disabled; the `noexcept` move constructor transfers storage, information and
the view cache, leaving the source empty without transferring reference counts.
Move only when callers can preserve the existing facade-address and reference
lifetime requirements.

```cpp
BufferCreateInfo info{};
info.SetBufferSize(4096);
info.SetBufferUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
auto buffer = MakeIntrusive<Buffer>();
buffer->Create(info);
// Record, submit, and complete all native uses before the final release.
buffer.reset();
```

CreateInfo does not require Finalize. Size and usage default to zero so omitted
required values are rejected. Creation checks an empty target, valid inputs and
available Device/allocator before native work. It creates and binds in temporary
storage and publishes only on success. Duplicate creation preserves the old
buffer and views; a failed creation leaves an empty target and permits retry.

Private `BufferStorage` owns the creating VkDevice, VkBuffer and a move-only
`MemoryAllocation`. Public headers do not expose VMA types. MemoryAllocation owns
one VmaAllocation and borrows its originating VmaAllocator; it maps directly from
that allocation and caches a single mapped address. Repeated host writes reuse
the mapping. A mapping failure leaves no cached address and permits retry.
Neither mapping nor release looks up VkBuffer in an allocation table.

`View()` retains its borrowed-pointer interface and per-Buffer cache. Views do
not independently retain the parent Buffer; callers keep it alive and coordinate
cache access. Creation and cache publication failures release unpublished views.
Cleanup destroys BufferViews, unmaps owned mappings, destroys VkBuffer, and then
frees its allocation. Saved creation dependencies are used for release, without
a MyDevice singleton lookup or implicit GPU wait.

After Device initialization, independent Buffers may create, map, write, create
views and release on different threads. One Buffer, its allocation, and its view
cache require exclusion of conflicting accesses. Different resources share VMA's
internally synchronized allocator; the RHI adds no allocator lock. Resource
creation, movement and reference counting do not synchronize GPU memory access.

Keep Device and VmaAllocator alive until all Buffers are released, and complete
GPU use before final release. Device/allocator initialization, destruction and
recreation require exclusive access. Buffer command and descriptor retention
behavior is unchanged. Buffer and Image allocations now travel with their
resource owners; MemoryAllocator keeps no native-handle allocation tables.

## Image values and resource versions

`Image` is a noncopyable, nonmovable RefCounted facade with synchronous public
`Create()` and a `noexcept` destructor. It has no public `Destroy()` or release
alias; use scope or release an external owner to end its lifetime. Create only
accepts an empty target and publishes after native creation, allocation, binding
and resource construction all succeed. Repeated Create fails before native work
and preserves the existing image, information and cached views. Failed creation
leaves an empty target and permits retry.

Reconstruction uses a new Image owner. `ImageView` is a copyable value retaining
its original native view, image and allocation, even after the original Image
facade is destroyed. Its `Reset()` drops that value's reference; other copies
remain valid. References point to resource objects, never back to facades or
their caches.

```cpp
ImageCreateInfo info;
info.SetUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
info.CustomizeSize2D(512, 512);
ImageView retained;
{
    auto image = MakeIntrusive<Image>();
    image->Create(info);
    retained = image->View();
} // retained still owns the native view, image and allocation
// Complete all native uses before releasing the last retained reference.
retained.Reset();
```

ImageCreateInfo needs no Finalize. Explicit dimensions do not read swapchain
state; default two-dimensional dimensions use the current nonzero swapchain
extent, which the caller keeps stable during creation. Invalid required fields
and basic dimension/type/mip/sample combinations fail before native creation.

Private ImageResource owns an ImageStorage containing the saved creating
VkDevice, native handle, native-ownership flag, MemoryAllocation and optional
swapchain-generation reference. Public Image headers expose no VMA types. VMA
allocation is returned directly from AllocateForVkImage; no handle-to-allocation
lookup or separate FreeMemory(VkImage) path exists. An owned native image is
destroyed before its allocation is freed, using saved dependencies without a
MyDevice singleton lookup. Views retain their ImageResource, ensuring native
view destruction precedes native image destruction. Native creation, resource
construction and cache-publication failures release unpublished resources.

ImageView information is a snapshot: creating a replacement Image owner cannot
change an old view's handle, dimensions, format or subresource range.
`ImageView::Information::width/height/depth` are mip zero dimensions of the source
image. `GetViewSize()` returns dimensions at the selected base mip. Remaining
mip/layer counts are resolved when the view is created.

`ImageView::GetResourceId()` returns a nonzero identity for each native view
instance, or zero for an empty value. Copies of a view have the same ID. A new
native instance receives a new ID even if Vulkan recycles an old handle value.
IDs are non-owning and provide no validity tracking.

`Image::View()` caches views with strong references. `ClearViewCache()` drops
only those references and does not invalidate external views or wait for GPU
work. Image destruction drops its cache and current resource references; external
references can keep both native objects and allocation alive. View creation uses
the parent resource's saved VkDevice and publishes complete metadata and resource
ownership together; a failed request preserves prior cached entries.

Private swapchain import also requires an empty target. Its native image is
borrowed: it has no VMA allocation and is never passed to vkDestroyImage. It
retains its swapchain generation; replacing the swapchain releases old wrappers,
while surviving views retain the old generation. Native replacement passes the
previous native swapchain to Vulkan. Acquire, present and semaphore management
continue to apply to the current generation with their existing contracts.

After Device initialization, independent ordinary Images may create, cache views
and release concurrently. Callers exclude conflicting access to one Image/view
cache and one ImageView wrapper. Independent copied view values can be released
on separate threads; VMA handles allocation synchronization and no RHI allocator
locks are added. Swapchain recreation, acquire/present and final generation
release still require coordination of their related shared state. Keep Device
and allocator alive through every resource reference, exclude teardown/recreation
from related operations, and complete GPU uses before the final native release.
No Image/view release waits for GPU completion implicitly.

## BLAS ownership

`BottomLevelAccelStruct` retains synchronous public
`Create(const BottomLevelAccelStructCreateInfo&)` and its `RefCounted` base.
It is noncopyable and nonmovable, has a `noexcept` destructor, and exposes no
public `Destroy()` or release alias. `IsValid()` reports whether it owns a
native AS. Use scope or release an external owner's reference for early cleanup;
other strong references keep the complete BLAS alive.

Creation accepts only an empty target. Repeated Create fails before native
queries or resource creation and preserves the existing handle, device address,
flags, build sizes, scratch alignment and geometry layout. Rebuild with a new
BLAS owner. CreateInfo keeps its current defaults and requires no Finalize.
Creation checks Device, PhysicalDevice, allocator and required native entrypoints
before queries, retains the existing geometry/device-limit validation, and
publishes only after backing Buffer, native AS and nonzero address all succeed.
Failures release temporary resources, leave an empty target and permit retry.

Private `VulkanDetail::AccelerationStructureStorage` saves the creating VkDevice
and owns the native AS and backing Buffer. The public header exposes no VMA
types. Final release destroys the AS before releasing the Buffer, which then
releases its allocation; cleanup uses saved creation dependencies without a
MyDevice lookup. Creation does not execute a GPU Build, and destruction does not
implicitly wait for GPU work.

After Device initialization, independent BLAS instances may create and release
concurrently, including final strong-reference releases on different threads.
Callers exclude conflicting access to one BLAS and keep inputs stable during
creation. VMA internal synchronization remains enabled; Device, allocator and
native dispatch must remain valid through final release, and teardown/recreation
must not overlap related operations. Finish all GPU uses before final release.

Build/Update/Copy command descriptions keep their current snapshot behavior and
do not automatically retain the BLAS or input buffers. Vertex/index/transform
buffers, scratch buffers and TLAS references to BLAS remain borrowed; callers
preserve the lifetimes needed for command enqueue, recording and GPU use.

## TLAS ownership

`TopLevelAccelStruct` follows the BLAS empty-target `Create()` and RAII contract:
it is RefCounted, noncopyable/nonmovable, exposes `IsValid()` and has a `noexcept`
destructor with no public `Destroy()` or release alias. Duplicate creation fails
before native queries and preserves the existing handle, flags, build sizes,
scratch alignment and layout. Rebuild with a new owner. Failures leave an empty
object and allow retry.

Both AS types own the same private `AccelerationStructureStorage`, which saves
VkDevice and destroys native AS before its backing Buffer and allocation. TLAS
creation validates initialized dependencies and its required native entrypoints,
then prepares metadata and resources locally before publication. It does not
query a BLAS address, upload instances or perform a GPU Build. An empty instance
list remains supported.

`WriteInstances` remains a separate static upload operation and never requires a
TLAS owner. An empty list accepts an uncreated destination Buffer without accessing
Device. Nonempty uploads retain their existing
range, payload and BLAS-address validation. TLAS does not retain referenced BLAS
objects, instance buffers or scratch buffers. Callers preserve their native
lifetimes for all uses, including BLAS references used by a built TLAS.

Independent TLAS instances can create and release concurrently, including final
strong-reference release from any thread. Same-instance conflicts require caller
exclusion. Keep Device/allocator/native dispatch stable and alive through final
release, exclude teardown/recreation, and complete GPU work before release. No
automatic retention, internal lock or GPU wait is introduced.

## SBT ownership

`ShaderBindingTable` retains synchronous `Create()`, record setters, `Commit()`
and region queries. It is noncopyable/nonmovable and is not RefCounted. Its
`noexcept` destructor releases the owned Buffer and Host data; there is no public
`Destroy()` or release alias. Default destruction does not access MyDevice.
Use scope or an external owner's `reset()` for early release; `IsValid()` reports
whether it owns a created backing Buffer.

Creation requires an empty target and initialized Device/allocator dependencies.
It reads cached ray tracing properties and prepares group metadata, layouts,
zeroed Host records and backing Buffer in temporary state before publishing.
Failure leaves an empty, retryable target; duplicate creation preserves existing
records, layout and uploaded data. Changing capacities requires a new SBT owner.
Create does not implicitly Commit records.

Group handles and record data are owned CPU copies, independent of the source
CreateInfo or Program's lifetime. GPU execution still requires a live compatible
pipeline. Record setters validate before writing; Commit uploads through Buffer
and can be retried after a mapping failure. Capacity, payload, alignment and region
semantics are described in [shader binding tables](shader_reflection_binding.md).

Independent SBT instances may create, edit, upload, query and release on different
threads under stable Device/allocator/cached-property dependencies. Callers
exclude conflicting access to one SBT and complete all GPU uses before releasing
it or updating records still in use. TraceRays snapshots region values without
retaining the SBT or backing Buffer. Buffer's saved dependencies and VMA
synchronization handle release; there is no implicit GPU wait or deferred cleanup.

## Rendering descriptions and native cache

`RenderPassDesc` is noncopyable and nonmovable. Its `Finalize()` resolves names,
validates attachment/subpass settings and publishes an immutable CPU snapshot;
`Finalize()` and `IsFinalized()` need no Device or Vulkan calls.
`GetSubpassCount()`, `GetClearValues()` and `GetAttachmentIndex(name)` require a
finalized description. Successful edits invalidate its current snapshot while
existing command and Graphics state snapshots remain unchanged.

`FramebufferDesc` is copyable and movable. `SetImageView(name, view)` stores only
the resource ID, raw VkImageView and attachment metadata; it retains no Image or
ImageView references. `Reset()` clears this CPU description. Dimensions default
to the minimum available size and layer count across the supplied views.
`CustomizeSize(width, height, layers)` supplies explicit dimensions, which are
required for an attachment-free framebuffer. Every attachment must select one
mip and satisfy the pass's format, sample count and usage requirements.

Native lookup uses these Device entries:

```cpp
auto nativePass = device.GetOrCreateRenderPass(passDesc);
auto nativeFramebuffer = device.GetOrCreateFramebuffer(passDesc, framebufferDesc);
```

The return values are borrowed VkRenderPass and VkFramebuffer handles. There are
no public RenderPass or Framebuffer facade types, per-object release operations
or public rendering allocator getter. The Device owns every cached native
object until teardown and destroys framebuffers before render passes. Native
creation and cache publication complete synchronously; failed publication rolls
back the new native object.

Render-pass keys contain the full native description. Attachment names and clear
values remain Host metadata and do not affect native reuse. Framebuffer keys
contain the native render pass, ordered attachment IDs and handles, dimensions
and layer count. These caches have no eviction: changing attachments can
accumulate native framebuffers until Device teardown. Cached framebuffers retain
no attachment resources. After an attachment's native view is destroyed, its old
FramebufferDesc and queued snapshots must never be used again, even if a native
handle with the same numeric value later exists.

A dedicated MyDevice mutex guards both caches' lookup, native creation,
publication and teardown. The private RenderTargetCache allocator has no locks
of its own. This mutex covers only rendering cache state; Program-local state,
pipeline/layout caches and their release paths keep their existing caller
synchronization requirements. Concurrent readers may use one finalized
RenderPassDesc; callers exclude conflicting description mutation and access to
one CommandBuffer. Device initialization, destruction and recreation still
require exclusive access relative to related resource operations.

## Recording and GPU completion

`BeginRenderPass(passDesc, framebufferDesc, x, y, width, height[, clearValues], contents)`
copies an immutable pass snapshot and borrowed attachment metadata during Host
enqueue. It calls no Vulkan functions and creates no native rendering objects.
The overload without clear values copies the pass's defaults; the explicit
overload copies the supplied values. Both descriptions may be edited or
destroyed after enqueue because the command owns its CPU snapshot.

FrameSlot serial preparation obtains native rendering handles before worker
recording, including command buffers with no Program bindings. `Record()` uses
the prepared handles and performs no cache lookup or native creation. Graphics
state selects a finalized description with `SetRenderPassSubpass(desc, index)`;
binding without an explicit target uses the active rendering description and
subpass. `GraphicsShaderProgram::GetDefaultRenderPassDesc()` exposes its default
description as a const reference.

Rendering descriptions and commands do not automatically retain attachments.
The caller keeps each native image/view alive through preparation, recording
and GPU execution, or requests command retention explicitly:

```cpp
const RenderPassDesc& pass = program.GetDefaultRenderPassDesc();
ImageViewInfo attachmentViewInfo;
attachmentViewInfo.CustomizeMipLevels(0, 1);
auto color = colorImage.View(attachmentViewInfo);
auto depth = depthImage.View(attachmentViewInfo);
FramebufferDesc framebuffer;
framebuffer.SetImageView("outColor", color);
framebuffer.SetImageView("__depth", depth);

CommandBuffer commands;
commands.KeepAlive(color);
commands.KeepAlive(depth);
commands.BeginRenderPass(pass, framebuffer, 0, 0, width, height);
commands.BindGraphicsProgram(program);
commands.SetViewport(0.f, 0.f, float(width), float(height));
commands.SetScissor(0, 0, width, height);
commands.Draw(3);
commands.EndRenderPass();
```

`KeepAlive(Image)` and `KeepAlive(ImageView)` preserve their native resource
version. Other typed image commands retain their existing automatic image
references. ShaderResources and DescriptorSetDesc continue to copy image views
rather than retaining pointers to local view values.

Command references move into the recording result before CPU command storage
is cleared. Normal submissions transfer them into a frame completion callback
before submission. The callback releases its references after all queues for
that frame have completed, normally when the frame slot is reused.
`ExecuteCommandsAndWait()` uses its own HostFence completion callback.

Native callbacks can use the same explicit image/view retention:

```cpp
const auto nativeView = view.GetVkImageView();
commands.KeepAlive(view);
commands.AppendCommand([nativeView](VkCommandBuffer commandBuffer)
{
    RecordCustomCommands(commandBuffer, nativeView);
});
```

KeepAlive preserves resources rather than the facade's C++ address. Callback
references must separately remain valid until CPU recording completes. Complete
all native uses before Device/cache teardown, and keep Device and allocation
dependencies alive through final Image/ImageView release. Resource types not yet
migrated to the RHI threading policy retain their existing restrictions.

Rendering descriptions do not change Program or acceleration structure ownership. Image
layout transitions and access synchronization remain explicit. Dynamic rendering
and imageless framebuffers are outside this change. The disabled RenderGraph
path receives only call-site migration; this change does not enable it.

## Command pool ownership

`CommandPool` owns one native command pool. It is noncopyable and nonmovable,
and keeps its synchronous `Create()`, `Reset()` and
`AllocateOrGetCommandBuffer()` interfaces. Its `noexcept` destructor destroys
the native pool once, using the VkDevice saved at creation. There is no public
`Destroy()` or release alias; use scope or an external owner's `reset()` for
early release. Destruction of an empty pool does not access MyDevice.

```cpp
{
    CommandPool pool;
    CommandPoolCreateInfo info;
    info.CustomizeQueueFamilyType(QueueFamilyType::GRAPHICS);
    pool.Create(info);
    auto commandBuffer = pool.AllocateOrGetCommandBuffer(); // borrowed
    // Record, submit, and complete GPU use before leaving this scope.
}
```

Keep Device alive until every pool has been destroyed. Reset and destruction
perform no implicit GPU waits: the caller completes GPU use of all buffers
first. Destroying a native pool frees all its command buffers, without
individual Free calls ([Vulkan destruction rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyCommandPool.html)).
Borrowed command-buffer handles become invalid when their pool is destroyed.
Reset invalidates their recorded contents and makes the tracked primary and
secondary buffers available for reuse; a native Reset failure leaves the Host
reuse queues unchanged.

Creation validates an empty target, an available Device and a valid queue
family before native creation. It publishes the device, pool and family only
after success. A failed creation leaves a null pool and
`VK_QUEUE_FAMILY_IGNORED`, allowing retry. Repeated Create rejects the request
and preserves the original pool and tracked buffers.

CommandPool has no thread affinity or internal lock. Independent pools may be
created, allocated from, reset and destroyed concurrently after Device
initialization. Callers exclude conflicting access to the same pool and its
command buffers, including recording; sequential transfer to another thread
is allowed. Device native forwarding maintains no pool registration table or
other shared allocation records. Device teardown and recreation still require
exclusive access relative to pool operations.

FrameSlot owns pools per queue and recording worker, publishes a lazy pool
only after successful creation, and can retry after failure. Its existing
completion path waits before resetting pools for reuse. This ownership does
not move the dynamic descriptor pool into CommandPool or change recording and
submission behavior.

## Sampler ownership

Sampler is a borrowed handle value. Create it through MyDevice's externally
synchronized creation entry point:

```cpp
Sampler sampler;
device.GetOrCreateSampler(sampler, SamplerCreateInfo{});
Sampler copy = sampler;
const auto nativeSampler = copy.GetVkSampler(); // borrowed
sampler = {}; // clears this value; the cache still owns nativeSampler
```

Sampler stores only `VkSampler` and has no public `Create()`, `Destroy()`, or
`Reset()`. Default construction produces a null handle. Copy and move copy the
handle without changing the move source. Assignment and default destruction do
not allocate or release resources, access Device state, or wait for the GPU.
Creation rejects an already initialized target without changing it. Failed
creation leaves an empty target and permits retry.

SamplerAllocator owns a cache from normalized descriptions to native handles.
Equivalent descriptions reuse the cached handle even after all wrappers have
disappeared. MyDevice destroys the entire cache during device teardown; there
is no per-wrapper release, Reset, or eviction. Each cache entry balances one
successful native creation, including entries whose handle values are equal.
Failure to publish a cache entry immediately rolls back that native creation.

The allocator has no internal locks. Callers exclude concurrent access to the
same sampler cache, including creation, lookup, and cache destruction. Sampler
value operations access no cache and may run on independent values on other
threads. Conflicting accesses to the same wrapper still require exclusion.

Borrowed values do not keep MyDevice alive. Finish all native uses, including
GPU execution, before its sampler cache is destroyed. Values may still exist
afterward, but their handles must not be used with Vulkan; no validity tracking
is added. Their subsequent destruction remains a host-only operation.

DescriptorSetDesc and ShaderResources still borrow the Sampler wrapper by
reference. That wrapper must remain alive and stable through host description
resolution and descriptor-write construction. After the native handle has been
copied into descriptors, its validity follows the Device cache's lifetime.

## Descriptor set layout ownership

DescriptorSetLayout borrows a native handle from the Device cache and owns a
snapshot of its binding metadata. Create a value through the Device:

```cpp
DescriptorSetLayoutCreateInfo info;
info.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
DescriptorSetLayout layout;
device.GetOrCreateDescriptorSetLayout(layout, info);
auto copy = layout;
layout = {}; // clears this value; copy retains its handle and binding metadata
```

There is no object-level Create, Destroy or Reset. Copy and move preserve the
existing value semantics; their resulting values own their binding information.
Assignment and destruction release only host storage and never access the
Device or cache. Clear a value with assignment to `{}`. Creating into a nonempty
target is rejected without changing it; a failed creation leaves an empty target
and permits retry. The input description may be modified or destroyed afterward.

Equivalent descriptions share a native layout regardless of binding insertion
order. Native layouts, including empty ones used for sparse set indices, remain
cached after all wrappers disappear. Device teardown destroys the cache; wrapper
values do not keep it alive. Complete every native use before cache destruction.
Surviving wrappers are safe to clear or destroy, but their old handles are
invalid and no validity tracking is provided.

Callers serialize creation, cache lookup and cache destruction on the same
Device descriptor-layout cache. Independent wrapper operations access no cache
and may run on other threads; modifying the same wrapper still requires
exclusion. This synchronization is separate from the sampler cache and from
descriptor-pool allocation and release. Other resource classes retain their
existing restrictions until their own migration.

DescriptorSetLayoutCreateInfo does not require a new Finalize step. Device
creation sorts directly supplied bindings and reuses the immutable ordered
binding arrays already prepared by PipelineLayoutCreateInfo::Finalize. Its
public allocator getter is removed; internal cache access stays behind the
Device creation entry point.

## Pipeline layout ownership

Create a PipelineLayout with
`device.GetOrCreatePipelineLayout(layout, finalizedInfo)`. The wrapper owns its
descriptor-layout values, binding and push-constant snapshots, resource names
and ambiguous-name set. It borrows a native pipeline layout owned by the Device
cache. Native identity uses the complete ordered set-layout and push-constant
description; reflection names belong to each wrapper and do not affect reuse.

There is no public Create, Destroy or Reset. Copying, moving, clearing with
`layout = {}` and destruction access only host storage. Moved-to values retain
their metadata; no additional moved-from state guarantee is made. Modifying or
destroying the source CreateInfo does not change an existing wrapper.

Creation rejects unfinalized input, a nonempty target, an unavailable Device or
required allocator, and push constants exceeding cached device limits before
native creation. A nonempty target remains unchanged on rejection. Creation
into an empty target publishes its handle and metadata only on success, so
failure permits retry. Child descriptor layouts already published to their
cache remain cached after an outer failure. Failure to publish a newly created
native pipeline layout immediately destroys that native object.

Callers serialize creation and lookup with all other accesses to the same
pipeline-layout cache and, when child layouts are needed, descriptor-layout
cache, including cache teardown. Allocators have no internal locks. Independent
wrapper operations access neither cache; conflicting accesses to the same
wrapper require exclusion. This does not require one global Device lock.

The cache retains native layouts until Device teardown. Complete all native
uses, including GPU execution, before cache destruction. Wrappers do not retain
the Device; surviving wrappers may safely be cleared or destroyed, but their
old handles are invalid and have no validity tracking.

## ShaderModule ownership

Internal `ShaderModule` wrappers borrow VkShaderModule from the Device's private
`VulkanDetail::ShaderModuleAllocator` and retain their own immutable
`shared_ptr<const ShaderReflector>` snapshot. They are default constructible,
noncopyable/nonmovable and have a default `noexcept` destructor, with no
Create/Destroy or release alias. GetShaderStageInfo/GetEntryReflection continue
to use the wrapper's reflection; entry names and returned references remain
valid while that snapshot is retained. Wrapper cleanup never accesses Device.

MyDevice's private `_GetOrCreateShaderModule` is used by Compute creation and the
shared `ShaderDetail::CreateStages` helper. It requires finalized input, an empty
target, initialized Device/cache and native entrypoints. Code and reflection
come from the existing snapshot without file I/O or additional reflection. Only
after obtaining a native handle does it publish the wrapper with nonthrowing
assignments. Rejected or failed creation leaves an empty target; duplicate
creation preserves the old handle and reflection.

The cache owns native handles and a copy of each complete SPIR-V key. Hashes
select buckets; full word comparison decides equality. Paths, stage/entry-name
parameters and reflection addresses are not independent cache-key fields.
Identical code across files and Programs shares native handles; reloading changed
code at the same path creates a new cache entry without invalidating existing
wrappers. Native flags remain zero and pNext remains null.

Failed native creation never adopts its output. Failed cache publication destroys
only the new unpublished module. Already published modules remain cached when a
later Program creation step fails and are reused by retries. Each successful
cached native creation is destroyed once; there is no per-wrapper native Free,
cache eviction, public cache getter, runtime Reset or deferred cleanup.

Callers serialize accesses to this cache, including creation, lookup and teardown;
it has no internal mutex. Module destruction only drops Host references, while
the Program's pipeline cleanup retains its existing shared-state requirements.
Graphics lazy pipeline creation uses acquired module handles, and Record does not
query or create modules. Finalize remains pure Host. Device teardown destroys
cached modules after pipeline caches and before VkDevice, without an implicit
GPU wait. The wrapper does not retain Device; after cache teardown its old native
handle must not be used, although Host-only wrapper cleanup is still valid.

All three ShaderPrograms are created synchronously through MyDevice entries and
have no public Create, Destroy or Reset. Programs own their internal module
wrappers and Host metadata; native module, pipeline and layout handles belong to
Device caches. Destruction drops wrappers and borrowed handles synchronously;
it does not wait for GPU work or retain submitted resources. Destroy Program
instances before Device/cache teardown and complete their GPU uses first.

## Shader program ownership

Create with `device.CreateComputeShaderProgram(program, finalizedInfo)`. The
Program owns its ShaderModule wrapper, retains its own PipelineLayout metadata and
borrows native module/pipeline/layout handles from Device caches. It cannot be copied or moved and has
no public Create, Destroy or Reset. Use scope exit or an owning `unique_ptr.reset()`
to release it. Input descriptions may be reset, modified or destroyed after
successful creation without changing the Program's reflection or native handles.

Creation validates finalized input, an empty output, a live Device, initialized
shader-module, descriptor-layout, pipeline-layout and compute pipeline allocators, and cached
device push-constant limits before native calls. Work completes synchronously in
a temporary Program; only a fully created result is transferred to the output
using nonthrowing member transfers. Failure releases temporary owned resources,
leaves the output unchanged and permits retry. Module and layout entries already
published remain cached. This migration preserves the existing compute allocator algorithm.

Callers coordinate creation with all other accesses to the shader-module cache,
both layout caches, the compute pipeline cache and the supplied VkPipelineCache,
including cache teardown. Destruction releases the module's Host wrapper and uses the existing
DestroyPipeline path to clear the pipeline borrow. That path searches the graphics
cache first, then compute, and ray tracing if earlier searches miss. Callers must
also synchronize those cache reads with conflicting accesses. PipelineLayout
metadata cleanup accesses only host storage. There are no internal locks or
thread affinity; exclude conflicting access to the same Program and keep creation
inputs stable. Independent operations using independent shared state may proceed
concurrently without a global Device lock.

An empty Program's destructor does not access Device. A successfully created
Program must be destroyed while its Device and relevant caches remain alive.
Native module/pipeline/layout resources stay cached until their owning caches are
destroyed. Program creation/destruction does not wait for the GPU, enqueue work
or change command recording, submission or resource retention. Complete native
uses before destroying the Program and before Device/cache teardown.

## Graphics and RayTracing programs

Create with `device.CreateGraphicsShaderProgram(program, finalizedInfo)` or
`device.CreateRayTracingShaderProgram(program, finalizedInfo)`. Both APIs require
finalized input, an empty target, a live Device and initialized required
allocators. They construct a temporary Program and publish only after successful
completion. Failures leave the output empty and retryable. Both wrapper types are
noncopyable and nonmovable; scope exit or an owning `unique_ptr.reset()` releases
them.

Graphics creation accesses shader-module, descriptor-layout and pipeline-layout caches, the
Graphics pipeline cache and, when a default pipeline is materialized, the native
render-pass cache under the Device's dedicated mutex.
`GetVkPipeline(state)` remains lazy and mutates both Program-local PSO state and
the Device Graphics pipeline cache. Program cleanup consults pipeline caches and
drops CPU render-pass snapshots; callers serialize those operations with related
creation, lookup, release and teardown, plus conflicting access to that Program.
The render-target mutex does not synchronize these other caches or Program state.

RayTracing creation accesses shader-module, descriptor-layout and pipeline-layout caches, the
RayTracing pipeline cache and the supplied `VkPipelineCache`. Group handles and
named host metadata are prepared before publishing the Program. If handle
retrieval fails after native pipeline creation, that pipeline remains owned by
the Device cache. A retry is allowed; module handles already cached are reused,
so identical inputs can also reuse an existing pipeline under its current key.
Cleanup follows `DestroyPipeline`,
which probes the Graphics, Compute and RayTracing caches; callers synchronize
those cache accesses as well as the input `VkPipelineCache` and teardown.

Program state and module/pipeline/layout caches have no internal locks. Independent
work may proceed when it uses independent shared state. The rendering cache's
dedicated mutex does not introduce a global Device lock. Device initialization,
destruction and recreation require exclusive access relative to these operations.
