# RenderGraph 跨 Graph Queue 同步设计

## 目标

为 RenderGraph 增加跨 Graph 的异步 queue 同步，同时保留现有 graph 内部的 graphics/compute queue synchronization。

设计目标：

- 不在 `Execute()` 中直接等待 GPU；
- 用 Binary semaphore 连接 producer graph 和 consumer graph；
- 让 graph 和 external resource 都能声明多个 entering/leaving sync；
- 由 `RenderGraph::Build()` 计算有效 submit 边界，`RenderGraphInstance` 只绑定同步对象；
- 保证一个 `QueueSyncInfo` 只能被一个 producer-consumer graph 连接使用一次。

## QueueSyncInfo

`QueueSyncInfo` 表示一个 producer graph 到一个 consumer graph 的一次性同步连接。

```cpp
struct QueueSyncInfo
{
    QueueDependency graphicsToCompute;
    QueueDependency computeToGraphics;
};
```

两个 semaphore 的方向固定为：

- `graphicsToCompute`：producer 的 graphics queue signal，consumer 的 compute queue wait；
- `computeToGraphics`：producer 的 compute queue signal，consumer 的 graphics queue wait。

同一个 queue 上的 graph-to-graph 依赖不使用 semaphore，而是依靠：

1. producer graph 先提交；
2. consumer graph 后提交；
3. external resource barrier 保证资源状态和 memory dependency。

一个 `QueueSyncInfo` 只能连接一个 producer-consumer graph 对。由于 Binary semaphore 只能被 wait 一次，不能把同一对象扇出给多个 consumer graph。

### 所有权和一次性检查

`QueueSyncInfo` 使用两个 `QueueDependency` 表示同步方向。对象不可复制、不可移动，以保证提交期间绑定引用稳定。dependency 只临时保存 producer 和 consumer submission 之间的待消费同步 handle；底层 semaphore 的分配和回收全部由 `CommandQueue` 负责。

内部维护两个使用标志：

```cpp
ENTERING_USED
LEAVING_USED
```

约束：

- 作为 entering 最多使用一次；
- 作为 leaving 最多使用一次；
- 同一个 Execute 中不能同时作为 entering 和 leaving；
- 同一个 Execute 内被多个资源重复引用时，按对象地址去重，视为一次使用；
- 第二次跨 Execute 使用时直接报错；
- 不提供 reset 或复用接口，新的 graph 连接必须创建新的对象。

entering 和 leaving API 都接受 `QueueSyncInfo&`。signal 会产生 pending dependency，wait 会消费并清空 dependency，因此两种操作都会修改同步对象。

对象必须保持有效，直到 producer signal 和 consumer wait 都提交完成。正常的 consumer wait 会将底层 handle 移交给 `CommandQueue`，由 completion fence 或 queue drain 延迟回收。`QueueDependency` 析构时必须为空；仍有未消费的 dependency 会直接报错，不执行等待或资源兜底。

## ExecuteInfo

所有 QueueSyncInfo 都属于单次 Execute，由临时的 `ExecuteInfo` 统一提供。

```cpp
struct ExecuteInfo
{
    void AddEnteringQueueSyncInfo(QueueSyncInfo&);
    void AddLeavingQueueSyncInfo(QueueSyncInfo&);

    void AddExternalBufferQueueSyncInfo(
        const std::string& name,
        QueueSyncInfo* entering,
        QueueSyncInfo* leaving);

    void AddExternalImageQueueSyncInfo(
        const std::string& name,
        QueueSyncInfo* entering,
        QueueSyncInfo* leaving);

    void SetGraphicsCompletionFence(HostFence&);
    void SetComputeCompletionFence(HostFence&);
};
```

执行接口：

```cpp
void Execute(const ExecuteInfo& info);
```

`ExternalBufferInfo` 和 `ExternalImageInfo` 只描述外部资源及其 entering/leaving 状态，不保存任何 `QueueSyncInfo` 指针。

这样可以让 external resource 的同步绑定具有 transient 语义：资源配置可以长期存在，但每次执行使用的 QueueSyncInfo 都在 Execute 入口提供。

### Graph 级同步

一个 graph 可以添加多个 graph-level entering 和 leaving sync：

