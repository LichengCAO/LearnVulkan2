# LVK2 外部项目接入问题与后续修改清单

记录日期：2026-10-26。

本文依据 `VirtualGeometry` 三角形示例的实际接入过程，以及对 LVK2 的只读复核整理。本文不实施代码修改。

## 基线与证据范围

- LVK2 固定依赖提交：`76dff077114552d864d01101afb23c2889760b63`。
- 消费项目：`E:\GitStorage\VirtualGeometry`，三角形实现提交 `9c5bb05`，改用 Framebuffer 封装的提交 `b80653a`。
- 环境：Windows、Visual Studio 2022、MSVC 19.38、Vulkan SDK 1.4.350.0；Intel UHD + NVIDIA GeForce RTX 3060 Laptop GPU。
- 已通过：Debug 构建、120 帧绘制提交、两次窗口缩放及交换链重建。测试使用 NVIDIA 驱动筛选参数，运行期错误计数未报告 Vulkan validation error。
- 证据边界：测试没有做像素回读比较；窗口截图工具超时，未完成肉眼画面确认。应用的错误计数 messenger 在设备创建后安装，不能据此宣称初始化及销毁阶段也已全面验证。
- 本地 `LearnVulkan2` 有两个原有的未提交头文件修改，未纳入消费项目。后续修复应基于明确提交验证。

状态约定：**已确认**表示实际构建/运行遇到，或调用链直接证明；**待定位**表示现象已复现但根因未证实；**设计改进**表示目前可用，但接入成本或生命周期契约不理想。优先级是建议实施顺序，不表示所有条目都已造成运行故障。

## 1. P1：Framebuffers 缓存失效范围过大，包装对象缺少有效性契约

状态：**已确认的实现行为；连带失效风险由调用链推导，尚未专门构造 GPU 复现。**

相关位置：

- [Framebuffer 与 FramebufferCreateInfo](../src/my_vulkan/pipeline/render_pass.h)
- [Framebuffer::Create / Destroy](../src/my_vulkan/pipeline/private/render_pass.cpp)
- [FramebufferAllocator::FreeFramebuffer / Destroy](../src/my_vulkan/allocator/framebuffer_allocator.cpp)
- [MyDevice::DestroyImageView / _ResetFramebufferAllocator](../src/my_vulkan/device.cpp)

实际调用链：

```text
Framebuffer::Destroy()
  -> MyDevice::DestroyFramebuffer()
  -> FramebufferAllocator::FreeFramebuffer()
  -> 仅清除传入句柄，不立即销毁缓存中的 VkFramebuffer

ImageView::_Destroy()
  -> MyDevice::DestroyImageView()
  -> _ResetFramebufferAllocator()
  -> 销毁整个 framebuffer 缓存，然后重新初始化 allocator
  -> vkDestroyImageView()
```

**更正之前的结论：缓存并非只在设备销毁时释放。销毁 image view 也会清空缓存；交换链重建销毁旧 image view 时会走到这条路径。** 不能把当前实现直接描述成“缩放必然持续泄漏 framebuffer”。

更具体的问题是：

1. 销毁一个与其他 framebuffer 无关的 image view，也会销毁全部缓存 framebuffer。
2. 其他存活的 `Framebuffer` 包装对象不会被通知，仍可能返回已经销毁的非空句柄。
3. 全局清理不会在这里检查其他 framebuffer 是否仍被 GPU 使用。仅保证当前 image view 不在使用，不足以保证全局清理安全。
4. `Framebuffer::Destroy()` 的逻辑释放和底层缓存清理语义没有在公共接口中明确说明。

建议修改：明确缓存所有权、引用关系与 GPU 完成条件；按 attachment 依赖失效，或按资源代际管理。只回收实际失效且 GPU 已完成使用的条目，防止其他包装对象静默失效。缓存如仍使用 hash 索引，应比较完整 key，避免仅凭 hash 判定同一配置。

验收：创建两个 attachment 独立的 framebuffer，销毁一组 image view 后，另一组仍可安全绘制；重复缩放后不复用失效句柄；资源回收有界且通过 validation。不要通过对每次 image view 销毁都执行全设备 WaitIdle 来掩盖依赖管理问题。

消费项目现状：已经使用 `FramebufferCreateInfo` 和 `Framebuffer`，缩放前等待 GPU、销毁本项目所有包装对象，再重建交换链。最初直接创建裸 framebuffer 是消费端的选择，不代表 LVK2 缺少封装，不应继续采用这一绕行方式。

