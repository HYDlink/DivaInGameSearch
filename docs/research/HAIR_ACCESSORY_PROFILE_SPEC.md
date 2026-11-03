# 发型／自定义道具原生规格填写说明

日期：2026-11-03。适用代码：`src/customization_item_profiles.cpp` 的 `kHairSpec`、`kAccessorySpec`。

本地后续修订已按研究证据登记 PC 发型／道具规格。下表仍说明通用构造器字段；PC 专用动作补齐了内嵌高亮控件、真实菜单目标、加载状态机和独立读回，不能只填写回调命令就省略协议。卸除协议见 [实机研究](HAIR_ACCESSORY_REMOVAL_VERIFIED.md)。

当前已接入 `Remove` 动作：仅当前原始覆盖 ID 对应的卡片替换按钮，发型显示“搭配模型”，道具显示“卸除”。原始覆盖为 -1 时不把解析出的模型默认发型误标为可移除。界面只使用游戏线程发布的短期快照，渲染线程不主动调用原生函数；请求到游戏帧后重新核对实际装备及上下文。

卸除使用发型命令6或道具命令5，准备真实目标后进入加载3并返回列表0；连续三帧核对目标字段=-1及其余35个模型／定制字段未变。不要求原生高亮停留在已卸除的旧发型，不直接写装备ID，也不将显示配置确认描述为保存后重启验证。18项离线验收已通过，含真实ImGui按钮提交及五类模拟事务；产品DLL尚未部署或实机验收。

## 字段与能力

| 字段 | 含义 | 定位 | 预览 | 更换 |
| --- | --- | --- | --- | --- |
| `evidence` | 证据说明（构建 SHA-256、报告或证据目录） | 必需 | 必需 | 必需 |
| `listBegin`／`listEnd` | scene 内记录指针数组起止偏移，每项 8 字节指针 | 必需 | 必需 | 必需 |
| `recordId` | 记录内 int32 稳定 CustomizeItem ID 偏移 | 必需 | 必需 | 必需 |
| `recordOwned` | 记录内 uint8 实时拥有标记（非零为已拥有） | — | — | 必需 |
| `highlightIndex` | scene 内 int32 当前高亮序号 | 必需 | 必需 | 必需 |
| `partIndex`／`partOrder` | 道具页 scene 内 int32 当前部位序号及 0～3 对应部位 | 道具必需 | 道具必需 | 道具必需 |
| `highlightRva` | `void(scene*,int index)` 直接高亮 | 与 `stepRva` 二选一 | 必需 | 必需 |
| `stepRva` | `void(scene*,int dir)` 左右一步（每帧一步，跨帧读回） | 与 `highlightRva` 二选一 | — | — |
| `partRva` | `void(scene*,int dir)` 部位切换一步 | 道具跨部位需要 | 同左 | 同左 |
| `loadingFlag` | scene 内 uint8 加载中标记 | — | 建议 | 建议 |
| `previewFlag`／`previewId` | scene 内 uint8 预览活动、int32 预览目标 ID | — | 必需 | — |
| `equippedBase`／`equippedStride` | controller 内各槽位本部位装备 int32 起始与步长 | — | 必需 | 必需 |
| `callbackObject`／`callbackRva` | scene 内回调对象指针偏移；`uint8(callback*,int command)` | — | 必需 | 必需 |
| `previewCommand`／`exitPreviewCommand`／`equipCommand` | 回调命令号，须独立认证；不得是购买命令 | — | 预览／退出 | 更换 |

函数签名形状来自 `ITEM_SELECTION_OBSERVATION.md` 的静态候选（`0x68C700(scene,direction)`、`0x68EF00(scene,direction)`、`scene+0x2D8` 回调绑定 `0x68F430(scene,command)`）。若实机协议与这些形状不同（例如更换需要先写入目标记录），请直接改写 `BuildCustomizationItemProfile` 中对应的 Profile 回调，执行器的阶段与读回规则不需要改。

构造器会拒绝模型页入口 `0x694D00`、`0x695250` 和不可执行的地址。记录 ID 为负值（随机、卸除等哨兵）时保留其列表位置，但永远不会匹配非负稳定 ID。

## 执行器保证（与规格无关）

- 定位：最新列表中唯一 ID → 序号；直接高亮或逐帧移动；连续两帧读回高亮 ID 等于目标才显示“已定位”，不回显请求 ID。
- 预览：等待加载结束，备份当前槽位与全部槽位装备；预览 ID 与活动标记连续两帧确认后才进入“正在临时预览”；退出（原生 Esc 或 `exitPreviewCommand`）后当前与其他槽位装备必须与备份一致，才显示“已退出预览，原装备未改变”。
- 更换：实时拥有标记为真才调用；连续两帧读回当前槽位装备等于目标且其他槽位未变，才显示“已更换”。
- 页面、歌者、槽位、范围、来源歌曲、线程、目录代数变化或超时即停止；预览进行中不接受新操作。
