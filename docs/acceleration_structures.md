# Acceleration structures

## Type and file layout

Public headers live in `src/my_vulkan/resource/`:

| Header | Types and purpose |
| --- | --- |
| `acceleration_structure_types.h` | `VkAccelStructBuildInfo` for the public low-level KHR batch command and `AccelStructMemoryRequirements` for AS storage and scratch allocation. |
| `acceleration_structure_gpu_layout.h` | GPU record strides, address alignments, field offsets and bit packing for CLAS, TLAS and PTLAS inputs. |
| `acceleration_structure.h` | Ordinary BLAS/TLAS owners, geometry and instance descriptions, and their CreateInfo types. |
| `cluster_acceleration_structure.h` | Fixed CLAS owner and CreateInfo, cluster build limits, CPU task descriptions and indirect command inputs. |
| `partitioned_acceleration_structure.h` | PTLAS owner, CreateInfo, operation records, optional CPU encoders and GPU command input. |

Include the ordinary, cluster or partitioned header explicitly for the path being used.
`ClusterBottomLevelAccelStructBuildInfo` describes one BLAS build from a packed
table of CLAS references. `ClusterLevelAccelStructBuildLimits`
and `ClusterBottomLevelAccelStructBuildLimits` describe build budgets and formats;
`AccelStructBuildInfoLayout` describes GPU record byte layout.

The PTLAS interface distinguishes a `PartitionedTopLevelAccelStructOperationGroup`
from its inner `WriteInstanceRecord`, `UpdateInstanceRecord` and
`WritePartitionTranslationRecord` arrays. `SetMaxInstanceCount` configures slot
capacity. The outer operation array supports at most three groups, one each for
WRITE_INSTANCE, UPDATE_INSTANCE and WRITE_PARTITION_TRANSLATION. It has no
configurable capacity in CreateInfo or CommandInput.
`AccelStructGpuLayout::Partitioned::OperationBufferSize` is a fixed 72-byte allocation
budget for three 24-byte native PTLAS operation records; it is independent of
the native AS/scratch size query. Callers allocate inner record arrays separately
using their record types' `GetBuildInfoLayout()`.
The GPU count specifies the actual number of outer groups. Its producer keeps
the count within three and the available array entries, and emits each operation
type at most once. Host code neither reads back nor clamps the count. Each group's
inner record count is independent of the number of outer groups.

PTLAS CreateInfo exposes `CustomizePartitionTranslation(bool)`, defaulting to
`false`. Enabling it requires extra storage for untranslated instance transforms,
so it belongs to creation configuration. The creation size query and subsequent
builds must both chain `VkPartitionedAccelerationStructureFlagsNV` through
`VkPartitionedAccelerationStructureInstancesInputNV::pNext` with the same saved
`enablePartitionTranslation` value. Native chain pointers must be reconstructed
from owned values rather than retained from the CreateInfo. Source compatibility
also includes this setting.
`WritePartitionTranslationRecord` and `SetWritePartitionTranslations()` require
an eventual target created with translation enabled. Encoding/uploading records
does not enable translation or apply it; the explicit GPU build consumes them.
PTLAS creation saves this setting and commands rebuild the native chain from the saved values.

`resource/private/` contains the implementations, native AS storage owner, and
`cluster_acceleration_structure_build.h` with the internal `ClusterBuildTarget`
snapshot shared with command recording. The cluster header includes it for
CommandInputs to store snapshots by value.
`partitioned_acceleration_structure_build.h` similarly holds the pointer-free
PTLAS configuration and borrowed build snapshot.
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

const auto sizes = blas.GetMemoryRequirements();
const auto scratchBytes =
    std::max(sizes.buildScratchBufferSize, sizes.updateScratchBufferSize.value_or(0));
