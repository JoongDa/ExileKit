# Tool Manifest Schema V1

权威实现为 core/src/manifest.cpp；编辑器可使用 [JSON Schema](../tools/manifest.schema.json)（draft 2020-12）。资源放在 tools/manifests/*.json，编码 UTF-8，每个工具一个文件。Schema 用于编辑提示，运行时 C++ parser 还执行路径、HTTPS authority、字节数等校验；本程序没有通用 JSON Schema 解释器。

## 必填字段

| 字段 | 规则 |
|---|---|
| schemaVersion | 整数 1；其他版本返回 UnsupportedSchema |
| id | 1–64 个小写 ASCII 字母、数字、连字符；首尾为字母或数字；拒绝 Windows 保留名 |
| name | 不翻译的品牌名，非空，最多 160 UTF-8 bytes |
| description | 语言 → 描述对象；en-US 必须非空，每项最多 2048 bytes |
| category | official / market / craft / database / regex / build / hideout / calculator / price-check / trade / filter / wiki / utility |
| tags | 字符串数组 |
| games | poe1 / poe2 数组，不能重复；不得写 both |
| type | web / application / builtin / external-link |
| distribution | none / external / managed / bundled，与 type 独立 |
| riskLevel | normal / elevated / game_modifying |
| official | 布尔值，指游戏官方入口，不代表已审计或可信认证 |
| launch | 见下面按类型规定 |

未知字段会忽略；缺少必填、已知字段类型错误、非法枚举均产生结构化错误。单个坏清单或重复 id 会记录诊断并跳过，继续加载其他文件，重复项按排序后的首个有效 id 为准。清单目录最多 512 个 JSON，单文件最大 256 KiB，JSON 深度最多 64，受检对象最多 256 成员、字符串数组最多 128 项。字符串拒绝 NUL。JSON Schema 的 maxLength 计 Unicode 字符，运行时长度上限计 UTF-8 bytes，以运行时为准。

gameSupportVerified 默认为 true。无法验证游戏支持时明确设为 false，可配 games: []，仅 All 下可见并显示未核实。已有部分支持且未完全核实也可保留已知 games；筛选仍以数组为依据。

## Web 示例

```json
{
  "schemaVersion": 1,
  "id": "example-web",
  "name": "Example",
  "description": {"en-US": "Example website.", "zh-CN": "示例网站。"},
  "category": "utility",
  "tags": ["example"],
  "aliases": {"en-US": ["sample"], "zh-CN": ["示例"]},
  "games": ["poe1"],
  "type": "web",
  "distribution": "none",
  "riskLevel": "normal",
  "official": false,
  "homepage": "https://example.com/",
  "launch": {"url": "https://example.com/"}
}
```

web / external-link 要求有效 launch.url，当前只允许 HTTPS。拒绝 userinfo、控制字符、反斜杠、非法主机/端口及 file/javascript 等协议。UI 从 Manifest 获取 URL，经 ToolLauncher 打开默认浏览器。

## Application

application 必须包含 launch.executableNames 数组（未核实文件名时允许空数组），每项为文件名，不得包含目录或盘符。它是未来检测提示，当前不会搜索全盘；只有用户保存的实际 EXE 路径用于启动。

```json
{
  "executableNames": ["Example.exe"],
  "arguments": ["--name", "中文 空格", ""],
  "workingDirectory": ".",
  "environment": {"EXAMPLE_MODE": "local"}
}
```

上述对象为 launch；仍须加上全部顶层必填字段。arguments 是独立参数数组，不接受 shell command。Windows adapter 按 CRT argv 规则引用，CreateProcessW 显式指定 executable，不经过 cmd/PowerShell。workingDirectory 缺省为 EXE 所在目录；相对路径相对此目录解析。environment 继承当前进程并以不区分大小写的键覆盖，使用 Unicode 环境块；键不可含 NUL 或等号。错误通过 std::expected 返回。

executableNames 不限定用户必须选择该名字；Locate 是用户明确关联。不得在公共清单写入本机路径，路径保存在用户 Config/executablePaths。此阶段仅 External 应用可用；Managed 没有安装器，Builtin 没有实现。

## 可选与未来字段

| 字段 | 当前处理 |
|---|---|
| aliases | 语言 → 字符串数组，搜索当前语言及 en-US |
| homepage / repository / downloadPage / licenseUrl | 非空时为有效 HTTPS；不推导 release URL |
| author / license | 字符串元数据，不推断第三方分发许可 |
| icon | 相对 exe 资源根的 bundled 文件，例如 resources/icons/generic-web.png；拒绝绝对路径、盘符、..；读取时再检查 canonical containment |
| permissions.network.outbound / inbound | 布尔声明，绝不自动创建防火墙权限 |
| install.provider / install.sha256 | 预留 provider 字符串 / 64 位十六进制 SHA-256；不执行下载、校验或安装 |
| update | 必须是对象，内部暂作为未知扩展字段 |
| verification | 项目目录采用的来源/核验日期记录；旧 parser 忽略，不是信任认证 |

添加工具时，先核实官网、类别、对应 URL 的游戏支持和风险；不要因为品牌有 POE2 字样就猜 games。复制示例、填写双语描述/aliases、运行 core_contracts 检查。实际 15 个工具见 [catalog.md](catalog.md)。
