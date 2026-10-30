# 依赖与数据来源

本项目代码以 GNU AGPL-3.0-or-later 发布，许可证见 LICENSE。发行同时附带可重建源码；没有网络服务或运行时 Python 依赖。

| 内容 | 固定版本或提交 | 来源与许可证 |
| --- | --- | --- |
| Dear ImGui 核心、Win32、DX11 后端 | v1.88，提交 9aae45eb4a05a5a1f96be1ef37eb503a12ceb889 | https://github.com/ocornut/imgui ，MIT；只读复用歌词项目中一致的核心与后端，许可证随源文件保存 |
| Microsoft Detours | v4.0.1，e4bfd6b03e50de46b47abfbd1e46b384f0c5f833 | https://github.com/microsoft/Detours ，MIT；由本工程从源码编译，不使用来源不明的预编译库 |
| toml++ | v3.1.0 | https://github.com/marzer/tomlplusplus ，MIT；单头文件内含完整许可证，从下述 DML 参考取得 |
| DIVA Mod Loader 接口与优先级规则参考 | 0abc2c89229c4b307bfa1555656c54c2e5cff480 | https://github.com/blueskythlikesclouds/DivaModLoader ，MIT；阅读 CodeLoader、ModLoader、DatabaseLoader，非运行时静态依赖 |
| QuantumSwitch 的 PC 选曲协议与地址参考 | 1001c43d67833b750f2a21def8c112c7b476f9aa | https://github.com/Snapmali/QuantumSwitch ，AGPL-3.0；未调用其 HTTP 服务，原生重写跨帧状态机 |

QuantumSwitch 对选曲研究的原作者致谢包括 hiki8man 的 Select Song with PVID 与 sasnchis 的 DivaSongViewer。本项目保留这一研究来源，不把地址研究声称为本项目原创。

原界面定位另参考 MikuMikuSwitchPlugin 的 SongQuickJump（提交 04d3f174dc62615a1178b914e3a4ded87665b98c，https://github.com/lsmsmx/MikuMikuSwitchPlugin ，仓库附 GPL-2.0 文本），并以 NewClassics PC 的对象声明和函数入口核对 PC 适配（提交 c41d4f1c7113ff44dbc7afb888803ea252e201f6，https://github.com/mrcloverthecoder/nc ，MIT，Copyright (c) 2025 mrcloverthecoder）。NewClassics 许可证附于 third_party/vendor/licenses/NewClassics.txt。上述参考仓库不参与构建，也不是新增运行时依赖。具体流程和验证边界见 docs/research/NATIVE_SELECTION_METHODS.md。

data/base_pv_db.txt 与 data/dlc_pv_db.txt 仅保留官方地区数据库中的歌曲名称、英文名称、读音、逻辑 PVID 与谱面路径及等级元数据；不包含音频、图像、脚本或模型。来源是已经定向解出的 pv_db.txt 和 mdata_pv_db.txt，未重新解包。游戏内容权利属于原权利人；这些元数据不随本项目代码许可证重新授权。成品不依赖开发环境的资产导出目录。

中文别名为少量可编辑示例，不声称覆盖所有 Mod 的中文译名；在 aliases.toml 按 PVID 扩展。没有数据库读音的汉字不推测唯一罗马音。

中日字体在运行时只读使用 Windows 安装的微软雅黑，不重新分发字体。依赖文件 SHA-256 清单由打包脚本生成，见发行包 dependencies.sha256。

## miniz 3.0.2
来源：https://github.com/richgel999/miniz/tree/3.0.2 。使用 MIT 许可的低层 tinfl 解压器，许可证保留于 third_party/vendor/miniz/LICENSE。miniz_tinfl.cpp 是原始 miniz_tinfl.c 的 C++ 编译副本；本地 miniz.h 仅引用 tinfl，miniz_export.h 使用静态链接导出宏。

## MikuMikuLibrary CPK／精灵桥接
来源：MikuMikuLibrary 固定提交 `82380f1e40bbcf07d19c4ac8d0bdb95845be3628`，MIT，Copyright (c) 2023 Skyth；上游许可证见 `third_party/vendor/mml-cpk/LICENSE.md`。本项目在 `AotBridge/Bridge.cs` 添加 NativeAOT C ABI，用于只读枚举和读取官方 CPK 条目、查询 SpriteDatabase 实际精灵索引，以及按 SpriteSet 矩形解码预览。CPK 与 SpriteSet 格式读取仍由 MikuMikuLibrary 实现，未在本项目另写 CPK 提取器。上游传递依赖版权和许可证汇总见 `third_party/vendor/mml-cpk/COPYRIGHT.md`。

## BC7 与官方精灵解码

bcdec v0.985，固定提交80859ed3b7afb1c527a2a99d70c61457bea72d0c，来源https://github.com/iOrange/bcdec，使用其MIT许可选项，完整声明保留在third_party/vendor/bcdec/bcdec.h。DivaTextureDecoder.dll是受限BC7块解码桥接。

官方YCbCr精灵的BC5双平面合成参照固定MikuMikuLibrary提交的TextureDecoderCore.cpp实现系数及线性采样，保留上游MIT声明；未复用其C++/CLI运行依赖。桥接对SpriteDatabase/SpriteSet/TextureSet/SubTexture增加声明数量、源纹理尺寸和字节上限检查；归档与精灵读取均复用上游结构解析。
