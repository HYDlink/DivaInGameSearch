# 服装与饰品本地导入

此功能目前提供搜索和详情浏览。Enter 只打开详情，所有原生定位、装备、写存档与原生预览能力均未接入。没有真实导入元数据时显示空库，不自动生成演示条目。未能读取已验证的定制菜单角色时，默认筛选初音未来并显示原因；可手动改为全部角色。

`customization_catalog.toml` 是**本插件定义的版本化导入格式，不是 DivaModLoader 标准**。放在本插件目录，即 `settings.ini` 同级。运行时只读本地文件，不联网、不解包 FARC／CPK、不读取模型／纹理。发现启用 include root 的实体 FARC 只产生诊断。原版／DLC 也必须由用户提供本地合法解包后的元数据；发行包不附游戏数据。

## 最终目录格式

下面只是格式示意，不是真实游戏项目；不要把这些编号当作游戏内存在的物品。用户应替换为自己已确认的数据。游戏宿主不会自动导入文档中的示例。

```toml
schema_version = 1
language = "cn"
# 可选：记录生成此目录时诊断中显示的DML指纹；不一致会标为过期。
# dml_fingerprint = "从诊断复制的指纹"

[[sources]]
id = "my-local-import"
name = "用户本地导入"
# 可选：只有与当前DML启用Mod目录和include完全匹配，才标为当前Mod。
# mod_directory = "MyMod"
# include_root = "."

[[items]]
kind = "module"                    # module 或 accessory
id = 10001                         # 非负整数；0合法，负数随机/取消项不导入
source = "my-local-import"
name = "用户确认的服装名称"
chara = "MIKU"
cos = "COS_001"                    # 原始字符串，不是Module ID
sort_index = 1                     # 可选；缺失排最后
aliases = ["用户维护的别名"]
names.cn = "用户确认的中文名称"       # 可选真实语言名，也参与搜索

[[items]]
kind = "accessory"
id = 10001                         # 与服装同数字仍是独立实体
source = "my-local-import"
name = "用户确认的饰品名称"
chara = "ALL"
parts = "FACE"
obj_id = 0                         # 可选，不能代替饰品ID
bind_module = 0                    # 可选，保留原值；0和负数语义尚未验证
```

`id` 是来源稳定身份，`name` 允许重复。不同来源即使显示名相同，也分别筛选；UI 附目录和 include root，离线来源附“导入”标记。最终清单按书写顺序整条覆盖重复 `{kind,id}`，并记录诊断；低优先级记录的贡献来源仍保留。请给每个实体明确的最终定义，避免在最终清单中人为模拟游戏数据库优先级。

**最终清单始终称为“导入目录”，不能证明当前游戏的有效、可装备或已解锁集合。** 不匹配启用 Mod/root 的手工 `items` 仍能作为离线导入目录浏览，来源不会被标成当前 Mod。本插件没有验证最终目录是否完整覆盖 DML 的运行时合并与资源覆盖。

已知角色为 `MIKU/RIN/LEN/LUKA/NERU/HAKU/KAITO/MEIKO/SAKINE/TETO`；饰品另有 `ALL`。角色未知时保留原 token，归入未知角色；服装的 `ALL` 也归入未知。饰品部位支持 `ZUJO/FACE/NECK/BACK`；其他值归入未知部位，发型等扩展类型不在本轮四部位适配范围。选具体角色时，饰品包含 `ALL` 共通项；未知条目只在全部／未知筛选中出现。

名称显示顺序为 DML 名称补丁（见下文）、清单 `names.<language>`、`name`、自动编号占位。所有真实语言名、原始表名和别名都参加搜索；不同项目不会共同满足一条多词查询。名称模式也匹配十进制 ID，来源模式匹配全部关联来源的身份、显示名和目录。角色、部位、来源过滤取交集。

## 登记已解包的实体文本

只有显式登记的 `gm_module_id.bin` 和 `gm_customize_item_id.bin` 才会读取。这两个内部条目是 UTF-8 `key=value` 文本；不要把 FARC 或其他二进制文件改后缀后登记。先用用户自行选择的离线工具解包，保持原 Mod 不变，把文本副本放到本插件子目录。运行时不执行外部解包器。

在以上清单的 `sources` 后增加：

```toml
[[tables]]
source = "my-local-import"
kind = "module"
path = "imports/my-source/gm_module_id.bin"     # 相对本插件目录
mode = "append"                               # append 或 replace，必须明确
logical_path = "rom/mod_gm_module_tbl.farc"     # 原始数据库逻辑路径，必须明确

[[tables]]
source = "my-local-import"
kind = "accessory"
path = "imports/my-source/gm_customize_item_id.bin"
mode = "replace"
logical_path = "rom/gm_customize_item_tbl.farc"
```

