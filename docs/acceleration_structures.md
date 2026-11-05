# Acceleration structures

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
`TopLevelAccelStruct::WriteInstances` is static: no TLAS object is required.
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
TopLevelAccelStruct::WriteInstances(tlasInfo, instanceBuffer);

TopLevelAccelStruct tlas;
tlas.Create(tlasInfo);
// Allocate tlasScratch using tlas.GetBuildSizes()/GetScratchAlignment(), as above.
commandBuffer.BuildAccelerationStructureKHR(
    tlas, tlasInfo, instanceBuffer, 0, tlasScratch);
// Later, after the original build and required synchronization:
commandBuffer.UpdateAccelerationStructureKHR(
    tlas, tlas, tlasInfo, instanceBuffer, 0, tlasScratch);
```

`WriteInstances` validates and encodes the entire list before writing once.
It requires host-visible, host-coherent destination memory, but no build-input
usage: a staging Buffer is also valid. For device-local instance data, write a
staging Buffer, record the existing `CopyBuffer`, and supply the destination to
the TLAS build. Transfers and BLAS-to-TLAS dependencies require explicit
synchronization. A build never implicitly calls `WriteInstances`.

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
