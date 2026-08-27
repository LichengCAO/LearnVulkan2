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
- `Sampler`

## Granite 的模式

Granite 使用 `Util::IntrusivePtrEnabled<T>` 加 `Util::IntrusivePtr<T>`。资源类通常提供线程安全引用计数、专用 deleter 和 `THandle` 别名。例如 `Buffer`、`Image`、`Sampler`、`CommandBuffer` 和 RTAS 都能通过 handle 在不同系统间共享。

引用计数只解决对象所有权和销毁时机；Vulkan API 的线程安全、设备销毁顺序以及 GPU 工作完成前不能释放资源，仍需要由上层同步保证。

## 第一批改造对象

`Buffer` 和 `Image` 都封装真实 Vulkan 资源及其内存，并已经拥有幂等的 `Destroy()`。它们还被 RenderGraph 以 `Buffer*`/`Image*` 形式保存，存在跨录制、编译和提交阶段的生命周期边界，因此最值得先改造。

当前阶段只让这两个类继承 `RefCounted`，并让析构函数调用 `Destroy()`。现有栈对象和 `unique_ptr<Buffer>`/`unique_ptr<Image>` 仍然兼容；RenderGraph 的裸指针 API 暂不改变。

## 后续候选

### `BufferView` 和 `ImageView`

它们是父资源的 Vulkan 子对象，适合返回 handle 并由父资源缓存。若 view 本身可以脱离父资源保存，view 还应持有父资源 handle，以保证父 `VkBuffer`/`VkImage` 不会提前销毁。

### 加速结构（已完成）

`BottomLevelAccelStruct` 和 `TopLevelAccelStruct` 内部持有 backing `Buffer`，并可能被跨帧或异步光追构建路径引用。TLAS 描述当前保存 BLAS 裸指针，后续 handle 化时应同时审视这条引用链。

当前两类加速结构已经继承 `RefCounted`，析构时调用幂等的 `Destroy()`；后续仍需在真正使用 `IntrusivePtr` 时统一其引用链和 BLAS/TLAS 描述的所有权语义。

### `Sampler`

Granite 将 sampler 做成线程安全 handle。`my_vulkan` 的 `SamplerAllocator` 已经维护缓存条目的 `refCount`，因此需要先决定对象引用计数是否替代、还是配合 allocator 内部计数，不能直接机械套用。

## 暂不改造的类型

- `MyDevice`、各类 allocator、`CommandQueue`、`CommandPool`：设备级独占对象，由 `MyDevice` 统一管理。
- `DescriptorSet`：实际 Vulkan descriptor set 由 descriptor pool 管理，当前对象不拥有独立销毁责任。
- `DescriptorState`、`DescriptorSetState`、各种 `CreateInfo`、pipeline state：值语义或短期构造状态。
- `CommandBuffer`：当前是命令描述数据，不拥有 Vulkan command buffer。
- `ShaderModule`、`PipelineLayout`、`RenderPass`、`Framebuffer` 和 ShaderProgram：目前主要由 program 或 RenderGraph 独占，暂时没有资源共享带来的同等收益。

## 兼容性风险

`src/my_vulkan/ref_counted.h` 中的计数器初始值为 `0`，`IntrusivePtr(T*)` 构造时再执行一次 `AddRef()`；Granite 的计数器从 `1` 开始，并采用不同的初始所有权语义。后续正式把资源容器改成 `IntrusivePtr` 前，必须统一这套规则，并补充自定义 deleter（尤其是 swapchain image）。

引用计数对象析构时会自动释放 Vulkan 资源，因此 `Destroy()` 必须保持幂等；同时必须确保 `MyDevice` 的生命周期晚于所有资源 handle。
