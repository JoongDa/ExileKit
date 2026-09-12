# Milestone 2.5 验收报告 — 2026-09-10

本阶段已实现真实工具启动器与主要验收路径。保留原生 UI 架构，未加入 WebView、自动下载/更新或游戏进程操作。尚未完成的实机验收项单独列出，不将功能实现等同于所有环境验证完成。

## 1. 修改模块

- Core：Tool / ToolManifest、四种类型与四种分发方式、游戏数组、风险级别、结构化 std::expected；V1 parser、Registry / 索引 / 双语搜索、LocalizationManager。
- Application：首帧后 worker 加载、状态编排、异步配置快照、收藏、最近使用、路径关联与真实启动、图标完成通知。
- Platform：PathManager、原子 ConfigManager、ShellExecuteExW / CreateProcessW、Unicode args/env/cwd、原生选择器、WIC / EXE / cache IconProvider。
- UI：真实卡片与状态、Open / Locate / 官网、星标、All / POE / POE2、双语 Settings、安装目录设置、通用风险确认。
- 工程：删除 demo_catalog；资源随构建复制，修正 cmake install 的 tools/manifests 布局，附 JSON Schema 与 JSON parser 的 MIT license；完善测试与文档。

## 2. 最终 Manifest Schema

schemaVersion=1；必填 id/name/description.en-US/category/tags/games/type/distribution/riskLevel/official/launch。Web 要求 HTTPS URL；Application 要求 executableNames，实际用户 EXE 路径另存 Config。未知字段忽略；不支持版本、缺少必填、错误类型返回结构化错误；坏文件跳过。aliases、gameSupportVerified、icon、source/license、permissions、install.provider/sha256、update 为当前或未来元数据。完整定义见 [manifest.md](manifest.md) 与 [schema 文件](../tools/manifest.schema.json)。

## 3. i18n

69 个 UI keys，en-US / zh-CN 两套字典；首次 Windows UI language，用户配置优先，设置立即刷新。Current → en-US → default/key fallback；品牌名不翻译。Renderer 不读 JSON。细节见 [architecture.md](architecture.md)。

## 4–5. 真实工具、分类与游戏支持

15 个条目：Path of Exile 2、Path of Exile、poe2.ninja、poe.ninja、Wealthy Exile、Craft of Exile、PoEDB、Path of Exile Regex、PoE Planner、PoeCharm2、Hideout Showcase、Timeless Jewel Calculator、POE2 Dust Analysis、PoE Overlay、PoeRedux。

[完整逐项分类、POE1/POE2 支持与官方来源表](catalog.md)。特别记录：poe2.ninja 已重定向；Dust Analysis 按官方 About 归 POE1；PoeCharm2 不假设 Releases；PoE Overlay 保持 External；PoeRedux 标为 game_modifying。

## 6. Web Launch

**系统启动路径实际通过。** application_flow --live-web 真实调用默认浏览器；真实 POEToolbox 窗口测试另完成搜索 poe.ninja → 卡片 Open → ShellExecuteExW 成功 → recentTools 写入。测试不是只调用 mock。

当前浏览器工具只接入 Codex 内嵌浏览器，没有接入系统默认浏览器，因此**未确认网页最终内容加载完成**；ShellExecuteExW 成功表示 Windows 接受打开请求。不会把它当成页面健康测试。

## 7. Application Launch

**真实 CreateProcessW 成功。** 自建无害 fixture 被复制到含中文和空格的路径，实际接收并核对空参数、空格、嵌入引号、末尾反斜杠、Unicode 环境变量和工作目录。

应用服务集成测试完成 ConfigureExecutable → 保存路径 → 不自动执行 → Launch → fixture 实际输出 → recent → Stop 刷盘；安装输出目录也跑过同一流程。未下载或运行 PoeRedux、PoeCharm2、PoE Overlay。Windows 原生 Locate 文件对话框已接线，但本轮未自动完成该对话框的文件选择交互；以上路径配置验证发生在它返回后的应用服务边界。

## 8. 中文 / 英文切换

**通过。** 真实主窗口 Settings 的中文/English 点击立即改变 native COMBOBOX 的本地化 All 标签，退出后设置成功持久化。应用服务测试另核对当前字典；20 张离屏 PNG 覆盖双语、720/1200 DIP 宽度、100/125/150/175/200% DPI，已检查中文三列、本地图标和英文窄窗口 200% 输出。

## 9. Configure / Build / CTest / Run

