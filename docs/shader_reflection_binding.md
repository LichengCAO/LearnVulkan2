# Reflection-driven programs and independent resources

## Command API

~~~cpp
auto BindGraphicsProgram(GraphicsShaderProgram& inProgram,
    const GraphicsPipelineStateInfo& inStateInfo = {}) -> CommandBuffer&;
auto BindComputeProgram(ComputeShaderProgram& inProgram) -> CommandBuffer&;
auto BindRayTracingProgram(RayTracingShaderProgram& inProgram) -> CommandBuffer&;
auto BindGraphicsShaderResource(const ShaderResources& inResources) -> CommandBuffer&;
auto BindComputeShaderResource(const ShaderResources& inResources) -> CommandBuffer&;
auto BindRayTracingShaderResource(const ShaderResources& inResources) -> CommandBuffer&;
~~~

Pipeline commands select programs independently for each bind point and never accept
resources. Each typed resource entry uses its own bind point's selected program and
reports an error if none was bound. It snapshots the complete settings and resolves
names immediately. Binding another kind of pipeline preserves the other selections.
Pipeline and descriptor preparation runs serially before worker recording. Each
buffer with new bindings remains one Vulkan command buffer. The previous untyped
BindShaderResources entry is removed.

## Default graphics setup

~~~cpp
GraphicsShaderProgramCreateInfo createInfo;
createInfo.AddVertexShader("scene.spv")
          .AddFragmentShader("scene.spv");
createInfo.SetDefaultColorAttachment(0, VK_FORMAT_R8G8B8A8_UNORM);
createInfo.SetDefaultDepthAttachment(VK_FORMAT_D32_SFLOAT); // Optional.
GraphicsShaderProgram program;
program.Create(&createInfo);
const RenderPass& renderPass = program.GetDefaultRenderPass();

FramebufferCreateInfo framebufferInfo;
framebufferInfo.SetRenderPass(&renderPass);
framebufferInfo.SetImageView("outColor", colorImage.View()); // Shader output name.
framebufferInfo.SetImageView("__depth", depthImage.View());
Framebuffer framebuffer;
framebuffer.Create(&framebufferInfo);

BeginRenderPassCommand::Parameters begin;
begin.renderPass = renderPass.GetVkRenderPass();
begin.framebuffer = framebuffer.GetVkFramebuffer();
begin.renderArea.extent = {width, height};
begin.clearValues = renderPass.GetClearValues();

ShaderResources resources;
resources.SetBuffer("camera", &cameraBuffer);
resources.SetCombinedImageSampler("albedo", albedoImage.View(), &sampler);
CommandBuffer commands;
commands.BeginRenderPass(begin).BindGraphicsProgram(program);
commands.BindGraphicsShaderResource(resources);
// Add existing Command implementations for viewport/scissor, vertex binding and draw.
commands.EndRenderPass();
~~~

Color configuration must cover every reflected fragment output location exactly;
sparse locations preserve unused subpass color slots. The caller supplies formats,
which must match the shader numeric category. SetDefaultColorAttachment's optional
finalLayout defaults to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL. Swapchain output
can specify VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.

Default attachments use one sample, CLEAR/STORE, transparent black, and depth 1.
Pass GetClearValues() to BeginRenderPass parameters to use those clear defaults.
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
program.Create(&compute);
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
RayTracingShaderProgram::Builder builder;
builder.AddRayGenerationShaderGroup("main", "scene.spv")
       .AddMissShaderGroup("background", "scene.spv");
RayTracingShaderProgram program;
program.Create(&builder);

ShaderBindingTable::Descriptor table(program.GetShaderGroupSet());
table.SetShaderTableBinding(0, "main")
     .SetShaderTableBinding(0, "background");
// Upload the SBT records and manage the SBT buffer through the explicit path.

ShaderResources resources;
resources.SetAccelerationStructure("scene", &tlas);
resources.SetImage("outputImage", outputImage.View(), VK_IMAGE_LAYOUT_GENERAL);
commands.BindRayTracingProgram(program).BindRayTracingShaderResource(resources);
// Use ExternalCommand to push constants and call vkCmdTraceRaysKHR.
~~~

Push constant ranges are stored in `PipelineLayout` and checked against the device
limit when creating a reflected layout. `PushConstantManager` and the combined
`RayTracingShaderProgram::Do()` / `PipelineInput` API have been removed. Record
push constant and trace commands explicitly through `ExternalCommand`.

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
ShaderBindingTable::Descriptor table(program.GetShaderGroupSet());
table.SetShaderTableBinding(0, "main")
     .SetShaderTableBinding(0, "background")
     .SetShaderTableBinding(0, "opaque", &record);
table.UpdateShaderTableBinding(0, "alpha", &record);
table.SetShaderTableBinding(1, "volume", bytes, byteCount);
table.UpdateShaderTableBinding(1, "volume_only");
~~~

GetGroupData is the only group-handle query and reports unknown names. Each GroupData
owns its handle bytes, padded to shaderGroupHandleAlignment. GroupSet copies own
independent storage; Reset clears only that copy. Descriptor stores its own GroupSet
copy, so record construction remains valid after the source GroupSet is reset or its
pipeline is destroyed. GPU execution still requires the corresponding live pipeline.

Set/Update derive the table type from the group name. `binding` is the record position
within that table, so different tables can use the same binding. Both methods support
no payload, a typed pointer, or a byte pointer and length. Payloads start after the
aligned handle; record strides use shaderGroupHandleAlignment, with raygen additionally
aligned to shaderGroupBaseAlignment. Set can grow the table stride and preserves
existing records. Update requires an initialized table and a payload fitting its
current stride, preserving the existing binding-position behavior. Smaller or absent
payloads clear the previous record bytes. Acceleration structure SBT record offsets
retain their meaning as offsets into the hit table; group names do not change them.

Do remains available to bind pipeline/resources and trace. For independent bindings,
use the typed commands and a trace command. Ray tracing submissions require an actual
compute-capable queue family. Payloads, hit attributes and shader record structures
are not reflected.

## Named resources

~~~cpp
void SetBuffer(std::string_view name, const Buffer* buffer,
    VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE, uint32_t arrayElement = 0);
void SetImage(std::string_view name, const ImageView* view,
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t arrayElement = 0);
void SetCombinedImageSampler(std::string_view name, const ImageView* view, const Sampler* sampler,
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t arrayElement = 0);
void SetSampler(std::string_view name, const Sampler* sampler, uint32_t arrayElement = 0);
void SetBufferView(std::string_view name, const BufferView* view, uint32_t arrayElement = 0);
void SetAccelerationStructure(std::string_view name, const TopLevelAccelStruct* acceleration,
    uint32_t arrayElement = 0);
void Clear();
~~~

Repeated settings and aliases resolving to one binding/array element use the last
value. Every typed resource binding call must provide all reflected bindings and fixed
array elements. Calls never fill gaps with resources from earlier calls. Unknown or
ambiguous names, type mismatches, missing elements, null references, invalid storage
image layouts, buffer bounds and array bounds raise exceptions. Storage images
require GENERAL. Runtime descriptor arrays and dynamic buffer descriptors are
unsupported on this path.

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
constant ranges and resource lookup metadata. Programs create it directly from
`ShaderProgramReflection`; the `ShaderProgramLayout` wrapper has been removed.
Manual creation through `PipelineLayoutCreateInfo` remains available and retains
descriptor metadata without resource names. `Destroy()` clears the handle and all
layout and lookup data.

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
