# 构建依赖锁定

记录日期：2026-10-29。

- 编译器：验证环境 MSVC 14.29.30133，cl 19.29.30153，Windows SDK 10.0.22621.0。更高版本工具集可用时脚本优先选择，但要求存在完整 include/lib。
- 构建：CMake 4.2.3、NMake、C++20，Detours 按上游兼容设置单独使用 C++14；全部目标静态 CRT。
- ImGui：1.88，9aae45eb4a05a5a1f96be1ef37eb503a12ceb889。核心、头文件、Win32 和 DX11 后端来自同一只读本地版本，源码随包保存，未混用新版本后端。
- Detours：4.0.1，e4bfd6b03e50de46b47abfbd1e46b384f0c5f833，官方 https://github.com/microsoft/Detours ，从本工程源码编译。最初检查过的 DML 预编译库已移除，不参与发行。
- toml++：3.1.0 单头文件，从 DivaModLoader 0abc2c89229c4b307bfa1555656c54c2e5cff480 的 Dependencies/toml.hpp 复制；文件内含 MIT 许可。
- PC 选曲参考：QuantumSwitch 1001c43d67833b750f2a21def8c112c7b476f9aa，https://github.com/Snapmali/QuantumSwitch 。没有引用其 Python 模块或启动 HTTP API。
- DML：接口协议参考官方 0abc2c89229c4b307bfa1555656c54c2e5cff480，不把 DML 本体打入发行包。

随发行生成 dependencies.sha256，列出实际 vendor 源码与许可证的 SHA-256；发行目录 manifest.sha256 校验二进制与配置。third_party/reference 不进入可重建源码包。

## 支持的 PC 构建资料

可执行文件 SHA-256：6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D。

PE TimeDateStamp：1666920875；SizeOfImage：602353664。运行时核查 PE 构建标记，并核查输入函数未覆盖尾部和数据范围。这里的 SHA-256 是只读资料记录；运行时不为每次初始化重新散列大型游戏文件。

地址均相对 PC 主模块：状态 0xCC61094/98、切换标记 0xCC610A0、请求字段 0x12B634C/50/54/5C。菜单高亮通过实际选曲任务路径读取；applySel RVA 0x6EFCF0 的相对调用解析只读对象，提供独立实际难度及模式。不能把选曲时为 -1 的演奏 PVID 当成高亮。详细对象与直接函数入口见 research/NATIVE_SELECTION_METHODS.md。

输入函数适配入口为 RVA 0x2AC970，启动时检查特征并保留现有输入 hook 链。这里只记录已核查构建的适配值，不能替代其他构建的签名验证。

New Classics 通过唯一可执行段模式解析风格变量与 GSWindow，实际风格应读取当前选项和索引，而不是只读取偏好缓存。跨风格仍需回归。参考提交与许可见 SOURCES.md。

## 官方基础元数据派生

data/base_pv_db.txt：已有定向资料 main-region-extract/rom_steam_region/rom/pv_db.txt，保留 2,880 行名称、读音与谱面元数据。

data/dlc_pv_db.txt：已有 dlc00-region-extract/rom_steam_region_dlc/rom/mdata_pv_db.txt，保留 1,157 行。没有重新全量解包，没有复制音频、模型或 DSC 内容；运行时不引用上述开发机路径。
