# Reflection-driven programs and independent resources

## Command API

~~~cpp
void BindGraphicsProgram(GraphicsShaderProgram& inProgram,
    const GraphicsPipelineStateInfo& inStateInfo = {});
void BindComputeProgram(ComputeShaderProgram& inProgram);
void BindRayTracingProgram(RayTracingShaderProgram& inProgram);
void BindGraphicsShaderResource(const ShaderResources& inResources);
void BindComputeShaderResource(const ShaderResources& inResources);
void BindRayTracingShaderResource(const ShaderResources& inResources);
void AppendCommand(std::function<void(VkCommandBuffer)> inRecord);
~~~

Pipeline commands select programs independently for each bind point and never accept
resources. Each typed resource entry uses its own bind point's selected program and
reports an error if none was bound. It snapshots the complete settings and resolves
names immediately. Binding another kind of pipeline preserves the other selections.
Pipeline and descriptor preparation runs serially before worker recording. Each
buffer with bindings, dynamic state, push constants or execution commands remains
one Vulkan command buffer, including when it exceeds 256 commands. The previous untyped
BindShaderResources entry is removed.

CommandBuffer member operations return void and enqueue owned commands. Buffer,
Image, RenderPass, Framebuffer, PipelineLayout and ShaderBindingTable arguments
use existing resource objects. Rectangles, viewports, buffer copy regions and
image clear ranges are scalar parameters; complex image transfer regions retain
their Vulkan structures. CPU arrays and bytes are copied when enqueued; Vulkan
resource handles and GPU addresses remain caller-owned and must stay valid until
GPU completion. AppendCommand owns its callback and invokes it later during
recording, in stream order. Capture parameters by value; captured references
must remain valid until recording finishes. Empty callbacks are rejected before
enqueuing. The callback must preserve the tracked RenderPass lifecycle and
Program/ShaderResource bindings; use member functions to change those states.
Any buffer containing a callback remains one native Vulkan command buffer.
RenderGraph compiled commands use a private borrowed-command entry; their owners
must keep them alive until asynchronous recording completes.

## Default graphics setup

~~~cpp
GraphicsShaderProgramCreateInfo createInfo;
createInfo.AddVertexShader("scene.spv")
          .AddFragmentShader("scene.spv");
createInfo.SetDefaultColorAttachment(0, VK_FORMAT_R8G8B8A8_UNORM);
createInfo.SetDefaultDepthAttachment(VK_FORMAT_D32_SFLOAT); // Optional.
GraphicsShaderProgram program;
program.Create(createInfo);
const RenderPass& renderPass = program.GetDefaultRenderPass();

FramebufferCreateInfo framebufferInfo;
framebufferInfo.SetRenderPass(renderPass);
framebufferInfo.SetImageView("outColor", *colorImage.View()); // Shader output name.
framebufferInfo.SetImageView("__depth", *depthImage.View());
Framebuffer framebuffer;
framebuffer.Create(framebufferInfo);

ShaderResources resources;
resources.SetBuffer("camera", cameraBuffer);
resources.SetCombinedImageSampler("albedo", *albedoImage.View(), sampler);
CommandBuffer commands;
commands.BeginRenderPass(renderPass, framebuffer, 0, 0, width, height);
commands.BindGraphicsProgram(program);
commands.BindGraphicsShaderResource(resources);
commands.SetViewport(0.0f, 0.0f, float(width), float(height));
commands.SetScissor(0, 0, width, height);
// BindVertexBuffers and BindIndexBuffer take Buffer objects when needed.
commands.Draw(3);
commands.EndRenderPass();
~~~

Color configuration must cover every reflected fragment output location exactly;
sparse locations preserve unused subpass color slots. The caller supplies formats,
which must match the shader numeric category. SetDefaultColorAttachment's optional
finalLayout defaults to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL. Swapchain output
can specify VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.

Default attachments use one sample, CLEAR/STORE, transparent black, and depth 1.
BeginRenderPass uses those clear defaults; its clearValues overload copies explicit overrides.
Attachment names are fragment output names, or color<location> for unnamed outputs.
Depth uses __depth; avoid that name for fragment outputs. The program owns its default
RenderPass; the caller owns framebuffer, images and views.

