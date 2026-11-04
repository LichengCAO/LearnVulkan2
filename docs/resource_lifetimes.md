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
