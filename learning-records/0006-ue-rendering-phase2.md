# UE 渲染源码学习：阶段 2

## 目标与推进方式

- 起点：2026-11-21。依据用户 11 月 8 日制定的计划，从阶段 2「进入 UE 渲染代码」开始。
- 用户当前状态：已有 UE 源码，尚未编译成功；每天约 2 小时。
- 采用约 3 周的工作量预算，按验收结果推进；编译等待时间单独记录，不视为源码学习时间。
- 每次聚焦一个主要任务：解释目的 → 用户操作或阅读 → 核对结果 → 记录证据与下一步。
- 此记录只覆盖本次 UE 学习，不替换此前 Cluster Template / PTLAS 课程记录。

阶段验收：

1. 固定一套能编译、启动并调试的 UE 源码环境和最小测试工程。
2. 结合本地源码绘制 Game Thread、Render Thread、Renderer、RDG、RHI 的关系图，区分调用关系、任务投递与 GPU 执行。
3. 从画面中的一个桌面 Pass 追到 C++、Shader 注册、参数与 `.usf` 入口，记录真实文件、函数和运行证据。
4. 定位移动渲染入口，与桌面路径对照；区分源码确认、桌面移动预览确认和 Android 真机确认的范围。

## 里程碑

| 顺序 | 学习与实践 | 验收证据 |
| --- | --- | --- |
| A：环境与一帧入口 | 完成源码编译；创建最小工程；建立调试入口；识别 Game / Render / RHI 线程职责及配置差异 | 固定提交、构建命令、启动日志、工程路径、至少一个有效断点与调用栈 |
| B：跟踪桌面 Pass | 以 Tonemap 为候选，追踪 Renderer 调用、RDG 资源声明、执行回调、RHI 命令与 Shader；实际路径以当前配置为准 | 带文件和函数名的链路图；输入输出和 Shader 参数说明；断点或 GPU 捕获证据 |
| C：识别移动路径 | 定位移动 Renderer 与后处理入口；对照桌面资源、Pass 和 Shader 选择；复述完整链路 | 桌面 / 移动对照表、修订后的链路图、约 5 分钟讲解；记录是否经过真机验证 |

阶段 3 的 Global Shader 与可开关效果改动在上述基础建立后开展。

## 已核实的环境基线

2026-11-21 只读检查结果：

- 源码路径：`E:/GitStorage/UnrealEngine`。
- `Engine/Build/Build.version`：5.8.4。
- 当前分支：`release`；HEAD：`f45729c0b6f0`。学习期间以该提交为基线，不随远端分支更新；尚未完成整个工作树改动核对。
- IDE：Visual Studio Community 2022，17.14.44。
- 已有 `UE5.sln`，未发现 `Engine/Binaries/Win64/UnrealEditor.exe`。
- 本地 UBT 日志：`E:/GitStorage/UnrealEngine/Engine/Programs/UnrealBuildTool/Log.txt`，最后修改于 2026-11-09。
- 该日志记录 MSVC 14.44.35229、Windows SDK 10.0.26100.0，末尾为 `[898/8682] Link [x64] UnrealEditor-OutputLog.dll`。
- 未在上述日志中找到明确的编译错误或最终成功 / 失败结论，不能据此认定失败原因。
- 用户目录下的 UBT 日志是旧工程记录，本课排障优先使用此源码树日志及本次保留的完整终端输出。

候选阅读入口已确认存在：

- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessTonemap.cpp`。
- `Engine/Shaders/Private/PostProcessTonemap.usf`。
- C++ 中注册了 `FTonemapVS`、`FTonemapPS`、`FTonemapCS`；实际执行哪条路径需要结合配置与运行证据判断。

## 第 1 次：保存一次可诊断的增量构建结果

目的：先获得完整结果，区分工具链、源码编译、链接与人为中断等情况。

在 PowerShell 中执行：

```powershell
Set-Location 'E:\GitStorage\UnrealEngine'
& .\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -WaitMutex -MaxParallelActions=4 2>&1 |
    Tee-Object -FilePath '.\UE-build-2026-11-21.log'
```

- 构建目标为 `UnrealEditor`，平台为 `Win64`，配置为 `Development`，对应 IDE 中编辑器的 Development 配置。
- 从 4 个并行任务开始，后续根据实际内存与构建表现调整；旧日志曾因可用内存限制为 6 个并行任务。
- 保留已有中间产物，先做增量构建。
- 首次完整构建可能超过一次学习时段；运行时长与真正的学习活动分别记录。
- 成功：记录退出结果，再进入启动编辑器与创建最小工程。
- 失败：检查本次日志中第一处实际错误及其上下文，不把最后的汇总退出码当作根因。
- 若在运行：以当前构建输出为准，等待本次构建结束，避免并发启动第二次引擎构建。

当前状态：2026-11-21 启动增量构建；截至 2026-11-22，用户请求停止后已中断。保留输出于 `E:/GitStorage/UnrealEngine/UE-build-2026-11-21.log`。本次实际开始完整 Editor 构建，UBT 报告 7557 项任务；中断前到达约 930/7557，最后任务为 `Module.RemoteControlUI.1.cpp`，未见编译错误。构建未完成，编辑器尚未生成或验收。重新运行相同命令可从已有产物继续增量构建。

2026-11-22，用户请求继续编译。确认无同目标 UBT 进程后，已重新运行相同增量构建命令（4 个并行任务上限）。本次输出保存于 `E:/GitStorage/UnrealEngine/UE-build-2026-11-22.log`；已启动 bundled DotNet，最终结果待确认。

2026-11-22 15:26 检查：本轮日志在 15:17:54 停于 318/6626，未发现活动编译进程，原工具会话已失效；日志无明确错误或退出原因，不能认定成功或推断退出原因。依据此前继续编译的授权，已用独立隐藏 PowerShell 进程恢复增量构建（启动 PID 44228），新日志为 `E:/GitStorage/UnrealEngine/UE-build-2026-11-22-152641.log`。本次 runner 另行记录构建退出码；结果待验收。

## 后续记录模板

- 日期 / 实际投入：
- 本次唯一主要目标：
- 操作或阅读位置：
- 观察到的结果与证据：
- 用户能够解释的内容：
- 未解决问题：
- 下一步：
