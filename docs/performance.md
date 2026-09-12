# Performance — Milestone 3

## 2026-09-12 M3 最终记录

同一 Windows 10 x64、MSVC 19.44 / SDK 10.0.26100、静态 CRT、96 DPI。测量时没有并发构建。Release exe 为 **782,336 bytes（764 KiB）**，比 M2.5 增加 135,680 bytes（约 21%）；23 个分发文件合计 814,496 bytes。

| 运行口径 | 热启动首帧中位数 / P95 | 初始工作集 / private bytes | 初始空闲 CPU |
|---|---:|---:|---:|
| M3 沙盒 GUI 冒烟，20 次 | 80.09 / 90.62 ms | 44.77 / 16.17 MiB | 0 ms / 3009.59 ms |
| M3 常规进程 GUI 冒烟，20 次 | 199.73 / 220.38 ms | 78.39 / 46.67 MiB | 0 ms / 3011.69 ms |
| M2.5 常规进程启动脚本，20 次 | 78.48 / 86.74 ms | 38.99 / 14.27 MiB | 0 ms / 3013.35 ms |
| M3 同一启动脚本，20 次 | 168.25 / 195.21 ms | 37.39 / 13.85 MiB | 0 ms / 2999.87 ms |
| M2.5 随后复核，10 次 | 187.70 / 222.81 ms | 83.20 / 48.00 MiB | 0 ms / 3013.20 ms |

所有表内启动均为暖缓存 / 未控制缓存，**冷启动未测**。旧版在复核中也出现约 100 ms 启动差异和明显内存变化，说明当前环境不够稳定，不能据其中一对数据认定 M3 稳定回退，也不能宣布不存在回退。暂未完成 ETW / WPR 因果定位；试验性 WinHTTP 延迟加载没有改善数据，已撤回，最终仍采用原加载方式。

M3 GUI 压力操作后的工作集分别为 65.02 MiB（沙盒）和 112.07 MiB（常规进程）；常规进程之后约三秒 CPU 为 15.625 ms / 3038.59 ms，折合单核约 0.51%。两次使用的快捷方式与网络环境不同，不能直接视作内存泄漏曲线。**50 MB 内存目标尚未全面达标**，应继续分离 D2D / 字体 / Shell / 驱动分配，测较长空闲时间及重复压力操作后的稳定性。

初始工作集在 Registry ready 后等待 10 秒测量，CPU 再采样约三秒。暖启动计时从创建进程到成功 EndDraw 后的首帧标记，外部轮询为 5 ms；不是 DWM 最终显示时间。后续暖启动使用该组同一隔离配置。GUI 测试与只测启动的脚本口径分别保存，不混用。

原始 JSON 位于 `out/validation/m3-final/{gui,comparable,regular-m25,regular-m3,regular-m25-repeat}/metrics.json`。用 `scripts/benchmark-startup.ps1 -Exe <exe> -OutputDirectory <fresh-directory>` 可在不同版本间复用相同启动流程；`scripts/smoke.ps1` 负责窗口功能和操作后测量。真实 WinHTTP 在当前沙盒因 TLS 初始化返回 12185，网络功能测试须在常规 Windows 进程执行，不可关闭证书校验来“修复”测试。

## 历史 M2.5 数据

## 2026-09-10 最终 Release 记录

Windows 10 x64 10.0.19045，MSVC 19.44.35222 / VS 2022 17.14，SDK 10.0.26100，CMake 3.31.6，静态 CRT，实际桌面 DPI 96。测量脚本运行时未同时构建。原始数据位于本地 out/validation/gui-final/metrics.json；它在 out 中，不作为可复现到所有机器的性能保证。

