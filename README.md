# DIVA 游戏内搜索

《初音未来 Project DIVA Mega Mix+》的搜索 Mod。在游戏里按 **Ctrl+F**，直接搜歌曲、服装、发型和自定义道具，选中后跳过去，不用再一页页翻。

![歌曲搜索界面](docs/images/search.png)

## 安装

1. 先装好 [DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader)。
2. 从 [Releases](https://github.com/HYDlink/DivaInGameSearch/releases/latest) 下载压缩包，退出游戏，把里面的 `DivaInGameSearch` 文件夹放进游戏的 `mods` 目录。
3. 用 Mod 管理器启用它，然后进游戏按 Ctrl+F。

更新时同样先退出游戏，覆盖文件即可；想保留自己的设置，就别覆盖 `settings.ini` 和 `aliases.toml`。包里的三个 DLL 都要留着。卸载时删掉这个文件夹，不影响存档和其他 Mod。

## 能做什么

**搜歌**：原名、英文、中文别名、假名、罗马音、PVID 都能搜。可以按曲包、风格、难度筛选，按相关度、PVID、名称或等级排序。

**按歌找服装和发型**：在服装页或发型页直接输入歌名，就能看到这首歌推荐的服装和发型。

![服装搜索界面](docs/images/modules.png)

**服装**：显示真实图片，官方、DLC 和已启用的 Mod 都会读取。可以直接预览和换装，只能换已拥有的，绝不会替你购买。

**发型和自定义道具**：同样可以搜索、定位和更换。发型默认显示当前歌者的，其他角色的只能看，不能换。

搜索只负责定位，不会替你开始演奏。

## 操作

| 操作 | 按键 |
| --- | --- |
| 打开搜索 / 全选搜索框文字，方便重新输入 | Ctrl+F |
| 选择结果 | ↑ ↓（服装等卡片还能用 ← →） |
| 选择歌曲难度 | ← → |
| 在当前页面定位 | Enter 或单击 |
| 从其他页面跳过去 | 连按两次 Enter 或双击 |
| 预览 / 换装（服装、发型、道具） | Ctrl+P / Ctrl+E |
| 切换顶部标签 / 回切 | Tab / Ctrl+Tab |
| 在搜索框里移动光标 | Ctrl+← → |
| 关闭 | Esc 或点击窗口外 |

点击歌曲的难度按钮，也能直接跳到那首歌的那个难度。

## 设置

`settings.ini` 可以改快捷键、字号、行高和透明度。`aliases.toml` 可以给歌曲加自己习惯的别名，改完在面板里点“刷新曲库”。

增删曲包后要重启游戏，否则搜到的内容可能和游戏已加载的不一致。

## 已知问题

- 特殊泳装的推荐继承还没有完整支持，推荐不确定时界面会说明。
- 不同游戏版本、Mod 组合下的表现不能全部保证，已测过的范围见[验收记录](docs/ACCEPTANCE.md)。
- 定位后，封面或作者信息偶尔会短暂停留在上一首。

遇到问题可以到 [Issues](https://github.com/HYDlink/DivaInGameSearch/issues) 反馈，带上复现步骤和 `search-diagnostics.log`，发出前记得去掉不想公开的路径。按键失灵可以先看[排错指南](docs/INPUT_TROUBLESHOOTING.md)。

## 开发

需要 Windows x64、PowerShell 7、Visual Studio C++ 工具集、Windows SDK 和 CMake 3.24+。依赖都在仓库里。

```powershell
./scripts/build.ps1
./scripts/verify.ps1 -GameDirectory '你的游戏目录'
./scripts/package.ps1
```

从 [AGENTS.md](AGENTS.md) 和[开发指南](docs/IMPLEMENTATION_HANDOFF.md)开始看。本项目由人和 AI 协作开发，欢迎参与。

## 致谢

这个 Mod 建立在社区公开的研究之上：

- **[QuantumSwitch](https://github.com/Snapmali/QuantumSwitch)**：选曲状态、PVID 定位和菜单切换的研究，以及歌曲数据解析的思路。同时感谢其致谢的 **hiki8man（Select Song with PVID）** 和 **[sasnchis（DivaSongViewer）](https://github.com/sasnchis/DivaSongViewer)**。
- **[MikuMikuSwitchPlugin](https://github.com/lsmsmx/MikuMikuSwitchPlugin)**：`SongQuickJump` 提供了游戏内搜索和直接重建列表的流程参考。
- **[NewClassics](https://github.com/mrcloverthecoder/nc)**、**[DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader)**、**[ReDIVA](https://github.com/korenkonder/ReDIVA)**：选曲对象、插件加载和数据结构资料。
- 界面和工程使用 [Dear ImGui](https://github.com/ocornut/imgui)、[Microsoft Detours](https://github.com/microsoft/Detours) 和 [toml++](https://github.com/marzer/tomlplusplus)。

具体用途、版本和许可见[资料来源](docs/SOURCES.md)、[第三方声明](THIRD_PARTY_NOTICES.md)和[依赖锁定](docs/DEPENDENCY_LOCK.md)。

## 许可证

代码以 **AGPL-3.0-or-later** 发布，见 [LICENSE](LICENSE)，第三方组件保留各自许可证。游戏名称、歌曲和相关内容的权利属于原权利人，本项目不是官方产品。