Vertex pipelines use triangle lists; mesh shaders supply their output topology.
Both paths use back-face culling, counter-clockwise fronts, disabled blending and
dynamic viewport/scissor. Depth test/write and LESS_OR_EQUAL are enabled
when the selected subpass has depth. Simple vertex inputs are packed into binding 0,
sorted by location, with vertex rate. Explicit AddVertexInputDescription calls replace
all inferred vertex descriptions. Matrices, arrays, interface blocks, packed component
layouts and 64-bit inputs require explicit vertex descriptions. For complex inputs,
Create builds the default RenderPass and defers pipeline creation until explicit
vertex state is supplied.

Without default attachment configuration, use the existing explicit RenderPass path
with GraphicsPipelineStateInfo::SetRenderPassSubpass. Graphics binding checks the
active RenderPass handle and subpass. Begin/EndRenderPass and NextSubpass clear only
graphics selection; compute and ray tracing selections survive. Compute/ray tracing
pipeline and resource bindings require no active rendering scope. Graphics resource
binding requires an inline render pass and a program selected in the current subpass.

## Shader selection and compute

Every shader argument uses a SPIR-V path (`std::string_view`). Builders immediately
copy paths; explicitly empty paths are errors. Files are read during `Create()`.
Each module may contain at most one entry per stage. Loading rejects duplicate stages,
including stages unused by the requested program, before creating a Vulkan module.
The function name or hit parameter position supplies the expected stage. Its unique
entry is used automatically, with its actual SPIR-V name, which need not be `"main"`.
Different stages in one file may have identical entry names.

~~~cpp
GraphicsShaderProgramCreateInfo graphics;
graphics.AddVertexShader("scene.spv")
        .AddFragmentShader("scene.spv"); // One candidate in each stage.

ComputeShaderProgramCreateInfo compute;
compute.SetShader("compute.spv").CustomizePipelineCache(pipelineCache);
ComputeShaderProgram program;
program.Create(compute);
~~~

Graphics exposes `AddVertexShader`, `AddFragmentShader`, `AddGeometryShader`,
`AddTaskShader`, and `AddMeshShader`, all with the same path-only signature
and a reference return for chaining. Adding the same stage twice fails immediately.
`Create()` requires vertex with optional geometry, or mesh with optional task; task
requires mesh and the two paths cannot be mixed. Fragment is optional on either path.
Mesh pipelines omit vertex input and input assembly state. Tessellation and patch
state are outside this interface. `Reset()` clears all shaders and default attachments.
Compute uses `SetShader(path)`; `Reset()` clears the shader and pipeline cache.

Within each program, one Vulkan shader module is created per exact file string.
Actual pipeline stages are deduplicated by `(file string, stage)` in
first-use order. Paths are not normalized and file contents are not hashed. Only
selected entries contribute descriptors, push constants, vertex inputs and fragment
outputs, including automatic vertex and attachment defaults. A declared but unused
resource is excluded. Resources used only by unrequested stages do not affect the
layout. Different requested stages using the same binding merge their stage masks.

`ShaderModule::Create(path)` loads, validates and caches reflection for every stage in a module.
`GetShaderStageInfo(stage)` returns a Vulkan stage whose entry name stays valid until
module destruction. `GetEntryReflection(stage)` returns a const reference to the same
cached entry; the Vulkan stage name points directly into that entry's owned string.
The public module configuration wrapper and generic graphics shader addition API
have been removed. Missing files, invalid SPIR-V, missing/duplicate stages and
illegal stage combinations raise exceptions; failed program creation releases partial
modules, layouts and pipelines and permits rebuilding.

## Ray tracing programs

~~~cpp
RayTracingShaderProgramCreateInfo builder;
builder.AddRayGenerationShaderGroup("main", "scene.spv")
       .AddMissShaderGroup("background", "scene.spv");
RayTracingShaderProgram program;
program.Create(builder);

