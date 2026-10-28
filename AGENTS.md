# 开发与 Agent 协作约定

本项目为 Windows x64 原生 C++ DivaModLoader 插件，当前版本 1.0.0。这是人与 AI 协作开发的 Vibe Coding 项目，所有提交仍需可审阅的代码和可复现的验证。

## 先读什么

先读 README.md、docs/STATUS.md、docs/IMPLEMENTATION_HANDOFF.md、design/INTERACTION_SPEC.md；修改定位时同时阅读 docs/research/NATIVE_SELECTION_METHODS.md。docs/ACCEPTANCE.md 区分自动测试与实际游戏测试。

## 工作约定

- 使用中文沟通、注释和控制台提示，文本保存为 UTF-8。新增函数、方法、回调及参数要有中文说明。
- 命令以仓库根目录为工作目录；产物放在 build/ 或 artifacts/。不提交游戏文件、个人路径、完整运行日志、认证信息和部署备份。
- third_party/reference 仅供阅读且被忽略；构建依赖为 third_party/vendor，更新时记录来源、版本及许可证。
- 游戏内帧回调不得阻塞等待，不在 DllMain 中初始化业务；保留其他 Mod 的输入与函数 hook 链。
- 仅在已确认菜单、版本和对象上下文提交定位；用真实高亮、难度、风格确认成功，不能只读请求缓存。
- 不把自动模拟、界面预览、静态反汇编记为实际游戏通过。没有测过的风格、模式和构建应明确列出。
- 部署需有用户授权、退出游戏、保存备份，只修改本 Mod。不得强制结束游戏、自动演奏或擅自改写其他 Mod、存档、加载顺序。
- 保留 QuantumSwitch、MikuMikuSwitchPlugin 等参考来源与致谢，不删除许可证和作者声明。

## 常用命令

```powershell
./scripts/build.ps1
./scripts/verify.ps1 -GameDirectory '游戏安装目录'
./scripts/package.ps1
```

不为小型可逆样式调整编写镜像测试，使用共用原生预览检查。定位、输入、来源解析等行为修改要补充有意义的回归。

如确需新增 Python，函数及参数加中文注释；交付时提供执行命令。包含多个参数时，首行仅放解释器和脚本，每个参数另起一行，缩进两个空格，用反斜杠连接。
