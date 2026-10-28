# 技术资料来源与致谢

感谢社区作者公开源码、逆向记录与工具。本项目的定位研究依赖这些成果，以下说明分别参考了什么，并固定所读版本。

| 项目 | 固定提交 | 用途 |
| --- | --- | --- |
| [QuantumSwitch（量子开关）](https://github.com/Snapmali/QuantumSwitch) | 1001c43d67833b750f2a21def8c112c7b476f9aa | PC 菜单状态、PVID/难度恢复协议、曲库解析与搜索数据组织 |
| [MikuMikuSwitchPlugin](https://github.com/lsmsmx/MikuMikuSwitchPlugin) | 04d3f174dc62615a1178b914e3a4ded87665b98c | SongQuickJump 原界面列表重建、筛选与定位流程；游戏内搜索交互 |
| [NewClassics](https://github.com/mrcloverthecoder/nc) | c41d4f1c7113ff44dbc7afb888803ea252e201f6 | PC 选曲对象、列表函数与谱面风格处理，是将直接路线适配到 PC 的重要依据 |
| [DivaModLoader](https://github.com/blueskythlikesclouds/DivaModLoader) | 0abc2c89229c4b307bfa1555656c54c2e5cff480 | 插件回调、加载优先级、数据库合并、名称 hook 与存取研究 |
| [ReDIVA](https://github.com/korenkonder/ReDIVA) | 207c270472ba3f0edcda29bae777e5bd4a8952d9 | 数据格式与后续 Module、定制物品研究；不是商业 PC 版 SDK |

特别感谢 **QuantumSwitch** 与 **MikuMikuSwitchPlugin**：前者提供菜单生命周期恢复路线的重要依据，后者启发当前界面的直接定位路线。两者不是同一方案，Switch 地址也不能直接用于 PC。具体比较见 [实现研究](research/NATIVE_SELECTION_METHODS.md)。

QuantumSwitch 上游同时致谢 hiki8man 的 [Select Song with PVID](https://gamebanana.com/tools/21051) 和 sasnchis 的 [DivaSongViewer](https://github.com/sasnchis/DivaSongViewer)。本项目保留对这些原始研究者的感谢。

服装方向另参考 [Armoire](https://github.com/jaykthx/Armoire) 的数据库编辑思路。底层依赖为 [Dear ImGui](https://github.com/ocornut/imgui)、[Detours](https://github.com/microsoft/Detours)、[toml++](https://github.com/marzer/tomlplusplus)，版本与许可见 [依赖锁定](DEPENDENCY_LOCK.md) 和 [第三方声明](../THIRD_PARTY_NOTICES.md)。

包内基础与 DLC 数据是游戏数据库中的名称、读音、PVID 和谱面等级等元数据派生；不包含音频、封面、模型或谱面脚本。它们不因本项目开源而改变权利归属。

参考仓库可按上述 URL 与提交自行获取；不需保留开发者的资产导出路径。运行时读取发行元数据与玩家自己的游戏/Mod 配置。