ShaderBindingTableCreateInfo tableInfo;
tableInfo.SetShaderGroupSet(program.GetShaderGroupSet())
         .SetTableCapacity(ShaderTableType::RayGeneration, 1, 0)
         .SetTableCapacity(ShaderTableType::Miss, 1, 0);
ShaderBindingTable table;
table.Create(tableInfo);
table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "main")
     .SetShaderTableBinding(ShaderTableType::Miss, 0, "background");
table.Commit();
const auto raygenRegion = table.GetShaderTableRegion(ShaderTableType::RayGeneration);
const auto missRegion = table.GetShaderTableRegion(ShaderTableType::Miss);

ShaderResources resources;
resources.SetAccelerationStructure("scene", tlas);
resources.SetImage("outputImage", *outputImage.View(), VK_IMAGE_LAYOUT_GENERAL);
commands.BindRayTracingProgram(program);
commands.BindRayTracingShaderResource(resources);
commands.TraceRaysKHR(table, width, height);
~~~

Push constant ranges are stored in `PipelineLayout` and checked against the device
limit when creating a reflected layout. `PushConstantManager` and the combined
`RayTracingShaderProgram::Do()` / `PipelineInput` API have been removed. Use
`commands.PushConstants(program.GetLayout(), stages, offset, size, data)`
to enqueue a copy of push constant data.

Builder copies paths and group names. Group names are nonempty
and unique across all table types. All arguments are validated before registration;
failed additions retain no partial shaders or reserved names. Stage resolution happens
at `Create()`, using the same selection rules as graphics and compute. Shaders and
Vulkan groups preserve first-use order. Different group names keep separate Vulkan
groups and handles even when they share identical shader combinations. At least one
ray generation group is required when creating a pipeline. Multiple shaders of the
same stage, such as miss shaders, can be supplied through separate files.

~~~cpp
builder.AddTriangleHitShaderGroup("opaque", "closest.spv")
       .AddTriangleHitShaderGroup("alpha", "closest.spv", "any.spv")
       .AddTriangleHitShaderGroup("mask", std::nullopt, "any.spv")
       .AddTriangleHitShaderGroup("empty")
       .AddProceduralHitShaderGroup("volume", "closest.spv", "intersection.spv", "any.spv")
       .AddProceduralHitShaderGroup("volume_only", std::nullopt, "intersection.spv")
       .AddCallableShaderGroup("lighting", "callable.spv")
       .CustomizeMaxRecursionDepth(2)
       .CustomizePipelineCache(pipelineCache);
~~~

General group signatures are `(name, spirv)`.
Triangle parameters are `(name, closestSpirv, anySpirv)`; both shader arguments are
optional and default to `std::nullopt`. Procedural parameters are
`(name, closestSpirv, intersectionSpirv, anySpirv)`: closest must be supplied explicitly
as a path or `std::nullopt`, intersection is required, and any-hit defaults to `std::nullopt`.
Optional paths use `std::optional<std::string_view>`; explicitly empty paths are errors.
Vulkan triangle hit groups can
omit closest-hit, any-hit, or both; procedural groups always require intersection.

The program owns its shader modules and always reflects descriptor layouts and push
constant ranges from the selected entries. Builder retains recursion depth (default 1)
and pipeline cache (default `VK_NULL_HANDLE`) settings. Creation failures clean partial
state; `Destroy()` permits recreation.

~~~cpp
const auto& group = program.GetShaderGroupSet().GetGroupData("opaque");
// group.type is ShaderTableType::Hit; group.handle owns the aligned handle bytes.
ShaderBindingTableCreateInfo tableInfo;
tableInfo.SetShaderGroupSet(program.GetShaderGroupSet())
         .SetTableCapacity(ShaderTableType::RayGeneration, 2, 0)
         .SetTableCapacity(ShaderTableType::Miss, 1, 0)
         .SetTableCapacity(ShaderTableType::Hit, 2, sizeof(record));