## 2. P1：ExternalCommand::Record 有声明但没有定义

状态：**已确认，真实链接失败。**

相关位置：[command.h](../src/my_vulkan/command/command.h)、[command.cpp](../src/my_vulkan/command/command.cpp)。

`ExternalCommand` 提供回调参数、`SetParameters()` 和虚函数 `Record()` 声明，但未找到其定义。三角形最初实例化它时产生 `LNK2001`，未解析符号为 `ExternalCommand::Record(VkCommandBuffer) const`。

建议修改：补齐 Record，验证命令缓冲和回调后调用回调；说明回调捕获对象的生命周期、异步录制线程以及异常如何传回提交线程。若不准备支持此接口，应移除或明确禁止使用，而不是保留可编译、不可链接的公共类。

验收：一个独立消费 target 只通过公共头文件实例化 ExternalCommand，能链接并经 DeviceContext 录制执行；空回调的行为有明确约定。

消费项目现状：使用项目自身的 `DrawTriangleCommand : Command` 录制 viewport、scissor 和 draw。

## 3. P1：设备初始化在本机未筛选驱动时崩溃

状态：**待定位。**

相关位置：[MyDevice::_InitVolk / _CreateInstance / _SelectPhysicalDevice / _CreateLogicalDevice](../src/my_vulkan/device.cpp)。

在 VirtualGeometry 根目录复现：

```powershell
.\build\bin\Debug\vg_triangle.exe --frames 120 --resize-test
```

本机现象：进程在输出 GPU 名称前访问冲突，CTest 报告 `SEGFAULT`。临时调试器捕获异常码 `0xC0000005`，异常地址解析到驱动符号 `SetDriverStoreDir` 附近，但没有取得足以定位根因的完整调用栈。

只对该进程筛选 NVIDIA Vulkan 驱动后可通过：

```powershell
.\build\bin\Debug\vg_triangle.exe --frames 120 --resize-test --driver "*nv-vk64.json"
```

该参数在 Vulkan 初始化前设置进程环境变量 `VK_LOADER_DRIVERS_SELECT`。这证明筛选驱动是本机有效的临时措施，**不能证明 Intel 驱动、LVK2、vk-bootstrap 或某个 feature 请求就是根因**。初始化时还观察到 OBS Vulkan layer 的版本警告，也不能仅据此归因。

建议排查：为初始化各阶段增加可选诊断日志；保存完整 crash stack 和模块版本；对比最小原生 Vulkan / vk-bootstrap 的窗口初始化；分别隔离 validation、隐式 layer、驱动枚举、surface/present 查询及 feature 请求。

验收：明确触发调用和责任范围，正常枚举设备或给出可操作的错误。驱动筛选保留为诊断选项，不应把 NVIDIA 文件名硬编码成库默认行为。

## 4. P1：普通图形程序被强制要求支持 Mesh、RT、Ray Query 等特性

状态：**已确认的配置行为。**

相关位置：[MyDevice::_SelectPhysicalDevice 和 _Add*ExtensionsAndFeatures](../src/my_vulkan/device.cpp)。

当前初始化无条件请求 mesh/task shader、ray tracing pipeline、acceleration structure、ray query、bindless，以及 geometry shader、shaderFloat64 等基础配置中的特性。仅画传统三角形也必须满足这些条件。`bufferDeviceAddress` 的请求位于 RT 配置路径，未来拆分能力时尤其容易遗漏。

建议修改：提供初始化配置，区分 required / optional feature groups；BDA 等通用能力独立表达；公开“支持”和“实际启用”能力及相关 limits。某个设备不满足请求时，输出具体缺少的扩展/feature，而不只打印总括错误。

验收：传统 triangle profile 可以在缺少 mesh/RT 的合格设备上运行；mesh profile 不要求 RT；请求不支持的能力得到清晰错误。使用 indirect count、多 draw、压缩数据类型之前，核对应用实际启用了对应 feature。

注意：vk-bootstrap 的 feature-chain 实现会合并同 sType 的请求；不要未经检查就把多次添加 Vulkan12Features 当成重复 pNext 结构缺陷。

## 5. P1：没有可直接消费的 CMake library target

状态：**已确认的构建限制。**

相关位置：[根 CMakeLists.txt](../CMakeLists.txt)、[第三方构建入口](../external/CMakeLists.txt)、[资源构建](../res/CMakeLists.txt)。

