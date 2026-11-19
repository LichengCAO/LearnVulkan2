# Mission: 动态光追加速结构的原理与 RHI 接入判断

## Why
依据本机 NVIDIA `vk_animated_clusters` 示例掌握 Cluster Template，先理解模板构建与每帧实例化，再能规划 LearnVulkan2 的 RHI 接入。
用户于 2026-11-19 选择“两者都要，先原理再接入”。
同日用户继续学习 Partitioned TLAS，并明确选择“理解原理，并判断引擎接入价值”。教学范围扩展到场景级稀疏更新，保留原有 Cluster Template 学习目标。

## Success looks like
- 能区分 Template、实例化得到的 CLAS、Cluster BLAS 与 TLAS。
- 能沿示例定位模板构建、实例化大小查询、输出分配与每帧 GPU 命令。
- 能指出当前 RHI 已支持的部分，以及接入模板所需的资源、任务编码和命令扩展。
- 能用变化实例的分布、受影响分区和 build + trace 总成本，判断 PTLAS 是否值得接入。

## Constraints
- 中文讲解；Cluster Template 以本地 NVIDIA 示例为依据，PTLAS 以 Microsoft / Khronos 规范和 NVIDIA 官方示例为依据。
- 先学习；本次不修改引擎实现。
- 涉及 RHI 时遵循仓库的同步创建、Buffer RAII、借用地址与 GPU 生命周期契约。

## Out of scope
- 本课不展开 meshoptimizer 聚类算法、LOD 流送或自适应细分。
