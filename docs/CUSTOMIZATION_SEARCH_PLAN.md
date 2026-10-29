# 服装与饰品搜索实施计划

日期：2026-10-29。范围：Windows x64 Mega Mix+、DivaModLoader（DML）。本文为实施计划与公开源码研究，不是实际游戏通过记录。云环境没有游戏、用户 Mod 资产或原生定制菜单内存样本。

## 本轮交付结论

可以先交付与歌曲共用入口的「歌曲／服装／饰品」模式，服装和饰品各自按名称、角色、Mod 来源搜索，使用纵向滚动格子和完整键盘导航。当前角色读取先留只读接口，尚未接线时默认初音未来并显示原因。排序先提供「默认顺序／名称／ID」。

本轮只交付检索与浏览基础流程。定位按钮保持禁用，显示「原生服装／饰品定位尚未接入」；Enter 打开所选项目详情。没有游戏适配时不能调用歌曲定位器、改变角色槽位、装备物品、写存档或游戏内存。以后定位也只改变原生高亮和预览，装备仍由玩家在原生菜单确认。

能取得真实实体元数据后即可搜索；仅有名称补丁时不能推断出完整实体。仓库现有 `data/` 只有 PVDB，没有官方服装／饰品表。空目录必须显示「未导入服装／饰品元数据」及可操作说明，不能在游戏宿主自动塞入演示项目。

运行时保持本地、离线，不访问在线 Mod 站点或搜索服务。本文在线研究只用于开发依据。

## 已核实的格式与缺口

### 实体表

ReDIVA 的 `module_table_handler::read/parse` 与 `customize_item_table_handler::read/parse`，以及 Armoire 的实际读取／写出实现相互印证：

| 内容 | 外层文件 | 内层条目 | 内层文本键 |
| --- | --- | --- | --- |
| 服装 Module | `gm_module_tbl.farc`、DML 追加的 `mod_gm_module_tbl.farc` | `gm_module_id.bin` | `module.<表内序号>.id/name/chara/cos/sort_index`、`module.data_list.length` |
| 饰品 CustomizeItem | `gm_customize_item_tbl.farc`、`mod_gm_customize_item_tbl.farc` | `gm_customize_item_id.bin` | `cstm_item.<表内序号>.id/name/chara/parts/obj_id/sort_index`、`cstm_item.data_list.length` |

这些特定 `.bin` 条目是 UTF-8 `key=value` 文本，不是可以直接套 C++ 内存结构的二进制数组。Armoire 的 PC 表还读写 `bind_module`、`ng`、商店日期、价格等属性；ReDIVA 的 AFT 路径没有覆盖全部 PC 属性。应保留原始未知属性，不能把 ReDIVA 的结构当作 PC 完整 ABI。

文本解析按完整键归组，再使用记录里的 `id`，不以行顺序或遇到 `sort_index` 作为结束条件。第一处 `=` 分隔，支持 BOM、CRLF、空行；声明长度设上限，检查稀疏记录、缺 ID、非法角色、重复 ID。`id=0` 可以是真实 Module，不能沿用歌曲的 `id>0` 判断。负数随机／取消项先不当作可定位实体。

`COS_001` 是原始字符串；ReDIVA 渲染内部会转为零基编号，但离线模型保留字符串，不能把 `cos` 当 Module ID。`obj_id` 也不能代替 CustomizeItem ID。`bind_module` 记录原值，零／负数／特殊值的 PC 语义待样本确认，不能自行宣称 `0` 必然表示无绑定。

公开源码已证实的角色 token：`MIKU/RIN/LEN/LUKA/NERU/HAKU/KAITO/MEIKO/SAKINE/TETO`；饰品另有 `ALL`。公开饰品部位有 `ZUJO/FACE/NECK/BACK`。未知 token 保留并归入「未知角色／未知部位」，不可默认为 MIKU。发型 KAMI、眼睛等扩展类型暂不混进本轮四部位饰品；以后按真实 PC 表扩展。