ShaderBindingTable table;
table.Create(tableInfo);
table.SetShaderTableBinding(ShaderTableType::RayGeneration, 0, "main")
     .SetShaderTableBinding(ShaderTableType::RayGeneration, 1, "main")
     .SetShaderTableBinding(ShaderTableType::Miss, 0, "background")
     .SetShaderTableBinding(ShaderTableType::Hit, 0, "opaque", record);
table.SetShaderTableBinding(ShaderTableType::Hit, 0, "alpha", record);
table.SetShaderTableBinding(ShaderTableType::Hit, 1, "volume", bytes, byteCount);
table.SetShaderTableBinding(ShaderTableType::Hit, 1, "volume_only");
table.Commit();
const auto raygenRegion = table.GetShaderTableRegion(ShaderTableType::RayGeneration, 1);
~~~

GetGroupData is the only group-handle query and reports unknown names. Each GroupData
owns its handle bytes, padded to shaderGroupHandleAlignment. GroupSet copies own
independent storage; Reset clears only that copy. CreateInfo and the created SBT each
store their own GroupSet copy, so record construction remains valid after the source
GroupSet is reset or its pipeline is destroyed. GPU execution still requires the
corresponding live pipeline.

SetTableCapacity configures each region's record count and maximum extra payload size
independently. Create allocates a fixed-size buffer and Host mirror; changing capacity
requires recreating the SBT with a new CreateInfo, which has no Reset method.
SetShaderTableBinding takes the table type explicitly and rejects mismatched group
types, out-of-range bindings and oversized payloads. `binding` is the record position
within that table, so different tables can use the same binding. Payload overloads
accept no data, a typed const reference, or a byte pointer and length. A null byte
pointer is valid only for an empty payload. Payloads start shaderGroupHandleSize bytes
after the record's start;
record strides use shaderGroupHandleAlignment, with raygen additionally aligned to
shaderGroupBaseAlignment, and must fit maxShaderGroupStride. Replacing a record clears
its previous bytes and never resizes the buffer. Commit uploads the full Host mirror
through Buffer::CopyFromHost. GetShaderTableRegion returns one raygen record selected
by its optional record offset; other types require zero offset and return the full
region, or a zero region when empty. Acceleration structure SBT record offsets retain
their meaning as offsets into the hit table; group names do not change them.

Use the typed commands and a trace command. Ray tracing submissions require an actual
compute-capable queue family. Payloads, hit attributes and shader record structures
are not reflected.

## Named resources

~~~cpp
void SetBuffer(std::string_view name, const Buffer& buffer,
    VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE, uint32_t arrayElement = 0);
void SetImage(std::string_view name, const ImageView& view,
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t arrayElement = 0);
void SetCombinedImageSampler(std::string_view name, const ImageView& view, const Sampler& sampler,
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t arrayElement = 0);
void SetSampler(std::string_view name, const Sampler& sampler, uint32_t arrayElement = 0);
void SetBufferView(std::string_view name, const BufferView& view, uint32_t arrayElement = 0);
void SetAccelerationStructure(std::string_view name, const TopLevelAccelStruct& acceleration,
    uint32_t arrayElement = 0);
void Clear();
~~~

Repeated settings and aliases resolving to one binding/array element use the last
value. Every typed resource binding call must provide all reflected bindings and fixed
array elements. Calls never fill gaps with resources from earlier calls. Unknown or
ambiguous names, type mismatches, missing elements, invalid storage
image layouts, buffer bounds and array bounds raise exceptions. Storage images
require GENERAL. Runtime descriptor arrays and dynamic buffer descriptors are
unsupported on this path.

Required CreateInfo and resource inputs use references. Setters borrow the referenced
resources; ownership and lifetime requirements are unchanged. Raw byte arrays,
pointer/count sequences, optional pNext chains and allocation callbacks retain their
pointer interfaces.

Resources can be rebound without rebinding a pipeline. After changing a program at
the same bind point,
explicitly bind the new program's resources before drawing or dispatching. Programs
without descriptors need no resource command. User-provided raw Command callbacks
remain responsible for legal draw/dispatch state.

Snapshots copy resource references and parameters, not GPU contents. Later edits to
ShaderResources do not affect queued commands. Keep programs and referenced resources
alive and stable until GPU completion. The caller manages image transitions, resource
barriers and synchronization between submissions or queues.

