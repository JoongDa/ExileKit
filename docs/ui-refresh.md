# Icon Grid UI 验证记录 — 2026-09-17

本轮只调整原生 UI、交互、品牌图标和相关验证。Home / POE / POE2 复用 ToolIconItem / ToolIconGrid，默认仅图标与名称；描述和必要状态放入原生 Tooltip，操作放入动态右键菜单。Home 添加项保留在普通网格中，空 Home 和搜索无结果时均可用，顶部原生添加按钮已移除。Registry、Home model、Config、Launcher 和异步图标服务未修改。

应用图标使用 2026-09-16 提供的简洁金色三分环原图，原文件 SHA256 与 `assets/branding/exilekit-source.png` 一致。ICO 包含 16 / 24 / 32 / 48 / 64 / 128 / 256 像素七帧，EXE 提取图标已查看；旧的未引用 ICO 副本已移出源码，工作区原有图标修改保存在 `out/validation/ui-refresh/backups`。

## 构建与自动验证

- Debug / Release 全量构建成功，CTest 各 **12/12** 通过：保留原 11 项，新增 `window_interactions`。
- 网格测试覆盖搜索、无结果、Home 添加项、游戏过滤、悬浮描述、原有 Locate 行为与移除后刷新；真实 Renderer 生成双语、五档 DPI、两档宽度及四页面预览，已查看中英文 Home、POE / POE2 与 200% 窄窗口。
- 新增窗口级测试在自身进程的隔离配置中验证原生 Tooltip 文本与显示、Web 右键菜单、Pin / Unpin / Remove 真实菜单选择及配置保存、最后一项移除后添加弹窗的打开与关闭。该测试需要可交互桌面并串行运行；点击前确认目标确属本测试菜单，不随应用分发。
- 最终分发程序 smoke 验证双语即时切换、四导航、搜索、滚动、最小化 / 最大化、Home 网格添加 LNK 并启动、网页图标调用 ShellExecuteExW 成功、配置保存和正常退出。独立 EXE 添加与启动已在本轮较早的窗口测试中通过。
- 大小窗口图标在初始 96 DPI、合成 120 / 144 / 168 / 192 DPI 及恢复后均符合系统尺寸。分发 EXE 与最终 Release 构建 SHA256 相同，大小 934,400 bytes。

日志位于 `out/validation/ui-refresh/Release-Build-Test.log`、`Debug-Build-Test.log`、`Package-Smoke.log`。最终 smoke 的两个约三秒空闲 CPU 样本均为 0 ms；这不是长期性能基准，也未据此宣称内存目标达标。

## 验证边界

原生截图工具在当前 Windows 环境返回 `SetIsBorderRequired failed: 不支持此接口 (0x80004002)`，原生控件信息可读取。任务栏和标题栏图标的最终屏幕外观、物理跨显示器、Windows 11、浏览器最终网页内容未验证；已验证资源、窗口图标句柄和尺寸，不能代替上述屏幕验收。渲染器预览不包含原生搜索框、标题栏和系统滚动条；默认占位图也不代表真实会话中异步下载的 favicon。

新版分发目录：`out/package/ExileKit-0.3.0`。运行其中的 `POEToolbox.exe`，保留同目录 resources 与 tools 资源。
