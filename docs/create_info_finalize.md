# CreateInfo finalization

Complex RHI descriptions have two synchronous steps:

```cpp
ComputeShaderProgramCreateInfo info;
info.SetShader("shaders/example.comp.spv");
info.Finalize();                 // Host: load SPIR-V, reflect, resolve layout data.

{
    ComputeShaderProgram program;
    device.CreateComputeShaderProgram(program, info); // Synchronous Device creation.
    info.Reset();               // The program retains the data it needs.
    // Record and submit commands using program.
    // Complete native uses before leaving this scope, and before Device teardown.
}                               // Releases Host wrappers; native modules remain cached.
```

`Finalize()` and `IsFinalized()` are provided by graphics, compute and ray tracing program CreateInfos, RenderPassDesc, PipelineLayoutCreateInfo, and the internal ShaderModuleCreateInfo. All three ShaderPrograms use `device.CreateXxxShaderProgram(program, info)`; pipeline layouts use `device.GetOrCreatePipelineLayout(layout, info)`. Internal module creation goes through Device's private `_GetOrCreateShaderModule`, with no ShaderModule Create/Destroy. These creation entries reject unfinalized inputs before calling Vulkan; they do not silently finalize them.

Rendering uses `RenderPassDesc` and `FramebufferDesc` directly. Native handles are obtained through `device.GetOrCreateRenderPass(passDesc)` and `device.GetOrCreateFramebuffer(passDesc, framebufferDesc)`, or during command preparation. The Device owns the native cache; there are no public RenderPass or Framebuffer facade types.

Program creation requires an empty output and live Device/allocators. It publishes
only after successful temporary creation; failures leave the output unchanged and
retryable. ShaderPrograms have no public Create/Destroy/Reset and cannot be copied
or moved. Use scope exit or owning `unique_ptr.reset()` for release. Creation and
destruction access shared state under caller synchronization; see
[Program ownership](resource_lifetimes.md#shader-program-ownership).

Native shader modules are cached by complete finalized SPIR-V content across
Programs and paths, with full comparison after hashing. Each wrapper retains its
own immutable reflection snapshot. Clearing descriptions or wrappers does not free
native modules; Device teardown releases them after pipeline caches. Already
published modules remain cached after later Program failures, so retries reuse
them without reloading files. Creation accesses the externally synchronized
module cache; Finalize and native recording do not. See
[module ownership](resource_lifetimes.md#shader-module-ownership).

## State and ownership

- A successful Set/Add/Customize/Reset invalidates that description's snapshot. Set device options, including pipeline caches, before Finalize.
- Finalize validates and builds a local result, then publishes an immutable snapshot. Failure leaves the description unfinalized.
- Repeated Finalize without changes reuses the snapshot and does not reload shader files. To reload the same path, call SetShader with that path again and then Finalize.
- Existing copy/move restrictions are preserved. Copies share immutable results; editing or resetting one copy does not invalidate another. RenderPassDesc is noncopyable and nonmovable.
- Native pointer views are assembled against owned storage when creating objects. Objects retain or copy their required data, so the input CreateInfo may be modified or destroyed after Create returns.
- Descriptions may be reused for several object creations. Concurrent mutation or Finalize of the same description requires caller synchronization.

Finalize does not load Vulkan, create resources, query device properties or wait for GPU work. Device Create checks actual capabilities against properties cached at device selection. Runtime options such as VkPipelineCache remain opaque inputs.

## Pipeline layouts and render passes

Pipeline layouts now accept descriptor-set-layout **descriptions** rather than previously created layout objects:

```cpp
DescriptorSetLayoutCreateInfo set;
set.SetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
PipelineLayoutCreateInfo info;
info.AddDescriptorSetLayout(set).Finalize();
PipelineLayout layout;
device.GetOrCreatePipelineLayout(layout, info);
```

To use reflection, call `info.SetReflection(reflection).Finalize()`, then `device.GetOrCreatePipelineLayout(layout, info)`. Sparse set indices retain empty layout descriptions. Device creation obtains native descriptor layouts through `MyDevice::GetOrCreateDescriptorSetLayout()`, reusing the ordered binding snapshots produced by Finalize.

Standalone DescriptorSetLayoutCreateInfo keeps its existing SetBinding interface
and does not require Finalize. Create a wrapper with
`device.GetOrCreateDescriptorSetLayout(layout, info)`; it owns its binding snapshot
and borrows the native handle held by the Device cache. It has no public Create,
Destroy or Reset; clear the value with `layout = {}`. See
[descriptor set layout ownership](resource_lifetimes.md#descriptor-set-layout-ownership)
for its external synchronization and lifetime contract. PipelineLayout likewise
owns its host metadata and borrows a Device-cached native handle. It has no
public Create/Destroy/Reset; clear it with `layout = {}`. See
[pipeline layout ownership](resource_lifetimes.md#pipeline-layout-ownership).

`RenderPassDesc::Finalize()` resolves attachment/subpass names, validates sample counts and resolve attachments, and publishes immutable arrays and derived metadata. It and `IsFinalized()` are Host-only operations and require no Device. `GetSubpassCount()`, `GetClearValues()` and `GetAttachmentIndex(name)` require finalized input. Successful edits invalidate the current snapshot; existing command and Graphics state snapshots remain unchanged. Generated default descriptions and automatic vertex layouts are finalized as part of GraphicsShaderProgramCreateInfo::Finalize. Programs expose their default with `GetDefaultRenderPassDesc()`.

`FramebufferDesc` is copyable and movable and has no Finalize step. `SetImageView(name, view)` snapshots only the view's resource ID, raw handle and attachment metadata. `CustomizeSize(width, height, layers)` overrides the derived dimensions; `Reset()` clears the description. These operations retain no Image or ImageView resource. The supplied attachments must remain valid through native preparation, recording and GPU execution.

`CommandBuffer::BeginRenderPass(passDesc, framebufferDesc, x, y, width, height[, clearValues], contents)` copies CPU snapshots without calling Vulkan. Serial preparation resolves native render-pass and framebuffer handles before worker recording, including buffers without Program bindings. Native recording uses the prepared handles. The Device internally synchronizes its two render-target caches and retains their native objects until teardown; see [rendering ownership](resource_lifetimes.md#rendering-descriptions-and-native-cache).

Simple Buffer/Image/Sampler CreateInfos do not require Finalize. Samplers are created through `MyDevice::GetOrCreateSampler(sampler, info)` as borrowed handle values owned by the Device cache; see [Sampler ownership](resource_lifetimes.md#sampler-ownership). Rendering descriptions do not retain attachments automatically; use `KeepAlive(Image/ImageView)` for explicit command retention. Other typed image commands and descriptor resources keep their existing retention behavior. Finalization itself does not introduce GPU synchronization.

## Independent Host build

In a configured C++20 toolchain with shaderc and Vulkan headers available:

```text
cmake -S . -B build_host -DLEARN_VULKAN_BUILD_DEVICE=OFF
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```

This builds `rhi_host` and its tests using Vulkan headers, GLM headers, SPIRV-Reflect and shaderc. It does not configure or link the window system, Vulkan loader, VMA or device runtime. A normal Device-enabled build also runs these Host tests alongside the existing regression suite.
