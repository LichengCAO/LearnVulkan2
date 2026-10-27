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
resource values. Cache mutation and final resource release follow the existing
device-thread convention.

Buffer, Sampler, Program, acceleration structure and other resource ownership
contracts remain unchanged. Image layout transitions and access synchronization
remain explicit. Dynamic rendering and imageless framebuffers are outside this
change.
