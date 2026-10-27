# `my_vulkan` 封装审查记录

**审查日期：** 2026-09-11  
**审查范围：** `src/my_vulkan`  
**审查方式：** 静态代码审查

## 结论摘要

当前封装已经具备较明确的模块划分，主要包含：

- `device`：Vulkan instance、physical/logical device、swapchain、队列等设备级对象。
- `command`：command pool、command buffer、queue submit、fence、semaphore chain。
- `resource`：buffer、image、descriptor 等资源对象。
- `allocator`：VMA、pipeline、render pass、framebuffer、pipeline layout 等缓存和分配器。
- `pipeline`：shader program 与图形管线相关逻辑。

整体设计已经开始处理延迟回收、同步链和资源复用问题，但目前最需要优先明确的是：

> GPU 提交完成之前，哪些 CPU 对象、Vulkan handle 和分配器状态必须保持有效。

现阶段存在几处可能导致窗口 resize 崩溃、CPU 内存越界、GPU 使用已回收对象、command buffer 持续增长，以及缓存返回错误 Vulkan 对象的问题。

## 主要问题

### P1：Swapchain acquire 的 `OUT_OF_DATE` 分支不可达

**位置：**

- `src/my_vulkan/device.cpp:326`
- 相关处理约在 `src/my_vulkan/device.cpp:331-342`

当前逻辑先将所有非 `VK_SUCCESS` / `VK_SUBOPTIMAL_KHR` 的结果抛出，后面的 `VK_ERROR_OUT_OF_DATE_KHR` 处理因此无法执行。

窗口缩放、最小化恢复或 surface 变化后，`vkAcquireNextImageKHR` 很可能返回 `VK_ERROR_OUT_OF_DATE_KHR`。按当前控制流，这类情况会直接进入异常路径，而不是设置 swapchain 重建标记。

**建议：**

1. 先单独判断 `VK_ERROR_OUT_OF_DATE_KHR`，设置 `m_needRecreate` 并返回可处理状态。
2. `VK_SUBOPTIMAL_KHR` 通常仍可以继续使用当前 swapchain，但建议同时设置重建标记，在本帧或合适的安全点重建。
3. 将 acquire、present 和 swapchain recreate 的结果处理统一成明确的状态协议，避免每个调用点自行解释 `VkResult`。

### P1：Buffer 拷贝范围检查不完整，可能发生越界

**位置：**

- `src/my_vulkan/resource/buffer.cpp:144`
- `src/my_vulkan/resource/buffer.cpp:158`

当前 `CopyFromMemory` 一类路径只检查了 `size <= buffer.size`，没有验证：

```text
offset <= capacity
size <= capacity - offset
```

例如容量为 100、offset 为 90、size 为 20 时，单独检查 `size <= capacity` 会通过，但实际写入范围已经越过 buffer 末尾。

`CopyFromBuffer` 对源 buffer 和目标 buffer 的 offset/size 也缺少完整的双端范围检查。

**建议：**

1. 统一封装一个不会产生整数溢出的范围检查函数。
2. 对 CPU 写入、CPU 读取、GPU copy 的源和目标都做检查。
3. Debug 构建中保留更详细的 buffer 名称、容量、offset、size 信息。
4. 对 `VkDeviceSize` 与 `size_t` 的转换进行显式检查，避免 32/64 位截断。

### P1：`vkQueueSubmit2` 成功后仍执行可能抛异常的操作

**位置：**

- `src/my_vulkan/command/command_queue.cpp:347`
- 异常回滚路径约在 `src/my_vulkan/command/command_queue.cpp:415`

当前 submit 成功后仍可能执行：

- `recycleActions` 扩容；
- `std::function` 构造；
- callback 注册；
- 其他可能分配内存的容器操作。

如果这些操作抛出异常，控制流会进入 catch。但 Vulkan queue submit 已经成功，GPU 工作无法撤销。此时 `AbortSubmit()` 可能把已经被 GPU 使用的 semaphore 放回 allocator，completion fence 的内部状态也可能与真实提交状态不一致。

这是典型的“外部提交已经发生，但本地事务回滚失败”问题。

**建议：**

1. 在调用 `vkQueueSubmit2` 之前完成所有可能失败的内存分配和 callback 构造。
2. 将 submit 后的状态切换设计成 `noexcept`。
3. 提交前准备一个完整的 `SubmissionRecord`，submit 成功后只移动或标记该记录。
4. 如果 Vulkan 调用失败，才执行 abort；一旦 Vulkan submit 成功，必须进入 commit/completion 路径，不能再按未提交状态回滚。

