# Acceleration structures

## Type and file layout

Public headers live in `src/my_vulkan/resource/`:

| Header | Types and purpose |
| --- | --- |
| `acceleration_structure_types.h` | `VkAccelStructBuildInfo` for the public low-level KHR batch command and `AccelStructSizesInfo` for caller-owned batch task/scratch allocation. |
| `acceleration_structure_gpu_layout.h` | GPU record strides, address alignments, field offsets and bit packing for CLAS tasks and packed TLAS instances. |
| `acceleration_structure.h` | Ordinary BLAS/TLAS owners, geometry and instance descriptions, and their CreateInfo types. |
| `cluster_acceleration_structure.h` | CLAS templates, CLAS/Cluster BLAS batch owners and CreateInfo types, CPU task descriptions, and public indirect-build arguments. |
| `partitioned_acceleration_structure.h` | PTLAS owner, CreateInfo, operation records and command input declarations; implementation is pending. |

Include the ordinary, cluster or partitioned header explicitly for the path being used.
`ClusterBottomLevelAccelStructBatch` and its associated CreateInfo/BuildInfo types
specifically build BLAS from CLAS references. `ClusterLevelAccelStructBuildLimits`
and `ClusterBottomLevelAccelStructBuildLimits` describe build budgets and formats;
`AccelStructBuildInfoLayout` describes GPU record byte layout.

The PTLAS declarations distinguish a `PartitionedTopLevelAccelStructOperationGroup`
from its inner `WriteInstanceRecord`, `UpdateInstanceRecord` and
`WritePartitionTranslationRecord` arrays. `SetMaxInstanceCount` configures slot
capacity; `CustomizeMaxOperationGroupCount` configures the outer group-array capacity.

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

Packed input records and counts may be produced entirely by application shaders.
`CommitToDeviceInline` is an optional convenience for CPU initialization and tests;
build commands do not depend on it or read back GPU-produced inputs. Producers
follow the published GPU ABI and capacity limits, then synchronize their writes
with the build's input reads. Host descriptions and GPU record contents have
independent lifetimes.

The host path prepares descriptions and resources. GPU work is recorded through
`CommandBuffer`; recording never allocates an input/scratch Buffer or uploads
instances. All setters and recording methods return `void`.

## Ordinary AS: creation capacities

BLAS `SetTriangles` and `SetAabbs` copy an ordered, nonempty list of
`AccelStructTriangleGeometryLimits` or `AccelStructAabbGeometryLimits`.
Triangle limits contain positive maximum triangle/vertex counts, vertex format,
index type, transform reservation and geometry flags. AABB limits contain a
positive maximum count and geometry flags. Build flags belong to CreateInfo.
Addresses, actual counts and strides belong to each build, because strides do
not constrain the size-query capacity guarantee.

