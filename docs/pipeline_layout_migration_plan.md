# PipelineLayout 迁移实施清单

## 当前状态与实施范围

本文记录已经确认、尚未实施的 PipelineLayout 迁移方案，供后续继续工作。
本轮目前只完成了代码检查，没有开始修改 PipelineLayout 实现或测试。

工作区已有前几轮迁移的未提交修改，继续实施时应在这些修改上工作，不要还原。
`CMakePresets.json` 是用户原有修改，不属于本次任务，不要修改或还原。

已经完成的相关接口：

```cpp
void MyDevice::GetOrCreateSampler(
    Sampler& outSampler,
    const SamplerCreateInfo& inCreateInfo);

void MyDevice::GetOrCreateDescriptorSetLayout(
    DescriptorSetLayout& outLayout,
    const DescriptorSetLayoutCreateInfo& inCreateInfo);
```

Sampler 和 DescriptorSetLayout 都借用 Device 缓存中的 native 句柄。
DescriptorSetLayout 另外拥有独立的绑定元数据；其公开 Create/Destroy 已移除。
用户 DescriptorPool 已直接返回 VkDescriptorSet，以显式 Free 回收；内部动态池使用
GetOrAllocateTransient，整体回收。这些已完成的行为保持不变。

本次只迁移 PipelineLayout，以及受影响的内部调用、测试和文档。
不迁移三类 Program 的公开接口，不改变录制、提交、等待或 GPU 资源保活语义。

## 目标接口与值语义

新增 Device 入口：

```cpp
void MyDevice::GetOrCreatePipelineLayout(
    PipelineLayout& outLayout,
    const PipelineLayoutCreateInfo& inCreateInfo);
```

使用方式：

```cpp
PipelineLayoutCreateInfo info;
// 设置 descriptor-set-layout 描述、push constants 或反射信息。
info.Finalize();

PipelineLayout layout;
device.GetOrCreatePipelineLayout(layout, info);
auto copy = layout;
layout = {}; // 只清空当前包装，copy 保留自己的元数据和借用句柄。
```

- 保留 PipelineLayout 封装，以及现有复制、移动和查询接口。
- 移除公开 Create/Destroy，析构改为默认实现，不增加 Reset 或其他释放别名。
- 包装保存 descriptor set layouts、push constants、bindings、名称映射和歧义名称。
- 复制、清空和析构只操作 Host 数据，不访问 Device 或 allocator。
- native pipeline layout 由 Device 缓存持有到缓存整体销毁，包装不保活 Device。
- 缓存销毁后旧 native 句柄失效，不增加有效性或代际追踪；包装仍可安全清空和析构。
- 不新增移动源状态保证，沿用现有默认移动语义。

用户已选择移除未使用的原生创建路径：删除旧 MyDevice::CreatePipelineLayout 的
两个原生重载，以及 DestroyPipelineLayout；不新增 CreateVkPipelineLayout 等替代入口，
不保留 allocation callbacks 独立创建分支。

## 实现步骤

### 1. Device 创建入口与一次性发布

主要涉及 [device.h](E:/GitStorage/LearnVulkan2/src/my_vulkan/device.h)、
[device.cpp](E:/GitStorage/LearnVulkan2/src/my_vulkan/device.cpp) 和
[PipelineLayout](E:/GitStorage/LearnVulkan2/src/my_vulkan/pipeline/pipeline_layout.h)。

- 在 PipelineLayout 和 PipelineLayoutCreateInfo 中给 MyDevice 必要的私有访问权限。
  CreateInfo 只增加前置声明或 friendship，不引入 Device 头文件，保持 Host 独立构建。
- 首先拒绝未 Finalize 的输入，保留错误消息中的 finalized 提示，且不自动 Finalize。
- 检查目标 native handle 为空、Device 有效、pipeline-layout allocator 可用；
  存在子布局描述时还需检查 descriptor-set-layout allocator 可用。
- 在任何 native 创建前，以缓存的设备属性校验全部 push constant 范围。
  不为此查询驱动，不改变现有 Finalize 校验。
- 构造临时 PipelineLayout，复制该输入的 bindings、push constants、名称映射及歧义集合，
  并准备子 DescriptorSetLayout 数组和 native handle 数组。
- 对每个子布局调用本 Device 的 GetOrCreateDescriptorSetLayout，保留空布局，
  维持稀疏 set 索引；复用 Finalize 生成的有序绑定数组。
- 使用稳定的临时数组组装 VkPipelineLayoutCreateInfo，向 allocator 获取 native layout。
- 成功后以现有 noexcept 移动赋值把临时对象发布到目标；发布前不写目标。

重复创建报错并保持原目标。任意创建异常保持空目标，允许重试。
外层创建失败时，已发布到 descriptor-set-layout 缓存的子布局继续由该缓存持有，
不要单独销毁或回滚这些可能已共享的对象。

原 [pipeline_layout.cpp](E:/GitStorage/LearnVulkan2/src/my_vulkan/pipeline/private/pipeline_layout.cpp)
只保留查询行为等仍需要的实现，不再保留对象创建和销毁逻辑。

### 2. PipelineLayoutAllocator 缓存接口

修改 [allocator 头文件](E:/GitStorage/LearnVulkan2/src/my_vulkan/allocator/private/pipeline_layout_allocator.h)
及其 [实现](E:/GitStorage/LearnVulkan2/src/my_vulkan/allocator/private/pipeline_layout_allocator.cpp)。

- 创建入口统一为 GetOrCreate(const VkPipelineLayoutCreateInfo&) -> VkPipelineLayout。
- 删除 AllocatePipelineLayout、AllocatePipelineLayoutWithResult，以及仅清空句柄的
  FreePipelineLayout。失败通过现有 VK_CHECK/异常机制报告。