```text
graph entering:
    每个对象的 graphicsToCompute -> graph 的首个 compute submit wait
    每个对象的 computeToGraphics -> graph 的首个 graphics submit wait

graph leaving:
    每个对象的 graphicsToCompute -> graph 的末个 graphics submit signal
    每个对象的 computeToGraphics -> graph 的末个 compute submit signal
```

### Resource 级同步

resource-level sync 使用同样的方向规则，但边界缩小到该 external Buffer/Image 实际访问的首个/末个 submit。

如果同一个 QueueSyncInfo 同时绑定 graph 级和多个 resource 级同步，需要先聚合：

- entering：每个 queue 取所有绑定目标中的最早 submit；
- leaving：每个 queue 取所有绑定目标中的最晚 submit；
- 每个方向最终只产生一次 wait 或 signal。

## SubmitBoundary

`RenderGraph::Build()` 负责计算并输出最终有效 submit 边界。Instance 不再扫描 pass 或 group 来查找 submit。

```cpp
struct SubmitBoundary
{
    uint32_t firstGraphicsSubmit = INVALID_INDEX;
    uint32_t firstComputeSubmit = INVALID_INDEX;
    uint32_t lastGraphicsSubmit = INVALID_INDEX;
    uint32_t lastComputeSubmit = INVALID_INDEX;
};
```

`BuildResult` 保存：

- graph 级 `SubmitBoundary`；
- 每个 external Buffer 的 `SubmitBoundary`；
- 每个 external Image 的 `SubmitBoundary`。

边界必须基于最终的：

- pass pruning；
- schedule 和 submit batch；
- pass group 合并；
- external prologue/epilogue barrier。

`INVALID_INDEX` 表示对应 queue 没有有效 submit，是正常状态：

- 不添加该 queue 的 wait；
- 不添加该 queue 的 signal；
- 如果四个字段都是 `INVALID_INDEX`，表示 graph/resource 没有有效访问。

必须验证 first/last 成对一致：一个 queue 不能只有 first 没有 last，或只有 last 没有 first。

建议为 `SubmitBatch::PassGroupPlan` 保存所属 `submitIndex`，并由 `BuildResult` 提供只读查询接口。资源边界应在生成 external prologue/epilogue barrier 的阶段同时记录。

## Execute 提交流程

`Execute()` 的处理顺序：

1. 验证 `ExecuteInfo` 中的 graph/resource 名称和资源类型；
2. 验证绑定资源确实是 external resource；
3. 根据 `BuildResult` 获取 graph/resource 的 `SubmitBoundary`；
4. 按 QueueSyncInfo 对象地址去重并聚合目标 submit；
5. 检查 entering/leaving 一次性使用状态；
6. 将外部 dependency 以显式 wait/signal 语义加入对应 submit 的 `CommandQueue::SubmitInfo`；
7. 将 graph 内部 queue-sync edge 绑定为 `QueueDependency`；
8. 按 Vulkan handle 去重每个 submit 的 wait/signal 列表；
9. 提交 graphics/compute queue；
10. 将调用者提供的 completion fence 绑定到每个活跃 queue role 的最后一次 submit。

一个 submit 中不能重复出现相同的 Binary semaphore。一个 semaphore 只能有一个 signal 和一个 wait。

## 异步执行和完成

`RenderGraphInstance` 只允许一个尚未完成的 Execute。它不在 graph 内部等待 GPU，而是要求调用者为 graph 实际使用的每个 queue role 提供一个 `HostFence`：

```text
Execute(info)
    -> 异步提交 command
    -> 最后一次 graphics/compute submit 分别绑定 caller-owned fence
    -> instance 标记 in-flight

下一次 Execute/Compile
    -> Poll 上一次提交的 fence
    -> 全部完成后清除 in-flight

析构
    -> 等待仍在 flight 的 caller-owned fence
    -> 再销毁 graph 内部 GPU 资源
```

同时使用 graphics 和 compute 时必须提供两个不同的 fence，即使两个 role 映射到同一个实际 `VkQueue`。这些 fence 必须存活到 Instance 在下一次 `Execute`/`Compile` 中观察到完成，或者存活到 Instance 析构；仅在外部等待完成后立即销毁 fence 仍会留下悬空引用。
Instance 观察完成前，其他 Instance 或直接 queue submit 不能复用已绑定的 fence；manager 会拒绝这种覆盖旧 completion frontier 的操作。