| 验证 | 结果 |
|---|---|
| Release configure + build | 成功，MSVC /W4，无编译警告 |
| Release CTest | 5/5 passed，1.23 秒 |
| Debug configure + build | 成功，无编译警告 |
| Debug CTest | 5/5 passed，2.14 秒 |
| core_contracts | fallback、合法/非法/schema/unknown、坏文件/重复 id/中文文件名隔离、真实 15 项搜索/aliases/filter |
| platform_contracts | 配置往返/无效配置保留、路径验证、mock Web、风险 guard、实际 EXE args/env/cwd、bundled/cache/图标失败 |
| application_flow | 真实配置、语言/筛选/收藏、Locate 后不自动执行、启动与 recent、目录校验、图标、退出刷盘 |
| layout_invariants / renderer_preview | 布局/滚动不变量，双语五档 DPI 实际 D2D/WIC 绘制 |
| 主窗口 smoke | 原生输入、搜索缩小结果、双语设置、游戏筛选、卡片 Open、滚动/滚轮/Home、resize/最小化/最大化/合成 DPI、正常关闭 |
| 安装输出验证 | cmake --install；对安装资源执行 application_flow 成功，15 清单 + 图标可用 |
| exe 资源与依赖 | mt.exe 提取确认 asInvoker / PerMonitorV2 / v0.2.5；dumpbin 仅见 Windows 系统 DLL |

主窗口及 20 次热启动测量进程均正常退出。测试数据在仓库 out/validation 内，未改用户正常 profile。

## 10–13. 大小、启动、空闲与资源加载

| 指标 | 值 |
|---|---:|
| Release exe | 646,656 bytes / 631.5 KiB |
| 冷启动 | 未测量 |
| 20 次热启动首帧中位数 / P95 | 86.41 / 97.68 ms |
| 20 次 Registry-ready 中位数 / P95 | 91.60 / 112.67 ms |
| 初始稳定工作集 / private bytes | 45.31 / 16.68 MiB |
| 初始空闲 CPU | 0 ms / 3009.27 ms |
| 压力操作后工作集 | 74.63 MiB，超过 50 MB 目标 |
| Manifest / locale 暖缓存单次 | 1.5471 / 0.3151 ms |

口径、M1 比较、原始样本位置和复测方法见 [performance.md](performance.md)。不能以热启动代替冷启动，不能用 private bytes 代替工作集。

## 图标、目录与安全追加验收

本地 Icon / Placeholder 实际渲染；bundled 与缓存读取、失败 fallback 已测试。EXE 图标实现通过 Shell 资源读取，不运行 EXE；远程 favicon 未实现，因此启动没有同步图标网络请求。

默认 Tools 目录创建成功；Settings 显示路径并提供 Change/Reset；自定义中文目录可保存，无效目录不改变当前配置，也不移动外部已安装程序。Manifest 与用户路径分离。

asInvoker 已从最终 exe 提取确认；当前无提权、Defender exclusion、firewall bypass、SmartScreen bypass 或自动安全设置修改。权限元数据与真实授权严格分开。具体约束见 [security.md](security.md)。

## 14. 已知问题与验证边界

1. 冷启动、系统默认浏览器最终页面、Locate 文件对话框完整交互、Windows 11、物理多显示器/4K 尚未实机验收。
2. 远程 favicon 延后；多数工具显示占位字母，本地 EXE / 缓存存在后可显示图标。
3. 压力后工作集超预算；需要进一步 profile，不声称所有使用状态均小于 50 MB。
4. 当前 worker 串行；系统 Shell / 文件 I/O 没有应用层取消，慢网络路径或 Shell 调用可能延迟退出。后续网络工作应独立。
5. 多实例配置为最后写入者生效；损坏配置使用默认值并报错，后续保存可能覆盖损坏文件，没有自动恢复备份。
6. 未验证所选 EXE 的发行者/签名/hash；工具名与文件匹配由用户确认。未来 managed 不应沿用这种无验证安装路径。
7. D2D 卡片/Settings 按钮暂未提供完整 UI Automation / 屏幕阅读器语义和逐控件键盘导航。
8. 项目自身许可证待所有者选择，第三方 parser MIT license 已随安装输出附带。

## 15. 下一阶段建议

先补 Remote Favicon Provider（异步/超时/缓存 TTL/有界大小/有限重试）和上述实机验证；同时 profile 压力后 D2D/WIC 内存。再实现独立 Package Provider 契约与 Download → Verify → Install → Register 的最小受管安装流程，逐工具核实许可证，保持 External 原有更新器不受接管。
