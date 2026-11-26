# LearnVulkan2 项目审查 · 2026-11-26

基线为 `ee4f5aa`。审查覆盖第一方 RHI、工具、测试、构建脚本、课程和文档。八个 Git 子模块均位于仓库指定的提交，依赖审查集中在本项目实际调用的接口和构建配置。

## 已修复

| 范围 | 可观察的变化 |
| --- | --- |
| 录制与提交 | 录制线程的异常返回调用方。录制或提交失败后，其余命令按原顺序保留，重试成功后才移交资源引用。围栏识别所属上下文，跨队列的下游工作完成时会检查生产者原生围栏是否可以回收。原先未执行的队列测试已通过公开提交接口接入回归套件。 |
| 资源与管线 | 临时描述符集按实际资源代际、采样器值和绑定参数复用。移动 Buffer 或复用旧原生句柄时，缓存会区分新旧资源。VMA 落实调用方要求的 Host 内存属性。同步光追管线入口拒绝延迟操作，三类管线缓存登记失败时会销毁刚创建的管线。 |
| 设备与窗口 | 设备初始化失败会清理已创建对象，重复销毁可安全返回。通用图形和计算队列可以承担传输。交换链使用实际选择的图形和呈现队列族，导入图像报告正确的 tiling，重建后重新获取图像时采用当次的信号量。 |
| 调度与工具 | 多前置任务的依赖节点在容器增长后仍有效。ImGui 纹理上传与图形队列的其他 Host 操作互斥，含百分号的文字按原文显示。二进制读取失败保留原输出，提取扩展名只检查文件名。反射快照提取元数据后释放解析器。 |
| 构建与清理 | 各构建目录独立保存程序、资源和生成的配置头。着色器按源码及其包含文件增量编译。删除了重复哈希实现、未参与构建的脚本、旧 CMake 缓存和无效的 OpenVDB 安装步骤。历史审查的源码链接固定到原提交。 |

## 待审查

| 事项 | 依据与需要决定的内容 |
| --- | --- |
| 恢复或退役 RenderGraph | 默认关闭。启用后，MSVC 在已移除的 `GetCommandQueueManager` 等接口处编译失败。恢复时需确定谁传入 `DeviceContext`，以及异步录制结束前谁持有命令和资源。[执行路径](../src/utility/render_graph/render_graph_instance.cpp) |
| 完成回调的异常契约 | 一个回调抛出会跳过其后的回调，从析构路径抛出还会终止进程。需要明确要求回调不抛异常，或定义逐个执行、收集错误和析构期报告的行为。[提交回调](../src/my_vulkan/render_context/private/submission_manager.cpp) · [帧回调](../src/my_vulkan/render_context/private/frame_slot.cpp) |
| 管线缓存碰撞 | Graphics、Compute、RT 分配器及 Graphics Program 的本地缓存只用哈希值判断命中。不同创建状态碰撞时会返回错误管线。本次未观测到实际碰撞。需确定原生 create-info 和程序内状态各自保存哪些字段以核对完整键，再加入强制碰撞测试。[设备侧](../src/my_vulkan/allocator/private/pipeline_allocator.cpp) · [程序侧](../src/my_vulkan/pipeline/private/graphics_shader_program.cpp) |
| 最低设备能力 | 普通窗口初始化也请求 Mesh、RT、Ray Query 和 Bindless。若目标包括仅需传统光栅或计算的设备，需确定各能力组哪些必选、哪些可选，以及调用方怎样查询已启用能力。[设备选择](../src/my_vulkan/device.cpp) |
| 录制线程和队列能力 | 调度器需要显式初始化。enkiTS 对任务集提交和未完成任务的等待有线程要求。另一个边界是纯传输队列，当前对 CLAS/PTLAS 有专门校验，任意录制回调缺少可供预检的命令类型。需明确普通调用线程和自定义回调要获得哪些保证。[调度器](../src/utility/task_scheduler.cpp) · [命令校验](../src/my_vulkan/command/command_buffer.cpp) |
| 帧结束的同步开销 | 空帧也为 Graphics、Compute 和 Transfer 三种角色各提交一个完成标记，同一原生队列可以兼任多个角色。上下文和提交管理器析构各有一次设备空闲等待。可先测量提交次数与耗时，再确定哪些标记能按原生队列合并，并保留回调和失败重试语义。[帧结束](../src/my_vulkan/render_context/device_context.cpp) · [管理器析构](../src/my_vulkan/render_context/private/submission_manager.cpp) |
| 长运行中的 framebuffer 缓存 | 新 attachment 代际产生新缓存项，原生 framebuffer 按当前契约保留到设备销毁。频繁缩放或替换 attachment 的应用会累积条目。若要设置上界，需确定 GPU 完成后由谁触发按代际回收。[缓存与销毁](../src/my_vulkan/allocator/private/render_target_cache.cpp) |
| 对外构建目标与可选测试 | Host 有独立的 `rhi_host` target，完整运行时仍与测试入口链接成可执行程序。外部工程是否需要稳定的 RHI library target，取决于接入目标。另有一个 shader binding smoke 依赖仓库未跟踪的测试源码，显式开启时配置会指出缺少的文件。需要决定是否随仓库交付这套测试。[构建入口](../CMakeLists.txt) |
| 根目录的 `vert.spv` | 仓库中未找到引用或对应源码。本机 SDK 1.4.309 的反汇编器无法识别其中的 capability 5128。若它是有意保留的测试样本，应补充来源及工具版本；若是临时输出，可以删除。[二进制文件](../vert.spv) |

## 验证范围

- Windows x64、MSVC 19.52。完整 Debug 的 CTest 通过 2/2，完整 Release 通过 2/2，独立 Host Release 通过 1/1。另外在 Debug CRT 注入一次管线缓存登记分配失败，修复前创建的原生管线被销毁 0 次，修复后恰好销毁 1 次。
- RTX 3060 Ti，驱动 621.24。CLAS 与 PTLAS 均执行真实 GPU 构建并通过。窗口探针使用 ImGui 录制、提交并呈现 10 帧，覆盖调整窗口尺寸后的交换链重建和重复设备销毁。PTLAS 的遍历命中与窗口像素均未做回读比较。
- 用带空格的路径测试着色器首次构建、无修改重建和仅修改 GLSL 包含文件三种情况。文档检查覆盖仓库内文件、章节锚点及历史提交中的源码路径。
- Linux、macOS、其他 GPU 型号及第三方子模块的上游测试套件不在本次实机验证范围内。
