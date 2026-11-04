# CreateInfo finalization

Complex RHI descriptions have two synchronous steps:

```cpp
ComputeShaderProgramCreateInfo info;
info.SetShader("shaders/example.comp.spv");
info.Finalize();                 // Host: load SPIR-V, reflect, resolve layout data.

ComputeShaderProgram program;
program.Create(info);            // Device: create native modules, layouts and pipeline.
info.Reset();                   // The created program retains the data it needs.
// Record and submit commands using program.
// Synchronize GPU use before destruction, as required by the existing RHI.
program.Destroy();
```

`Finalize()` and `IsFinalized()` are provided by graphics, compute and ray tracing program CreateInfos, RenderPassCreateInfo, PipelineLayoutCreateInfo, and the internal ShaderModuleCreateInfo. Runtime objects have one `Create(const XxxCreateInfo&)` entry. Create rejects unfinalized inputs before calling Vulkan; it does not silently finalize them.

## State and ownership

- A successful Set/Add/Customize/Reset invalidates that description's snapshot. Set device options, including pipeline caches, before Finalize.
- Finalize validates and builds a local result, then publishes an immutable snapshot. Failure leaves the description unfinalized.
- Repeated Finalize without changes reuses the snapshot and does not reload shader files. To reload the same path, call SetShader with that path again and then Finalize.
- Existing copy/move restrictions are preserved. Copies share immutable results; editing or resetting one copy does not invalidate another. RenderPassCreateInfo remains noncopyable and nonmovable.
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
layout.Create(info);
```

To use reflection, call `info.SetReflection(reflection).Finalize()`, then `layout.Create(info)`. The old `layout.Create(reflection)` overload is removed. Sparse set indices retain empty layout descriptions. Native descriptor layouts are allocated during Device Create.

RenderPassCreateInfo::Finalize resolves attachment/subpass names, validates samples and resolve attachments, and owns the arrays and derived metadata. RenderPass::Create consumes that snapshot. Generated default render passes and automatic vertex layouts are finalized as part of GraphicsShaderProgramCreateInfo::Finalize.

Simple Buffer/Image/Sampler CreateInfos retain their previous usage. Command recording, asynchronous recording dispatch, queue submission, explicit waits and resource retention retain their existing semantics. Finalization itself does not introduce GPU synchronization.

## Independent Host build

In a configured C++20 toolchain with shaderc and Vulkan headers available:

```text
cmake -S . -B build_host -DLEARN_VULKAN_BUILD_DEVICE=OFF
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```

This builds `rhi_host` and its tests using Vulkan headers, GLM headers, SPIRV-Reflect and shaderc. It does not configure or link the window system, Vulkan loader, VMA or device runtime. A normal Device-enabled build also runs these Host tests alongside the existing regression suite.