// Allocate scratchBytes with STORAGE_BUFFER | SHADER_DEVICE_ADDRESS usage,
// aligning its device address to sizes.scratchAlignment.
```

`GetMemoryRequirements()` is shared by ordinary AS, cluster and PTLAS owners. It
reports only AS storage size/alignment, build scratch size, optional KHR UPDATE
scratch size and the shared scratch alignment. UPDATE scratch is present only
for ordinary BLAS/TLAS created with `ALLOW_UPDATE`; a present zero still permits
UPDATE without a scratch allocation. It is absent for CLAS and PTLAS. These are
saved Host requirements, not GPU build history or readiness.
Input capacity and record layout are separate: TLAS instance bytes are
`VkDeviceSize{tlas.GetCapacity()} * TopLevelAccelStructBuildInfo::GetBuildInfoLayout().recordStride`.
Scratch requirements cover the creation maxima, so they remain
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
// Allocate instanceBuffer using VkDeviceSize{tlas.GetCapacity()} *
// TopLevelAccelStructBuildInfo::GetBuildInfoLayout().recordStride,
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

A single indirect command can size or build multiple targets using
`VK_NV_cluster_acceleration_structure`. Device initialization enables the
extension and feature when available. The caller chooses the operation, its
limits and a positive maximum target count, then owns or suballocates the GPU
memory. Use AS storage and device-address usage for object storage. An implicit
build takes one contiguous storage range and a separate packed output-address
table. An explicit build reads the caller's table of destination addresses.

Both BuildLimits types describe positive per-object and aggregate budgets. CLAS
limits also specify the vertex format. Query allocation requirements before binding a CommandInput.
`ImplicitClusterAccelStructCommandInput` and
`ExplicitClusterAccelStructCommandInput` expose static
`GetClusterMemoryRequirements(limits, maxCount)`,
`GetInstantiateClusterMemoryRequirements(limits, maxCount)` and
`GetBottomLevelAccelStructMemoryRequirements(limits, maxCount)` queries.
The implicit input also exposes `GetClusterTemplateMemoryRequirements(limits, maxCount)`.
These calls validate positive capacities and synchronously query the Device.
Owner `GetMemoryRequirements()` queries continue to return saved Host requirements.
For explicit builds the AS byte count bounds one object at the configured maxima;
for implicit builds it bounds the contiguous batch. Scratch requirements cover
the full batch. The caller supplies sufficient storage and scratch, aligns their
device addresses to the queried requirements, and keeps allocation ranges disjoint
where required. Round uniform explicit-slot strides up to the AS alignment.

Select the task record's `GetBuildInfoLayout()` and use its
`GetBufferSize(maxCount)` for full-capacity task bytes and `addressAlignment` for
the input address alignment. This pure Host calculation requires a positive count
and returns `VkDeviceSize{maxCount} * recordStride`, without accessing an owner or
Device. It ignores the optional GPU actual count and is shared by all command modes.

`ComputeClusterAccelStructSizesCommandInput` supports direct CLAS, Template,
instantiate, Cluster BLAS, MOVE and template-index queries. For build tasks, its static
`GetClusterBuildScratchRequirements(limits, maxCount)`,
`GetClusterTemplateBuildScratchRequirements(limits, maxCount)`,
`GetInstantiateClusterBuildScratchRequirements(limits, maxCount)` and
`GetBottomLevelAccelStructBuildScratchRequirements(limits, maxCount)` each return
`ClusterScratchRequirements`. Allocate `size` bytes at the returned `alignment`.
When `size` is zero, pass zero to `SetScratchAddress()`.
Computing sizes has no AS storage allocation or KHR UPDATE requirement. Select the
operation with limits, maximum count and a packed `uint32_t` output-size address,
bind its argument records and scratch, then append
`commands.ComputeClusterAccelStructSizes(input)`. The GPU writes one size in bytes
per active task. To use those values for explicit builds, synchronize the
size outputs with the allocator or readback, allocate each destination at the
matching build alignment, and populate a packed destination-address table.

For a command that builds up to two direct CLAS, provide positive per-cluster
and aggregate geometry budgets and query the matching mode.

```cpp
constexpr uint32_t maxCount = 2;
ClusterLevelAccelStructBuildLimits limits;
limits.SetMaxTriangleCount(100, 160);
limits.SetMaxVertexCount(80, 130);
const auto memory = ImplicitClusterAccelStructCommandInput::GetClusterMemoryRequirements(
    limits, maxCount);
