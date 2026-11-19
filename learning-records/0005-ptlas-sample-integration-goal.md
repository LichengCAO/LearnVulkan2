# 示例源码阅读面向引擎接入

2026-11-19，用户克隆并配置 NVIDIA vk_partitioned_tlas 后，请求带读示例，并明确选择“为自己的引擎接入 PTLAS 做准备”。这与现有 MISSION 一致；本轮仅教学，不修改引擎实现。

第 05 课以本地示例提交 07acd56 为准，追踪 Host 容量与初次 build、GPU 紧凑变化列表、间接计数、原地 PTLAS build 及 push constant 地址追踪。静态阅读还发现 global 分区策略未写入输出 partitionIndex，以及通用 barrier 未显式涵盖 operations/count 的 INDIRECT_COMMAND_READ；均未做运行验证或修复。

尚无本轮用户练习反馈，不据此认定已掌握 GPU 计数、分区编码或同步规则。后续可从 createPartitionedTopLevelAS 逐行读起，再对照引擎的 Buffer RAII 与 CommandBuffer 命令参数快照。
