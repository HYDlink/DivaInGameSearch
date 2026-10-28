# 歌曲搜索架构选择

已采用 C++、DivaModLoader、Dear ImGui Win32/DX11，在游戏内完成搜索与定位。检索层管理逻辑歌曲、别名、来源与生效谱面；界面输出请求；游戏适配层核对版本、菜单及真实结果。

早期考虑的外部服务/API 桥接方案未采用。正式版本不依赖 QuantumSwitch 后端，但参考其公开协议与数据组织；当前直接定位另参考 MikuMikuSwitchPlugin 和 NewClassics PC 源码。

曲库身份使用逻辑 PVID，不能使用搜索行号或物理 DSC 编号替代。按 DML 配置合并来源，并按风格与难度判断生效谱面。未知读音不凭空推测，缺失难度不自动降级。

输入法组词、游戏输入隔离、其他 Mod 的 hook 链与图形资源生命周期独立处理。静态实现和预览不代替实际游戏测试。

技术细节见 [两种定位路线](NATIVE_SELECTION_METHODS.md)，资料归属见 [来源与致谢](../SOURCES.md)。
