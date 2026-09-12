# 首批工具目录 — 2026-09-10

当前加载 15 个工具，无演示数据。游戏支持对应所填 URL / 项目版本，不能仅凭品牌名推断。除两项 GGG 官方入口外，official 均为 false。全部网页为 Web / None / normal；三个本地应用为 Application / External。

| 工具 | category | 游戏支持 | 类型 / 风险 | 官方入口或项目来源 |
|---|---|---|---|---|
| Path of Exile 2 | official | POE2 | Web / normal | [官网](https://pathofexile2.com/early-access) |
| Path of Exile | official | POE1 | Web / normal | [官网](https://www.pathofexile.com/) |
| poe2.ninja | market | POE2 旧入口 | Web / normal | [旧域名](https://poe2.ninja/) 已重定向到 poe.ninja |
| poe.ninja | market | POE1、POE2 | Web / normal | [官网](https://poe.ninja/) |
| Wealthy Exile | market | POE1 | Web / normal | [官网](https://wealthyexile.com/) |
| Craft of Exile | craft | POE1、POE2 | Web / normal | [官网游戏选择](https://www.craftofexile.com/) |
| PoEDB | database | POE1 | Web / normal | [当前目录入口](https://poedb.tw/us/) |
| Path of Exile Regex | regex | POE1 | Web / normal | [官网](https://poe.re/)、[作者仓库](https://github.com/veiset/poe.re) |
| PoE Planner | build | POE1、POE2 | Web / normal | [官网](https://poeplanner.com/) |
| PoeCharm2 | build | POE2 | Application / normal | [作者仓库](https://github.com/Chuanhsing/PoeCharm2) |
| Hideout Showcase | hideout | POE1、POE2 | Web / normal | [官网](https://hideoutshowcase.com/) |
| Timeless Jewel Calculator | calculator | POE1 | Web / normal | [计算器](https://vilsol.github.io/timeless-jewels/tree)、[作者仓库](https://github.com/vilsol/timeless-jewels) |
| POE2 Dust Analysis | calculator | **POE1** | Web / normal | [用户指定入口](https://poe2lens.com/dust_analysis.html)、[官方 About](https://poe2lens.com/about.html) |
| PoE Overlay | price-check | POE1、POE2（不同版本） | Application / normal | [官网](https://www.poeoverlay.com/) |
| PoeRedux | utility | POE1、POE2 | Application / **game_modifying** | [作者仓库](https://github.com/Gineticus/PoeRedux) |

## 核验说明与局限

- poe2.ninja 的 HTTP 重定向本轮已核验；保留用户要求的独立旧入口。poe.ninja 为合并网站，两个目录项可能最终打开同一站点。
- POE2 Dust Analysis 保留用户要求的显示名，但 [About 页面](https://poe2lens.com/about.html) 把 Dust Analysis 列在 POE1 Features 下，所以 games 填 poe1。品牌名不能作为 POE2 支持的依据。
- PoEDB 当前 URL 是 POE1 入口；独立 PoE2DB 不在这次 URL 中。poe.re 亦有独立 poe2.re，本次只录用户指定的 POE1 地址。
- [PoeCharm2 README](https://github.com/Chuanhsing/PoeCharm2) 指向 Path of Exile 2，并列出 PoeCharm3.exe，因此 executableNames 用此文件名。未假设 GitHub Releases 存在，没有拼接 releases/latest。
- PoE Overlay 官网提供两代游戏相关产品/不同发行形式。用户必须 Locate 自己安装的正确版本；未核实统一 exe 文件名，所以 executableNames=[]。不接管其更新器，不处理 Overwolf。
- PoeRedux 仓库说明包含两代游戏并修改游戏文件；仅提供出处与手动启动入口，风险由通用元数据机制处理。未下载、安装或运行该工具。
- Wealthy Exile 当前资料只确认 POE1；没有按网站名猜 POE2 支持。未来核验不明确时可使用 gameSupportVerified=false，不强行填满 games。
- 分类体现当前主要用途，游戏筛选不等于特定赛季、补丁或功能的兼容性承诺。官网是动态站点；此目录不是运行时健康监测。
- 所有清单有中英描述与相关中文 aliases。未复制第三方 logo / favicon 或软件二进制；只有原创通用图标和占位图。license/licenseUrl 保留但没有猜填分发许可。
