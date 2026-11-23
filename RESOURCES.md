# Cluster Template / Partitioned TLAS Resources

## Knowledge

- 本地 NVIDIA 示例：`E:/GitStorage/nvidia_examples/v2/vk_animated_clusters/README.md`。
  使用范围：模板复用的动机、界面参数、静态与动画路径；优先以本地版本解释本地代码。
- 本地实现：`E:/GitStorage/nvidia_examples/v2/vk_animated_clusters/src/renderer_raytrace_clusters.cpp`。
  重点入口：508 模板初始化、813 实例化大小查询、879 实例输出准备、360 每帧实例化、283 帧内依赖链、1167 Cluster BLAS 准备。
- [Khronos 扩展设计](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cluster_acceleration_structure.html)。
  使用范围：Template、CLAS、Cluster BLAS 的关系与预计算动机。正式有效性规则以 API reference 为准。
- [Template 构建任务](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV.html)。
  使用范围：索引、参考顶点、包围盒限制与位置截断字段，以及设备对齐约束。
- [实例化任务](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureInstantiateClusterInfoNV.html)。
  使用范围：模板地址、顶点地址及 ID 偏移。动画路径始终提供实际顶点；不要把样例中 COMPUTE_SIZES 的空顶点直接用于实际实例化。
- [GPU 命令](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildClusterAccelerationStructureIndirectNV.html)。
  使用范围：任务类型、scratch、GPU count、输入与输出的同步及 Buffer usage。
- [操作模式](https://docs.vulkan.org/refpages/latest/refpages/source/VkClusterAccelerationStructureOpModeNV.html)。
  使用范围：区分 COMPUTE_SIZES、IMPLICIT_DESTINATIONS 与 EXPLICIT_DESTINATIONS。
- [本仓库的资源生命周期](docs/resource_lifetimes.md#cluster-acceleration-structure-batches)。
  使用范围：容量与对齐查询、调用方存储、借用地址、GPU 有效前缀以及模板、地址表和 CLAS 的生命周期。

- [Microsoft：CLAS / PTLAS preview](https://devblogs.microsoft.com/directx/clas-ptlas-preview/)。
  PTLAS 的应用动机、与 CLAS 的独立性及 DXR preview 状态；核对日期：2026-11-19。
- [Khronos：PTLAS 扩展设计](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_partitioned_acceleration_structure.html)。
  分区内部结构复用、分区粒度与空间重叠的取舍、global partition 的用途。
- [Microsoft：DXR Part 2 规范](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html#partitioned-top-level-acceleration-structures)。
  DXR 的持久实例槽、WRITE / UPDATE、explicit AABB 与分区平移语义；不把 preview 的字段直接当成 Vulkan 字段。
- [Khronos：WRITE_INSTANCE 数据](https://docs.vulkan.org/refpages/latest/refpages/source/VkPartitionedAccelerationStructureWriteInstanceDataNV.html)。
  完整实例变换、稳定 instanceIndex、partitionIndex 和 BLAS 地址。
- [Khronos：UPDATE_INSTANCE 数据](https://docs.vulkan.org/refpages/latest/refpages/source/VkPartitionedAccelerationStructureUpdateInstanceDataNV.html)。
  仅修改 BLAS 地址与 hit-group 索引贡献，不能用于移动实例；显式范围与 refit 的关系。
- [Khronos：PTLAS GPU 命令](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildPartitionedAccelerationStructuresNV.html)。
  独立构建入口、容量上界、源 / 目标与 scratch 的同步要求。
- [Khronos：PTLAS descriptor](https://docs.vulkan.org/refpages/latest/refpages/source/VkWriteDescriptorSetPartitionedAccelerationStructureNV.html)。
  独立 descriptor 类型及通过 GPU 地址绑定 PTLAS 的方式。
- [NVIDIA：vk_partitioned_tlas](https://github.com/nvpro-samples/vk_partitioned_tlas)。
  同场景 TLAS rebuild / refit 与 PTLAS 对比、局部分区更新、动态实例迁移到 global 的策略。适合后续最小性能实验。

- [Khronos：PTLAS 构建参数](https://docs.vulkan.org/refpages/latest/refpages/source/VkBuildPartitionedAccelerationStructureInfoNV.html)。
  第 03 课主线：源 / 目标 GPU 地址、srcInfos、srcInfosCount 与原地 / 异地修改语义。
- [Khronos：PTLAS 间接操作](https://docs.vulkan.org/refpages/latest/refpages/source/VkBuildPartitionedAccelerationStructureIndirectCommandNV.html)。
  分清外层操作数与内层 argCount；同类操作合并为一组参数，实例索引不得重复。
- [Khronos：常规 KHR 构建](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html)。
  对照完整实例输入与 UPDATE 约束；输入记录局部上传和 AS 稀疏更新不是同一概念。
- [Khronos：KHR indirect 构建](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresIndirectKHR.html)。
  区分间接提供 build range 与 PTLAS 的持久实例 / 分区操作，避免把区别简化成 GPU / CPU 驱动。

## Wisdom (Communities)

- [NVIDIA 示例项目 Issues](https://github.com/nvpro-samples/vk_animated_clusters/issues)。
  使用范围：携带驱动、GPU、输入模型与最小复现，讨论示例特有行为。本次未向社区发送消息。

## Gaps

- 未进行本机 GPU 性能测量；收益需要以自己的变形内容、build 与 trace 总时间验证。
