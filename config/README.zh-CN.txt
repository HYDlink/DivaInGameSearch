DIVA 游戏内搜索
English: see README.en.txt

《初音未来 Project DIVA Mega Mix+》的搜索 Mod。
在游戏里按 Ctrl+F，搜歌曲、服装、发型和道具，选中就跳过去。
只能更换已拥有的物品，不会替你购买，也不会替你开始演奏。

=== 安装 ===
1. 先装好 DivaModLoader：
   https://github.com/blueskythlikesclouds/DivaModLoader
2. 退出游戏，把整个 DivaInGameSearch 文件夹放进游戏的 mods 目录。
3. 用 Mod 管理器启用。

=== 更新和卸载 ===
更新：退出游戏后覆盖。想保留设置就别覆盖 settings.ini。
卸载：删掉这个文件夹，不影响存档和其他 Mod。

=== 按键 ===
Ctrl+F              打开搜索；已打开时全选搜索文字
Tab / Ctrl+Tab      切换顶部标签 / 回切
方向键              选择结果和难度
Enter 或单击        在当前页定位
连按两次 Enter 或双击   从其他页跳过去
Ctrl+P / Ctrl+E     预览 / 换装
Esc 或点窗口外      关闭

快捷键、字号、行高、透明度、界面语言和主题可以在 settings.ini 里改。
界面默认是珍珠白，点面板右上角的

=== Luma／HDR 界面亮度 ===
默认自动适配已识别的 DIVA Luma HDR10／scRGB 输出，保持搜索界面和缩略图的原有层次。
ui_white_nits=auto 按 Luma 已保存的显示模式与白亮度设置解析；普通 SDR 保留原绘制方式。
HDR 下主窗口背景的透底程度按原 SDR 观感保持，opacity 仍使用你设置的原值。
如果用过早期测试包，并保留了 ui_white_nits=203，请将它改成 auto。
重命名的插件或自定义 ReShade 配置路径可能需要手动设置，排查时查看日志中的“界面色彩：”。
ui_color_output=sdr 可恢复原有绘制方式；修改配置后重启游戏或使用“重载样式”。

=== 自定义搜索服装名 ===
先打开一次模型搜索，Mod 文件夹会生成 module_names.toml。
表中每行带服装 ID、当前名、原名、角色和来源；只填写等号右侧的名称。
留空使用原名，填写后保存为 UTF-8，再从搜索的“选项”刷新目录。
只影响搜索面板，原来的名字仍能搜到，游戏原生菜单不变。
个人表不会被程序覆盖；新增服装可在 module_names_reference.toml 查找后复制对应行。
自动参照表会刷新，请勿把个人名称填写在参照表里。

=== 注意 ===
游戏更新后，如果主程序和已核实的版本不同，定位和换装会自动关闭，
需要等 Mod 更新。

=== 主页和反馈 ===
下载、源码和完整说明：https://github.com/HYDlink/DivaInGameSearch
演示视频：https://www.bilibili.com/video/BV1JMxm6YEsN
遇到问题请到 GitHub Issues 反馈，附上复现步骤和 search-diagnostics.log，
发出前去掉不想公开的路径。

=== 许可 ===
代码以 AGPL-3.0-or-later 发布，见 LICENSE。
第三方组件的许可见 THIRD_PARTY_NOTICES.md 和 licenses 文件夹。
游戏名称、歌曲和相关内容的权利属于原权利人，本项目不是官方产品。
