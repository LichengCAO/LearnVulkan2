# `my_vulkan` RefCounted 候选评审

## 结论

参照 `E:\GitStorage\Granite` 的 intrusive handle 模式，`my_vulkan` 中最适合逐步改造成 `RefCounted` 对象的是 GPU 资源，而不是所有 Vulkan 包装类型。

第一批对象是：

- `Buffer`
- `Image`
- `BottomLevelAccelStruct`
- `TopLevelAccelStruct`

它们会被 RenderGraph、描述符、视图和异步命令路径跨模块引用，生命周期不能只依赖调用方的裸指针约定。

后续候选是：

- `BufferView`、`ImageView`

## Granite 的模式

Granite 使用 `Util::IntrusivePtrEnabled<T>` 加 `Util::IntrusivePtr<T>`。资源类通常提供线程安全引用计数、专用 deleter 和 `THandle` 别名。例如 `Buffer`、`Image`、`Sampler`、`CommandBuffer` 和 RTAS 都能通过 handle 在不同系统间共享。

引用计数只解决对象所有权和销毁时机；Vulkan API 的线程安全、设备销毁顺序以及 GPU 工作完成前不能释放资源，仍需要由上层同步保证。

## 第一批改造对象

`Buffer` 和 `Image` 都封装真实 Vulkan 资源及其内存，并已继承 `RefCounted`。RenderGraph 仍以 `Buffer*`/`Image*` 形式保存它们，跨录制、编译和提交阶段的裸指针生命周期仍由调用者保证。

`Buffer` 和 `Image` 都保留公开 `Create()`，只接受空目标，并移除公开 `Destroy()`。Buffer 析构通过私有存储释放 native buffer 和 allocation；Image 析构释放缓存与当前 ImageResource 引用，外部 ImageView、描述符和命令可继续保活 image 与 allocation。不同普通资源可并发使用，见 [Buffer ownership](resource_lifetimes.md#buffer-ownership) 和 [Image ownership](resource_lifetimes.md#image-values-and-resource-versions)。栈对象和 `unique_ptr` 仍然兼容；RenderGraph 的裸指针接口不变。

## 后续候选

### `BufferView` 和 `ImageView`

它们是父资源的 Vulkan 子对象，适合返回 handle 并由父资源缓存。若 view 本身可以脱离父资源保存，view 还应持有父资源 handle，以保证父 `VkBuffer`/`VkImage` 不会提前销毁。

### 加速结构（已完成）

`BottomLevelAccelStruct` 和 `TopLevelAccelStruct` 内部持有 backing `Buffer`，并可能被跨帧或异步光追构建路径引用。TLAS 描述当前保存 BLAS 裸指针，后续 handle 化时应同时审视这条引用链。

两类加速结构都已继承 `RefCounted`，保留只接受空目标的公开 `Create()`，移除公开 `Destroy()`，由 `noexcept` 析构通过共享的私有存储依次释放 native AS 和 backing Buffer。不同 AS 实例可并发创建和最终释放，见 [BLAS ownership](resource_lifetimes.md#blas-ownership) 和 [TLAS ownership](resource_lifetimes.md#tlas-ownership)。命令、几何输入和 TLAS 中 BLAS 裸指针的保活语义保持不变。

### `Sampler`（不再作为 RefCounted 候选）

`Sampler` 现在是只保存 `VkSampler` 的可复制借用值，复制、移动、赋空和析构均不管理 native 生命周期。`SamplerAllocator` 按描述缓存句柄，由 `MyDevice` 销毁时统一回收，不再维护包装对象的引用计数。缓存访问采用外部同步；具体使用约束见 [Sampler ownership](resource_lifetimes.md#sampler-ownership)。

## 暂不改造的类型

- `MyDevice`、各类 allocator、`CommandQueue`：设备级独占对象，沿用各自现有所有权。
- `CommandPool`：不可复制、不可移动的 RAII 所有者，由作用域或外部所有者管理；FrameSlot 按队列和录制工作线程持有池，Device 必须比池长寿。
- `VkDescriptorSet`：直接使用 native 句柄，由 `DescriptorPool` 显式分配和回收，不另设 RefCounted 包装。
- `DescriptorState`、`DescriptorSetState`、各种 `CreateInfo`、pipeline state：值语义或短期构造状态。
- `CommandBuffer`：当前是命令描述数据，不拥有 Vulkan command buffer。
- `PipelineLayout`：拥有独立的 Host 元数据并借用 Device 缓存句柄，复制和析构不管理 native 生命周期，不增加 RefCounted；见 [Pipeline layout ownership](resource_lifetimes.md#pipeline-layout-ownership)。
- `ShaderBindingTable`：保持非 RefCounted、不可复制/移动的独占所有者，保留只接受空目标的 Create，由 RAII 释放 backing Buffer 和 Host 数据；见 [SBT ownership](resource_lifetimes.md#sbt-ownership)。
- `ShaderModule`、`RenderPass`、`Framebuffer` 和 ShaderProgram：保留各自现有所有权模式，本轮不增加 RefCounted。

## 兼容性风险

`src/my_vulkan/ref_counted.h` 中的计数器初始值为 `0`，`IntrusivePtr(T*)` 构造时再执行一次 `AddRef()`；Granite 的计数器从 `1` 开始，并采用不同的初始所有权语义。后续正式把资源容器改成 `IntrusivePtr` 前，必须统一这套规则，并补充自定义 deleter（尤其是 swapchain image）。

引用计数对象的最后一个强引用释放时会自动销毁其拥有的 Vulkan 资源。仍提供 `Destroy()` 的类型需保持其幂等性；已迁移的 Buffer/Image/BLAS/TLAS 通过 RAII 释放各自拥有的资源或引用。同时必须确保 Device 和 allocator 覆盖资源生命周期，以及最终 native 释放前 GPU 使用已完成。
