# Vulkan CLAS 与 DirectX 12 CLAS

核对日期：2026-11-18。本文的 Vulkan 指本仓库采用的
`VK_NV_cluster_acceleration_structure`；DX12 主要指 Microsoft 于
2026-10-28 发布的标准 DXR Clustered Geometry **预览**，另说明 NVIDIA NVAPI 路线。
标准 DXR 已提供跨厂商接口，不能再把“DX12 CLAS”等同于 NVAPI。
发布包为 Agility SDK 1.722.0-preview，仍需配套 runtime、编译器和支持的驱动。
见 [Microsoft 发布说明](https://devblogs.microsoft.com/directx/clas-ptlas-preview/)。

## 相同的几何模型

两者都是 `三角形 → CLAS → Cluster BLAS → TLAS → 光线遍历`。
CLAS 不能直接作为 TLAS 实例；Cluster Template 则保存可复用的构建信息，
实例化时提供实际顶点位置生成 CLAS，模板本身不参与遍历。
因此“模板实例化”也不等于在 TLAS 中实例化一个可带变换的对象。
见 [Khronos 模型说明](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cluster_acceleration_structure.html#_proposal)、
[Microsoft 模型说明](https://devblogs.microsoft.com/directx/clas-ptlas-preview/#clustered-geometry)。

## 主要差异

| 项目 | Vulkan NV 扩展 | 标准 DXR 预览 |
| --- | --- | --- |
| 能力检测 | 扩展与 `clusterAccelerationStructure` feature | `CheckFeatureSupport` 查询 `ClustersAndPTLASSupported`；预览头文件使用 `D3D12_FEATURE_DATA_D3D12_OPTIONS_RAYTRACING2` |
| 每个 cluster 的限制 | 查询 `maxTrianglesPerCluster`、`maxVerticesPerCluster` 与 geometry index 上限 | 256 三角形、256 个唯一顶点索引；geometry index 最大 `0xffffff` |
| 对齐 | 查询 CLAS、template、Cluster BLAS 与 scratch 各自的 properties | CLAS 128 字节，template 32 字节，Cluster BLAS 与 scratch 256 字节 |
| Host 查询 / GPU 执行 | `vkGetClusterAccelerationStructureBuildSizesNV` / `vkCmdBuildClusterAccelerationStructureIndirectNV` | `GetRTASOperationPrebuildInfo` / `ExecuteIndirectRTASOperations`；后者可一次提交多组独立操作描述 |
| 顶点表示 | `VkFormat`，要求 AS vertex-buffer format feature；支持位置低位截断 | 独立 `D3D12_VERTEX_FORMAT`，还包含按 cluster 编码的 `COMPRESSED1`；模板区分 hint、存储与实例化源格式 |

能力与限制来源：[Vulkan feature](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceClusterAccelerationStructureFeaturesNV.html)、
[Vulkan properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceClusterAccelerationStructurePropertiesNV.html)、
[Vulkan 顶点输入](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureTriangleClusterInputNV.html)、
[DXR 预览头文件](https://github.com/microsoft/DirectX-Headers/blob/preview/include/directx/d3d12.h)、
[DXR 限制与格式](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#cluster-level-acceleration-structure)。
Vulkan 限制应以查询结果为准；DXR 的唯一顶点按索引数值计数，不能通过比较位置值来合并计数。

`COMPRESSED1` 使用共享 anchor 和二次幂尺度编码整数偏移；它不是
`PositionTruncateBitCount` 的另一个名字。后者在转换为 float32 后清零低位，
对 `COMPRESSED1` 不生效。移植模板时还须处理目标格式的舍入与范围要求。
见 [Microsoft 顶点编码](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#cluster-input-geometry-encoding)。

## 操作对应

下表仅表示功能对应，不表示输入结构布局、数值枚举或二进制结果兼容。

| Vulkan `VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_*_NV` | DXR `D3D12_RTAS_OPERATION_TYPE_*` |
| --- | --- |
| `BUILD_TRIANGLE_CLUSTER` | `BUILD_CLAS_FROM_TRIANGLES` |
| `BUILD_CLUSTERS_BOTTOM_LEVEL` | `BUILD_BLAS_FROM_CLAS` |
| `BUILD_TRIANGLE_CLUSTER_TEMPLATE` | `BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES` |
| `INSTANTIATE_TRIANGLE_CLUSTER` | `INSTANTIATE_CLUSTER_TEMPLATES` |
| `MOVE_OBJECTS` | `MOVE_CLUSTER_OBJECTS` |
| `GET_CLUSTER_TEMPLATE_INDICES` | 当前标准操作枚举未列同名功能 |

见 [Vulkan 操作枚举](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureOpTypeNV.html)、
[DXR 操作枚举](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#d3d12_rtas_operation_type)。
两者都支持 GPU 参数数组与 GPU count；Host 仍需声明容量上限。

## 内存、地址与大小数组

两者都有隐式目标、显式目标、只计算大小三种模式。“隐式”是驱动在应用预分配的
Buffer 内选择结果地址，不是驱动替应用创建资源；应用仍需提供结果与 scratch 内存。
隐式地址应从输出表读取，不能自行假定固定步长或跨重建保持不变。
见 [Vulkan 模式](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureOpModeNV.html)、
[DXR 目标分配](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#implicit-vs-explicit-destinations-for-acceleration-structure-operations)。

| 数据 | Vulkan | DXR |
| --- | --- | --- |
| 隐式结果区 | `dstImplicitData` | `BatchResultData` |
| 地址数组 | `dstAddressesArray`：隐式写、显式读 | `ResultAddressArray`：隐式写、显式读 |
| 大小数组 | `dstSizesArray`：隐式/算大小时写，**显式时读** | `ResultSizeArray`：结果输出；除 `GET_SIZES` 外可省略 |

这意味着显式目标模式不能只按名称替换字段。
见 [Vulkan CommandsInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureCommandsInfoNV.html)、
[DXR BatchedOperationData](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#d3d12_rtas_batched_operation_data)。

版本陷阱：当前已发布 DXR clustered-geometry preview 的大小数组元素始终为 `UINT64`。
2026-11-11 的规范 v0.39 又定义了默认 `UINT32` 与 `RESULT_SIZE_64BIT` 开关，
但明确该开关尚不在上述预览中；`StrideInBytes` 只控制间隔，不选择元素宽度。
实现必须匹配所用 SDK/runtime，不能把最新文稿直接当作已发布 ABI。
见 [DXR 大小输出与 preview 注释](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#d3d12_rtas_batched_operation_data_flags)。

## 遍历、标识与同步

Vulkan RT pipeline 通过 `VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV`
启用 CLAS。DXR 同样需要显式 opt-in：pipeline 使用
`D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_CLUSTERED_GEOMETRY`，inline RayQuery 使用
`RAYQUERY_FLAG_ALLOW_CLUSTERED_GEOMETRY`；`ClusterID()` 等新增 HLSL 查询对应 SM 6.10。
RayQuery opt-in 还需按 shader 版本处理，不能把开关和新增查询的版本要求混为一谈。
见 [Vulkan pipeline 开关](https://docs.vulkan.org/refpages/latest/refpages/source/VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV.html)、
[Microsoft HLSL 约定](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0045-clustered-geometry.md#allow-clustered-geometry-flag)。

两者都有用户指定的 ClusterID；命中 primitive index 局限于 CLAS，属性查找应组合使用二者。
DXR 提供 `ClusterID()` 及 RayQuery/HitObject 对应查询。
同一批内部没有依赖顺序保证；CLAS 构建、Cluster BLAS 构建和 TLAS 构建应按依赖分阶段并加 barrier。
Vulkan 的 task/count 输入使用 AS-build stage 加 indirect-command-read access；
DXR 的只读参数要求 shader-resource 可访问，输出表与 scratch 要求 UAV 可访问，AS 数据使用 AS access/state。
见 [Vulkan 命令同步](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildClusterAccelerationStructureIndirectNV.html)、
[DXR 参数资源状态](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#resource-state-for-read-only-arguments)、
[Microsoft 分阶段示例](https://devblogs.microsoft.com/directx/clas-ptlas-preview/#gpu-driven-acceleration-structure-builds)。

## NVAPI 与本仓库的关系

NVAPI 是较早的 NVIDIA 专用 DX12 路线，入口为
`NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo` 与
`NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation`，也支持模板与三种目标模式。
它与 Vulkan NV 接口概念接近，但不能据此把它的结构、大小数组或 shader 扩展约定当作标准 DXR ABI。
见 [NVIDIA NVAPI 官方头文件](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h)。

本仓库使用 Vulkan 原生任务结构。直接构建 CLAS、实例化模板和构建 Cluster BLAS 均支持显式与隐式目标。
显式构建读取调用方提供的目标地址表，隐式构建将分配的地址写入结果表。模板本身通过隐式目标构建。
MOVE 和模板索引查询都支持三种目标模式。模板索引查询要求扩展版本至少为 3，结果是由 GPU 消费的不透明数据。
各操作均可用 GPU count 选择有效前缀。Compute Sizes 写入必需的紧密排列 `uint32_t` 大小表，隐式模式可选写入大小表，
显式模式可选读入大小表。Host 查询返回所选模式的 native 容量需求，逐对象字节数由 GPU 写入大小表。
[当前接口](acceleration_structures.md#clas-and-cluster-blas-batches)
工程推论：可以共享“Host 容量查询与分配、GPU 批构建、地址表、依赖同步”的上层设计，
但 GPU task 编码、结果大小格式、能力检测与 shader 开关应由后端负责。
API 功能对应不能证明两者的构建速度、占用或遍历性能相同；这些需要指定 GPU、驱动与场景实测。
