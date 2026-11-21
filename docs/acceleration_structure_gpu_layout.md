# GPU acceleration-structure input layout

`src/my_vulkan/resource/acceleration_structure_gpu_layout.h` publishes byte
offsets, unshifted masks and shifts for native GPU build inputs. This is a C++
header; shaders can follow this contract without a shared-language include or
changes to existing shaders. All public AS headers expose it through
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
| PTLAS operation group | 24 | 8 | `AccelStructGpuLayout::Partitioned::Operation` |
| PTLAS write instance | 104 | 8 | `AccelStructGpuLayout::Partitioned::WriteInstance` |
| PTLAS update instance | 16 | 8 | `AccelStructGpuLayout::Partitioned::UpdateInstance` |
| PTLAS partition translation | 16 | 4 | `AccelStructGpuLayout::Partitioned::Translation` |

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

## Partitioned TLAS

All four `GetBuildInfoLayout()` queries are static constexpr. Public constants
live under `AccelStructGpuLayout::Partitioned`; static assertions compare every
published offset and stride with the native Vulkan structures. Addresses are
raw 64-bit GPU addresses, not KHR handles or Host pointers.

| Operation offset | Bytes | Native field |
| ---: | ---: | --- |
| 0 | 4 | `opType`: WRITE_INSTANCE = 0, UPDATE_INSTANCE = 1, WRITE_PARTITION_TRANSLATION = 2 |
| 4 | 4 | `argCount`, number of inner records |
| 8 | 8 | `argData.startAddress`, inner array address |
| 16 | 8 | `argData.strideInBytes`, fixed 104/16/16 for the chosen operation |

The outer table requires **72 accessible bytes**, eight-byte aligned, even when
the GPU count selects fewer than three groups. Its count is a separate required
four-byte aligned `uint32_t`; zero means zero groups. The producer bounds it by
three, supplies unique operation types and initializes the selected prefix. There
is no readback or clamp. An individual group's record count may be zero; its
address may then be zero. GPU producers must follow native stride requirements;
the optional Host setters always emit the fixed packed strides shown here.

| WRITE_INSTANCE offset | Bytes | Native field |
| ---: | ---: | --- |
| 0 | 48 | Three transform rows, four floats per row |
| 48 | 24 | Explicit AABB: minX, minY, minZ, maxX, maxY, maxZ |
| 72 | 4 | Full 32-bit instance ID |
| 76 | 4 | Instance mask, only low 8 bits used |
| 80 | 4 | Hit-group offset, only low 24 bits used |
| 84 | 4 | Instance flags (native bits 0-4) |
| 88 | 4 | Persistent instance slot index |
| 92 | 4 | Ordinary partition index, or `UINT32_MAX` for global |
| 96 | 8 | BLAS/Cluster BLAS address; zero disables the slot |

The Host encoder writes `transform.matrix[row][column] = glm[column][row]`;
translation is the last column. The AABB is used only with
`ENABLE_EXPLICIT_BOUNDING_BOX_NV` (bit 4); it describes transformed geometry
before partition translation and must contain later replacement geometry.
The Host encoder checks finite, ordered bounds when enabled. It rejects unknown
flags, simultaneous FORCE_OPAQUE/FORCE_NO_OPAQUE and mask/SBT overflow. Disabled
AABBs are ignored. Full 32-bit IDs, zero references and the global marker are
preserved.

| UPDATE_INSTANCE offset | Bytes | Native field |
| ---: | ---: | --- |
| 0 | 4 | Persistent instance slot index |
| 4 | 4 | Hit-group offset, low 24 bits |
| 8 | 8 | Replacement BLAS address, zero disables |

UPDATE_INSTANCE cannot change transform, flags or partition membership. A slot
previously assigned a BLAS through WRITE requires the explicit-bounding-box flag;
a WRITE-disabled slot must be reenabled by WRITE. These are GPU history rules,
not properties the Host encoder can check.

| WRITE_PARTITION_TRANSLATION offset | Bytes | Native field |
| ---: | ---: | --- |
| 0 | 4 | Ordinary partition index |
| 4 | 12 | Translation x, y, z |

The target must have partition translation enabled during creation. Global
instances are outside ordinary partitions and do not use their translations.

Each optional CPU encoder rejects duplicate indices within its own record type;
the outer encoder rejects duplicate operation types and more than three groups.
The complete input is encoded before one upload. Empty spans are no-ops. These
helpers do not access an owner or validate slot/partition capacity, occupancy,
translation enablement, BLAS contents or previous GPU operations. Shader producers
must enforce all those native constraints and remain within the saved creation
capacities. Host records have no GPU ABI guarantee; use the published byte layout,
not `sizeof(WriteInstanceRecord)` or raw GLM storage.

[Native PTLAS command](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildPartitionedAccelerationStructuresNV.html)

## Buffer use and synchronization

GPU-written input Buffers need `STORAGE_BUFFER` alongside
`ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` and `SHADER_DEVICE_ADDRESS` for
this path. Callers provide adequate allocation size, address alignment,
producer/consumer synchronization and resource lifetime.

CLAS and PTLAS task/count producers synchronize with `ACCELERATION_STRUCTURE_BUILD` /
`INDIRECT_COMMAND_READ`. TLAS instance producers synchronize with
`ACCELERATION_STRUCTURE_BUILD` / `SHADER_READ`. Builds introduce no implicit
barrier or CPU inspection of GPU records. Native requirements:
[CLAS build](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildClusterAccelerationStructureIndirectNV.html),
[KHR build](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html).

Compile-time assertions check native sizes and address/stride offsets. Host
regression tests serialize native bitfields and verify their actual words,
including TLAS's distinct 16-byte GPU input alignment. Existing CLAS GPU smoke
tests exercise builds using the published stride and alignment values.