`path` 禁止绝对路径、`..` 和经链接解析后的目录逃逸。`logical_path` 是原始数据表的身份记录，不由本插件自动推测；请准确登记原始普通表或 DML 追加表。来源有 `mod_directory` 时必须匹配当前启用的 DML include root，否则该来源的 `tables` 跳过，并显示诊断。没有 `mod_directory` 的来源属于用户明确导入的离线基础快照，优先级低于已匹配的当前 Mod。不同的普通数据库版本应使用独立导入目录，不能把不相容版本混进同一逻辑路径。

普通 `replace` 表同种类、同 `logical_path` 只读取优先级最高的文件；同优先级选登记较早的表。所有普通表先载入，再将 `append` 表按 DML 低到高优先级整条覆盖 `{kind,id}`。已匹配 Mod 的优先级来自当前 DML 配置，用户不能在清单里自填优先级提升。没有游戏实际加载结果可核对，因此复杂或混合的输入组合仍只称导入目录。`items` 先载入，登记表可覆盖它们；建议一次选一种实体导入方式，减少歧义。

原始文本按完整键和表内序号分组，使用记录中的 `.id`，保留未知字段。第一处 `=` 分隔，支持 BOM、CRLF、空行与字段乱序。声明长度上限 100000；序号不得超过声明长度，缺 ID、负 ID、超范围 ID 会跳过。同文件同 ID 采用较大表内序号的整条记录并诊断，缺失字段不会从旧记录继承。`cos`、`obj_id`、`bind_module` 不用于推测实体 ID，也不构成可定位能力。

## DML 来源和名称规则

读取游戏 `config.toml` 的 `enabled/mods/priority`，只扫描列出的启用 Mod，再扫描其配置中启用的 `include`。空／缺 priority 按目录字母顺序回退并提示与运行版本待核对。Mod 和 include 的第一个目录优先。当前最小适配只接受游戏目录内的相对 `mods` 路径与 Mod 目录内的 include root；逃逸路径会跳过。

每个启用 root 的 `rom/lang2/mod_str_array.toml` 可为已有实体提供名称：

```toml
# 这里同样只是格式示意，名称不会证明实体存在。
cn.module.10001 = "中文服装名称"
module.10001 = "Generic Module Name"
cn.customize.10001 = "中文饰品名称"
cstm_item.10001 = "兼容别名"
```

每个 root 先本插件 `language`（默认 `cn`），再通用；root 从高优先级到低优先级，首次写入决定显示名。饰品同语言内 `customize` 优先于 `cstm_item`。因此高优先级通用名可覆盖低优先级中文名。其他语言只参加搜索；它们的来源同样记入关联来源。孤立名称只累计诊断，不生成实体或格子。旧平面字符串偏移本轮不猜，也不自动将数字名称减去某个常数。

详情分别展示定义来源、名称来源和全部关联贡献；被覆盖的定义仍可按关联来源筛选，但不会变成另一个可装备版本。该来源的定义未被采用时会明确标记。

## 刷新、指纹和边界

刷新在后台读取清单、登记的文本表、DML 配置和名称补丁，构建不可变快照。查询使用150毫秒防抖、独立版本和快照代数，旧结果不能覆盖新输入。指纹包含所读文件的相对身份／完整本地路径、大小与mtime；它不是安全哈希，不写入在线服务。DML指纹只供本地比对，移动目录后应重新生成；在诊断中复制 `当前DML配置指纹` 到可选 `dml_fingerprint` 可标记最终清单的生成环境。指纹不匹配标为来源配置已变化／导入目录过期，不宣称当前生效。原始归档没有被解析，登记文本与归档是否一致仍需用户离线导入时核对。

刷新不会改变游戏已经加载的数据，也不会修改 Mod、加载顺序、装备或存档。预览一律“暂无预览”；没有 PNG/FARC/CPK/游戏纹理加载器。当前角色只读适配返回未知；能力报告中原生读取、定位和原生预览均禁用。

键盘可用 Tab 到达各控件与单个格子入口；格子内方向键按行列移动，保留末行列意图，Home/End 跳首尾，PageUp/PageDown 按完整可见行滚动。搜索框左右保留光标编辑；输入法组词与下拉框展开时不接管格子导航。Enter／空格仅打开详情，Esc 先退出详情／下拉再关闭面板。

自动回归见 `tests/customization_check.cpp`，目标为 `CustomizationCheck`。测试使用临时手写格式夹具，不包含游戏数据。交叉编译与隔离逻辑测试不能代表 Windows DX11、真实输入法或实际游戏定制菜单已经验证。
