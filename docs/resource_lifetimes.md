# Resource ownership and rendering descriptions

Native images and views are owned by `VulkanDetail::VulkanHandleResource<T>`, a
`RefCounted` heap object held through `IntrusivePtr<T>`. Its destructor releases
the native object before releasing parent dependencies. This ownership does not
insert synchronization or wait for GPU work. Native render passes and
framebuffers instead belong solely to the Device's private render-target cache.

## Image values and resource versions

`Image` is a noncopyable facade with explicit `Create()` and `Destroy()`.
Creation publishes a new version only after creation succeeds; `Destroy()` clears
its view cache and releases its current version. `ImageView` is a copyable value
that retains the native image and allocation. `Reset()` drops that value's
reference; other copies remain valid. References point to native resource
objects, never back to facade objects or their caches.

ImageView information is a snapshot: rebuilding the original Image cannot change
an old view's handle, dimensions, format or subresource range.
`ImageView::Information::width/height/depth` are mip zero dimensions of the source
image. `GetViewSize()` returns dimensions at the selected base mip. Remaining
mip/layer counts are resolved when the view is created.

`ImageView::GetResourceId()` returns a nonzero identity for each native view
instance, or zero for an empty value. Copies of a view have the same ID. A new
native instance receives a new ID even if Vulkan recycles an old handle value.
IDs are non-owning and provide no validity tracking.

`Image::View()` caches views with strong references. `ClearViewCache()` drops
only those references and does not invalidate external views or wait for GPU
work. Image destruction and successful replacement creation clear the cache.
Native swapchain images retain their swapchain generation; replacement creation
passes the previous native swapchain to Vulkan. Acquire and present apply only
to the current generation.

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

Buffer, Program and acceleration structure ownership remains unchanged. Image
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

All three ShaderPrograms are created synchronously through MyDevice entries and
have no public Create, Destroy or Reset. Their wrappers own ShaderModules and
host reflection metadata while pipeline/layout handles remain owned by Device
caches. Destruction releases owned modules and drops borrowed handles synchronously;
it does not wait for GPU work or retain submitted resources. Destroy Program
instances before Device/cache teardown and complete their GPU uses first.

## Shader program ownership

Create with `device.CreateComputeShaderProgram(program, finalizedInfo)`. The
Program owns its ShaderModule, retains its own PipelineLayout metadata and borrows
pipeline/layout handles from Device caches. It cannot be copied or moved and has
no public Create, Destroy or Reset. Use scope exit or an owning `unique_ptr.reset()`
to release it. Input descriptions may be reset, modified or destroyed after
successful creation without changing the Program's reflection or native handles.

Creation validates finalized input, an empty output, a live Device, initialized
descriptor-layout, pipeline-layout and compute pipeline allocators, and cached
device push-constant limits before native calls. Work completes synchronously in
a temporary Program; only a fully created result is transferred to the output
using nonthrowing member transfers. Failure releases temporary owned resources,
leaves the output unchanged and permits retry. Layout entries already published
remain cached. This migration preserves the existing compute allocator algorithm.

Callers coordinate creation with all other accesses to both layout caches, the
compute pipeline cache and the supplied VkPipelineCache, including cache teardown.
Destruction synchronously releases the owned shader module and uses the existing
DestroyPipeline path to clear the pipeline borrow. That path searches the graphics
cache first, then compute, and ray tracing if earlier searches miss. Callers must
also synchronize those cache reads with conflicting accesses. PipelineLayout
metadata cleanup accesses only host storage. There are no internal locks or
thread affinity; exclude conflicting access to the same Program and keep creation
inputs stable. Independent operations using independent shared state may proceed
concurrently without a global Device lock.

An empty Program's destructor does not access Device. A successfully created
Program must be destroyed while its Device and relevant caches remain alive.
Native pipeline/layout resources stay cached until their owning caches are
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

Graphics creation accesses descriptor-layout and pipeline-layout caches, the
Graphics pipeline cache and, when a default pipeline is materialized, the native
render-pass cache under the Device's dedicated mutex.
`GetVkPipeline(state)` remains lazy and mutates both Program-local PSO state and
the Device Graphics pipeline cache. Program cleanup consults pipeline caches and
drops CPU render-pass snapshots; callers serialize those operations with related
creation, lookup, release and teardown, plus conflicting access to that Program.
The render-target mutex does not synchronize these other caches or Program state.

RayTracing creation accesses descriptor-layout and pipeline-layout caches, the
RayTracing pipeline cache and the supplied `VkPipelineCache`. Group handles and
named host metadata are prepared before publishing the Program. If handle
retrieval fails after native pipeline creation, that pipeline remains owned by
the Device cache. A retry is allowed; cache reuse depends on the existing full
pipeline key, including shader module handles, so a retry that creates new
modules may produce a separate cache entry. Cleanup follows `DestroyPipeline`,
which probes the Graphics, Compute and RayTracing caches; callers synchronize
those cache accesses as well as the input `VkPipelineCache` and teardown.

Program state and pipeline/layout caches have no internal locks. Independent
work may proceed when it uses independent shared state. The rendering cache's
dedicated mutex does not introduce a global Device lock. Device initialization,
destruction and recreation require exclusive access relative to these operations.
