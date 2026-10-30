# 1.0.6

服装搜索作为正式功能发布，保留原有歌曲搜索。用户已在实际游戏中确认服装原生定位可用。

- 自动合并官方、DLC和启用Mod的服装目录，显示真实图片，无需手工导出或安装.NET。
- 支持名称、英文、平片假名、别名、ID、角色与来源筛选；隐藏无关联来源，官方与DLC聚合显示。
- 单击卡片、Enter或定位按钮定位原生列表，方向键浏览；顶部使用分段导航，额外设置按需展开。
- 修复正常模型列表状态2被拒绝，以及原始模型字段非零导致误判的问题；定位成功后沿用关闭面板设置。
- 歌曲搜索保留后台查询、输入隔离、难度选择和原生定位。
- 9项自动回归、歌曲目录回归、DLL导出与独立DX11宿主检查通过；这些不替代全部游戏模式的实机验收。

安装：退出游戏，将DivaInGameSearch文件夹放入mods；保留三个DLL。更新已有安装时保留自己的settings.ini和aliases.toml，无需修改其他Mod或加载顺序。

边界：服装定位需要已进入原生模型列表，不自动装备、购买、保存或演奏。跨菜单进入模型、饰品原生定位、任意页面返回选曲尚未提供；全分类／全槽位／所有Mod组合未全面验收。

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