LVK2 顶层只创建可执行程序，将运行时与测试入口混合收集。外部项目无法直接链接一个稳定的 LVK2 target，当前三角形只能自行列出运行时源码和多个 include 路径，并直接引入 LVK2 的 third-party CMake。这使消费端依赖内部目录结构。

同时存在的接入成本：

- shaderc 在顶层被强制查找，而本例只需要构建时的 glslangValidator 与运行时 SPIR-V 加载。
- 部分裸文件名 include 依赖全局式 include directory，消费端需补齐多个子目录。
- `config.h` 配置输出位于源码目录，资源宏及输出目录与项目目录绑定。
- 顶层使用 `CMAKE_PROJECT_NAME` 创建 target，不适合作为其他 project 的子目录直接复用。
- 资源 shader 编译入口依赖 `VK_SDK_PATH` 和 Windows 工具路径，消费端另写了 SDK 工具发现及多配置输出规则。

建议修改：提供 `lvk2` / `LVK2::lvk2` target 和目标级 include/compile/link 配置，拆分 examples、tests、runtime shader compiler；生成文件放 binary directory。最初可先支持 add_subdirectory，是否提供安装包另行决定。

验收：全新外部工程能通过一个 target 接入，不 glob LVK2 私有实现、不复制 include 清单、不编译 LVK2 测试入口；不启用运行时编译时无需 shaderc；配置构建不使 submodule 工作区变脏。

## 6. P2：窗口、validation、任务系统和设备生命周期配置分散

状态：**设计改进，部分失败清理路径尚未验证。**

相关位置：[MyDevice::Create / Destroy / _InitGLFW / _CreateInstance](../src/my_vulkan/device.cpp)、[task_scheduler.h](../src/utility/task_scheduler.h)、[DeviceContext](../src/my_vulkan/render_context/device_context.h)。

当前窗口尺寸 800×600、标题 My Vulkan、应用名 Hello Triangle 和 validation 请求写在设备内部。消费项目另行改窗口标题，并额外创建 debug messenger 计数。任务系统也必须由应用显式初始化，生命周期顺序需自行掌握。

`Create()` 按顺序创建多种资源，直到最后才置 `m_initialized=true`；析构仅在该标志为真时调用 Destroy。源码未提供统一的初始化失败回滚。中间步骤失败后已创建窗口、instance 或 device 的清理需要专项验证。`glfwInit()` / `glfwCreateWindow()` 的返回值也缺少直接检查。

建议修改：提供窗口、应用信息、validation 与日志回调配置；明确 scheduler 所有者及创建顺序；初始化采用可回滚资源管理，允许安全清理部分创建状态。提供覆盖初始化、运行和销毁全过程的 validation 收集方式。

验收：注入窗口、instance、device、shader 创建失败后无残留资源；成功退出与关闭窗口均安全；业务端无需重复实现框架诊断逻辑。

## 7. P2：资源包装对象的显式 Destroy 契约不统一

状态：**已确认的接口差异。**

相关位置：[render_pass.cpp](../src/my_vulkan/pipeline/private/render_pass.cpp)、[shader.cpp](../src/my_vulkan/pipeline/private/shader.cpp)、[image.cpp](../src/my_vulkan/resource/image.cpp)、[graphics_shader_program.cpp](../src/my_vulkan/pipeline/private/graphics_shader_program.cpp)。

Framebuffer、ShaderModule 等对象析构时检查句柄已为空，需要调用者显式 Destroy；Image 等类型则在析构时调用 Destroy。这两种模式各自可以成立，但混用且缺少统一说明，会使消费端的异常清理和容器所有权处理容易出错。

建议修改：统一或明确区分“拥有资源、借用资源、缓存引用”的生命周期，文档说明 GPU 完成前不能释放。不要把改成析构立即释放简单等同于解决 GPU 生命周期问题。异步录制使用 borrowed Command 时，也应继续明确其必须存活到录制完成的约定。

验收：公共资源类型有一致的使用示例；正常退出、异常退出和重复 Destroy 的行为明确；容器移动/复制不会重复管理底层句柄。

## 8. P1：测试基线不完整，已跑通的路径不能从干净克隆复现

状态：**已确认。**

相关位置：[.gitignore](../.gitignore)、[CMakeLists.txt](../CMakeLists.txt)、[common.h](../src/common.h)。