### P1：无 fence 提交缺少完整的 command pool 生命周期闭环

**位置：**

- `src/my_vulkan/command/command_queue.cpp:149`
- `src/my_vulkan/command/command_queue.cpp:405`
- `src/my_vulkan/command/command_queue.cpp:428`
- `src/my_vulkan/command/command_pool.cpp:131`

无 fence 提交路径目前主要保存 semaphore recycle action。`WaitTillDone()` 等待后没有统一调用 `_ResetFrameCommandPools`。

而 `CommandPool::AllocateOrGetCommandBuffer` 只有在 command pool reset 后才能真正复用 command buffer。因此反复执行：

```text
Submit();
WaitTillDone();
```

可能导致 command buffer 持续分配，而不是稳定复用。

此外，队列销毁逻辑只在存在 pending recycle action 时等待队列。如果一次提交没有可回收 semaphore，但仍有 command buffer 在 GPU 执行，销毁 command pool 时就可能违反 Vulkan 生命周期要求。

**建议：**

1. 每次提交都生成统一的 completion ticket，不能只追踪“是否有 semaphore 要回收”。
2. ticket 至少关联 command pool、command buffer、fence/timeline semaphore、资源引用和延迟回收动作。
3. completion 后统一执行：
   - 回收 semaphore；
   - 释放提交期资源引用；
   - reset frame command pool；
   - 执行 callback。
4. 队列和 command pool 销毁前统一等待所有未完成 ticket。

### P2：多个缓存直接把哈希值当成唯一身份

**代表位置：**

- `src/my_vulkan/resource/descriptor_set.cpp:663`
- `src/my_vulkan/allocator/pipeline_allocator.cpp:423`
- `src/my_vulkan/allocator/render_pass_allocator.cpp:173`
- `src/my_vulkan/allocator/framebuffer_allocator.cpp:100`
- `src/my_vulkan/allocator/pipeline_layout_allocator.cpp:76`
- `src/my_vulkan/pipeline/graphics_shader_program.cpp:747`

这些缓存命中时主要依据哈希值，没有进一步比较完整的创建参数。

哈希碰撞虽然概率低，但一旦发生，缓存会静默返回错误的 Vulkan 对象。对于 pipeline、render pass、framebuffer、descriptor set layout 等对象，这类错误通常很难定位，可能表现为验证层错误、渲染结果异常或设备丢失。

**建议：**

1. 定义拥有数据的 canonical `Key` 类型。
2. 为 `Key` 实现完整 equality。
3. 使用 `unordered_map<Key, Value, KeyHash>`，让哈希只负责分桶。
4. 不要把包含短生命周期指针的 Vulkan CreateInfo 直接作为长期缓存键。
5. 对 descriptor binding、attachment、stage、format、sample count、layout 等字段进行完整归一化。

### P2：`BufferCreateInfo` 必填字段可能未初始化

**位置：**

- `src/my_vulkan/resource/buffer.h:55`
- `src/my_vulkan/resource/buffer.h:61-62`

`m_bufferSize` 和 `m_usage` 没有可靠的默认值。调用方如果忘记 setter，就可能把未初始化数据传入 Vulkan 创建流程。

**建议：**

优先采用构造函数强制传入必填参数，例如 size 和 usage。即使保留 builder/setter 风格，也应：

1. 初始化字段为安全默认值；
2. 在 `Buffer::Create` 前检查 size 非零、usage 合法；
3. 对未完成配置返回明确错误，而不是依赖 Vulkan 返回错误。

### P2：VMA 调用返回值被丢弃

**位置：**

- `src/my_vulkan/allocator/memory_allocator.cpp:65`
- `src/my_vulkan/allocator/memory_allocator.cpp:87`
- `src/my_vulkan/allocator/memory_allocator.cpp:111`
- `src/my_vulkan/allocator/memory_allocator.cpp:117`

当前未完整检查以下调用的结果：

- `vmaCreateAllocator`
- `vmaBindImageMemory`
- `vmaBindBufferMemory`
- `vmaMapMemory`

潜在后果包括：

- allocator 创建失败后仍继续使用无效状态；
- bind 失败后仍将 allocation 插入内部 map；
- map 失败后调用方对空地址执行 `memcpy`；
- Vulkan handle、allocation 和内部记录之间出现不一致。

**建议：**

1. 所有 VMA 返回值统一经过错误检查函数。
2. 只有在完整创建、绑定、映射成功后，才提交到内部容器。
3. 失败路径按创建阶段逆序释放已成功创建的对象。
4. 将 map/unmap 语义明确化，避免调用方假定 map 一定成功。

