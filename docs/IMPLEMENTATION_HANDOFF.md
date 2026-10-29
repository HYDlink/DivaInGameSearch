# 开发指南

这是面向维护者与 AI Agent 的工程入口，不依赖原作者的目录或聊天记录。

## 源码结构

| 位置 | 职责 |
| --- | --- |
| src/core.*、catalog.cpp | 数据模型、检索、来源合并与别名 |
| src/ui.* | 游戏与预览共用的搜索界面 |
| src/customization* | 服装／饰品导入、独立搜索、键盘格子和安全能力 stub；不写游戏内存 |
| src/host.* | Win32、IME、DX11、图形资源与输入捕获 |
| src/game.* | 游戏构建校验、对象读取、原生列表定位 |
| src/selector.* | 请求校验、跨帧结果确认、超时与恢复 |
| src/mod.cpp | DML 回调、初始化、定位提交和诊断 |
| src/preview.cpp | 不访问游戏内存的界面预览 |
| tests/ | 核心测试、输入宿主、只读探针及受控调试工具 |
| scripts/ | 构建、验证、打包、部署工具 |

先执行 scripts/build.ps1。scripts/verify.ps1 和 scripts/check-profile.ps1 必须显式提供 GameDirectory，不能假定固定盘符。部署与更新脚本也要求显式目录，运行中游戏会被拒绝更新。

服装／饰品实现依据在 [CUSTOMIZATION_SEARCH_PLAN.md](CUSTOMIZATION_SEARCH_PLAN.md)，实际导入范围在 [CUSTOMIZATION_IMPORT.md](CUSTOMIZATION_IMPORT.md)。`SearchPreview --customization-demo` 只为独立预览显式生成合成数据；游戏宿主没有导入数据时必须为空库。未验证的当前角色读取、原生定位和纹理能力保持禁用。`InputCheck --messages-only <项目目录>` 可单独验证 Win32 消息生命周期，但不替代 DX11 和实际游戏输入验收。

## 修改定位之前

阅读 research/NATIVE_SELECTION_METHODS.md，区分菜单切换恢复与原位列表调用。正式游戏适配使用后者，不在失败后自动将玩家送入播放列表。实际高亮、请求缓存、演奏数据、风格偏好是不同语义，不能互换。

优先保留现有函数 hook 链。地址只对已核查构建有效；新增 profile 要有来源、对象核对和运行证据。只读探针与截图工具用于定向排错；ControlledSwitch 会写游戏菜单，只能在授权的开发环境受控使用，不是普通安装步骤。

## 发布

保持 CMakeLists.txt、config/config.toml、初始化日志和打包脚本版本一致。运行相关检查，检查原生预览，更新 STATUS、ACCEPTANCE 与 RELEASE_NOTES。scripts/package.ps1 生成包含运行文件、许可证和可重建源码的 ZIP。公开发行不包含 artifacts 下的私人日志、审计表和部署记录。
