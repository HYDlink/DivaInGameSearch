# 1.0.0

首次正式发布 DIVA 游戏内搜索。这是一个人与 AI 编程助手协作完成的 Vibe Coding 项目。

- 游戏内原生搜索面板，默认 Ctrl+F。
- 曲名、英文、别名、假名、罗马音、PVID 与 Mod 检索。
- 曲包、风格、难度筛选及结果排序。
- 点曲名或具体等级，在当前选曲页定位歌曲与难度；不会自动演奏。
- 筛选、定位、排序分行显示，难度按钮强化边框与悬停反馈。
- Windows x64、静态 CRT，运行时无需外部服务；发行附源码及许可证。

实际游戏确认了街机风格下换歌和难度切换，包括 EX EXT。其他风格、Eden、更多构建、IME/DPI 与预览资源同步仍有待完善，详见 README 的支持边界。

特别感谢 [QuantumSwitch（量子开关）](https://github.com/Snapmali/QuantumSwitch) 与 [MikuMikuSwitchPlugin](https://github.com/lsmsmx/MikuMikuSwitchPlugin) 的公开研究；PC 适配同时依据 NewClassics 和 DivaModLoader。完整来源与许可见仓库 THIRD_PARTY_NOTICES.md。

下载 DivaInGameSearch-1.0.0-win64.zip，退出游戏后将其中 DivaInGameSearch 文件夹放入 mods 并启用。更新请保留自己的 settings.ini 和 aliases.toml。