```

The two total budgets are independent upper bounds, not inferred products. In
this example two tasks with 80 triangles each fit, but two with 100 triangles
each exceed the total even though neither exceeds the per-cluster maximum.
Count vertices uniquely within each task and sum those counts across tasks;
vertices shared between clusters still contribute to each cluster's count.
The target setter snapshots the budgets, vertex format and maximum count.
The default vertex format is `VK_FORMAT_R32G32B32_SFLOAT`. The direct task's
`CustomizeIndexType()` defaults to `VK_INDEX_TYPE_UINT32` and also accepts
`VK_INDEX_TYPE_UINT8` and `VK_INDEX_TYPE_UINT16`.
`ClusterLevelAccelStructDirectBuildInfo` requires two bindings.
`SetIndexBuffer(address, indexCount)` takes the number of indices. Six indices
encode two triangles. A count that is not divisible by three throws and preserves
the previous index address and triangle count.
`SetVertexBuffer(address, uniqueVertexCount)` takes the number of distinct vertices
referenced by those indices. The caller must provide vertex storage covering every
referenced index. `CustomizeClusterID()` defaults to 0, and
`CustomizeVertexStride()` defaults to 0 for tightly packed vertices.
The direct encoder uses the task's vertex format and index type. The caller sets the formats for the
eventual build and guarantees device format support and all
batch budgets. `CustomizeIndexStride(bytes)` sets the distance
between consecutive indices. Zero selects packed indices. Nonzero direct and
template index strides must fit 16 bits and be a multiple of the index width.
Direct and template tasks use flags 0, opaque geometry and GeometryIndex 0.

### Fixed destinations

Create each fixed CLAS using `ClusterLevelAccelStructCreateInfo`. Set its maximum
triangle and vertex counts and choose the vertex format and template usage
for the tasks targeting it. Each direct task chooses its index type.
`ClusterLevelAccelStruct::GetDeviceAddress()` returns its aligned
device address after creation. `IsValid()` reports allocated storage, not GPU build
completion. `GetMemoryRequirements()` reads the saved creation requirements without queries,
uploads or GPU work. Failed creation leaves these values empty; repeated Create
preserves the existing owner. For a fixed Cluster BLAS, use
`BottomLevelAccelStructCreateInfo::SetClusters(maxClusterCount)` and
`BottomLevelAccelStruct::GetDeviceAddress()`. Cluster mode uses build flags zero.
Its owner participates in TLAS address encoding and the NV indirect build path.
KHR geometry builds and updates require triangle/AABB owners.

Supply fixed-owner addresses or aligned suballocation addresses in a packed Buffer
with `STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT`.
An address table subsequently used as Cluster BLAS build input also needs
`ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`. Include transfer usage
if uploading by transfer or reading back. Table offset and effective device address
must be eight-byte aligned and the Buffer must contain all entries after that offset.

```cpp
const std::array destinationAddresses{firstClas.GetDeviceAddress(), secondClas.GetDeviceAddress()};
Buffer::UploadToDeviceInline(table, tableOffset, destinationAddresses.data(), sizeof(destinationAddresses));
ExplicitClusterAccelStructCommandInput build;
build.SetClusterTarget(limits, maxCount, table.GetDeviceAddress() + tableOffset);
build.SetArgumentAddress(taskAddress);
build.SetScratchAddress(scratchAddress);
commands.ExecuteClusterAccelStructIndirect(build);
```

Here `maxCount` is two. Synchronize the table upload before the GPU reads it.
Each task must fit both the command limits and its individual destination. Retain
the table through the last build that reads it and each destination through its
last dependent GPU use. A table can also be produced entirely on the GPU.

### Building and instantiating templates

```cpp
constexpr uint32_t maxTemplates = 1;
ClusterLevelAccelStructBuildLimits templateLimits;
templateLimits.SetMaxTriangleCount(1, 1);
templateLimits.SetMaxVertexCount(3, 3);
const auto memory = ImplicitClusterAccelStructCommandInput::GetClusterTemplateMemoryRequirements(
    templateLimits, maxTemplates);

BufferCreateInfo storageInfo;
storageInfo.SetBufferSize(memory.accelerationStructureSize);
storageInfo.SetBufferUsage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
storageInfo.CustomizeAlignment(memory.accelerationStructureAlignment);
Buffer templateStorage;
templateStorage.Create(storageInfo);

BufferCreateInfo tableInfo;
tableInfo.SetBufferSize(sizeof(VkDeviceAddress) * maxTemplates);
tableInfo.SetBufferUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
tableInfo.CustomizeAlignment(8);
Buffer templateAddresses;
templateAddresses.Create(tableInfo);

