# ExileKit — Milestone 3

<img src="assets/branding/exilekit-source.png" width="96" alt="ExileKit icon">

A toolbox for Path of Exile & Path of Exile 2.

Windows x64 原生 C++23 工具入口，采用 Win32 / Direct2D / DirectWrite。一级导航为 Home、POE、POE2、Settings，保留 15 份公共工具清单、双语搜索和使用记录，支持 English / 简体中文即时切换。

ExileKit is not affiliated with or endorsed by Grinding Gear Games. Path of Exile and Path of Exile 2 belong to Grinding Gear Games. Third-party tools, names, and icons belong to their respective owners.

## 构建与运行

需要 Windows 10/11 x64、Visual Studio 2022 C++ 桌面工作负载（支持 C++23 std::expected）、Windows SDK 和 CMake 3.25+。已在 MSVC 19.44、SDK 10.0.26100 验证。运行时只有 Windows 系统组件，无 .NET、Node、Python、WebView2 依赖。

```powershell
./scripts/build.ps1 -Configuration Release -Test
./scripts/build.ps1 -Configuration Debug -Test
./out/build/windows-msvc/bin/Release/POEToolbox.exe
```

脚本自动寻找 VS 附带 CMake，也修正某些终端向子进程同时注入 PATH / Path 导致的 MSBuild 环境错误。可直接使用 cmake --preset windows-msvc、cmake --build --preset release、ctest --preset release。

程序必须与同目录的 resources/、tools/manifests/ 一起运行；不能单独复制 exe。可用 cmake --install out/build/windows-msvc --config Release --prefix out/package/ExileKit-0.3.0 生成分发目录（需将 CMake 加入 PATH）。内部命名空间和 POEToolbox.exe 暂时保留。

默认数据根目录为 %LOCALAPPDATA%\ExileKit，未来受管程序默认放在 Tools 子目录。首次运行会在新配置不存在时复制旧 POEToolbox 配置，保留已记录的安装位置；不会移动外部程序。测试可通过 --data-dir <absolute-path> 隔离配置。

## 使用

1. 新用户 Home 显示欢迎语。近期重复成功使用的工具会自动加入；POE / POE2 中右键工具可以 Add to Home 或 Pin。
2. Home 右上角 `+` 支持输入 HTTPS 网址、域名、EXE / LNK 路径，也可 Browse 选择文件。名称与图标自动获取，网页信息在后台补齐。
3. 点击整张卡片打开网站或启动程序；工具库中尚未配置的程序先选择其 EXE。选择文件本身不会执行。
4. Home 右键可固定、取消固定或移除。移除保留工具数据和磁盘文件；普通启动不会撤销用户的移除决定，重新添加可恢复。
5. Settings 可选 7 / 30 / 90 天或从不自动移除，默认 30 天；固定项目不自动移除。这里也可切换语言、更改未来受管工具目录。

Home 不设固定项目数量上限。收藏和最近使用数据继续保留；自定义快捷方式只进入个人 Home，不修改公共工具清单。PoeRedux 等 game_modifying 工具每次启动前显示风险确认，取消不会启动。

图标支持本地资源、EXE / LNK 图标、网站缓存与异步 favicon。启动不联网；操作后按需请求，失败保留域名和占位图，有效缓存不重复下载。当前没有第三方软件安装、更新或卸载功能，不修改防火墙、Defender、SmartScreen 或游戏进程。

应用图标使用项目所有者提供的图片，已嵌入 EXE 并用于窗口标题栏和任务栏，包含 16–256 像素的七档尺寸。原图与生成方式见 [品牌资源](assets/branding/README.md)；可运行 `./scripts/build-icon.ps1` 重新生成 ICO。

## 工程与验证

- [架构与线程边界](docs/architecture.md)
- [Home 与快捷方式产品规则](docs/product-design.md)
- [Schema V1 与扩展方式](docs/manifest.md) / [JSON Schema](tools/manifest.schema.json)
- [15 个工具的分类、游戏支持与来源](docs/catalog.md)
- [安全边界](docs/security.md)
- [M3 验证记录](docs/milestone-3.md) / [性能记录](docs/performance.md) / [历史 M2.5 验收](docs/acceptance.md)

CTest 覆盖解析、搜索、配置、Home 时间规则、真实 EXE / LNK 启动、网络缓存与异常、网络阻塞时本地操作和取消、完整应用流程及双语五档 DPI 绘制。scripts/smoke.ps1 是开发专用真实窗口测试；加 -LaunchWeb 会打开默认浏览器，加 -CustomTarget <path> 会通过原生添加弹窗添加并启动指定程序。测试工具不随产品分发。

图标接入可通过 `./scripts/smoke.ps1 -VerifyApplicationIcon -WarmSamples 0` 验证，覆盖窗口图标句柄与缩放更新。

项目自身发布许可证尚待所有者决定，见 [LICENSE](LICENSE)。nlohmann/json 3.12.0 以 MIT 授权内嵌，见 [第三方说明](third_party/nlohmann/README.md)；安装输出带其许可证。不包含第三方工具二进制。