| 指标 | 实测值 | 口径 |
|---|---:|---|
| Release exe | 646,656 bytes（631.5 KiB） | 不含资源 / PDB / 测试程序 |
| 安装输出文件合计 | 676,550 bytes | exe + locale/icon + 15 清单 + schema + MIT license，无压缩 |
| 冷启动 | **未测量** | 没有重启系统或清空 OS 文件缓存 |
| 主冒烟测试首帧 | 100.14 ms | 缓存未控制；不是冷启动 |
| 主冒烟 Registry ready | 102.89 ms | 从进程启动到 UI 接收 Registry |
| 后续 20 次热启动首帧中位数 | **86.41 ms** | 最小 72.62，最大 112.11，P95 97.68 ms |
| 20 次 Registry ready 中位数 | **91.60 ms** | 最小 83.61，最大 115.60，P95 112.67 ms |
| 初始稳定工作集 | **45.31 MiB** | Registry ready 后无操作 10 秒，压力操作前 |
| 初始 private bytes | 16.68 MiB | 与 working set 不同 |
| 初始空闲 CPU | 0 ms / 3009.27 ms | 单进程 TotalProcessorTime 增量，单核百分比 0% |
| 压力操作后工作集 | **74.63 MiB** | 搜索/切换语言/最大化/合成 DPI/恢复后 |
| 压力操作后 private bytes | 35.34 MiB | 非初始空闲基线 |
| 压力操作后空闲 CPU | 0 ms / 3010.48 ms | 约三秒样本，不代表长时间绝对零消耗 |
| 15 个 Manifest 加载 | 1.5471 ms | Release application_flow worker 单次暖缓存样本 |
| 两套 locale 加载 | 0.3151 ms | 同一测试单次样本 |
| 新安装输出首次资源读取 | Manifest 8.4497 ms / locale 1.776 ms | 系统整体并非冷缓存；Debug 构建并发，非基准 |
| 退出 | 所有测量进程 exit 0 | worker/config 正常结束，无测试主进程残留 |

首帧标记位于成功 EndDraw 之后，不是 DWM 最终屏幕呈现时间。外部轮询为 5–10 ms，Windows 调度可能扩大误差。20 次 P95 使用 nearest-rank 第 19 项；记录不能直接作为产品 SLA。

## 与 M1 比较

M1 保存的单次首帧约 67.70 ms，当前 20 次中位数为 86.41 ms；采样方式、构建内容、缓存条件不一致，不能认定精确回退比例。Manifest + locale 的暖缓存工作约 1.86 ms，且发生在首帧后，已从启动路径中分离。当前热启动远低于 500 ms 工程目标，但没有冷启动数据证明冷启动达标。

原 M1 exe 261,120 bytes，M2.5 增加 JSON parser、Registry、配置、启动器、图标和业务流程后为 646,656 bytes。增加的功能保留，没有通过删功能缩减指标。

初始工作集 45.31 MiB（约 47.51 MB）低于 50 MB 目标；压力操作后 74.63 MiB 超出目标，**不能宣布内存目标全面达标**。后续需用 WPR/WPA 或 VS Profiler 分离 D2D surface、字体缓存、Shell/WIC 和驱动分配，再判断是否释放缩小/隐藏窗口后的资源。目前没有足够样本判定泄漏。

## 已落实与剩余验证

首帧不读 JSON/图标、不联网；Registry/Config/icon I/O 位于 worker，缺失图标不循环重试。D2D/DWrite factory、文字格式、画刷复用，离屏卡片跳过绘制；图标仅对应区域 invalidation；退出等待不会在待执行工作之间空转。exe imports 仅为 Windows 系统 DLL，WIC 通过 COM 使用。

双语 × 五档 DPI × 两种宽度生成 20 张 D2D/DWrite/WIC PNG；已查看中文三列、本地图标和英文窄窗口 200% 输出，无卡片重叠。PNG 不包括 native EDIT、COMBOBOX、标题栏和系统滚动条。真实窗口消息测试另覆盖这些控件的功能路径和合成 DPI。

真实 4K、多显示器物理跨屏、Windows 11、设备丢失和最终 DWM 屏幕图像仍需实机验证。此前 PrintWindow 无法捕获当前 D2D surface；本轮采用真实 Renderer 离屏 PNG 与窗口功能测试，未声称获得完整桌面截图。

## 复测

运行 scripts/build.ps1 -Configuration Release -Test，再运行 scripts/smoke.ps1 -OutputDirectory <isolated-directory> -WarmSamples 20。只有显式加 -LaunchWeb 才会通过卡片打开默认浏览器。

冷启动应在重启后首次运行或严格受控冷缓存实验中另行采样，不要连续启动应用冒充冷启动。长期 CPU 推荐延长到 30–60 秒；内存应重复 resize/minimize/DPI 循环并观察是否稳定。所有数据需注明机器、系统、驱动、DPI、缓存状态和是否同时进行其他工作。
