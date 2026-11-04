# ComputeShaderProgram 创建与生命周期迁移

## 实现

- 新增 `MyDevice::CreateComputeShaderProgram(program, finalizedInfo)`，同步完成创建。
- 移除 Program 公开 Create/Destroy，不增加 Reset；保持不可复制、不可移动。
- 创建前校验 Finalize、空目标、Device、三类 allocator 初始化和 push constant 设备限制。
- 私有 `_Create` 只在临时 Program 上运行；成功后无异常地转交成员，失败保持输出不变并允许重试。
- 析构调用私有 `_Destroy`，释放自有 ShaderModule、清空布局元数据与 pipeline 借用。
  native pipeline/layout 继续由 Device 缓存持有；已发布的缓存项在外层失败后保留。
- 空对象析构不访问 Device；成功对象在 Device 和相关缓存销毁前析构。

## 同步与边界

调用者同步两类布局缓存、Compute pipeline 缓存以及传入的 VkPipelineCache。
析构沿用 DestroyPipeline，它先查 Graphics 再查 Compute，前面未命中时继续查 RayTracing；
这些间接缓存访问也需与冲突操作同步。没有内部锁、线程亲和性或延迟释放队列。

创建与析构不等待 GPU，不改变录制、提交或 GPU 资源保活语义；调用者在释放前完成 native 使用。
本轮保留 allocator 算法及其缓存键策略，保留其他类接口，不自动提交或推送。
保留工作区原有 CMakePresets.json 修改。

## 回归覆盖与验证

- 未 Finalize、非空目标、缺失及未初始化的 allocator、无 Device 和设备限制越界。
- ShaderModule、descriptor-layout、pipeline-layout、Compute pipeline 创建失败与同输入重试。
- 稀疏 set 反射、独立输出、源 CreateInfo 清空与销毁后数据有效。
- 正常退出、异常退出、unique_ptr.reset 自动释放 ShaderModule；缓存借用不被析构回收。
- 现有 native cache 命中、缓存销毁和重复清理不重复释放。
- 编译期验证 Program 无公开 Create/Destroy/Reset，且不可复制、不可移动。

按下列顺序运行，完整构建与 Host 构建共享部分输出，不能并行：

```powershell
.\build_command_buffer\run_finalize_cmake.cmd --build build_finalize_nmake
ctest --test-dir build_finalize_nmake --output-on-failure
.\build_command_buffer\run_finalize_cmake.cmd --build build_finalize_host
ctest --test-dir build_finalize_host --output-on-failure
git diff --check
```

沿用 [PipelineLayout 清单](pipeline_layout_migration_plan.md) 中的本地 MSVC/NMake
验证配置；已忽略的旧 shader_binding_tests.cpp 不参与该构建，真实 GPU 路径需另外验证。
完整构建与 CTest（2/2）、Host 独立构建与 CTest（1/1）均已通过。
后续 Graphics 与 RayTracing Program 迁移也已完成；现有三类 Program 均使用
MyDevice 创建入口和析构释放，具体同步约定见 [资源生命周期](resource_lifetimes.md#graphics-and-raytracing-programs)。