- 保留 hash 分桶后的完整字段比较：structure type、flags、有序 descriptor set layouts，
  以及 push constant ranges 的 stageFlags、offset、size。
- 反射资源名称和歧义信息不进入 native 缓存键；它们属于各包装的元数据。
- 尽量在 native 创建前完成缓存条目描述数组的复制。native 创建成功后，
  缓存发布若抛出异常，立即 vkDestroyPipelineLayout 回滚本次 native 对象并重新抛出。
- 不缓存指向临时数组的指针，不增加内部锁、淘汰或逐对象释放。
- 保留内部 Create/Destroy；缓存 Destroy 后重复清理和析构不得重复销毁。

当前实现是在 native 创建后复制数组并 push_back，缺少发布失败的 native 回滚，
本轮应修复这条异常清理路径。

### 3. Program 内部调用与并发过渡契约

- Graphics、Compute、RayTracing ShaderProgram 内部的 m_pipelineLayout.Create(...)
  统一改为 device.GetOrCreatePipelineLayout(m_pipelineLayout, ...)。
- 原 m_pipelineLayout.Destroy() 位置改为 m_pipelineLayout = {}，
  保留 Program 原有创建失败清理和重试流程。
- 本轮保留 Program 的公开 Create/Destroy，不提前实现新的 Program Device 入口。

在三类 Program 的头文件与生命周期文档中明确：

- 这三类 Program 尚未完成创建接口的并发迁移，不能根据对象上仍有 Create 就推断
  不同实例可以无同步并行创建。
- 同一 Device 上的 Program 创建共享 descriptor-layout 和 pipeline-layout 缓存，
  因此当前创建调用必须由调用者协调。
- Program 的 Destroy 和 GraphicsShaderProgram::GetVkPipeline(state) 的惰性创建，
  也要与访问相同共享状态的操作互斥，不能只同步创建端。
- PipelineLayout 创建同时访问自身缓存及所需的子布局缓存；关联同步需覆盖这两者。
  不要求所有 Device 操作使用一把全局锁，独立共享状态仍可独立安排。
- 独立包装的复制、清空、析构不访问缓存；同一包装的修改仍需排他。
- Finalize 保持纯 Host 准备阶段，不引入 Device API。

后续迁移顺序已讨论为 Compute、Graphics、RayTracing Program，逐类迁入
MyDevice::CreateXxxShaderProgram 并移除相应公开 Create；这些属于后续工作。

### 4. 文档与编码规范

- 更新创建/Finalize 文档中的 PipelineLayout 示例和调用说明。
- 在资源生命周期文档中补充借用缓存、包装元数据、失败处理及两类缓存同步要求。
- 项目规范中记录 Program 的过渡期限制，避免把本次内部调用迁移误认为 Program 已支持并行。
- 新增私有成员函数使用单下划线前缀，非 void 函数使用尾置返回类型。
- 不为统一风格修改 external 下的代码。

## 测试与验收

优先扩展 [create_info_finalize_tests.cpp](E:/GitStorage/LearnVulkan2/src/create_info_finalize_tests.cpp)
已有的真实 allocator + native Vulkan 替身环境，保持 Host Finalize、稀疏布局和 Program
失败重试测试可用。测试中的 Owner<PipelineLayout> 依赖公开 Destroy，需改用普通值对象，
不能为了保留测试辅助逻辑而给生产类增加释放别名。

- [ ] 相同 native 描述命中缓存，不同 set layouts 或 push constant 字段产生独立布局。
- [ ] 空 PipelineLayout 和稀疏 set 保持有效，子空布局不被省略。
- [ ] 相同 native 描述、不同反射名称复用 native handle，但查询结果各自独立。
- [ ] 覆盖同名歧义、未知名称和原有绑定查询行为。
- [ ] CreateInfo 修改、Reset 或销毁后，包装保留其原元数据。
- [ ] 包装复制、移动到目标、赋空和析构不调用 native 创建/销毁 API。
- [ ] 未 Finalize、非空目标、无 Device、allocator 缺失或未初始化，在创建前被拒绝。
- [ ] push constant 超出缓存设备属性限制时，不进入 native 创建路径。
- [ ] 子 DescriptorSetLayout 创建失败、native PipelineLayout 创建失败后，目标为空且可重试。
- [ ] 外层失败不销毁已缓存子布局，已有缓存项和其他包装不受影响。
- [ ] 缓存整体销毁准确回收 native 创建，重复清理无重复销毁；包装随后析构无 Device 调用。
- [ ] 保留三类 Program 的成功创建、失败清理与重试用例。
- [ ] 不把串行测试通过表述为 Program.Create 已具备无同步并发能力。

若新增 cpp 测试文件，先重新配置完整构建，现有 CMake GLOB 不会自动发现新增文件：

```powershell
.\build_command_buffer\run_finalize_cmake.cmd -S . -B build_finalize_nmake
```

在仓库根目录按顺序执行以下验证。完整构建和 Host 构建共用部分输出，不并行运行：

```powershell
.\build_command_buffer\run_finalize_cmake.cmd --build build_finalize_nmake
ctest --test-dir build_finalize_nmake --output-on-failure

.\build_command_buffer\run_finalize_cmake.cmd --build build_finalize_host
ctest --test-dir build_finalize_host --output-on-failure

git diff --check
```

继续沿用现有 NMake 构建和工具链 helper，不切换构建系统。完成后检查剩余旧接口引用，
确认它们仅出现在历史说明中；复核失败发布、native 回滚和 Program 同步注释。
本次不自动提交或推送。