// Optional CPU initialization; a shader may produce these packed records instead.
ClusterLevelAccelStructTemplateBuildInfo topology;
topology.SetIndexBuffer(indexAddress, 3);
topology.SetVertexCount(3);
topology.CustomizeIndexType(VK_INDEX_TYPE_UINT16);
ClusterLevelAccelStructTemplateBuildInfo::CommitToDeviceInline(
    std::span(&topology, 1), templateTaskBuffer);

ImplicitClusterAccelStructCommandInput build;
build.SetClusterTemplateTarget(templateLimits, maxTemplates, templateStorage.GetDeviceAddress(),
    templateAddresses.GetDeviceAddress());
build.SetArgumentAddress(templateTaskBuffer.GetDeviceAddress());
build.SetScratchAddress(templateScratchAddress);
commands.ExecuteClusterAccelStructIndirect(build);
// Explicitly synchronize template storage and the output address table before use.
// A shader can read templateAddresses and generate instantiation tasks.
```

Allocate scratch for `memory.buildScratchBufferSize` at `memory.scratchAlignment`,
using address zero when the requirement is zero. Index addresses, types and
strides belong to each template task. The GPU build publishes template addresses
in the active prefix of `templateAddresses`. Count each instance's template
geometry when budgeting the subsequent instantiation command.

The RHI builds templates using implicit destinations. Without a GPU count the
build processes the entire capacity; with `CustomizeOperationCountAddress()` the producer
bounds the count and consumers use only the active prefix. Target selection,
appending and recording perform no size, property or device-address queries.
Recording does not allocate, upload or submit independently.

Synchronize task/count writes from their producer before building. For a shader
that generates instantiation tasks, make template-build writes visible to its
shader reads, then make the generated task/count writes visible to the subsequent
indirect instantiation build. Also synchronize template storage writes with the
instantiation build's acceleration-structure reads. Submission and completion
remain explicit. With exclusive storage, keep dependent work in the owning
queue family or transfer ownership explicitly.
[Vulkan queue-family ownership](https://docs.vulkan.org/refpages/latest/refpages/source/VkSharingMode.html)

For CPU consumption, copy the address table to a readback Buffer, apply the
transfer/Host dependency and wait for completion before reading the local array.

```cpp
std::array<VkDeviceAddress, 1> addresses{};
Buffer::DownloadFromDeviceInline(readback, 0, addresses.data(), sizeof(addresses));
ClusterLevelAccelStructInstantiateBuildInfo pose;
pose.SetTemplateReference(addresses[0]);
pose.SetVertexBuffer(deformedVertexAddress);
pose.CustomizeVertexFormat(VK_FORMAT_R32G32B32_SFLOAT); // Matches templateLimits.
pose.CustomizeClusterID(42);
ClusterLevelAccelStructInstantiateBuildInfo::CommitToDeviceInline(
    std::span(&pose, 1), instanceTaskBuffer);
```

The CPU template helper encodes base Cluster ID zero. Every instance supplies
its own vertex positions. Its `CustomizeClusterID()` becomes the final ID via
the native ID offset. When supplying raw template addresses, the caller guarantees
that the template is built, has base ID zero, matches the batch formats and fits
both per-cluster and aggregate budgets. Keep index inputs through the template
build, vertex inputs through instantiation, and template storage through all
dependent GPU uses. The CPU helper above emits one pose, so use capacity 1 or
supply GPU count 1 when building a larger instantiation batch.

### Cluster BLAS and indirect commands

A single Cluster BLAS command can build two BLAS from up to three CLAS references
in total, with up to two references per BLAS.

```cpp
constexpr uint32_t maxBlasCount = 2;
ClusterBottomLevelAccelStructBuildLimits blasLimits;
blasLimits.SetMaxClusterCount(2, 3);
ImplicitClusterAccelStructCommandInput build;
build.SetBottomLevelAccelStructTarget(blasLimits, maxBlasCount, blasStorageAddress,
    blasOutputReferencesAddress);
