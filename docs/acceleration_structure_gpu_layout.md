# GPU acceleration-structure input layout

`src/my_vulkan/resource/acceleration_structure_gpu_layout.h` publishes byte
offsets, unshifted masks and shifts for native GPU build inputs. This is a C++
header; shaders can follow this contract without a shared-language include or
changes to existing shaders. Both public AS headers expose it through
`acceleration_structure_types.h`.

The layout queries are static, `constexpr`, and perform no allocation or Device access:

```cpp
constexpr auto direct = ClusterLevelAccelStructDirectBuildInfo::GetBuildInfoLayout();
constexpr auto topology = ClusterLevelAccelStructTemplateBuildInfo::GetBuildInfoLayout();
constexpr auto instantiate = ClusterLevelAccelStructInstantiateBuildInfo::GetBuildInfoLayout();
constexpr auto clusterBlas = ClusterBottomLevelAccelStructBuildInfo::GetBuildInfoLayout();
constexpr auto tlas = TopLevelAccelStructBuildInfo::GetBuildInfoLayout();
```

| Input record | `recordStride` | `addressAlignment` | Constant namespace |
| --- | ---: | ---: | --- |
| Direct CLAS task | 64 | 8 | `AccelStructGpuLayout::Direct` |
| Template CLAS task | 72 | 8 | `AccelStructGpuLayout::Template` and shared `Direct` prefix |
| Instantiate CLAS task | 32 | 8 | `AccelStructGpuLayout::Instantiate` |
| Cluster BLAS task | 16 | 8 | `AccelStructGpuLayout::ClusterBlas` |
| Packed TLAS instance | 64 | 16 | `AccelStructGpuLayout::Tlas` |

Sizes and offsets are bytes. Record `i` starts at `base + i * recordStride`.
`addressAlignment` applies to the GPU input address, including a suballocation
offset. It is separate from output storage, template, scratch and C++ `alignof`
requirements. TLAS uses packed instances with `arrayOfPointers = VK_FALSE`.

Each address occupies eight bytes. On current little-endian targets, a shader
using 32-bit stores writes the low word at the field offset and the high word
four bytes later. Masks below are unshifted; bit 0 is the least significant bit
of the word. Producers supply values that fit their fields.

## Direct CLAS

| Offset | Size | Native field / packed word |
| ---: | ---: | --- |
| 0 | 4 | `clusterID` |
| 4 | 4 | `clusterFlags` |
| 8 | 4 | Counts, position truncation and index formats |
| 12 | 4 | Geometry index, reserved bits and geometry flags |
| 16 | 4 | Index stride in low 16 bits; vertex stride in high 16 bits |
| 20 | 4 | Geometry stride in low 16 bits; opacity-index stride in high 16 bits |
| 24 | 8 | `indexBuffer` |
| 32 | 8 | `vertexBuffer` |
| 40 | 8 | `geometryIndexAndFlagsBuffer` |
| 48 | 8 | `opacityMicromapArray` |
| 56 | 8 | `opacityMicromapIndexBuffer` |

The word at offset 8 contains:

| Bits | Field | Unshifted mask |
| --- | --- | --- |
| 0–8 | Triangle count | `0x1FF` |
| 9–17 | Unique vertex count | `0x1FF` |
| 18–23 | Position truncate bit count | `0x3F` |
| 24–27 | Index format | `0xF` |
| 28–31 | Opacity-index format | `0xF` |

Native index-format values are **1 for 8-bit, 2 for 16-bit, and 4 for 32-bit**,
distinct from `VkIndexType`. At offset 12, bits 0–23 are the geometry index,
bits 24–28 are reserved and zero, and bits 29–31 are geometry flags. Zero
index/vertex stride selects packed values. Host defaults include GeometryIndex
0, opaque geometry, no opacity micromaps and position truncation 0. Native
definitions: [direct task](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureBuildTriangleClusterInfoNV.html),
[geometry word](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureGeometryIndexAndGeometryFlagsNV.html).

## CLAS templates