## 架构层面的建议

### 1. 建立统一的 GPU submission ownership 模型

当前 command、descriptor、resource 之间仍大量传递裸 Vulkan handle。资源类虽然已经有 `RefCounted` 基础，但 `IntrusivePtr` / `MakeIntrusive` 在实际 C++ 路径中没有形成完整的使用闭环。

建议引入类似下面的概念：

```text
SubmissionRecord
  - completion primitive
  - command buffers
  - used resources
  - used descriptor objects
  - semaphores
  - deferred destruction callbacks
```

提交时把本次 GPU 工作实际引用的资源放入 `SubmissionRecord`，completion 后再释放。这样可以把资源生命周期从“调用方认为应该还活着”变成明确的提交所有权。

### 2. 拆分 `MyDevice`

**位置：**

- `src/my_vulkan/device.h:36`

当前 `MyDevice` 同时承担：

- GLFW/input 相关职责；
- Vulkan instance；
- physical/logical device；
- queue；
- swapchain；
- VMA allocator；
- 大量 Vulkan 薄转发接口。

建议至少拆成以下边界：

```text
VulkanInstanceContext
VulkanDeviceContext
Swapchain
QueueSubmitContext
ResourceFactory
```

子模块应显式持有或注入所需依赖，减少通过全局 singleton 回取设备状态。这样也更容易做单元测试、错误隔离和多设备扩展。

### 3. 强类型化 CreateInfo 和缓存 Key

CreateInfo builder 应区分：

- 必填参数；
- 可选参数；
- 创建后不可变参数；
- 只在调用期间有效的指针字段。

长期缓存则应使用拥有数据的 key，而不是直接保存 Vulkan CreateInfo 指针或单独保存 hash。

### 4. 统一错误处理策略

建议统一区分：

- 可恢复状态：`VK_SUBOPTIMAL_KHR`、`VK_NOT_READY` 等；
- 需要重建状态：`VK_ERROR_OUT_OF_DATE_KHR`；
- 资源耗尽：`VK_ERROR_OUT_OF_HOST_MEMORY`、`VK_ERROR_OUT_OF_DEVICE_MEMORY`；
- 不可恢复错误：其他严重 `VkResult`。

不要让每个模块独立决定“哪些结果抛异常、哪些结果继续”，否则调用方很难形成稳定的状态机。

## 建议修复顺序

1. **先修提交生命周期**
   - 统一 completion ticket；
   - 修复 submit 成功后的异常安全；
   - 保证 command pool、semaphore、fence 和资源都在 GPU 完成后回收。

2. **修复所有 Buffer 范围检查**
   - 覆盖 CPU copy 和 GPU copy；
   - 加入 offset 溢出检查；
   - Debug 构建增加上下文日志。

3. **修复 Swapchain 状态机**
   - 正确处理 acquire/present 的 `OUT_OF_DATE`；
   - 让 `SUBOPTIMAL` 进入延迟重建流程；
   - 明确 resize、minimize、surface lost 的行为。

4. **补齐 VMA 错误处理**
   - 检查所有返回值；
   - 失败时回滚内部 map 和对象状态；
   - 检查 map 返回地址。

5. **重构缓存键**
   - 从 hash-only 改为 key + equality；
   - 对指针字段做深拷贝或归一化；
   - 增加碰撞回归测试。

6. **再处理设备对象拆分和资源引用模型**
   - 明确模块边界；
   - 接通 `RefCounted/IntrusivePtr`；
   - 减少全局 singleton 依赖。

## 建议补充的测试

当前没有发现正式的 `add_test` / `enable_testing` 测试注册，也没有运行实际 GPU 路径。建议至少增加以下测试或验证场景：

- swapchain resize、最小化后恢复、surface 重建；
- `offset == capacity`、`offset + size == capacity`、越界和整数溢出；
- 连续多帧无 fence submit/wait，检查 command buffer 数量是否稳定；
- submit 前分配失败、submit 后 callback 分配失败；
- VMA bind/map 失败时内部状态是否回滚；
- 构造参数不同但 hash 可能相同的缓存 key；
- 销毁 queue、device、command pool 前存在未完成 submission；
- validation layer + GPU assisted validation；
- Debug messenger、object name 和 submission ticket 日志。

## 审查边界

本记录是基于源码阅读的静态审查结果：

- 没有修改 `src/my_vulkan` 下的源码；
- 没有运行实际 Vulkan/GPU 渲染路径；
- 没有执行正式测试套件；
- 上述问题应在修复后通过 validation layer、GPU assisted validation 和实际窗口/提交压力测试复核。