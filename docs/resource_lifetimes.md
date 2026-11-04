# Image, ImageView, RenderPass and Framebuffer lifetimes

Native Vulkan objects are owned by `VulkanDetail::VulkanHandleResource<T>`, a
`RefCounted` heap object held through the existing `IntrusivePtr<T>`. Its
destructor releases the native object before releasing its parent dependencies.
This layer owns resources; it does not insert synchronization or wait for GPU
work.

## Values and resource versions

`Image` and `RenderPass` remain noncopyable facades with explicit `Create()` and
`Destroy()`. Creation publishes a new version only after creation succeeds.
`Destroy()` clears the facade's caches and releases its current version.

`ImageView` and `Framebuffer` are copyable values. `Reset()` drops that value's
references. Other copies remain valid. A view retains the native image and its
allocation, and a framebuffer retains its views and native render pass. These
references point to native resource objects, never back to facade objects or
their caches. ImageView information is a snapshot: rebuilding the original
Image cannot change an old view's handle, dimensions, format or subresource
range.

`ImageView::Information::width/height/depth` retain their existing meaning: mip
zero dimensions of the source image. `GetViewSize()` returns dimensions at the
selected base mip. Remaining mip/layer counts are resolved when the view is
created.

```cpp
ImageViewInfo attachmentViewInfo;
attachmentViewInfo.CustomizeMipLevels(0, 1);

auto color = colorImage.View(attachmentViewInfo);
auto depth = depthImage.View(attachmentViewInfo);

FramebufferInfo info;
info.SetImageView("color", color);
info.SetImageView("depth", depth);
auto framebuffer = renderPass.GetFramebuffer(info);

// Existing values retain their original resources and description.
colorImage.Destroy();
depthImage.Destroy();
renderPass.Destroy();

CommandBuffer commands;
commands.BeginRenderPass(framebuffer, 0, 0, width, height);
commands.BindGraphicsProgram(program);
commands.SetViewport(0.f, 0.f, float(width), float(height));
commands.SetScissor(0, 0, width, height);
commands.Draw(3);
commands.EndRenderPass();
```

Framebuffer-only BeginRenderPass uses the stored pass description and default
clear values. An explicit compatible RenderPass and explicit clear values may
also be supplied. Graphics binding with no explicit target uses the active
render pass and subpass.

## Strong caches

`Image::View()` and `RenderPass::GetFramebuffer()` cache child values with strong
references. Drop those references with `ClearViewCache()` and
`ClearFramebufferCache()`. These methods neither invalidate external values nor
wait for GPU work. Destroy and successful replacement creation also clear the
facade's cache.

Clear the relevant framebuffer cache when replacing attachments, especially on
swapchain resize. Keeping a cached framebuffer intentionally keeps its image
resources alive. Native swapchain images retain their swapchain generation;
replacement creation passes the previous native swapchain to Vulkan. Acquire
and present still apply only to the current generation.

Framebuffer dimensions default to the minimum available dimensions and layer
count across the supplied views. Use `CustomizeSize()` for explicit dimensions;
an attachment-free framebuffer requires them. Attachments must each select one
mip and satisfy the render pass's format, sample count and usage requirements.

## Recording and GPU completion

Typed image and render-pass commands automatically retain the resource version
used to create their native parameters. ShaderResources and DescriptorSetDesc
copy image views rather than retaining pointers to local view values.

Command references move into the recording result before CPU command storage
is cleared. Normal submissions transfer the references into a frame completion
callback before submission. The callback releases its references after all
queues for that frame have completed, normally when the frame slot is reused.
`ExecuteCommandsAndWait()` uses its own HostFence completion callback.

For native callbacks, register each image, view, pass or framebuffer explicitly:

```cpp
const auto nativeView = view.GetVkImageView();
commands.KeepAlive(view);
commands.AppendCommand([nativeView](VkCommandBuffer commandBuffer)
{
    RecordCustomCommands(commandBuffer, nativeView);
});
```

KeepAlive preserves the native resource version, not the facade's C++ address.
References captured by a callback must separately remain valid until CPU
recording completes. Device and allocator lifetimes must cover all surviving
resource values. Resource types not yet migrated to the RHI threading policy
retain their existing device-thread restrictions for cache mutation and final
resource release.

Buffer, Program, acceleration structure and other resource ownership
contracts remain unchanged. Image layout transitions and access synchronization
remain explicit. Dynamic rendering and imageless framebuffers are outside this
change.

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
Graphics pipeline cache and, when defaults are configured, the RenderPass cache.
`GetVkPipeline(state)` remains lazy and mutates both Program-local PSO state and
the Device Graphics pipeline cache. Program cleanup consults pipeline caches and
drops RenderPass snapshots; callers serialize those operations with related
creation, lookup, release and teardown, plus conflicting access to that Program.
Default RenderPass/framebuffer operations retain their existing synchronization
requirements.

RayTracing creation accesses descriptor-layout and pipeline-layout caches, the
RayTracing pipeline cache and the supplied `VkPipelineCache`. Group handles and
named host metadata are prepared before publishing the Program. If handle
retrieval fails after native pipeline creation, that pipeline remains owned by
the Device cache. A retry is allowed; cache reuse depends on the existing full
pipeline key, including shader module handles, so a retry that creates new
modules may produce a separate cache entry. Cleanup follows `DestroyPipeline`,
which probes the Graphics, Compute and RayTracing caches; callers synchronize
those cache accesses as well as the input `VkPipelineCache` and teardown.

Neither path uses internal locks or a global Device lock. Independent work may
proceed when it uses independent shared state. Device initialization, destruction
and recreation require exclusive access relative to these operations.