A template record shares the Direct layout and bit encodings for bytes 0–63.
Byte 64 holds the eight-byte `instantiationBoundingBoxLimit` address, exposed as
`AccelStructGpuLayout::Template::InstantiationBoundingBoxLimitOffset`. The record
stride is 72 and its input address alignment is 8 for both sizing and building.
The optional CPU helper emits base ID zero, flags zero, opaque GeometryIndex zero,
the supplied topology counts/index address/type/stride, and zero vertex, geometry,
opacity and bounding-box addresses. A shader producer may write the native fields
directly while satisfying the target limits and Vulkan requirements. Index type
is per task and is independent of TemplateBatch creation.
[Native template record](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV.html).

## CLAS instantiation

| Offset | Size | Native field |
| ---: | ---: | --- |
| 0 | 4 | Cluster ID offset |
| 4 | 4 | Signed geometry-index offset in low 24 bits; reserved high 8 bits zero |
| 8 | 8 | Template address |
| 16 | 8 | Vertex-buffer address |
| 24 | 8 | Vertex byte stride; only low 32 bits are used |

Negative geometry-index offsets use 24-bit two's-complement encoding. The Host
helper uses base template ID zero and geometry-index offset zero. It expands
requested zero vertex stride to the task's element size; GPU producers write the
actual vertex byte stride. Vertex format must match the template and build.
Native definitions: [instantiation record](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureInstantiateClusterInfoNV.html),
[stride slot](https://docs.vulkan.org/refpages/latest/refpages/source/VkStridedDeviceAddressNV.html).

## Cluster BLAS

| Offset | Size | Native field |
| ---: | ---: | --- |
| 0 | 4 | CLAS reference count |
| 4 | 4 | Byte stride between references |
| 8 | 8 | Address of the CLAS reference array |

The Host encoder writes packed reference stride 8. Each task's references must
be unique addresses of built CLAS objects, and the array must have enough entries.
See the native [Cluster BLAS record](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV.html).

## CLAS command outputs

A reference table contains one packed 8-byte `VkDeviceAddress` per slot.
Explicit builds read that table. Implicit builds write references and may also
write a separate size table. Compute sizes writes only the size table. Allocate
four bytes per maximum target for packed `uint32_t` byte sizes, with a four-byte
aligned base, storage-buffer usage and device address usage. Both output tables
preserve entries beyond the GPU count. Synchronize their writes before dependent
GPU reads or Host readback. Template sizing takes
`VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV` records at an
eight-byte aligned base. Each 72-byte record uses the direct task layout for its
first 64 bytes, followed by the optional `instantiationBoundingBoxLimit` address.
[Native command fields](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureCommandsInfoNV.html)

## TLAS instances

| Offset | Size | Native field / packed word |
| ---: | ---: | --- |
| 0 | 16 | Transform row 0: four floats |
| 16 | 16 | Transform row 1: four floats |
| 32 | 16 | Transform row 2: four floats |
| 48 | 4 | Custom index in low 24 bits; visibility mask in high 8 bits |
| 52 | 4 | SBT record offset in low 24 bits; instance flags in high 8 bits |
| 56 | 8 | BLAS device address; zero selects an inactive instance |

The transform is a **row-major 3×4 affine matrix**, distinct from default
`glm::mat4` memory order. Translation occupies its last column. The Host uploader
already selects and transposes the relevant GLM elements. The reference may
address an ordinary BLAS or a Cluster BLAS. Native definitions:
[instance](https://docs.vulkan.org/refpages/latest/refpages/source/VkAccelerationStructureInstanceKHR.html),
[transform](https://docs.vulkan.org/refpages/latest/refpages/source/VkTransformMatrixKHR.html).

## Buffer use and synchronization

GPU-written input Buffers need `STORAGE_BUFFER` alongside
`ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` and `SHADER_DEVICE_ADDRESS` for
this path. Callers provide adequate allocation size, address alignment,
producer/consumer synchronization and resource lifetime.

CLAS task/count producers synchronize with `ACCELERATION_STRUCTURE_BUILD` /
`INDIRECT_COMMAND_READ`. TLAS instance producers synchronize with
`ACCELERATION_STRUCTURE_BUILD` / `SHADER_READ`. Builds introduce no implicit
barrier or CPU inspection of GPU records. Native requirements:
[CLAS build](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildClusterAccelerationStructureIndirectNV.html),
[KHR build](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html).

Compile-time assertions check native sizes and address/stride offsets. Host
regression tests serialize native bitfields and verify their actual words,
including TLAS's distinct 16-byte GPU input alignment. Existing CLAS GPU smoke
tests exercise builds using the published stride and alignment values.
