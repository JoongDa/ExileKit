# Architecture — Milestone 3

ExileKit 保留 C++23、Win32、Direct2D、DirectWrite、WIC、DPI 和现有搜索架构。用户可见品牌更新为 ExileKit；`POEToolbox.exe`、命名空间和仓库目录继续保留。不引入数据库、WebView 或新 UI framework。

## 模块与 UI

| 模块 | 职责 |
|---|---|
| apps/toolbox | COM / common-controls 初始化、资源定位、服务与窗口组合；asInvoker / PerMonitorV2 |
| core | Manifest、Registry、搜索、LocalizationManager、用户配置、CustomTool、可注入时间的 HomeService |
| application | UI 线程服务入口、Home / 自定义快捷方式动作、本地及网络任务队列、配置快照、成功使用统计和完成事件 |
| platform/windows | 路径、原子配置写入、Launcher、EXE / LNK 检查、原生对话框、IconProvider、WebMetadataProvider / WinHTTP、日志 |
| ui | Home / POE / POE2 / Settings 四导航，native 搜索框与添加入口、快捷方式卡片、上下文菜单、语言及风险提示 |
| resources / tools/manifests | 双语字典、本地图标与公共工具库；不保存用户快捷方式 |

Core 不包含 HWND 或 MessageBox。UI 不解析 JSON、下载图标或直接启动进程；Renderer 只接收最终文字和图像。POE / POE2 直接使用原有 `games` 元数据与 Registry 查询，不再使用 All 游戏下拉框，也不建立游戏路由系统。

## 生命周期与线程

窗口先绘制首帧，再启动 ApplicationServices 的本地 `std::jthread`。该 worker 读取路径、配置、语言与 Manifest，检查已配置 EXE 并计算 Home；配置写入、文件检查、工具启动和本地图标读取也在此串行执行。所有数据更新经完成队列交给 UI 的 `Drain()`，通过 WM_APP 通知窗口；后台线程不操作 D2D 或控件。

网络使用独立、按需创建的 `std::jthread`，不会占用本地任务队列。启动与恢复已有 Home 只请求本地图标，不触发 HTTP。用户导航 / 操作后的可见图标请求以及添加自定义 URL 才允许排队获取网站信息。每会话按工具 ID 去重，网络等待队列最多 128 项；未取得结果时保留名称和占位图。

退出先清空待执行网络任务、请求取消并 join 网络 worker，再处理本地完成事件及其产生的配置写入，最后停止本地 worker。没有渲染或 Home 清理定时器，也没有空闲网络轮询；WinHTTP 仅在活跃请求期间检查截止时间与取消。原生对话框仍在 UI 线程显示，本地队列暂未实现保存合并或系统启动调用取消。

## Home、配置与兼容

HomeService 接收 Unix 秒时间戳，集中处理近期两次成功使用自动加入、Pin / Unpin、显式隐藏、7 / 30 / 90 天及 Never 策略和排序。只有启动成功才更新累计使用次数；过期或移除只影响 Home，不删除 CustomTool、Manifest、用户路径或文件。固定项目优先且顺序稳定，普通项目按最近使用与次数排序，无固定数量上限。具体规则见 [product-design.md](product-design.md)。

Config schema 仍为 1，新增可选 `home`、`homeAutoRemoveDays`、`customTools`，保留 `favorites`、`recentTools`、语言、游戏与 EXE 映射。M2.5 配置可直接读取；近期重复使用记录可初始化 Home。自定义工具存于用户配置，不进入官方 POE / POE2 Registry。

PathManager 统一使用 `%LOCALAPPDATA%\ExileKit`，包含 Config、Logs、Downloads、Cache/Icons、Tools。新配置尚不存在时，仅复制 `%LOCALAPPDATA%\POEToolbox\Config\settings.json`；旧文件和已有程序均不移动，原配置中的路径保持原值。显式测试数据根不执行此默认迁移。更改受管安装目录只影响未来安装，先验证绝对路径、目录与可写性。

配置经过类型、ID、路径和 256 KiB 大小校验，以同目录临时文件刷新后原子替换。损坏配置先复制成 `settings.json.recovery-*.json` 再允许保存默认状态；备份失败则禁用保存，避免破坏原文件。多实例仍采用最后写入快照生效，不合并状态。

新增快捷方式和网页标题更新在本地 worker 预检候选配置，之后才提交到 UI 状态。校验期间配置发生变化时重新校验最新快照；超过容量的新项目不能令现有配置持续保存失败。

## 自定义工具与图标

自定义入口只有输入框和 Browse，识别 HTTPS 地址、EXE、LNK；名称先使用可用元数据，再回退域名或文件名。LNK 通过 IShellLink 读取目标、参数、工作目录与图标位置，不调用 `Resolve`，不搜索磁盘或网络修复目标。启动时重新检查文件，使用现有 Launcher 显式启动 EXE。

IconProvider 统一处理 bundled → EXE / LNK 指定资源及目标资源 → 本地网站缓存 → 占位图；WebMetadataProvider 在独立网络 worker 按需补齐标题与 favicon。成功图标缓存为稳定 ID 的 `.icon` 文件，也兼容 `.png` / `.ico`；已有有效图标不因重启重新下载。失败标记抑制 24 小时内重复获取缺失图标，新 URL 的标题获取独立于此规则。

一次元数据任务的 HTML、favicon 和重定向共享 6 秒截止时间；每资源最多 3 次重定向，无重试。HTML 最大 256 KiB、图标最大 2 MiB，WIC 源图像最大 4096×4096，并缩放成 64 像素范围的 BGRA。网络失败不影响本地使用；更新只通知对应工具区域。D2D 位图只在 UI 线程创建，保留像素所有权，缓存最多 256 项并在 device loss 时释放；该缓存限制不限制 Home 数量。

## 搜索、语言与扩展边界

Registry 搜索保留空白分词 AND、ASCII 大小写折叠及 UTF-8 子串匹配，覆盖品牌、描述、tags、category 和当前语言 / 英语 aliases。LocalizationManager 的查找顺序为当前语言 → en-US → 调用者默认值 / key；切换立即刷新界面并异步保存，品牌名不翻译。

Managed 包下载、安装、更新、卸载、Authenticode、按需提权 helper 和 Builtin 工具仍未实现。Home Remove 不伪装为卸载，外部程序继续由用户和第三方管理。网络与执行边界见 [security.md](security.md)。