Each FrameSlot owns independent descriptor pools. Cache hits compare descriptor
contents as well as hashes. Changed snapshots receive separate sets, and pools reset
only after frame completion. Explicit/manual descriptor and pipeline paths remain.

## Reflection and validation

Descriptor layouts, sets, allocation caching and `ShaderResources` are declared in
`my_vulkan/resource/descriptor.h`. The named-resource and explicit-binding APIs
remain separate classes within this module.
`ShaderEntryReflection` and `ShaderReflector` are declared together in
`my_vulkan/pipeline/private/shader_reflect.h`; third-party reflection headers remain
confined to its implementation.

`PipelineLayout` holds the Vulkan pipeline layout, descriptor set layouts, push
constant ranges and resource lookup metadata. Programs obtain it through
`MyDevice::GetOrCreatePipelineLayout`, using their finalized layout description.
For reflection, use `info.SetReflection(reflection).Finalize()`; manual descriptions
also remain available. The wrapper owns its metadata and borrows the Device-cached
native handle. Clear it with assignment to `{}`; it has no public Create/Destroy.
See [pipeline layout ownership](resource_lifetimes.md#pipeline-layout-ownership)
for cache lifetime and external synchronization requirements.

Query a resource with `auto [set, binding] = program.GetLayout().GetSetBinding(name)`.
The result is a `std::pair<uint32_t, uint32_t>` returned by value. Unknown or
ambiguous names throw; manually created layouts have no resource names. Programs
do not forward name queries, and `PipelineLayout` does not expose its name map or
ambiguity set.

ShaderReflector retains descriptors, inputs, outputs and push constants. Inputs and
outputs are vectors of `ShaderEntryReflection::InterfaceVariable`, each holding a
name, location and `VkFormat`. Variables sharing a location remain separate records;
default graphics layout generation sorts by location and checks for duplicates.
`ShaderReflector::ReflectEntry(stage)` returns a const reference to cached data without
repeating reflection. References and entry-name pointers remain valid until the next
successful `Create()` or `Destroy()`; failed recreation preserves existing data.
Cross-stage descriptors merge by
set/binding, validate types/counts, retain aliases and report ambiguous names. Programs
reflect the unique entry of each requested stage. Duplicate stages are rejected when
loading a module; identical entry names in different stages remain supported.

CPU tests compile temporary GLSL and use scoped Vulkan call substitutes. Coverage
includes merging/errors, packed vertices, sparse MRT, optional depth, explicit layouts,
all resource setter types, arrays, snapshots, program switches, ordered commands,
cache collisions and frame recycle ordering. Ray tracing mocks capture actual Vulkan
stages/groups and cover shader deduplication, first-addition order, optional hit stages,
name/role validation, named handles, SBT payload/stride constraints, copies/Reset,
failed creation cleanup and rebuilding. Assembly fixtures cover duplicate-stage
rejection, non-main names, cross-stage identical names, all five graphics
addition methods, invalid stage combinations, shared module handles, remapped ray
tracing groups, selected-entry interfaces and exclusion of unused resources.

~~~powershell
cmake -S . -B build -DLEARN_VULKAN_GPU_TESTS=ON
cmake --build build --config Debug --target learn_vulkan
ctest --test-dir build -C Debug --output-on-failure
~~~

GPU testing is opt-in and requires a Vulkan 1.3 graphics/compute device. Tests enable
Khronos validation when installed, select a non-main entry from a multi-entry compute
module and verify buffer readback over five frames and two frame slots, and read a
pixel from an offscreen color/depth draw. No window
or swapchain is required. When the necessary ray tracing extensions/features are
available, an explicitly selected raygen entry in a multi-stage module also interleaves
compute and ray tracing over five frames and two slots, checking independent bindings, resource snapshots and recycling.
Unsupported devices explicitly skip only the ray tracing GPU smoke test.
For an existing build, set LEARN_VULKAN_GPU_TESTS=1 in the
process environment and run the executable.
