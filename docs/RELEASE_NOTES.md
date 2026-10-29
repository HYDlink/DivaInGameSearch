# 未发布

- 搜索框内长按上下连续选曲，左右跳过无谱面难度；目标难度加粗边框，每次打开沿用游戏难度。
- Tab / Shift+Tab 可访问筛选、排序和工具，支持全键盘操作。
- 150 毫秒防抖、后台检索、过期结果丢弃和名称规范化缓存；Enter 等最新查询完成再定位。
- 关闭后检查物理释放状态，防止缺失 key-up / mouse-up 使游戏一直被隔离；补充输入摘要日志和收集指南。
- 新增视图与输入回归；本地 Windows MSVC、DX11 宿主与五组自动验收已通过，实际游戏验证尚待完成。
- 修正受控宿主快速退出时后台任务越过运行库销毁阶段的问题；测试失败使用进程退出码，不再弹出系统错误框。
- 新增服装／饰品基础浏览：版本化本地导入、已解包文本表、角色／部位／关联 Mod 筛选、格子键盘导航和详情。
- Future Tone Customization 的公开 sprite 资源已形成研究计划；本轮预览使用占位，原生定制定位及装备保持关闭。

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