`Create()` synchronously queries sizes and creates an empty native AS plus its
backing Buffer; it does not execute a GPU build. Repeated creation fails and
preserves the existing object. There is no public `Destroy()`: RAII releases
the native AS before the backing allocation, without a GPU wait. Independent
instances may create/release concurrently; callers exclude conflicting access
to one instance and keep Device/allocator/dispatch alive through final release.
See [BLAS ownership](resource_lifetimes.md#blas-ownership) and
[TLAS ownership](resource_lifetimes.md#tlas-ownership).

```cpp
AccelStructTriangleGeometryLimits triangleLimits;
triangleLimits.SetMaxTriangleCount(2);
triangleLimits.SetMaxVertexCount(6);
// Defaults: float3, nonindexed, opaque, no transform reservation.

BottomLevelAccelStructCreateInfo blasInfo;
blasInfo.SetTriangles(std::span(&triangleLimits, 1));
blasInfo.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
BottomLevelAccelStruct blas;
blas.Create(blasInfo);

const auto sizes = blas.GetSizesInfo();
const auto scratchBytes =
    std::max(sizes.buildScratchBufferSize, sizes.updateScratchBufferSize);
// Allocate scratchBytes with STORAGE_BUFFER | SHADER_DEVICE_ADDRESS usage,
// aligning its device address to sizes.scratchAlignment.
```

`GetSizesInfo()` is shared by ordinary AS and cluster interfaces. For ordinary
BLAS, `buildInfoBufferSize` is zero; TLAS reports its capacity times the packed
instance stride. Scratch requirements cover the creation maxima, so they remain
safe for smaller actual builds. Zero required scratch bytes accept address zero.
Input buffers require `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
SHADER_DEVICE_ADDRESS_BIT`; scratch needs `STORAGE_BUFFER_BIT |
SHADER_DEVICE_ADDRESS_BIT`.

## Combined target/data selection and batch builds

`BuildAccelStructCommandInput` represents one task. `SetTrianglesTarget`,
`SetAabbsTarget` and `SetInstancesTarget` validate the target, input and
capacity together, then copy Host metadata. Successful reselection replaces the
whole task, clears the update source, restores BUILD and preserves scratch.
Failure preserves the previous task. Assigning `input = {};` clears both task and scratch.
Copies/moves own only Host values and retain no GPU resources.

```cpp
AccelStructTriangleBuildInfo triangle;
triangle.SetVertexBuffer(vertexBuffer.GetDeviceAddress(), 6);
triangle.SetTriangleCount(2);
// Optional: CustomizeVertexStride; indexed targets require SetIndexBuffer.
// Transform addresses require creation-time CustomizeAllowTransform(true).

AccelStructAabbBuildInfo aabb;
aabb.SetAabbBuffer(aabbBuffer.GetDeviceAddress(), 3);
// Default stride: sizeof(VkAabbPositionsKHR).

std::array<BuildAccelStructCommandInput, 2> builds;
builds[0].SetTrianglesTarget(blas, std::span(&triangle, 1));
builds[0].SetScratchAddress(triangleScratchAddress);
builds[1].SetAabbsTarget(aabbBlas, std::span(&aabb, 1));
builds[1].SetScratchAddress(aabbScratchAddress);
commands.BuildAccelerationStructuresKHR(builds);
```

The geometry list must be a nonempty prefix of the creation Limits, matching
each entry by index. Each actual count must fit that entry. Vertex format,
index type and geometry flags come from the target. Reserved transform capacity
may be used or omitted; an unreserved transform is rejected. Vertex count
describes the addressable vertex range, rather than a count of unique indices.
AABB stride defaults to 24 bytes; zero triangle stride selects packed vertices.

One call appends one native KHR batch. All tasks are validated before appending;
any failure appends nothing. Appending snapshots all arrays; later edits,
destruction or moves of the inputs do not affect the command. Recording neither
queries sizes, uploads data nor allocates arrays. The low-level
`BuildAccelerationStructuresKHR(span<const VkAccelStructBuildInfo>)` overload
and typed `CopyAccelerationStructureKHR` overloads remain available. Ordinary
KHR inputs reject Cluster BLAS owners, whose build mode remains fixed.

## TLAS: required capacity and explicit upload

TLAS CreateInfo requires `SetMaxInstanceCount(n)` with `n > 0`; a default or
cleared (`info = {};`) CreateInfo cannot create a TLAS. Actual BUILD count may be zero.
`TopLevelAccelStructBuildInfo` stores a raw BLAS address; zero encodes an
inactive instance. Its static layout and upload functions require no AS owner.

```cpp
TopLevelAccelStructCreateInfo tlasInfo;
tlasInfo.SetMaxInstanceCount(128);
TopLevelAccelStruct tlas;
tlas.Create(tlasInfo);

TopLevelAccelStructBuildInfo instance;
instance.accelStructReference = blas.GetDeviceAddress();
// Defaults: identity transform, mask 0xFF, zero custom index/SBT offset/flags.
// Allocate instanceBuffer using tlas.GetSizesInfo().buildInfoBufferSize,
// input usage, and TopLevelAccelStructBuildInfo::GetBuildInfoLayout() alignment.
TopLevelAccelStructBuildInfo::CommitToDeviceInline(
    std::span(&instance, 1), instanceBuffer);

BuildAccelStructCommandInput input;
input.SetInstancesTarget(tlas, instanceBuffer.GetDeviceAddress(), 1);
input.SetScratchAddress(tlasScratchAddress);
commands.BuildAccelerationStructuresKHR(std::span(&input, 1));
// An empty BUILD is selected with input.SetInstancesTarget(tlas, 0, 0).
```

`CommitToDeviceInline` validates and encodes the complete span before one
synchronous Buffer upload. Empty spans do not access Device or Buffer.
Coherent Host memory uses mapped copies; other destinations follow Buffer's
staged-transfer contract and require transfer-destination usage. Build commands
never upload instances implicitly. GPU-produced packed instances can be passed
directly by address.

## Updates, batch independence and lifetime

Configure an update source after selecting a target:

```cpp
input.SetTrianglesTarget(blas, std::span(&triangle, 1));
input.CustomizeSourceAccelStruct(blas); // In-place UPDATE is allowed.
input.SetScratchAddress(updateScratchAddress);
commands.BuildAccelerationStructuresKHR(std::span(&input, 1));
```

The source and target must have compatible kinds, flags and creation capacities
for the actual task, and must permit UPDATE. Host checks cannot establish GPU
history: the source must have completed an earlier build with ALLOW_UPDATE.
Callers preserve Vulkan's UPDATE constraints, including actual geometry and
primitive counts, format, maxVertex, index topology/content, transform presence
and active/inactive state. BUILD can change actual counts within capacity;
changes forbidden by UPDATE require BUILD again.

Tasks in a batch must be independent. Duplicate destinations, cross-task
source/destination handles and known destination/source/scratch storage overlaps
are rejected; scratch regions must be disjoint. Shared read-only sources are
allowed, and one task may update its own source in place. Dependencies hidden
in GPU instance/index data remain the caller's responsibility. Build a BLAS and
its dependent TLAS in separate calls with explicit synchronization.

Keep AS owners, input buffers and scratch alive through command recording and
GPU completion. A built TLAS continues to borrow its active BLAS addresses.
Snapshots copy Host arrays, not Buffer contents or resource ownership. Callers
exclude conflicting access to each input/CommandBuffer and synchronize resource
reuse, queue access and transfers. Record outside RenderPass on a compute-capable
queue; recording order does not substitute for barriers.

## CLAS and Cluster BLAS batches

`ClusterLevelAccelStructBatch` and `ClusterBottomLevelAccelStructBatch` use
`VK_NV_cluster_acceleration_structure` with implicit or explicit destinations. Device
initialization enables the extension and its feature when available; unsupported
devices keep the existing paths and reject CLAS creation or a CLAS-enabled Program.

Implicit builds keep their output storage and a packed `VkDeviceAddress` table in
owned Buffers. `Create()` requires an empty target, queries whole-batch capacity,
and allocates these Buffers synchronously. It performs no upload, submission, or
GPU build. Empty batches and batches exceeding the device's single Buffer or
allocation limit reject; callers split large workloads into multiple batches.
Failure leaves an empty target that can be retried. Release never waits for GPU work.

Both BuildLimits types describe positive per-object and aggregate budgets and the CLAS
vertex format. Select build limits and a maximum target count on a CommandInput to
validate them and synchronously query the Device. Its const
`QueryCurrentTargetSizesInfo()` returns the saved requirements without GPU bindings.
Task bytes, build scratch bytes and scratch alignment cover the full batch.
For explicit builds the AS byte count bounds one object at the configured maxima.
For implicit builds it bounds the contiguous batch. Align each destination base
to the reported AS alignment. Round the stride of uniform explicit slots up to
that alignment.

`ComputeClusterAccelStructSizesCommandInput` supports direct CLAS, Template,
instantiate and Cluster BLAS tasks. Its Host AS size, AS alignment and update
scratch are zero. Bind the operation's argument records, any required scratch
and a packed `uint32_t` size output with `SetOutputSizesArray`, then append
`commands.ComputeClusterAccelStructSizes(input)`. The GPU writes one byte
size per active task. To use those values for explicit builds, synchronize the
size outputs with the allocator or readback, allocate each destination at the
matching build alignment, and populate a packed destination-address table.

`ClusterLevelAccelStructImplicitBatchCreateInfo` describes capacity, not geometry or a
list of tasks. All five required budgets must be set to positive values:

```cpp
ClusterLevelAccelStructImplicitBatchCreateInfo info;
info.SetMaxClusterCount(2);
info.SetMaxTriangleCountPerCluster(100);
info.SetMaxVertexCountPerCluster(80);
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

### Fixed destinations

Create each fixed CLAS using `ClusterLevelAccelStructCreateInfo`. Set its maximum
triangle and vertex counts, then choose a common index type, vertex format and
template usage. `ClusterLevelAccelStruct::GetDeviceAddress()` returns its aligned
device address after creation. `IsValid()` reports allocated storage, not GPU build
completion. `GetSizesInfo()` reads the saved creation requirements without queries,
uploads or GPU work. Failed creation leaves these values empty; repeated Create
preserves the existing owner. For a fixed Cluster BLAS, use
`BottomLevelAccelStructCreateInfo::SetClusters(maxClusterCount)` and
`BottomLevelAccelStruct::GetDeviceAddress()`. Cluster mode uses build flags zero.
Its owner participates in TLAS address encoding and the NV indirect build path.
KHR geometry builds and updates require triangle/AABB owners.

Create owners and the packed address-table Buffer through `MakeIntrusive`.
Supply their addresses in a Buffer with `STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT`.
An address table subsequently used as Cluster BLAS build input also needs
`ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`. Include transfer usage
if uploading by transfer or reading back. Table offset and effective device address
must be eight-byte aligned and the Buffer must contain all entries after that offset.

```cpp
ClusterLevelAccelStructExplicitBatchCreateInfo fixed;
fixed.SetReferenceBuffer(*table, tableOffset);
fixed.SetDestinationAccelStructs(owners); // Contiguous list of const CLAS pointers.
ClusterLevelAccelStructBatch batch;
batch.Create(fixed);
const auto tableAddress = batch.GetReferenceBuffer()->GetDeviceAddress() + batch.GetReferenceBufferOffset();
```

`ClusterBottomLevelAccelStructExplicitBatchCreateInfo` accepts the same two setters
with Cluster BLAS owners. The caller fills the table so entry `i` points to owner
`i`. Each task must fit that owner's individual capacities. The native batch
query uses the largest per-owner capacities and the sums of individual budgets,
so a smaller owner can have less storage than the maximum in `GetSizesInfo()`.
Batch creation copies no GPU data. It retains each distinct destination owner and the supplied
Buffer. Keep its allocation stable and retain the Batch through dependent GPU uses.

### Building and instantiating templates

```cpp
ClusterLevelAccelStructTemplateBuildInfo topology;
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

`ClusterBottomLevelAccelStructImplicitBatchCreateInfo` describes an implicit Cluster BLAS batch. Its
three required budgets must be positive:

```cpp
ClusterBottomLevelAccelStructImplicitBatchCreateInfo info;
info.SetMaxAccelStructCount(2);
info.SetMaxClusterCountPerAccelStruct(2);
info.SetMaxTotalClusterCount(3);
ClusterBottomLevelAccelStructBatch batch;
batch.Create(info);
```

The total cluster budget is independent of the product of the other two limits.
Creation snapshots all three values and needs no source CLAS Batch.
`GetCapacity()` returns the maximum Cluster
BLAS count. Each `ClusterBottomLevelAccelStructBuildInfo` describes an actual task through
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
   `ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(infos, taskBuffer)`.
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
BuildImplicitClusterAccelStructCommandInput build;
build.SetInstantiateClusterTarget(instances);
build.SetArgumentAddress(taskBuffer.GetDeviceAddress());
build.SetScratchAddress(scratchAddress);
build.CustomizeBuildCountAddress(buildCountAddress);
commands.BuildAccelerationStructureIndirect(build);
```

Choose `BuildExplicitClusterAccelStructCommandInput` or
`BuildImplicitClusterAccelStructCommandInput`, then select `SetClusterTarget`,
`SetInstantiateClusterTarget` or `SetBottomLevelAccelStructTarget`. The Batch
overload checks destination mode and operation, copies limits and sizes, and
binds the Batch table (including its byte offset), storage and known destination
ranges. A setter for a managed address accepts only that same address.

For caller-managed storage, pass build limits and capacity to the target setter.
Use `SetReferencesArray` for the packed explicit destination addresses, or
`SetStorageAddress` and `SetOutputReferencesArray` for implicit output.
Implicit builds optionally write packed per-object byte sizes through
`CustomizeOutputSizesArray`. Explicit builds leave the native sizes array absent.
Every successful target change clears previous target addresses and size output,
then installs any new Batch bindings. Argument, scratch and GPU count addresses
survive reselection. Rejected selection preserves the previous input. Moving an
input transfers the binding and known destination ranges. Select another target
before querying or recording the moved-from input.

The native `maxAccelerationStructureCount` comes from the selected maximum
count or Batch capacity. If `buildCountAddress` is 0, the command processes the full
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
clear them. Both implicit address tables have `TRANSFER_DST_BIT` for explicit caller
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
at two poses and builds Cluster BLAS from each path. It also submits direct and
instantiate builds into fixed CLAS owners and consumes them in explicit Cluster
BLAS builds using two slices of one caller-populated table. It checks the addresses
and GPU count values zero, one and full capacity, plus a producer request
restricted to capacity before submission. Sentinels check inactive address and
size tails. The smoke also computes direct, Template, instantiate and Cluster BLAS
sizes at counts zero, one and full capacity, builds into individually aligned
GPU-sized allocations and consumes the resulting CLAS in a dependent BLAS build.

```powershell
$env:LEARN_VULKAN_CLAS_GPU_TESTS = '1'
ctest --test-dir build -C Debug --output-on-failure -V -R '^learn_vulkan_tests$'
Remove-Item Env:LEARN_VULKAN_CLAS_GPU_TESTS
```
