# Acceleration structures

## Type and file layout

Public headers live in `src/my_vulkan/resource/`:

| Header | Types and purpose |
| --- | --- |
| `acceleration_structure_types.h` | `VkAccelStructBuildInfo` for the public low-level KHR batch command and `AccelStructSizesInfo` for caller-owned batch task/scratch allocation. |
| `acceleration_structure_gpu_layout.h` | GPU record strides, address alignments, field offsets and bit packing for CLAS tasks and packed TLAS instances. |
| `acceleration_structure.h` | Ordinary BLAS/TLAS owners, geometry and instance descriptions, and their CreateInfo types. |
| `cluster_acceleration_structure.h` | CLAS templates, CLAS/Cluster BLAS batch owners and CreateInfo types, CPU task descriptions, and public indirect-build arguments. |

Include the ordinary or cluster header explicitly for the path being used.
`resource/private/` contains the implementations, native AS storage owner, and
`cluster_acceleration_structure_build.h` with the internal `ClusterBuildAddresses`
value snapshot shared with command recording. It is included by the cluster
header to store snapshots by value; callers do not construct or use it directly.
Single-implementation helpers remain in anonymous namespaces in the `.cpp` files.

Regression sources and test fixtures live in `src/test/`; `main.cpp` is the
device regression entry point. CMake also builds the Host-only tests from this
directory. GPU smoke tests remain opt-in as described below.

See [GPU build-input layout](acceleration_structure_gpu_layout.md) for the static
`GetBuildInfoLayout()` queries, native record offsets and bit encodings used by
GPU task producers, including packed TLAS instances.

The host path prepares descriptions and resources. GPU work is recorded through
`CommandBuffer`; recording never allocates an input/scratch Buffer or uploads
instances. All setters and recording methods return `void`.

## BLAS: host creation and explicit scratch

Geometry descriptions are independent types. `SetTriangles` and `SetAabbs`
copy the complete list and select its geometry type. Buffer references are
borrowed. Setters do not query Vulkan or resolve device addresses.

