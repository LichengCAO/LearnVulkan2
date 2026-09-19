# RenderGraph 外部资源 Queue 同步

## 目标

RenderGraph 的跨 queue 同步分为两类：

- Graph 内部的 graphics/compute 依赖由 Build 生成 `QueueSyncPlan`，Execute 使用临时 `QueueDependency` 连接对应 submit。
- Graph 边界同步绑定到具体 external resource，不提供 graph 级 semaphore 组。

同步使用 binary semaphore，不依赖 timeline semaphore。底层 semaphore 由 `CommandQueueManager` 分配，并根据 submission frontier 延迟回收。

## 外部资源声明

外部资源创建时必须固定它在 RenderGraph 内的访问 queue：

```cpp
RenderGraph::BufferInfo buffer;
buffer.SetAsExternal(RenderGraph::QueueType::COMPUTE);

RenderGraph::ImageInfo image;
image.SetAsExternal(RenderGraph::QueueType::GRAPHICS);
```

Build 会验证每个使用该资源的 pass。外部资源不能被声明 queue 之外的 pass 直接访问。因此每个外部资源只有一个有序的 submit 序列，不需要把一个 binary semaphore 扇出到多个 queue。

需要跨 queue 处理时，应先在声明 queue 上复制到 internal resource，再让 internal resource 使用现有的 graph 内 queue handoff。

## ExecuteInfo

具体资源和边界同步属于单次 Execute：

```cpp
struct ExternalBufferInfo
{
    Buffer *pBuffer;
    QueueDependency *pAcquireDependency;
    QueueDependency *pReleaseDependency;
    VkPipelineStageFlags2 enteringStage;
    VkAccessFlags2 enteringAccess;
    VkPipelineStageFlags2 leavingStage;
    VkAccessFlags2 leavingAccess;
};
```

Image 另外提供 entering/leaving layout。调用方式：

```cpp
ExecuteInfo executeInfo;
executeInfo.SetUpPass("draw", passInfo);
executeInfo.SetUpExternalBuffer("output", bufferInfo);
executeInfo.SetUpExternalImage("color", imageInfo);
executeInfo.SetGraphicsCompletionFence(graphicsFence);
executeInfo.SetComputeCompletionFence(computeFence);
instance.Execute(executeInfo);
```

`RenderGraphInstance` 构造时创建 internal resource。每次 `Execute()` 都必须通过 `ExecuteInfo::SetUpPass()` 提供所有 active pass 的 process；pass process 和 clear-value override 不会沿用上一次 Execute 的配置。包含 external Vulkan handle 的 barrier command、image view 和 framebuffer 在 `Execute()` 绑定外部资源后准备。Internal resource 会跨 Execute 保留；调用者必须在同一 Instance 再次 Execute 或销毁前完成上一次 Execute。Swapchain size 改变时，应在相关 GPU 工作完成后重建整个 Instance。

## Dependency 语义

Acquire dependency 必须持有 producer 已 signal 的 pending semaphore。RenderGraph 在该资源声明 queue 的最早访问 submit wait。

Release dependency在该资源声明 queue 的最晚访问 submit signal。调用者随后可以在外部 consumer submit 中 wait。

同一个 dependency 可以同时用于 acquire 和 release：

```text
external producer signal S0
    -> graph first submit waits S0
    -> graph last submit signals S1
    -> external consumer waits S1
```

如果 first 和 last 是同一个 submit，`CommandQueue::SubmitInfo` 会在同一次提交中 wait 旧 semaphore 并 signal 新 semaphore。

多个外部资源可以共享 dependency，但它们必须声明相同的逻辑 queue：

- acquire 聚合到所有关联资源的最早 submit，只 wait 一次；
- release 聚合到所有关联资源的最晚 submit，只 signal 一次；
- 每个资源仍保留独立的 stage、access、layout 和 barrier。

不同逻辑 queue 的资源不能共享一个 binary `QueueDependency`，即使某台设备上两个 role 恰好映射到同一个 `VkQueue`。

## Completion 和回收

调用者必须为 Graph 实际使用的每个 queue role 提供一个 `HostFence`。同时使用 graphics 和 compute 时需要两个不同的 fence。

consumer wait 成功提交后，旧 binary semaphore 以 consumer queue/version 进入 retired 队列。`HostFence::Poll()`、`Wait()`、fence 复用或 manager 销毁会推进 completed frontier，并回收已覆盖的 semaphore。

外部资源、dependency 和 completion fence 必须存活到相关提交完成。`RenderGraphInstance` 不会等待或追踪 completion fence；调用者必须在再次 Execute 或销毁 instance 前等待相关 fence。Release dependency 在被后续 consumer wait 前不能销毁。

## 当前限制

- External resource 只能由一个声明的 RenderGraph queue 访问。
- 不支持一个 binary dependency 向多个 queue fan-out。
- 不支持多个 queue 尾端自动 join 成一个 release dependency。
- External queue-family ownership transfer 尚未由 RenderGraph 自动生成；资源应使用同一 family，或由调用者满足相应的 sharing/ownership 要求。
- 调用者必须在同一 RenderGraphInstance 的上一次 Execute 完成后才能再次 Execute 或销毁该 instance。