- `src/shader_binding_tests.cpp` 被忽略且未跟踪。此前 CPU/GPU smoke 依赖这个本地文件，干净克隆不会自动取得它。
- `MY_VULKAN_ENABLE_RENDER_GRAPH` 默认是 0；RenderGraph 源码和相应测试受此宏保护，常规测试通过不证明 RenderGraph 可构建或可执行。
- 当前 GPU smoke 覆盖 compute、传统离屏绘制和可选 RT；mesh 管线有编译/mock 检查，但不等于实际 mesh draw 验证。

建议修改：将需要长期维护的测试与 shader fixtures 正式纳入仓库；通过 CMake 选项显式控制 Graph 和 GPU 测试，区分跳过与通过；增补真实 mesh 绘制和 GPU-driven indirect 链路的测试。

验收：干净递归克隆后可复现文档中的测试；Graph 开关两种构建均验证；mesh/RT 不支持时报告具体 skip 原因，不静默冒充覆盖。

## 9. P1：RenderGraph 的 task/mesh stage 转换条件无效

状态：**代码复核确认，三角形未使用此路径。**

相关位置：[render_graph_instance.cpp 的 _ToStageFlags](../src/utility/render_graph/render_graph_instance.cpp)、[render_graph.cpp 的 stage 排序](../src/utility/render_graph/render_graph.cpp)。

代码使用 `#ifdef VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT` 和 mesh 对应标识符。当前 Vulkan SDK 中它们是 `static const VkPipelineStageFlagBits2`，不是预处理宏，因此对应分支会被排除。只传 task/mesh stage 时可能转换成 0；混合 stage 时可能遗漏必要阶段。RT/AS 的同类 guard 也需要一并审计。

建议修改：以真正的版本/扩展宏做编译保护，或在所要求头文件版本下直接转换；优先统一使用 synchronization2，避免往返转换损失语义。若保留 legacy 路径，必须明确所有支持 stage/access 的映射并拒绝未处理标志。

验收：分别传入纯 task、纯 mesh 和混合 stage，生成合法且包含预期作用域的 barrier；实际 compute-write → mesh-read 能通过同步验证。

## 10. P2：GPU-driven 所需的资源访问和绑定能力仍需补齐

状态：**已确认的接口限制；不是本次传统三角形的阻塞项。**

相关位置：[RenderGraph::PassInfo](../src/utility/render_graph/render_graph.h)、[资源访问声明实现](../src/utility/render_graph/render_graph.cpp)、[ShaderProgramReflection](../src/my_vulkan/pipeline/private/shader_program_reflection.cpp)。

- Graph 目前提供 uniform read 和 storage read/write 声明，storage buffer 统一标记成读写；缺少只读 SSBO、indirect read、transfer 等明确表达。
- compute 写 indirect 参数后的消费者应是 DRAW_INDIRECT + INDIRECT_COMMAND_READ，不能用 storage read/write 代替。BDA 访问的数据也要显式纳入依赖图。
- 反射绑定路径明确拒绝 runtime descriptor arrays；设备启用 descriptor indexing 并不表示上层 bindless 路径已经完成。
- 原生 draw/dispatch 可以通过项目自己的 Command 接入，但常用命令缺少统一入口，使消费端反复书写少量相同胶水代码。

建议修改：先补 generic buffer access 与 indirect read，验证 compute → indirect mesh draw 闭环；bindless 作为独立可选能力实现。最初 meshlet 数据可放少量大 SSBO，无需等完整 runtime descriptor arrays 才开始项目。

验收：声明可准确区分只读/写入/间接读取，自动产生正确同步；meshlet 全驻留原型先跑通，后续再验证运行时 descriptor 容量与更新语义。

## 建议实施顺序

1. 补齐 ExternalCommand 定义；修复 framebuffer 缓存失效范围及生命周期契约。
2. 提供最小 graphics 能力配置和初始化诊断，定位本机双显卡崩溃。
3. 提供可复用的 runtime CMake target，纳入必要测试，固定干净克隆的验证基线。
4. 恢复 Graph 可选构建，修正 stage 转换，补齐 buffer access 与 indirect 链路。
5. 根据 meshlet renderer 的实际需要继续扩充 bindless、上传反馈、统计等能力。

每次修复应在 LVK2 独立提交并验证，再由 VirtualGeometry 主动更新 submodule 指针。不要直接在消费项目的固定 submodule 中积累未提交修补。