```

The maximum target count and both cluster budgets must be positive. The total
budget is independent of the product of the other two limits. Each
`ClusterBottomLevelAccelStructBuildInfo` describes an actual task through
`SetClusterReferences(referenceAddress, count)`. This sole input setter borrows a
packed array of `VkDeviceAddress` values and requires a nonzero, aligned address,
a positive count and a non-overflowing address range. Native reference stride is
always `sizeof(VkDeviceAddress)`. A rejected setter preserves the previous values.
Callers compute any table offset in bytes before passing the address. The table
may be caller-owned or GPU-generated; BuildInfo retains no source owner and does
not inspect table contents or allocation capacity. Callers provide valid built
CLAS references, keep the table alive through the BLAS build and keep referenced
CLAS storage alive through the last GPU use of every BLAS that references it,
including traversal via a TLAS.

For CLAS, template and Cluster BLAS batches, prepare and execute in this order:

1. Query the operation's requirements for the chosen mode, limits and maximum
   count. For a build, allocate aligned object storage and an address table.
   Populate the destination addresses for explicit builds. For Compute Sizes,
   allocate a packed size-output table.
2. Select the record type's `layout = GetBuildInfoLayout()` and allocate a task
   Buffer using `layout.GetBufferSize(maxCount)` and `layout.addressAlignment`.
   It needs
   `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | SHADER_DEVICE_ADDRESS_BIT`
   for the build. A staging Buffer can instead be written and copied explicitly.
3. Use `ClusterLevelAccelStructDirectBuildInfo::CommitToDeviceInline(infos, taskBuffer)`
   or the corresponding template/instantiate uploader. For Cluster BLAS call
   `ClusterBottomLevelAccelStructBuildInfo::CommitToDeviceInline(infos, taskBuffer)`.
   These four optional CPU encoders validate the entire task span
   before one upload. They check native field representation, addresses, strides
   and the upload range; the caller ensures matching formats/templates and
   compliance with the eventual build's task, per-task and aggregate budgets.
   Instantiate encoding resolves a zero vertex stride from each task's format
   and queries the active physical device for template address alignment.
   Cluster BLAS uploads use coherent Host memory. Direct, template and instantiate encoders
   also support a staged upload to task buffers with `TRANSFER_DST_BIT`.
4. Provide scratch of `buildScratchBufferSize` bytes with
   `STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT` and the reported alignment.
   `SetScratchAddress` stores the literal supplied address; use address 0 for a
   zero scratch requirement.
5. Append the matching direct, template, instantiate or Cluster BLAS command. For example:

```cpp
ImplicitClusterAccelStructCommandInput build;
build.SetInstantiateClusterTarget(limits, maxCount, instanceStorageAddress, outputReferencesAddress);
build.SetArgumentAddress(taskBuffer.GetDeviceAddress());
build.SetScratchAddress(scratchAddress);
build.CustomizeOperationCountAddress(buildCountAddress);
commands.ExecuteClusterAccelStructIndirect(build);
```

Choose `ExplicitClusterAccelStructCommandInput` or
`ImplicitClusterAccelStructCommandInput` to build, or
`ComputeClusterAccelStructSizesCommandInput` to size. Each setter takes the
limits, maximum count and all required destination addresses together.

```cpp
implicit.SetClusterTarget(limits, maxCount, storageAddress, outputReferencesAddress);
explicitBuild.SetClusterTarget(limits, maxCount, destinationReferencesAddress);
computeSizes.SetClusterTarget(limits, maxCount, outputSizesAddress);
```

The same argument pattern applies to `SetInstantiateClusterTarget` and
`SetBottomLevelAccelStructTarget`; implicit and Compute Sizes inputs also have
`SetClusterTemplateTarget`. Static queries remain independent of target selection;
CommandInputs store no memory requirements or per-destination allocation ranges.
Implicit operations optionally write packed per-object byte sizes through
`CustomizeOutputSizesArray`. Explicit operations can read a packed size table
through `CustomizeInputSizesArray`. Both tables default to zero.
Successful reselection replaces target bindings and clears the optional size table,
while preserving argument, scratch and GPU count addresses. A rejected setter
preserves the previous input. Copies and moves contain borrowed Host snapshots,
retain no resource ownership, and leave a moved-from target invalid. Select another
target before appending or recording the moved-from input.

The native `maxAccelerationStructureCount` comes from the selected maximum
count. If `buildCountAddress` is 0, the command processes the full
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
Output, scratch, task and address-table regions retain their maximum-count
capacity. Static task-byte queries always describe that full capacity.

The command snapshots Host limits, maximum count and the supplied addresses.
It never copies task contents or retains borrowed inputs. GPU-generated tasks
must remain within those frozen limits, including count, format and aggregate
geometry counts. With raw device addresses, callers guarantee underlying Buffer
usage, AS/scratch capacity, device-specific alignment, nonconflicting allocation ranges, index
contents and lifetime. Host validation checks description completeness, positive
capacities, required addresses, fixed ABI address alignment, overflow and known
task/count/output-array overlaps. Target selection, appending and recording query
neither native sizes/properties nor Buffer device addresses. Feature/entrypoint,
queue and rendering-scope checks remain required.

After an implicit build and its required synchronization, consume only the
GPU-built prefix of the caller's address table. Inactive entries may contain
uninitialized or stale addresses. If the caller initializes the table by transfer,
include `TRANSFER_DST_BIT` and synchronize the transfer with the build.
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

### Moving cluster objects and querying template indices

MOVE operates on a single selected object type per command. Set the type with
`ClusterAccelStructMoveLimits::SetObjectType` to triangle cluster, triangle cluster
template or Cluster BLAS. Use `SetMaxMovedBytes` to bound the total bytes across
the active tasks. A positive bound is required. Set `CustomizeNoMoveOverlap(true)`
when source and destination storage are disjoint and the original objects must
remain valid after copying. Follow the native MOVE overlap rules for the entire
operation and synchronize previous source reads before a destructive MOVE.
[Vulkan MOVE input](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureMoveObjectsInputNV.html)

After a destructive MOVE of a triangle cluster, rebuild each Cluster BLAS that
referenced it using the moved cluster's new address before any further traversal
of that BLAS. Synchronize MOVE writes with the dependent build. A no-overlap copy
allows an existing BLAS to keep using the original cluster for as long as its
storage is retained. Build a BLAS with the copy's new address to use the copy.
[Vulkan MOVE operation](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureOpTypeNV.html)

For each command mode, `GetMoveObjectsScratchRequirements(limits, maxCount)`
returns the scratch size and alignment together. The size is the larger of the native
`buildScratchSize` and `updateScratchSize` fields. `updateScratchSize` can carry
MOVE scratch requirements and does not enable KHR UPDATE. Explicit and implicit
inputs also expose `GetMoveObjectsDestinationAlignment(objectType)`. For implicit MOVE, supply
contiguous storage sufficient for the moved objects and their placement alignment.
For explicit MOVE, supply the packed destination-address table, with space for
each object at the selected type's alignment. MOVE Compute Sizes writes the
per-object sizes you can use to plan those allocations. `maxMovedBytes` is a batch
budget and may span multiple separate explicit allocations.

```cpp
ExplicitClusterAccelStructCommandInput move;
move.SetMoveObjectsTarget(moveLimits, maxCount, destinationReferencesAddress);
move.SetArgumentAddress(sourceReferencesAddress);
move.SetScratchAddress(moveScratchAddress);
move.CustomizeOperationCountAddress(activeCountAddress);
commands.ExecuteClusterAccelStructIndirect(move);
```

Template-index queries use the native `GET_CLUSTER_TEMPLATE_INDICES` operation
from extension revision 3 or newer. Supply the source template's triangle limits
and choose the mode with `SetQueryTemplateIndicesTarget`.
Compute Sizes takes the packed size output address. Explicit takes a packed
table of addresses for caller-provided data destinations. Implicit takes
contiguous output data storage and a packed address table to receive the data
locations. Each mode exposes `GetQueryTemplateIndicesBuildSizes(limits, maxCount)`,
which returns the native `VkAccelerationStructureBuildSizesInfoKHR` values,
including any zero sizes. Use `GetScratchAlignment()` when allocating scratch
from these native sizes. The index data payload is opaque to this API.
[Vulkan template-index record](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureGetTemplateIndicesInfoNV.html)

Both operations accept an existing packed GPU reference table as argument records,
with eight-byte stride and address alignment. Query the full task bytes with
`GetBuildInfoLayout().GetBufferSize(maxCount)` on `ClusterAccelStructMoveInfo` or
`ClusterTemplateIndicesQueryInfo`. Both types provide optional
CPU upload helpers for the same layout. Keep the source objects, argument and
count tables, scratch and destination buffers alive through the command's GPU
execution. Keep destination AS storage and any transitive dependencies alive
through the final dependent use.
[GPU record layout](acceleration_structure_gpu_layout.md#move-and-template-index-query)

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


## Partitioned TLAS

PTLAS support is enabled independently of CLAS when the Device supports
`VK_NV_partitioned_acceleration_structure` and its feature. An unsupported device
can still initialize; PTLAS `Create` rejects without allocating storage. This
path does not add descriptors, pipeline binding, traversal or an application task
producer shader.

`PartitionedTopLevelAccelStruct::Create` queries sizes and allocates only a
Device-local Buffer with `ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
SHADER_DEVICE_ADDRESS_BIT`, aligned to 256 bytes. It does not create a KHR AS
handle, upload records, build, submit or wait. All getters read saved Host data;
`IsValid` means allocated storage. Failed creation leaves the owner empty and
retryable. The owner is noncopyable/nonmovable and releases its Buffer through
RAII, synchronously without a GPU wait.

```cpp
PartitionedTopLevelAccelStructCreateInfo info;
info.SetMaxInstanceCount(1024);                 // Persistent instance slots; required > 0.
info.SetPartitionCount(16);                    // Ordinary partitions; required > 0.
info.SetMaxInstanceCountPerPartition(128);      // Each ordinary partition; required > 0.
info.CustomizeMaxInstanceCountInGlobalPartition(32); // Optional, default 0.
info.CustomizePartitionTranslation(true);      // Optional, default false.
info.CustomizeBuildFlags(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
PartitionedTopLevelAccelStruct ptlas;
ptlas.Create(info);
const auto sizes = ptlas.GetMemoryRequirements();
```

The number of ordinary partitions plus global instance capacity must fit the
Device's `maxPartitionCount`. AS and scratch sizes must fit both maximum Buffer
and allocation sizes. `sizes.buildScratchBufferSize` is used for **every** PTLAS
build, including builds with a source. `updateScratchBufferSize` is `std::nullopt` because
PTLAS does not use the KHR UPDATE scratch requirement. AS/scratch alignment is
256. `AccelStructGpuLayout::Partitioned::OperationBufferSize` is always 72 for the outer table; this value is
independent of the native size query and does not cover inner records or count.

Allocate a 72-byte outer operation Buffer, a separate four-byte GPU `uint32_t`
count and the inner record arrays. The count is required even for zero groups.
Input Buffers need `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
SHADER_DEVICE_ADDRESS_BIT`; shader producers also need `STORAGE_BUFFER_BIT`.
Scratch uses `STORAGE_BUFFER_BIT | SHADER_DEVICE_ADDRESS_BIT`. Record arrays and
outer table may be entirely GPU-produced. The optional CPU upload is separate:

```cpp
using Group = PartitionedTopLevelAccelStructOperationGroup;
std::array<Group::WriteInstanceRecord, 2> instances;
instances[0].instanceIndex = 0;
instances[0].partitionIndex = 2;                // An ordinary partition.
instances[0].accelStructReference = blasAddress;
instances[1].instanceIndex = 1;
instances[1].partitionIndex = VK_PARTITIONED_ACCELERATION_STRUCTURE_PARTITION_INDEX_GLOBAL_NV;
instances[1].accelStructReference = blasAddress; // The same API selects global.
Group::CommitToDeviceInline(instances, instanceRecords, 0);

Group group;
group.SetWriteInstances(instanceRecords.GetDeviceAddress(), 2);
Group::CommitToDeviceInline(std::span(&group, 1), operationGroups, 0);
const uint32_t groupCount = 1;
Buffer::UploadToDeviceInline(operationCount, 0, &groupCount, sizeof(groupCount));

BuildPartitionedTopLevelAccelStructCommandInput build;
build.SetTarget(ptlas, operationGroups.GetDeviceAddress(), operationCount.GetDeviceAddress());
build.SetScratchAddress(sizes.buildScratchBufferSize ? scratch.GetDeviceAddress() : 0);
commands.BuildPartitionedTopLevelAccelStruct(build);
```

Here `instanceRecords`, `operationGroups`, `operationCount` and `scratch` are
caller-owned Buffers allocated from the published layouts/requirements; uploads
do not record the build or supply its synchronization. CPU descriptions are
encoded into native layouts rather than copied as GLM structures. Encoding
validates the whole array before one upload, so an invalid later element leaves
the destination unchanged. Empty spans are no-ops even with an empty Buffer.
Coherent Host memory uses a mapped copy; other memory uses Buffer's synchronous
staged-transfer path and requires `TRANSFER_DST_BIT`. Outer upload validates
that all 72 bytes are accessible even when only a prefix is uploaded. Uploads
never execute a PTLAS build. See [GPU ABI](acceleration_structure_gpu_layout.md#partitioned-tlas)
for field offsets and producer constraints.

`SetTarget` validates a created owner and the outer/count address ranges,
snapshots capacities/options/sizes/addresses, clears source and preserves scratch.
Failure preserves its old description. Call `CustomizeSourceAccelStruct(source)`
after target selection to modify persistent contents. Source and destination
must have identical capacities, build flags and translation setting; source may
be the target itself. A distinct source remains unchanged, while its persistent
contents carry into the destination. The caller must have built that source and
synchronized its writes. Use WRITE_INSTANCE to move an existing slot between
ordinary/global partitions or change its transform. UPDATE_INSTANCE only changes
BLAS/SBT fields and has additional history restrictions below.

CommandInput copies/moves retain no resource references. Assign `build = {};`
to clear it, including scratch; no Reset is provided. Appending validates aligned,
non-overflowing AS, scratch, 72-byte outer and four-byte count ranges, rejecting
known overlap except an in-place source/destination. Each call snapshots one
native PTLAS command; later changes/destruction of the Input do not change it.
Recording only constructs local native parameters and invokes Vulkan, without
queries, uploads, allocation, submission or waits. Builds must be outside any
rendering scope on a compute-capable queue. CLAS ray-pipeline opt-in checks do
not apply to PTLAS. PTLAS commands support stream movement, merge and splitting.

The producer guarantees actual slot and partition indices, ordinary/global
occupancy, unique indices per operation type, translation enablement, counts at
most three and unique outer operation types. The global sentinel is `UINT32_MAX`
and does not increase the ordinary partition count. `instanceCustomIndex` is a
full 32-bit PTLAS instance ID; mask and SBT offset are limited to 8 and 24 bits.
A zero BLAS reference disables a slot. A slot disabled through WRITE must be
reenabled through WRITE. UPDATE of a slot assigned a BLAS through WRITE requires
that slot's explicit-bounding-box flag, and the stored box must contain any
replacement geometry. Host encoders do not validate target capacities, GPU
history or indirect BLAS dependencies. Explicit AABBs are transformed geometry
bounds before partition translation. Translation records require creation with
translation enabled; global instances do not receive ordinary-partition
translation.

Synchronize task/count writes with `ACCELERATION_STRUCTURE_BUILD` /
`INDIRECT_COMMAND_READ`; synchronize source/destination and scratch access with
AS build read/write access, including consecutive builds and scratch reuse.
Before traversal, make the resulting AS writes visible to the consuming shader
stage. Keep owners, input/count/scratch Buffers and referenced BLAS storage alive
through their GPU uses. The RHI inserts no implicit barriers, queue transfer or
resource retention and does not inspect GPU arrays or infer build history.

Independent owners may create and release concurrently after Device startup;
exclude conflicting access to the same owner, mutable CreateInfo or CommandInput.
Keep Device/allocator/dispatch stable through final release and complete GPU uses
first. See [PTLAS lifetime](resource_lifetimes.md#partitioned-tlas-ownership).

An independent opt-in smoke builds a real triangle BLAS and executes initial
PTLAS construction, ordinary/global migration, in-place writes, reference updates
with distinct source/destination, and partition translation. It waits for each
submission and exercises staged CPU record uploads, without enabling CLAS. It
checks completion only, not traversal results. Unsupported devices explicitly
print `SKIPPED`.

```powershell
cmake -E env LEARN_VULKAN_PTLAS_GPU_TESTS=1 ./bin/learn_vulkan.exe
```