证据：[ReDIVA module_table.cpp](https://github.com/korenkonder/ReDIVA/blob/207c270472ba3f0edcda29bae777e5bd4a8952d9/src/CRE/module_table.cpp)、[customize_item_table.cpp](https://github.com/korenkonder/ReDIVA/blob/207c270472ba3f0edcda29bae777e5bd4a8952d9/src/CRE/customize_item_table.cpp)、[Armoire Program.cs 的 ReadModuleFile/ReadCustomFile/SaveFile](https://github.com/jaykthx/Armoire/blob/8acdfada755323afdab03624d9dd3f35828ee222/Program.cs)、[Classes.cs 的 Module/CustomizeItem](https://github.com/jaykthx/Armoire/blob/8acdfada755323afdab03624d9dd3f35828ee222/Classes.cs)。

### 名称补丁与语言

DML 在每个启用 include root 的 `rom/lang2/mod_str_array.toml` 读取名称。已确认支持：

```toml
# DML 现有格式示意；这里的编号只是示例，不是随包游戏数据。
module.998 = "Example Module"
cn.module.998 = "示例服装"
customize.998 = "Example Accessory"
cn.customize.998 = "示例饰品"
```

`cstm_item.<ID>` 是 `customize.<ID>` 的兼容别名，二者共用名称空间；DML 先读取 customize，再读 cstm_item，已存在项不覆盖。每个 root 先本语言再通用，按高优先级 root 到低优先级 root「首次写入生效」。因此高优先级 Mod 的通用名称会压过低优先级 Mod 的中文名称；不能把所有中文条目合并完再统一覆盖。

分组中的编号是真实物品 ID。旧平面字符串的 Module 偏移 `+275` 有 README 例证，但不能把全部数字字符串减 275 后当作服装。饰品的旧平面偏移本轮不猜；只有已经存在的实体且偏移有对应构建证据时才允许关联。分组名称优先于旧平面名称，再回退实体表名称，最后「服装 #ID／饰品 #ID」。保存所有真实语言名称用于搜索，显示使用明确配置的语言顺序；未读取游戏语言时标明使用本插件语言设置，默认 `cn → 通用 → 表名称`。

名称补丁只能给已有实体增加显示名、别名和名称来源。孤立 `module.998` 不能证明物品存在、角色为初音未来、预览可用或已解锁。孤立名称记入诊断计数，等实体表导入后再关联。

证据：[DML README 的 Mod String Array Loading](https://github.com/blueskythlikesclouds/DivaModLoader/blob/0abc2c89229c4b307bfa1555656c54c2e5cff480/README.md#mod-string-array-loading)、[StrArray.cpp](https://github.com/blueskythlikesclouds/DivaModLoader/blob/0abc2c89229c4b307bfa1555656c54c2e5cff480/Source/DivaModLoader/StrArray.cpp)。

### 封装、原版数据与导入边界

真实 Mod 常把文本放进压缩 FARC；官方表还在游戏 CPK 内。现有歌曲加载器不具备 FARC／CPK 读取能力。不能用文本读取函数直接读取 `.farc`，也不能把 `mod_gm_module_tbl.txt` 当成 DML 已知标准。

MikuMikuLibrary 已提供可核对的普通 FARC 格式：`FArc` 为未压缩，`FArC` 为可压缩条目；大端 uint32，头长度字段加 8 得到头末位置，随后 alignment，再由 NUL 文件名及 offset/size 组成目录。`FArC` 额外记录未压缩长度，非零时用 gzip 解压，零表示该条目未压缩。全大写 `FARC` 另有 flags 和加密等变体，不得按上述简化结构硬读。该证据允许写有边界检查的普通格式读取器；不证明用户资产一定采用其中某个变体。

可实施的最小数据通道：

1. 先支持本插件定义的、版本化 `customization_catalog.toml` 导入清单，和显式登记来源的已解包两个 `.bin` 文本文件。
2. 自动扫描 DML 启用的 include roots，读取对应名称补丁，发现实体 `.farc` 但尚不支持解包时只计数和说明「发现 X 个实体归档，尚未导入」。不要声称完整读取游戏库。
3. 可选下一小步：用 Python 标准库 `struct/gzip` 实现离线导入命令，只支持已核对的 FArc/FArC 两种，将这两个指定内部文本条目规范化为清单。Python 是开发／导入工具，DLL 运行不调用 Python，不把游戏或 Mod 文件复制进仓库。若另写原生 gzip 解码，需要明确依赖、许可和测试，不在本轮顺手增加大型资源库。
4. 本机取得原版／DLC 表后，通过相同导入器建立本地基础快照。CPK 提取、加密 FARC、sprite 解析不属于立即 MVP。官方表缺失时明确显示「当前仅搜索已导入的 Mod 元数据，原版／DLC 未导入」。

导入清单记录原文件相对位置、大小／mtime 或哈希、DML 来源 ID、include root、数据库族和版本，刷新核对；不能仅导出最后的名字而丢掉来源和优先级。若导入器输出已合并目录，必须带合并所用的 DML 配置指纹；当前指纹不同即标为过期，禁止称为当前生效目录。

证据：[MikuMikuLibrary FarcArchive.cs](https://github.com/blueskythlikesclouds/MikuMikuLibrary/blob/82380f1e40bbcf07d19c4ac8d0bdb95845be3628/MikuMikuLibrary/Archives/FarcArchive.cs)。

## 数据模型与来源合并

建议独立 `CustomizationCatalog`，与歌曲 Catalog 分开；复用 Unicode `Normalize`、后台任务模式和 DML 来源发现原则，不往 Song 塞物品类型。

| 字段 | 语义 |
| --- | --- |
| `ItemKey {kind,id}` | `kind=Module/Accessory`，同数字的服装和饰品是两项 |
| `character`、`part` | 带 Unknown/All 状态的枚举和原始 token；Module 没有饰品 part |
| `name`、`namesByLanguage`、`aliases` | 展示名称与真实名称集合；缓存规范化搜索键 |
| `sortIndex` | optional，缺失排最后；不是 ID，也不是原生当前列表索引 |
| `cos`、`objectId`、`bindModule` | 保留原表字段，optional，不能猜缺失值 |
| `definitionSource` | 当前胜出的实体定义来源 |
| `nameSource` | 当前显示名称提供方，可以不同于定义来源 |
| `previewSource` | 经验证后单独记录，未知时为空 |
| `sources` | 参与定义／命名／预览的来源关联与各自作用、是否生效 |
| `metadataState` | 已导入／来源过期／未知字段等，不能等同解锁或可装备状态 |
| `preview` | 缺图状态或已验证资源引用；不是裸游戏纹理指针 |

`SourceId` 用 Mod 目录身份／明确导入来源身份，显示名允许重复；两个 Mod 的 `name` 相同不能合为一个筛选值。用户界面可以加目录短名区分。预览 fixture 的 source 带独立 Demo 标记。

### 合并规则

- DML 顶层 `enabled=false` 时不扫描 Mod；只加载 priority 数组列出的目录；空数组／缺数组按已建立的字母顺序回退，并说明与运行 DML 版本行为待核对。逐 Mod 检查 `enabled`、`include`，不把被禁用目录递归扫进来。include 的顺序也是优先级的一部分。复用现有规则可以，但本轮不借此重构歌曲来源代码。
- DML README 定义首个目录优先；DatabaseLoader 将 Mod 数据库 prefix 逆序追加，使低优先级先读。实体定义按低到高解析，按 `{kind,id}` 整条记录覆盖；名称补丁按前述高到低「首次生效」。不要照搬 PVDB 的逐字段覆盖：ReDIVA 的实体 `parse` 创建新记录，再按 ID 替换，具有整条语义。
- `gm_...tbl.farc` 是普通文件替换：同一逻辑路径只有高优先级文件生效。`mod_gm_...tbl.farc` 是 DML 追加数据库，各启用来源可贡献记录。必须区分普通替换与追加，不能将所有普通 gm 表都累加后声称游戏有效集合。
- 相同 ItemKey 的多个来源只显示一个有效格子，详情列出定义胜出方、名称胜出方和其他贡献方；来源过滤与歌曲体验一致，按关联来源匹配，来源标签明确「关联来源」，避免把被覆盖的低优先级定义显示成另一个可装备版本。只要匹配该来源但全部贡献已被覆盖，详情应标记「定义已由其他来源覆盖」。
- 同来源同文件重复 ID、同 ID 冲突不静默生成两项；记录诊断并给确定性结果。跨文件高优先级整条覆盖有源码依据，但商业 PC 的缺字段默认、同文件重复规则尚需真实样本确认，重复定义不得取得可定位能力。
- `str_array` 孤立名称不参加格子、总实体数或角色过滤。纯纹理替换 Mod 可能没有实体表；在未分析资源归属前，不能宣称来源过滤覆盖了这类 Mod。
- 给 DML config、include roots、读取表、名称表、导入清单建立快照指纹。后台刷新只读磁盘；游戏已加载目录不会随刷新自动变化。后续启用定位时沿用变更后需重启的约束。

公开 DML 的加载顺序与路径依据：[ModLoader.cpp](https://github.com/blueskythlikesclouds/DivaModLoader/blob/0abc2c89229c4b307bfa1555656c54c2e5cff480/Source/DivaModLoader/ModLoader.cpp)、[DatabaseLoader.cpp](https://github.com/blueskythlikesclouds/DivaModLoader/blob/0abc2c89229c4b307bfa1555656c54c2e5cff480/Source/DivaModLoader/DatabaseLoader.cpp)。整条替换参考 [ReDIVA vector_pair_combine::combine](https://github.com/korenkonder/ReDIVA/blob/207c270472ba3f0edcda29bae777e5bd4a8952d9/src/KKdLib/prj/vector_pair_combine.hpp)，商业 PC 实际重复覆盖仍需对照本机结果。

### 本插件导入清单示例

以下是拟定的新格式，**不是 DML 标准或真实游戏条目**。正式测试 fixture 放 `tests/fixtures/customization/`；预览演示运行时另行显式生成，游戏宿主默认目录保持空。

```toml
schema_version = 1
language = "cn"

[[sources]]
id = "example-mod"
name = "示例导入来源"
mod_directory = "ExampleMod"
include_root = "."

[[items]]
kind = "module"
id = 10001
chara = "MIKU"
name = "导入格式示例"
source = "example-mod"
cos = "COS_001"
sort_index = 1
```

实现时在 `items` 中区分普通替换／追加记录及原表逻辑路径，或改为按 source/database 分组；在固定 schema 前补齐这些信息，不能只有 `source` 而丢掉合并语义。最简 MVP 若只接受手工最终清单，应明确它是「导入目录」而非已验证的 DML 有效目录。清单来源只有被当前启用 Mod/root 匹配后才可显示为当前 Mod，导入记录不能通过自填优先级压过 DML。

## 角色默认与过滤语义

每次从关闭状态打开面板时调用 `TryReadCustomizationContext()`。返回必须有 `known`、角色 token、角色槽位、菜单种类、上下文 generation 和验证状态；未知用 `std::nullopt`，不把默认 0 伪装成读取成功。

- 已有本机验证的菜单／角色上下文：服装和饰品过滤默认其角色。
- 尚未接线、对象失效、不在可验证菜单：过滤初音未来，显示「无法读取当前角色，默认初音未来」。本轮接口始终返回未知。
- 用户可选「全部角色」和每个已知角色。打开后手工更改筛选，不因后台轮询而跳回默认；下次重新打开重新采样。
- Module 选择具体角色仅显示其真实 chara；饰品显示该角色专用项及 `ALL` 共通项，并给共通标签。未知角色条目只在全部／未知角色筛选显示，不能混进 MIKU。
- 绑定物品保留「绑定服装 #ID」提示。当前 Module 未知时不把绑定限制当作已通过，也不依此隐藏真实元数据。

没有玩家的当前角色地址时，不复用歌曲对象的 performer 或曲目默认角色假装定制菜单当前角色。

## 共用入口与键盘格子

保留 Ctrl+F 和单一搜索窗口，顶部模式为「歌曲／服装／饰品」。歌曲保持当前行为；另外两个模式由独立 `CustomizationView` 持有查询、结果、选择与滚动状态。切换模式清空待提交动作，保留各模式文本和筛选，避免歌曲 Enter 请求漏到服装。当前宿主的 `move`、`difficultyMove` 可分别作为垂直／水平动作队列；由活动模式解释，不把服装左右键送去调整歌曲难度。

服装工具行：名称搜索框、角色、Mod 来源、排序；饰品额外有部位（全部／头部／脸部／胸颈／背部／未知）。Mod 来源提供全部、导入原版／DLC、各来源，名称文本检索匹配展示名、真实其他语言名、别名和十进制 ID。可另保留「名称／Mod」搜索模式，Mod 模式匹配关联来源名，筛选仍取交集。

默认顺序按 `sort_index（缺失最后）→ id → kind/source稳定键`；名称排序按规范化名称再 ID；ID 排序按数值。带文本时可先相关度、再所选稳定排序；无文本严格稳定顺序。本轮不做游戏原生复杂的收藏／购买／推荐排序，也不声称与原生列表完全同序。

格子按行优先排列，仅垂直滚动；列数由可用宽度／卡片最小宽度求得，最少 1 列。每卡固定预览区、两行名称、角色／部位、来源短标签、ID；截断名称能从详情读全。缺图是清楚的「暂无预览」占位与文字，不从网上抓图。先按可见行裁剪；图像加载和文件扫描不能在绘制帧阻塞。

| 位置／按键 | 行为 |
| --- | --- |
| 初次打开 | 文本框焦点，屏幕中保留可见选中项 |
| 搜索框内 ↑↓ | 上／下一个格子行，步长为当前列数，保留列意图；长按累计重复消息 |
| 搜索框内 ←→ | 光标编辑，保留正常选择文字；不能无条件吞掉文本光标 |
| Tab／Shift+Tab | 遍历模式、搜索、各筛选、排序、刷新、格子区、详情动作；格子区只有一个 Tab 停靠点 |
| 格子区 ←→ | 同行前／后一列，行边界停止、不回绕 |
| 格子区 ↑↓ | 前／后一行；末行不足列数时落在该行最后一项，回到上一完整行恢复原列意图 |
| 格子区 Home／End | 首项／末项 |
| 格子区 PageUp／PageDown | 按可见完整行数移动，并滚动跟随；不越界 |
| Enter／空格 | 打开详情或选中；不定位、不装备 |
| Ctrl+F（已打开） | 沿现有快捷键关闭；可另外定义 `/` 回搜索，但不干扰输入法和文本框 |
| Esc | 先关闭 Combo／详情弹层，再关闭搜索窗口；IME 组词时交由输入法取消 |

搜索框的左右键需要新增模式判断；本轮不能沿用歌曲「搜索框内左右选择难度」的规则到服装。格子获得焦点后才接管全方向键。筛选 Combo 获得焦点时方向键、Enter、空格由 Combo 处理。Tab 不逐卡走数千个结果，空结果时仍能回到搜索与过滤器。

ImGui vendor 1.88 的原生 Tab 主要遍历 Inputable，按钮／Combo 不能只靠设置 NavEnableKeyboard 宣称可用。主 agent 已验证 `ui.h` 公开的 `EnableControlTabNavigation()`，在当前搜索窗口 `Begin` 后调用以支持控制区 Tab；结果中的大量逐卡按钮要用 `ImGuiItemFlags_NoNav` 排除默认逐项遍历，单独设置格子区停靠点。模式切换、Combo、刷新、格子入口都必须有真实 Tab 回归。不能设置全局强制 focus 造成弹窗或其他 Mod 窗口抢焦点。

以 `{kind,id}` 保留选择，结果更新仍存在则保留；不存在选首项，空结果清空。窗口改宽重算列数后保留同一 ItemKey。滚动保证所选卡片可见，不每帧跳到顶部。导航动作需要按到达顺序或可证明等价的队列解释，混合左右／上下不能被同帧聚合后产生反直觉位置。

延用 150ms 防抖、不可变快照、后台搜索、查询 revision／目录 generation。输入法组词暂停检索与导航，旧查询不能覆盖新结果。Enter 在新结果待更新时不打开旧项目详情，可待最新版本完成后只执行一次。新模式不使用歌曲的定位状态文案。

## 预览路线与 Future Tone Customization 结论

### 第 0 级：本轮安全占位

默认所有卡片使用本插件绘制的角色／部位色块与「暂无预览」。保留 `PreviewDescriptor {state,style,source,assetKey}` 接口。名字、ID、角色和来源构成完整可用格子；不承诺游戏模型已加载。

### 第 1 级：可验证本地缩略图

首先支持用户本地导入清单明确映射的 PNG，解码走 WIC／既有 DX11 设备，后台读图、受控线程创建纹理、限制尺寸／总内存、LRU、设备重绑时清理。只接受导入目录内相对路径，规范化后验证目录边界，防止相对路径逃出。缺失／损坏／不支持一律回退占位。

以后用 MikuMikuLibrary 等离线工具从玩家自己的 sprite 集导出缩略图。应读取 sprite database 取得集合、条目及纹理／UV，不假设纹理第一页就是完整卡片，不以物品 ID 当作纹理 ID；读取资源按 DML 资源实际覆盖规则，资源来源可以与实体来源不同。导出清单记录 ItemKey、sprite 名、风格、图像来源和配置指纹，不随本 Mod 发布游戏图像。

### 第 2 级：复用原生 sprite 纹理

Future Tone Customization 的公开 `Mod.cpp` 已证实 PC 存在 `GetModules()`、`GetCstmItems()` 及各记录中的 sprite ID／sprite set ID。它按名称查找并替换现有数据项的 sprite ID：

| 项目 | Mega Mix sprite 名 | Future Tone sprite 名 |
| --- | --- | --- |
| Module | `SPR_SEL_MD%03dCMN_MD_IMG` | `SPR_SEL_MD%03dCMN_MD_IMG_FT` |
| 饰品 | `SPR_CMNITM_THMB%03d_ITM_IMG` | `SPR_CMNITM_THMB%03d_ITM_IMG_FT` |

`%03d` 是最小三位宽，不能限制 ID 必须三位。对应集合通常 `SPR_SEL_MD%03dCMN`、`SPR_CMNITM_THMB%03d`，但最后要以 sprite db 解析为准，集合数字 ID 可为 hash，不能由 Module ID 加常数推算。

该 Mod 同时调整定制菜单风格和 NPR 资源选择，让 3D 定制画面跟随 Visual Setting；缩略图是额外的 `_FT` sprite 资源。它没有暴露一个「给本插件 DX11 shader resource view」的稳定 API，也没有证明可将任意游戏 GL／引擎纹理裸指针直接交给本项目的 ImGui DX11 后端。源码内有函数扫描、结构布局和写操作，本轮只作为研究依据，不复制其地址、不安装这些 hook、不重写它的数据。

Armoire `WizardCode.ExportSprite` 能把 MM 和 FT 两张图编码为同一 sprite set 内的两张 DXT5 纹理，并添加对应 `MD_IMG[_FT]`／`ITM_IMG[_FT]` 条目。这证明离线导出真实缩略图路线可行；不证明每个用户 Mod 已提供双风格图。请求风格缺图时明确提示并回退另一张或占位，不凭 FTC 已启用就宣称预览齐全。

已证实的结论是「FTC 有可复用命名规则和本地 sprite 资产来源，适合做缩略图适配研究」；尚不能交付「直接读取其当前渲染纹理」。必须本机核对 sprite db、已加载集合、纹理句柄类型、UV／翻转／alpha、加载线程、资源生命周期、设备丢失以及与 FTC 的 hook 顺序，才可实现只读桥接。尽量读取实际生效记录中的 sprite ID，避免自行和 FTC 竞争选择风格。

证据：[FutureToneCustomization README](https://github.com/ActualMandM/DivaDllMods/blob/a3fb8ba380063966febfed942324e73c4e9439ab/Source/FutureToneCustomization/README.md)、[Mod.cpp](https://github.com/ActualMandM/DivaDllMods/blob/a3fb8ba380063966febfed942324e73c4e9439ab/Source/FutureToneCustomization/Mod.cpp)、[Armoire WizardCode.cs](https://github.com/jaykthx/Armoire/blob/8acdfada755323afdab03624d9dd3f35828ee222/WizardCode.cs)、[Program.cs 的 GetSprite](https://github.com/jaykthx/Armoire/blob/8acdfada755323afdab03624d9dd3f35828ee222/Program.cs)。[Mod 作者发布页](https://divamodarchive.com/post/266) 提供源码链接及 sprite set 清单；实现判断以公开源码为依据，不把发布页宣传当作内存适配证据。

### 第 3 级：实时模型／原生预览接口

在原生定制菜单已打开、角色／槽位确认后，搜索高亮驱动游戏现有预览是优先路线。独立给每个格子创建 3D 模型，需要角色骨骼、服装／绑定饰品、材质、着色、灯光、动画、对象集依赖和渲染上下文，显著超出搜索 MVP。即便原生预览完成，也不等于有安全的多格子离屏渲染接口。

预留 `ICustomizationPreviewProvider` 的只读能力查询／异步请求／释放接口即可。默认返回 `Unavailable`，资源由 provider 持有，UI 不持有跨菜单裸指针；不能用切换当前装备的方法批量截图生成格子。

## 分阶段实施与具体文件

### A：medium agent 可直接实施的 MVP

允许修改下列源码时先读取当前工作树，保留已有歌曲导航、防抖、输入隔离、Tab 修复。建议 1 次实现任务完成基础模型、导入、格子、键盘与针对性测试；压缩归档／PNG 作为后续增量，不能挤掉正确的来源和空库处理。

| 文件 | 具体改动 |
| --- | --- |
| `src/customization.h/.cpp`（新增） | ItemKey、角色／部位、来源模型、Prepare/Search、稳定排序、纯格子导航函数；不 include 游戏内存或 DX11 |
| `src/customization_catalog.cpp`（新增） | 版本化清单、已解包 UTF-8 实体表、DML roots／str_array、整条覆盖与诊断；后台构造不可变目录 |
| `src/customization_ui.h/.cpp`（新增） | 独立 CustomizationView、筛选、虚拟化格子、详情、占位预览、后台 revision、全部文字说明 |
| `src/ui.h/.cpp`（主 agent） | 新增活动模式／模式选择与分发，独立保留歌曲 View 状态；复用主 agent 的 Tab helper |
| `src/host.cpp`（主 agent） | 将方向／确认动作路由活动模式；服装文本左右保留编辑，格子才用水平动作；保留 IME、长按、释放保护与 hook 链 |
| `src/preview.cpp` | 仅 Preview 显式注入少量中文 Demo 项，顶部标记「演示数据」；可传真实导入目录复查 |
| `src/mod.cpp` | 只读上下文适配 stub 返回未知；能力默认不支持定位，禁止调用现有歌曲 Submit |
| `CMakeLists.txt`（主 agent） | 将纯数据接入 search_core，UI 接入 search_ui，增加目标性测试 |
| `tests/customization_check.cpp`（新增） | 真实格式手写 fixture，名称／来源／重复 ID／角色／导航／过期查询测试 |
| `tests/input_check.cpp`、`tests/view_check.cpp` | 补活动模式键盘路由与真实 Tab/Combo/格子焦点回归，不复制实现当断言 |

如为避免改动主歌曲 View，可新增 `SearchMode` 与桥接成员，保留外层 `View::open/Draw/Refresh/Poll` API，内部委托 CustomizationView；不要为共用外观一次性重写歌曲功能。

本次协作文件边界：medium agent 负责新增 `customization*`、新增专用测试、可选导入脚本，以及约定的功能文档；主 agent 负责 `ui.*`、`host.*`、`CMakeLists.txt`、外层 Preview/Mod 入口接线和已有输入／视图测试。medium 不并发修改这些共享文件，先提供头文件接口，再由主 agent 编译接线。

建议先固定的桥接接口（实际命名可与主 agent 一次性对齐）：

```cpp
// 参数为本地数据目录和游戏目录；后台加载，不扫描绘制帧。
void CustomizationView::Refresh(const fs::path& home, const fs::path& game);
// 接收后台目录与查询回复；不会等待未完成 future。
void CustomizationView::Poll();
// 参数 kind 为服装或饰品；切换时清空待处理确认／方向动作。
void CustomizationView::SetKind(CustomizationKind kind);
// 参数 context 为经验证的只读上下文，未知传 nullopt 并默认初音未来。
void CustomizationView::OnOpen(std::optional<CustomizationContext> context);
// 只绘制当前搜索窗口内容，外层负责 Begin/End 和共用模式控件。
void CustomizationView::Draw();
// 参数为上下／左右重复次数；按当前焦点区解释，返回是否消费。
bool CustomizationView::Navigate(int vertical, int horizontal);
// 当前是否应把左右键送入格子；文本框或 Combo 返回 false。
bool CustomizationView::WantsHorizontalNavigation() const;
// 当前搜索框／格子是否可接收导航；弹出筛选器时返回 false。
bool CustomizationView::WantsListNavigation() const;
// Enter 只请求最新结果的详情，绝不触发游戏 selector。
void CustomizationView::RequestDetails();
```

主 agent 在外层模式控件后分派 `Draw/Poll`，打开时分派 `OnOpen(nullopt)`；服装／饰品激活时将 host `move/difficultyMove/requestEnter` 交给上述接口，立即清空外层动作，禁止落入歌曲 `Navigate/Locate`。Home/End/Page 动作可以另加 enum 或专用队列；不能为了接口省事遗漏其实现。外层统一处理 Esc 关闭与输入捕获，详情／Combo 弹层优先收到 Esc。`EnableControlTabNavigation()` 由外层在 Begin 后调用一次，新 view 不重复 Begin 同名窗口。

建议的 stub 边界：`CustomizationContextReader` 返回 `optional<CustomizationContext>`；`CustomizationCapabilities {canReadContext=false,canLocate=false,canPreviewNative=false}`；本轮不注册可写 selector。UI 能力门禁与任何未来提交入口均要检查，不能只把按钮涂灰却留 Enter 提交路径。

### B：真实本地目录与缩略图

添加离线 FArc/FArC 导入工具、基础／DLC 快照导入、可验证的 PNG 映射和异步缓存。用本机样本验证同 ID 覆盖、角色 ALL、bind_module、语言、生效来源。导入命令不联网，不写原 Mod，输出只进本插件数据目录。新增 Python 函数与参数写中文注释，并给出可复现命令。

### C：只读探针与原生定位

等待用户本机采集后做游戏构建 profile 与只读上下文读取；再验证 ID→原生当前列表索引映射、原生高亮／预览函数。本阶段另行记录风险和实测证据，具备真实接口后才启用定位。装备／保存接口仍不属于默认搜索任务。

## 本机只读探针采集清单

用户后续自行观察内存时优先记录下列成对状态，不扫描后直接猜写地址：

1. 游戏可执行文件版本／哈希、DML 版本、启用 Mod 与优先级指纹、FTC／角色或服装扩展 Mod、Visual Setting。公共报告去掉个人路径。
2. 菜单上下文：从选曲进入服装页、饰品页、退出；角色槽位、人物、当前曲专用／共通设置，以及上下文 generation 的可观测变化。
3. 同人物两件已知服装 A/B：逻辑 Module ID、当前列表索引、手动高亮、预览模型／sprite、已装备值。分别记录只移动高亮、等待预览完成、取消、人工确认后的值，区分这些字段。
4. 两个不同人物、第二个角色槽位、列表排序／筛选变化：确认 chara／slot 字段，不把 performer 默认值当当前对象；索引随筛选变化但逻辑 ID 不变。
5. 饰品每个部位各两项、共通 ALL、一个绑定物品及取消／无装备项；记录 CustomizeItem ID 与 obj_id 的差异。额外发型另采，不共享未经验证布局。
6. 运行中模块／饰品集合：数量、容器边界、ID/name/chara/parts/sort_index/sprite_id/spr_set_id 的只读样本；指针可读性、字符串长度上限、对象随切页失效情况。先少量脱敏字段，避免完整内存转储。
7. 原生当前列表中可见条目的 ID 序列、索引、锁定／隐藏／购买状态；Mod 高 ID、重复定义、过滤后不可见目标分别采集。
8. FTC 关闭／开启、MM／FT 两风格：同 ItemKey 的实际 sprite 名／ID、集合加载前后及切页释放情况；若能安全观察渲染资源，记录 API 类型、纹理尺寸、UV、线程及生命周期，不能只给一个地址。
9. 截图对照真实高亮与预览，用独立字段确认；记录异步预览何时稳定。探针通过不等于定位通过，定位缓存也不能作为成功依据。

未来定位请求至少包含 `{kind,id,chara,slot,part,contextGeneration,catalogFingerprint,serial}`。适配器验证受支持构建、菜单、对象、目录未变、物品确实在当前可用列表，再调用已核查原生函数；返回 `UnsupportedBuild/UnavailableContext/ContextChanged/NotInList/PreviewPending/Success` 等明确状态。跨帧确认读取实际高亮及独立预览状态，不写已装备字段，不自动按确认，不改存档。

## 可验收标准与报告方式

MVP 的完成条件是：

- 普通游戏宿主无真实数据时是空库与中文导入说明；孤立 str_array 名称不能生成格子，Preview 才显示标记的 Demo。
- 用显式测试清单或已解包真实格式 fixture 能分别搜索服装和饰品；同数字跨种类不碰撞；名称、角色、ALL 共通、来源、部位组合过滤正确且排序稳定。
- DML 禁用／priority／include、名称语言优先、整条重复 ID 合并有目标性自动回归；未知字段和过期数据有诊断。
- 列数 1/2/多列、末行不足、窗口改宽、长按、Home/End/PageUp/PageDown、查询变化后选中与滚动、空结果都可重复验证。
- 从打开到切换模式、输入、筛选 Combo、进入格子、打开详情、关闭全程不需要鼠标；真实 ImGui Tab 回归覆盖按钮、Combo、格子入口。IME 与现有歌曲输入协议保持通过。
- 任何服装／饰品点击或 Enter 都不会进入歌曲定位器或游戏写接口；禁用能力可通过注入回调调用计数等方式验证。未读取角色时始终显示默认初音未来的原因。
- 后台扫描／搜索不阻塞绘制帧，旧 revision／快照结果不覆盖新条件，输入法组词不导航；运行时没有联网依赖。

报告拆为「自动测试」「共用原生 Preview」「Windows 构建」「实际游戏」四类。云环境能完成源码、静态／可运行测试与支持平台内验证；没有 Windows 工具链或游戏时明确未验证。不能把 fixture、模拟上下文、公开源码地址或 Preview 截图写成真实游戏定位成功。

本轮研究没有复制第三方实现或游戏资产；后续若引入代码／依赖，维护对应固定提交与许可声明。所有计划中的游戏结构／扫描证据都只是后续本机核查起点。
