# Security — Milestone 3

## 权限与第三方程序

ExileKit 保留内部可执行文件名 `POEToolbox.exe`，嵌入 manifest 为 `asInvoker`，没有 runas、自动提权或常驻 Elevated Helper。程序继承启动者 token；从已提升终端启动时不会主动降权。用户数据默认位于 LocalAppData，不向 Program Files 或注册表写配置。

本期支持网站打开、用户选择的 EXE / LNK、自定义快捷方式和后台网站元数据。没有第三方程序下载器、安装器、更新器、插件 ABI、游戏 Hook 或游戏内存读取，也不重新分发第三方程序。External 程序保留原安装位置，更改受管目录不会移动它们。

Manifest 是声明，不能代替可信授权。`riskLevel=game_modifying` 由通用 UI 规则提示游戏文件修改风险；取消后不启动，Launcher 在缺少确认时也拒绝。规则不硬编码某个工具 ID。确认不构成对第三方程序的审计、沙箱化或游戏条款保证；自定义程序的安全性也不会因用户添加而得到验证。

Launcher 显式传入 EXE，按 Windows 参数规则逐项引用，支持 Unicode 参数、环境与工作目录，不调用命令解释器或继承句柄。Locate / 添加快捷方式只保存数据，不自动执行。当前不校验程序是否与展示品牌相符，也不做 Authenticode 或发布哈希验证。

## URL、EXE 与 LNK

网站打开及元数据请求只接受有效 HTTPS URL；拒绝 file / javascript、userinfo、控制字符和非法 authority。可识别的裸域名规范为 HTTPS。网站打开交给系统默认浏览器，元数据解析不执行脚本，也不嵌入浏览器页面。

EXE 名称来自受限版本资源或文件名，图标通过 Windows 资源 API 读取。LNK 文件限制为 2 MiB，通过 `IPersistFile::Load` / `IShellLinkW` 读取目标、参数、工作目录和图标；不调用 `Resolve`，不自动搜索或修复目标。当前要求目标是存在的 EXE，启动时重新检查；不扫描 Desktop、Start Menu、Program Files、注册表或整个磁盘。

解析快捷方式和图标仍依赖 Windows Shell / codec，没有额外进程沙箱。支持的绝对文件路径可能位于用户明确选择的网络共享；不调用 Resolve 不等于承诺所有文件访问均不产生网络 I/O。

## 后台网站信息

启动阶段不发 HTTP，包括恢复已有 URL Home。用户导航 / 操作后按需请求可见工具图标，或添加 URL 后获取标题和 favicon。网络 worker 与本地启动、配置、Home worker 隔离；网络阻塞或失败不应阻止本地操作。退出清空待执行网络任务并通过 stop token 取消活跃请求。

WinHTTP 使用 HTTPS、系统代理设置及默认证书校验，不禁用 TLS 验证；关闭 cookies、认证、自动登录与自动重定向。程序手动校验重定向地址，每资源最多 3 次跳转，不重试。HTML / 图标及跳转共享一个 6 秒截止时间；限制响应头 16 KiB、HTML 256 KiB、图标 2 MiB。没有上传用户配置、凭据或遥测的实现，但网站和代理会收到用户触发的正常 URL / favicon 请求。

有效图标缓存不在每次启动重取；失败标记抑制 24 小时内重复获取缺失图标。缓存键使用校验过的稳定工具 ID；缓存写失败时仍可使用已解码图标。网页标题清理控制字符并限制长度，不能作为命令或配置指令执行。当前不提供域名白名单、私网地址隔离或对站点内容的信任保证。

## 用户数据与恢复

配置位于 `%LOCALAPPDATA%\ExileKit\Config\settings.json`，自定义快捷方式与 Home 状态保存于用户数据，公共 Manifest 不含本机用户路径。新配置缺失时仅复制旧 `%LOCALAPPDATA%\POEToolbox\Config\settings.json`，不移动、删除旧配置或已有程序。

ConfigManager 对 256 KiB JSON、类型、ID、路径与 Home 状态进行校验；同目录临时文件刷新后替换，校验或写入失败保留旧文件。损坏配置先复制为 `settings.json.recovery-*.json`；备份失败则禁用后续保存，防止默认状态覆盖原数据。没有自动合并多实例修改或加密配置；最后写入的完整快照生效。

Home 自动移除、Remove、Unpin 均不卸载程序，不删除 Manifest、CustomTool 或用户文件。bundled 图标验证 canonical 路径包含关系；WIC 限制输入和图像尺寸。缓存文件、解析结果和第三方名称始终按不可信数据处理。

## 防火墙、杀毒与后续包管理

没有创建防火墙规则、添加 Defender exclusions、关闭保护、信任整个 Tools 目录、自动 Run Anyway 或绕过 SmartScreen 的实现。`permissions.network` 仍是声明，普通出站请求不附带入站规则。未来确需权限变更时应针对具体工具和操作获取授权，使用最小权限。

Managed 下载、更新与卸载尚未实现，不提供删除路径冒充卸载的菜单。未来包流程应为临时下载 → 验证可信发布信息 / SHA-256 → 安装 → 注册 → 用户启动，不能下载后立即执行；Provider 与 repository 字段保持独立。签名和哈希尚未验证时，不显示虚假的可信结果。