`QueueSyncInfo` 不由 Instance 回收。它的生命周期由连接两端的调用者管理，必须覆盖 producer signal 和 consumer wait 的 GPU 使用周期。

`CommandQueueManager` 为每个实际 `VkQueue` 维护递增 submission version 和 `SubmissionFrontier`。dependency signal 保存 producer frontier，consumer wait 将其合并到自己的 submission frontier。某个 `HostFence` 完成后，manager 合并该 fence 的 frontier，并回收所有已被完成 frontier 覆盖的 consumer-wait semaphore。这个模型不使用 timeline semaphore。

## 跨 Graph 使用示例

```cpp
QueueSyncInfo sync;
HostFence producerGraphicsDone;
HostFence producerComputeDone;
HostFence consumerGraphicsDone;
HostFence consumerComputeDone;

// Producer graph
ExecuteInfo producerInfo;
producerInfo.AddLeavingQueueSyncInfo(sync);
producerInfo.SetGraphicsCompletionFence(producerGraphicsDone);
producerInfo.SetComputeCompletionFence(producerComputeDone);
producer.Execute(producerInfo);

// Consumer graph
ExecuteInfo consumerInfo;
consumerInfo.AddEnteringQueueSyncInfo(sync);
consumerInfo.SetGraphicsCompletionFence(consumerGraphicsDone);
consumerInfo.SetComputeCompletionFence(consumerComputeDone);
consumer.Execute(consumerInfo);

consumerGraphicsDone.Wait();
consumerComputeDone.Wait();
```

producer 的 Execute 必须先提交，consumer 的 Execute 后提交。这样同 queue 可以依靠提交顺序，跨 queue 则使用 `QueueSyncInfo` 中对应方向的 semaphore。

## 验证和错误处理

需要报错的情况：

- graph/resource 名称不存在；
- Buffer/Image 类型与 typed binding 不匹配；
- 非 external resource 添加 external sync；
- SubmitBoundary first/last 不一致；
- 一个 QueueSyncInfo 在多个 Execute 中重复作为 entering；
- 一个 QueueSyncInfo 在多个 Execute 中重复作为 leaving；
- 同一个 Execute 中同时作为 entering 和 leaving；
- 需要 signal/wait 的 submit 不存在；
- submit 内出现重复 semaphore handle；
- 活跃 queue role 没有提供 completion fence；
- graphics 和 compute 使用同一个 completion fence；
- 单个 RenderGraphInstance 在前一次 Execute 未完成时再次 Execute。

## 测试清单

- graphics-only graph 的 boundary 和 sync 绑定；
- compute-only graph 的 boundary 和 sync 绑定；
- graphics/compute 双 queue graph 的 `graphicsToCompute` 和 `computeToGraphics`；
- 同 queue graph 链路只使用 barrier 和 submit order；
- 跨 queue graph 链路不依赖 RenderGraph 的 CPU wait API；
- 多个 graph-level entering sync；
- 多个 graph-level leaving sync；
- 多个 resource-level sync；
- 同一 QueueSyncInfo 被多个资源引用时只生成一次 wait/signal；
- graph-level 和 resource-level 同时引用同一对象时正确聚合；
- `INVALID_INDEX` queue 边界被正确跳过；
- 重复使用 QueueSyncInfo 被检测；
- Execute 异步提交，caller fence 完成后恢复可执行状态；
- 现有 graph 内部 queue-sync edge 行为不受影响。

## 回收实现

1. `MyDevice` 持有一个 `CommandQueueManager`；graphics、compute、transfer role 根据 `(queueFamilyIndex, queueIndex)` 映射到最多三个实际 queue state。
2. 每个实际 queue state 的 `m_tailFrontier` 自身分量就是该队列的 submission version，不维护额外的全局或 static 计数器。
3. consumer wait 成功提交后，binary semaphore 以 consumer queue/version 进入 retired 队列。
4. `HostFence::Poll/Wait` 确认完成时推进 manager completed frontier，并释放满足版本条件的 retired semaphore。
5. manager 销毁时等待 device idle，并在 semaphore allocator 销毁前清空剩余 retired semaphore。

## 暂不包含

- 一个 Binary semaphore 向多个 consumer graph 扇出；
- relay/join submit；
- external resource 的 queue-family ownership transfer；
- 多个未完成 Execute 的 frame-in-flight 管理。