BLAS `Create()` requires an empty target; repeated creation rejects before native
queries and preserves the existing object. `IsValid()` reports native ownership.
There is no public `Destroy()`: scope or external owner release destroys the
native AS before its backing Buffer/allocation, without a GPU wait. Independent
BLAS instances can create and release concurrently; keep Device/allocator alive
and exclude conflicting operations on one instance. See [BLAS ownership](resource_lifetimes.md#blas-ownership).

The following uses an existing `vertexBuffer` with
`VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`:

```cpp
AccelStructTriangleGeometry triangle;
triangle.vertexBuffer = &vertexBuffer;
triangle.vertexOffset = 0;
triangle.vertexStride = 12;
triangle.vertexCount = 6;
triangle.primitiveCount = 2;
// Defaults: R32G32B32_SFLOAT, OPAQUE, non-indexed, no transform.

BottomLevelAccelStructCreateInfo blasInfo;
blasInfo.SetTriangles(std::span(&triangle, 1));
blasInfo.CustomizeBuildFlags(
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

BottomLevelAccelStruct blas;
blas.Create(blasInfo); // Host: query sizes and create the handle/backing Buffer.

Buffer scratch;
const auto& sizes = blas.GetBuildSizes();
const auto scratchBytes = std::max(sizes.buildScratchSize, sizes.updateScratchSize);
if (scratchBytes != 0)
{
    BufferCreateInfo scratchInfo{};
    scratchInfo.SetBufferSize(scratchBytes);
    scratchInfo.SetBufferUsage(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    scratchInfo.CustomizeAlignment(blas.GetScratchAlignment());
    scratch.Create(scratchInfo);
}

commandBuffer.BuildAccelerationStructureKHR(blas, blasInfo, scratch);
// Later, after the original build and required synchronization:
commandBuffer.UpdateAccelerationStructureKHR(blas, blas, blasInfo, scratch);
```

For indexed triangles set `indexBuffer`, `indexOffset` and `indexType`.
`primitiveCount` is the number of triangles, not the number of indices.
An optional transform uses `transformBuffer` and `transformOffset`.
AABBs use `AccelStructAabbGeometry` and `SetAabbs`, with an explicit Buffer,
offset and primitive count; the default stride is `sizeof(VkAabbPositionsKHR)`.

Both AS types expose `GetBuildSizes()` and `GetScratchAlignment()`. The latter
comes from the physical device's AS properties. Offsets are byte offsets from the
Buffer's device address, and the resulting absolute address must meet alignment
requirements. A zero scratch requirement accepts an uncreated scratch Buffer.

## TLAS: explicit host upload

TLAS has the same empty-target Create and RAII release contract as BLAS, including
`IsValid()` and no public `Destroy()`. Independent instances may create and release
concurrently; same-instance conflicts require caller exclusion. Empty instance
lists remain supported. See [TLAS ownership](resource_lifetimes.md#tlas-ownership).

`SetInstances` copies CPU descriptions without resolving BLAS addresses.
`TopLevelAccelStruct::WriteBuildInfoBufferInline` is static: no TLAS object is required.
Referenced BLAS objects need valid handles/device addresses; a null BLAS reference
encodes an inactive instance.

```cpp
AccelStructInstance instance;
instance.accelStruct = &blas;
// Defaults: identity transform, mask 0xFF, custom index/SBT offset/flags 0.

TopLevelAccelStructCreateInfo tlasInfo;
tlasInfo.SetInstances(std::span(&instance, 1));
tlasInfo.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);

BufferCreateInfo instanceBufferInfo{};
instanceBufferInfo.SetBufferSize(
    std::max(tlasInfo.GetInstanceBufferSize(), VkDeviceSize(sizeof(VkAccelerationStructureInstanceKHR))));
instanceBufferInfo.SetBufferUsage(
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
instanceBufferInfo.CustomizeAlignment(16);
instanceBufferInfo.CustomizeMemoryProperty(
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

Buffer instanceBuffer;
instanceBuffer.Create(instanceBufferInfo);
TopLevelAccelStruct::WriteBuildInfoBufferInline(tlasInfo, instanceBuffer);

TopLevelAccelStruct tlas;
tlas.Create(tlasInfo);
// Allocate tlasScratch using tlas.GetBuildSizes()/GetScratchAlignment(), as above.
commandBuffer.BuildAccelerationStructureKHR(
    tlas, tlasInfo, instanceBuffer, 0, tlasScratch);
// Later, after the original build and required synchronization:
commandBuffer.UpdateAccelerationStructureKHR(
    tlas, tlas, tlasInfo, instanceBuffer, 0, tlasScratch);
```

`WriteBuildInfoBufferInline` validates and encodes the entire list before writing once.
It requires host-visible, host-coherent destination memory, but no build-input
usage: a staging Buffer is also valid. For device-local instance data, write a
staging Buffer, record the existing `CopyBuffer`, and supply the destination to
the TLAS build. Transfers and BLAS-to-TLAS dependencies require explicit
synchronization. A build never implicitly calls `WriteBuildInfoBufferInline`.

## Snapshots, updates and lifetime

Commands copy descriptions/Vulkan arrays when appended; the arrays may then be
changed or destroyed. Buffer contents are not copied. Keep input Buffers, scratch,
and source/destination AS resources alive and stable through GPU use. A TLAS also
continues to reference its active BLAS objects after its own build completes.

Use separate Buffer regions for overlapping in-flight tasks. Reusing scratch
between builds, overwriting instance data, recreating an AS, and resource access
across queues all remain the caller's responsibility. Record builds outside a
RenderPass and submit to an actual compute-capable queue family. Recording order
does not replace memory barriers.

The object interfaces require the creation layout/counts/flags to match.
Update supports identical source/destination (in-place) or compatible distinct AS
objects, and requires an earlier GPU build with `ALLOW_UPDATE`. Host creation
does not imply that build has executed. The caller preserves Vulkan's update
constraints on index contents, topology and active/inactive primitives.
Changing layout or counts requires creating a new AS owner and building it again.

The existing `BuildAccelerationStructuresKHR(span<const VkAccelStructBuildInfo>)`
batch interface and typed Copy overloads remain available. A single batch is for
independent builds; dependent BLAS/TLAS builds need separate calls and synchronization.
Host Build, automatic submission/waiting, implicit barriers and new compaction
query flows are outside this interface.

## CLAS and Cluster BLAS batches

`ClusterLevelAccelStructBatch` and `BottomLevelAccelStructBatch` use
`VK_NV_cluster_acceleration_structure` with implicit destinations. Device
initialization enables the extension and its feature when available; unsupported
devices keep the existing paths and reject CLAS creation or a CLAS-enabled Program.

Both Batch types keep their output storage and a packed `VkDeviceAddress` table in
owned Buffers. `Create()` requires an empty target, queries whole-batch capacity,
and allocates these Buffers synchronously. It performs no upload, submission, or
GPU build. Empty batches and batches exceeding the device's single Buffer or
allocation limit reject; callers split large workloads into multiple batches.
Failure leaves an empty target that can be retried. Release never waits for GPU work.

`ClusterLevelAccelStructBatchCreateInfo` describes capacity, not geometry or a
list of tasks. All five required budgets must be set to positive values:

```cpp
ClusterLevelAccelStructBatchCreateInfo info;
info.SetMaxClusterCount(2);
info.SetMaxClusterTriangleCount(100);
info.SetMaxClusterVertexCount(80);
info.SetMaxTotalTriangleCount(160);
info.SetMaxTotalVertexCount(130);
ClusterLevelAccelStructBatch batch;
batch.Create(info);
```

The two total budgets are independent upper bounds, not inferred products. In
this example two tasks with 80 triangles each fit, but two with 100 triangles
each exceed the total even though neither exceeds the per-cluster maximum.
Count vertices uniquely within each task and sum those counts across tasks;
vertices shared between clusters still contribute to each cluster's count.
Creation snapshots these budgets and formats; later changes to `info` do not
affect the Batch. `GetCapacity()` returns
the maximum CLAS count, not the latest GPU count.

CLAS batches have one vertex format and one index type for all tasks. Their
defaults are `VK_FORMAT_R32G32B32_SFLOAT` and `VK_INDEX_TYPE_UINT32`.
`CustomizeIndexType()` also accepts `VK_INDEX_TYPE_UINT8` and `VK_INDEX_TYPE_UINT16`.
`ClusterLevelAccelStructDirectBuildInfo` describes one direct task.
`SetVertexBuffer(address, uniqueVertexCount)` and `SetIndexBuffer(address, indexCount)`
provide its inputs; indexCount must be divisible by three and determines
`primitiveCount`. `CustomizeClusterID()` defaults to 0, and
`CustomizeVertexStride()` defaults to 0 for tightly packed vertices. The actual
index values must reference valid vertex elements; uniqueVertexCount does not
prove the extent of a raw device-address range. Encoders use each task's own
vertex format and index type without requiring a Batch. The caller sets those
formats to match the eventual build and guarantees device format support and all
batch budgets. `CustomizeIndexStride(bytes)` sets the distance
between consecutive indices. Zero selects packed indices. Nonzero direct and
template index strides must fit 16 bits and be a multiple of the index width.
Direct and template tasks use flags 0, opaque geometry and GeometryIndex 0.

### Building and instantiating templates

```cpp
ClusterLevelAccelStructTemplateInfo topology;
topology.SetIndexBuffer(indexAddress, 3);
topology.SetVertexCount(3);
topology.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
ClusterLevelAccelStructTemplateBatch templates;
device.CreateClusterLevelAccelStructTemplatesAndWait(std::span(&topology, 1), templates);

info.CustomizeTemplateUsage(true);
info.CustomizeIndexType(templates.GetIndexType());
info.CustomizeVertexFormat(templates.GetVertexFormat());
ClusterLevelAccelStructBatch instances;
instances.Create(info); // Uses the five capacity settings above.
ClusterLevelAccelStructInstantiateBuildInfo pose;
pose.SetTemplateReference(templates.GetReference(0));
pose.SetVertexBuffer(deformedVertexAddress);
pose.CustomizeVertexFormat(templates.GetVertexFormat());
pose.CustomizeClusterID(42);
ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(
    std::span(&pose, 1), taskBuffer);
```

The template factory accepts multiple index buffers, requires common vertex/index
formats and derives per-cluster maxima and batch totals from the actual counts.
Its default overload builds and waits on COMPUTE from any thread. The explicit
overload takes `DeviceContext&` and a compute-capable queue as its first arguments
and runs on the context's owning thread. Keep index buffers alive and ready for
that queue until return. Successful creation publishes template storage, the
packed address table and CPU copies of the addresses and topology counts.
`GetTriangleCount(i)`, `GetVertexCount(i)` and `GetBuildLimits()` expose the
metadata for budgeting subsequent instances. Repeat each template's contribution
for every pose that uses it.

The factory's private template storage uses exclusive sharing. On devices that
require queue-family ownership transfers, select the instantiation queue family
when building templates. The public reference Buffer exposes the address table,
and the factory keeps the native storage handle private. [Vulkan queue-family ownership](https://docs.vulkan.org/refpages/latest/refpages/source/VkSharingMode.html)

Templates contain indices with base Cluster ID zero. Every instance supplies
its own vertex positions. Its `CustomizeClusterID()` becomes the final ID via
the native ID offset. When supplying raw template addresses, the caller guarantees
that the template is built, has base ID zero, matches the batch formats and fits
both per-cluster and aggregate budgets. Keep the template and vertex inputs
alive through instantiation. In the example above the upload contains one pose,
so supply GPU count 1 or initialize all remaining capacity before a full build.

### Cluster BLAS and indirect commands

`BottomLevelAccelStructBatchCreateInfo` also describes explicit capacity. Its
three required budgets must be positive:

```cpp
BottomLevelAccelStructBatchCreateInfo info;
info.SetMaxAccelStructCount(2);
info.SetMaxClusterCountPerAccelStruct(2);
info.SetMaxTotalClusterCount(3);
BottomLevelAccelStructBatch batch;
batch.Create(info);
```

The total cluster budget is independent of the product of the other two limits.
Creation snapshots all three values and needs no source CLAS Batch.
`GetCapacity()` returns the maximum Cluster
BLAS count. Each `BottomLevelAccelStructBuildInfo` describes an actual task through
`SetClusterReferences(referenceAddress, count)`. This sole input setter borrows a
packed array of `VkDeviceAddress` values and requires a nonzero, aligned address,
a positive count and a non-overflowing address range. Native reference stride is
always `sizeof(VkDeviceAddress)`. A rejected setter preserves the previous values.
Callers compute any table offset in bytes before passing the address. The table
may be caller-owned or GPU-generated; BuildInfo retains no source owner and does
not inspect table contents or allocation capacity. Callers provide valid built
CLAS references, keep the table alive through the BLAS build and keep referenced
CLAS resources alive through dependent GPU uses.

For either Batch, prepare and execute in this order:

1. Call `Create(info)` and read `GetSizesInfo()` and `GetScratchAlignment()`.
2. Allocate a task Buffer of `buildInfoBufferSize` bytes. It needs
   `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | SHADER_DEVICE_ADDRESS_BIT`
   for the build. A staging Buffer can instead be written and copied explicitly.
3. Use `ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(infos, taskBuffer)`
   or the corresponding instantiate uploader. For Cluster BLAS call
   `BottomLevelAccelStructBuildInfo::CommitToDeviceInline(infos, taskBuffer)`.
   All three encoders require no target Batch and validate the entire task span
   before one upload. They check native field representation, addresses, strides
   and the upload range; the caller ensures matching formats/templates and
   compliance with the eventual build's task, per-task and aggregate budgets.
   Instantiate encoding resolves a zero vertex stride from each task's format
   and queries the active physical device for template address alignment.
   Cluster BLAS uploads use coherent Host memory. Direct and instantiate encoders
   also support a staged upload to task buffers with `TRANSFER_DST_BIT`.
4. Provide scratch of `buildScratchBufferSize` bytes with
   `STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT` and the reported alignment.
   A zero scratch requirement uses address 0.
5. Append the matching direct, instantiate or Cluster BLAS command. For example:

```cpp
ClusterLevelAccelStructInstantiateBuildIndirectInfo build;
build.SetTarget(instances);
build.SetBuildInfoAddress(taskBuffer.GetDeviceAddress());
build.SetScratchAddress(scratchAddress);
build.CustomizeBuildCountAddress(buildCountAddress);
commands.BuildAccelerationStructureIndirect(build);
```

`ClusterLevelAccelStructDirectBuildIndirectInfo` and `BottomLevelAccelStructBuildIndirectInfo`
use the same setters with their matching targets. `SetTarget` copies limits,
capacity, required sizes and output addresses. The recorded command borrows the
native resources through completion. The optional count address defaults to zero.

The native `maxAccelerationStructureCount` always comes from
`GetCapacity()`. If `buildCountAddress` is 0, the build processes the full
capacity, so every task in that range must be initialized and valid. Otherwise
the GPU count selects the actual number, which must not exceed capacity. The
producer must bound the count before submitting it; the RHI does not read back
or clamp the value, and callers must not rely on the driver to handle an
over-limit count. The count address refers to a four-byte aligned
`uint32_t` in a Buffer with
`ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | SHADER_DEVICE_ADDRESS_BIT`.
The Host snapshots this address without reading its contents. Synchronize count
and task producers with `ACCELERATION_STRUCTURE_BUILD_BIT_KHR` and
`INDIRECT_COMMAND_READ_BIT` before the build consumes them.

A zero value in a supplied GPU count Buffer builds no structures, while still
requiring a normal native command and compute-capable queue. Partial builds
use a count Buffer, which may also be written by the CPU with the required
visibility and synchronization. Uploading fewer BuildInfo records does not
change the count: supply a compatible count Buffer when only a prefix is valid.
Output, scratch, task and address-table regions retain their creation-sized
capacity. `buildInfoBufferSize` remains the maximum task storage required by it.

The command snapshots the Batch's queried Host limits and the supplied addresses.
It never copies task contents or retains borrowed inputs. GPU-generated tasks
must remain within those frozen limits, including count, format and aggregate
geometry counts. With raw device addresses, callers guarantee underlying Buffer
usage, capacity, index contents and lifetime. Known scratch/output overlap and
invalid addresses/alignment reject before appending.

`GetReferenceBuffer()` becomes non-null after Host creation, but only the actual
GPU-built prefix is valid after the build and its required synchronization.
Inactive entries may contain uninitialized or stale addresses; the Batch does not
clear them. Both address tables have `TRANSFER_DST_BIT` for explicit caller
initialization when needed; initialization and synchronization remain explicit.
Do not assume individual implicit object addresses stay unchanged across builds.
Synchronize CLAS output writes before Cluster BLAS reads and scratch reuse, then
Cluster BLAS address writes before an engine shader patches standard TLAS instances.
TLAS Build already accepts the externally prepared instance Buffer. No RHI instance
patch helper or implicit barrier is introduced.

Builds require an actual compute-capable queue and must be outside a RenderPass.
If a ray tracing pipeline is already bound before a cluster build, it must enable
CLAS; recording builds before binding any ray tracing pipeline is also valid.
For tracing, call
`RayTracingShaderProgramCreateInfo::CustomizeAllowClusterAccelerationStructure(true)`
before `Finalize()`. The option defaults to false, participates in the immutable
snapshot and pipeline cache key, and only attaches the native cluster `pNext` when
enabled.

The opt-in native smoke test uses the normal regression executable without a
window. It builds direct CLAS and implicit templates, instantiates one template
at two poses and builds Cluster BLAS from each path. It checks the output addresses
and GPU count values zero, one and full capacity, plus a producer request
restricted to capacity before submission. Fixed sentinel entries verify that
inactive address-table tails are not overwritten:

```powershell
$env:LEARN_VULKAN_CLAS_GPU_TESTS = '1'
ctest --test-dir build -C Debug --output-on-failure -V -R '^learn_vulkan_tests$'
Remove-Item Env:LEARN_VULKAN_CLAS_GPU_TESTS
```
