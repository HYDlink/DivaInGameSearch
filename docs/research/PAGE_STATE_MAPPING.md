# 页面状态映射与逆向证据（交接版）

更新：2026-10-31 00:35。本文是《Project DIVA Mega Mix+》PC 版"当前处于哪个自定义页面"研究的完整交接：已确认事实、排除方向、失败记录、操作规程与复现材料。

**一句话结论**：页面身份已经可以零风险读出（管理器 `+0x18` 的键 = 当前页面）；"从任意页面进入目标子页"尚未打通——`Manager#6` 的调用在子页之间有效，但从父页发起仍无效，原因未定。

## 1. 已确认事实

### 1.1 构建与环境

| 项目 | 值 | 来源 |
| --- | --- | --- |
| 主程序 | `DivaMegaMix.exe`，SHA-256 `6F4DF714…BC04504D` | 磁盘哈希，两次一致 |
| 文件大小 | `402069440` 字节 | 文件系统 |
| 映像大小 | `602353664`（`SizeOfImage`） | 运行时读取，与 `src/game.cpp` 构建检查一致 |
| 模块基址 | `0x140000000` | Frida，多进程一致 |
| 工具链 | REA 6.7.0（Frida provider）+ Ghidra 12.2 headless + JDK 25 | 见第 5 节 |

### 1.2 顶层菜单不能区分子页

七阶段采样（歌曲选择 → 自定义父页 → 模型子页 → 父页 → 子页 → 父页 → 歌曲选择）中，`0xCC61094/98/A0` 恒为 `5/6/1`，选曲对象 `+0x6C` 恒为 `6`。

与 `NATIVE_SELECTION_METHODS.md` 一致：`5/6/1` 只是"选曲稳定态"，自定义体系是其内部子状态。

### 1.3 页面类族（RTTI，已自检）

命名空间 `customize_selector_switch`，共 11 个类；`SceneModule` 解出的虚表 `0xCC4BB0` 与独立实测一致，方法可信。

| 类 | 虚表 RVA | 首项 RVA |
| --- | --- | --- |
| `SceneTopMenu` | `0xCC5170` | `0x699E70` |
| `SceneModule` | `0xCC4BB0` | `0x690BA0` |
| `SceneHairstyle` | `0xCC4678` | `0x688040` |
| `SceneItem` | `0xCC48C8` | `0x68C0D0` |
| `SceneTshirt` | `0xCC5318` | `0x69D510` |
| `SceneSound` | `0xCC5020` | `0x696B60` |
| `SceneConfig` | `0xCC3AB0` | `0x674150` |
| `SceneFinish` | `0xCC4560` | `0x687EA0` |
| `SceneLoading` | `0xCC4A80` | `0x690610` |
| `SceneManager` | `0xCC4E38` | `0x696600` |
| `SceneBase` | 未解析 | 基类 |

### 1.4 场景表（管理器红黑树，已完整遍历）

管理器 `+0x08` 是 `std::map` 表头；节点布局：`+0x00 left / +0x08 parent / +0x10 right / +0x18 color / +0x19 isnil / +0x20 key(int) / +0x28 → 场景对象`。

父页状态下整棵树的内容（实测，全部**已实例化**）：

| 键 | 场景类 |
| --- | --- |
| 0 | Loading |
| 1 | TopMenu（父页） |
| 2 | Module（模型） |
| 3 | Hairstyle（发型） |
| 4 | Item（自定义道具） |
| 5 | Tshirt |
| 6 | Sound |
| 7 | Config |
| 8 | Finish |

**推论**：不能用"某场景对象是否存在"判断当前页——它们同时存在。必须看"当前键"。

### 1.5 管理器三字段 = 页面状态（关键）

`SceneManager` 实例（虚表 `0xCC4E38`）的三个字段：

| 字段 | 含义 | 实测 |
| --- | --- | --- |
| `+0x18` | 当前场景节点指针 | 节点 `+0x20` 的键 = 当前页面 |
| `+0x20` | 目标场景节点；等于表头表示"无挂起目标" | — |
| `+0x28` | 过渡状态（空闲时恒为 1） | — |

**当前键与页面的对应关系已实测**：父页 `1`、模型 `2`、发型 `3`、自定义道具 `4`。

这是"当前在哪个页面"的**零风险读法**：扫一次管理器，之后只读 `+0x18` 节点的键。

### 1.6 场景切换原语 `Manager#6`

`Manager` 虚表第 6 槽 = RVA `0x6968D0`。反编译（运行时页与文件一致）：

```c
// 在 manager+8 的红黑树里按 param_2 查键
if (找到节点 && 节点 != manager->当前节点(+0x18)) {
    manager->目标节点(+0x20) = 节点;
    return 1;
}
return 0;   // 未找到，或目标等于当前
```

**L3 同层切换实测走的就是它**：

```
当前键=2（模型） → Manager#6(id=3) → 50ms 后 当前键=3（发型）
                 → Manager#6(id=4) → 60ms 后 当前键=4（道具）
                 → Manager#6(id=2) → 48ms 后 当前键=2（模型）
```

### 1.7 父页确认处理函数（静态证实）

`FUN_14069BA10`（RVA `0x69BA10`）：

```c
lVar1 = *(*(param_1 + 8) + 8);
if (FUN_14069B000(param_1)) return;          // 选中项不可确认则直接返回
switch (*(*(param_1 + 8) + 0x28)) {          // 父菜单光标索引
  case 0: id = 2; break;   // 模型
  case 1: id = 3; break;   // 发型
  case 2: id = 4; break;   // 自定义道具
  case 3: id = 5; break;   // T恤
  case 4: id = 6; break;   // 按键音
  case 5: id = 7; break;   // 设置
}
plVar2 = *(lVar1 + 0x998);                   // 管理器
(**(code **)(*plVar2 + 0x30))(plVar2, id);   // 虚表 +0x30 = Manager#6
*(*(param_1 + 8) + 0x19) = 1;                // 置完成标记
```

`FUN_14069B000` 是守卫：按 `+0x28` 索引取选中项，调用谓词判断"该项是否可确认"，不可确认则阻止。
`+0x998` 就是管理器偏移——`NATIVE_PAGE_ROUTING_HANDOFF.md` 里那个数没错。

### 1.8 管理器每帧函数

`FUN_1406969B0`（RVA `0x6969B0`）是过渡状态机主体：按 `+0x28` 状态依次调用当前场景虚表槽 3 → 4 → 5，第 5 槽返回真时执行 `当前 = 目标`。

## 2. 未解决

| 编号 | 问题 | 现状 |
| --- | --- | --- |
| U1 | 从父页调用 `Manager#6(2)` 不切页 | 三次受控调用 + 5 帧连调均失败；虽写入了 `+0x20`，当前键不变 |
| U2 | 真正的"父页→子页"驱动者 | 自然点击时 `confirm` 被连续调用 5 次，随后当前键 1→2；但复刻调用无效 |
| U3 | 各子页的列表结构与高亮入口 | 只有模型页已知（`0x6916E0(page, flag)`） |
| U4 | 从自定义体系外进入自定义 | 未研究（`R` 键路径未抓） |
| U5 | 页面完成加载的判据 | 未建立 |

## 3. 受控实验失败记录（避免重犯）

| 尝试 | 做法 | 结果 |
| --- | --- | --- |
| 1 | 父页帧 `onEnter` 调 `Manager#6(manager, 2, 0x6eaba60)` 一次 | 正常返回，画面不动 |
| 2 | 对陈旧状态对象调 `FUN_14069BA10` | 完成标记 `+0x19` 置 1，画面不动 |
| 3 | 先自然点击捕获当轮状态对象，5 秒后复调 | 正常返回，画面不动 |
| 4 | 父页帧连续 5 帧各调一次 `FUN_14069BA10` | 当前键始终为 1，未切页 |
| 5 | 父页帧 `onLeave` 调 `Manager#6` | 窗口内父页帧未运行（对象在模型页），未触发 |

**崩溃事故**：00:05 与 00:18 各发生一次游戏闪退。第二次的日志显示脚本只发了"前置"、**未发生任何调用**，且第二段附加被系统拒绝（`VirtualAllocEx` 返回 5）。Windows 事件日志无 Application Error、无 Crashpad 转储。**不能把崩溃归因于单次调用，但整晚反复附加 + 在自修改代码页上装 inline hook 是已知风险，应避免。**

## 4. 操作规程（必须遵守）

1. **优先零插桩**：只读扫描 + 定时读字段，能回答的问题不要用 hook。
2. **一次只装一个 hook**，单个会话 ≤ 60 秒，结束即分离；不要连开三段以上。
3. **不要从 hook 内部调用游戏函数**。已证实这会导致状态机被破坏或闪退。
4. 若必须调用，只在游戏线程、只在帧边界、只调一次，并先用只读方式核对前置状态。
5. 出现 `VirtualAllocEx` 拒绝对附加时，立即停止，重启游戏，不要重试。
6. 每次实验前记录：目标、假设、成功判据、失败判据、回滚方式。

## 5. 记录方法（怎么取证）

### 5.1 运行时观察：REA + Frida

```powershell
rea instrument-with-frida attach <pid> --script-file <脚本> --duration-ms 58000 --format json
```

- 单次上限 60000 ms；更长窗口用"分段接力"循环，每段 58 秒。
- 每次调用返回一条 Evidence（含 `evidence_id`），日志同时用 `Tee-Object` 落盘到 `artifacts/native-routing-research/`。
- 脚本一律放 `artifacts/`，文件名带版本，便于回溯。

**环境坑（务必记住）**：

- `Memory.readByteArray` 在该运行时不存在的，调用报 `TypeError: not a function`；批量读必须写 `ptr.readByteArray(size)`。此前多个"窗口差分 0 命中"都是这个原因造成的假阴性。
- `ptr`、`args` 等是保留名，脚本里不能定义同名变量（`cannot define variable 'ptr'`）。

### 5.2 脚本分类（都在 `artifacts/native-routing-research/`）

| 类型 | 作用 | 代表文件 |
| --- | --- | --- |
| 采样型 | 定时读字段、差分、稳定过滤 | `page-stage-sampler-v2.js`、`page-global-diff-v1.js` |
| 扫描型 | 按虚表找活对象、遍历红黑树 | `scene-table-dump-v1.js`、`rtti-vtables-v3.js` |
| 观察型 | `Interceptor.attach` 记录参数/返回值/调用栈 | `confirm-caller-v1.js`、`sibling-switch-v1.js` |
| 受控型 | 调用游戏函数（**高风险，已暂停使用**） | `controlled-enter-burst-v1.js` |

### 5.3 静态分析：Ghidra headless

REA 的 Ghidra provider 不可用（要求 Ghidra 12.1.x + JDK 25，本机是 12.2 + JDK 22），因此直接驱动 `analyzeHeadless`：

```powershell
$env:JAVA_HOME = 'D:\Software\jdk-25\jdk-25.0.5+8'
& "$env:GHIDRA_INSTALL_DIR\support\analyzeHeadless.bat" D:\Tools\ghidra-proj DivaProj `
  -import "…\DivaMegaMix.exe" -noanalysis `
  -scriptPath "…\artifacts\native-routing-research\ghidra-scripts" `
  -postScript DecompileAt.java 0x14069BA10 0x1406969B0
```

- `-noanalysis` 只导入不分析（402MB 全量分析不可行），脚本内就地 `disassemble` + `createFunction` + 反编译。
- 首次导入约 5–10 分钟；之后用 `-process DivaMegaMix.exe` 复用工程。

### 5.4 运行时代码页转储

部分函数在磁盘文件里只是 `E9` 跳转桩，真实代码位于运行时生成的 `rwx` 页：

| 桩 | 目标 | 运行时页 |
| --- | --- | --- |
| `0x6968D0`（Manager#6） | `0x1603826E0` | `0x160382000` |
| `0x69A100`（父页每帧） | `0x1605C4B00` | `0x1605C4000` |
| `0x69A120`（父页槽 5） | `0x16060C980` | `0x16060C000` |

转储脚本 `dump-runtime-code-v1.js` 用 `ptr.readByteArray` + `new File(path,'wb')` 写出 16KB 页，再用 Ghidra `-loader BinaryLoader -loader-baseAddr 0x160382000 -processor x86:LE:64:default` 按原基址导入反编译。结果与文件版一致，说明桩后的逻辑是同一份。

### 5.5 截图与交叉核对

```powershell
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
# CopyFromScreen 抓全屏 → PNG → 用读图工具核对
```

截图只作**旁证**：受控调用后必须看画面，不能只看"场景对象存在"——已验证对象会残留，单凭存在性会得出假阳性。

## 6. 已排除方向

| 方向 | 结论 | 依据 |
| --- | --- | --- |
| 用 `5/6/1` 区分子页 | 排除 | 七阶段采样恒定 |
| 旧链 `task+0x78 → +0x998 → +0x1A8` | 排除 | 新进程读出未映射地址 |
| `.data` 192KB 窗口差分 | 排除 | 只有 `task+0x78` 随进入自定义变化，其余是计时器噪声 |
| `.rdata 0xCC4800–0xCC6800` 459 个候选虚表 | 误判 | 过滤条件过松，该段是代码指针表 |
| 虚表 `0xCC5060` 当作页面类 | 误判 | 落在 `SceneSound` 虚表内部 |
| 场景对象存在性当页面判据 | 排除 | 所有场景在父页时已实例化 |
| 单次 `Manager#6` 从父页切页 | 失败（原因未定） | 三次受控调用无效 |

## 7. 下一步（建议顺序）

1. **验证零 hook 页面识别**（最安全、收益最大）：启动游戏后运行 `readonly-page-watch-v1.js`，用 L3 与 Esc 走一圈，确认它报出的页面名与画面一致。**这一步不改任何状态，先把"我在哪一页"变成可用能力。**
2. **补齐子页判据**：在发型页、自定义道具页各读一次当前键（应为 3、4），并记录进入方式（L3 与父页 Enter 是否等价）。
3. **查清 U1**：对比"L3 切换"与"父页确认"两条路径中 `Manager#6` 的调用上下文（调用时的 `manager+0x28`、调用者栈、是否在同一帧内），找出父页发起时的差异。
4. **查清 U4**：抓 `R` 键从歌曲列表进入自定义父页的调用链，与第 3 步对齐，找共同入口。
5. 上述稳定后，再谈"从搜索结果进入目标页并高亮"，以及各子页的列表与控件入口（U3）。

## 8. 复现材料

| 文件 | 用途 |
| --- | --- |
| `artifacts/native-routing-research/page-stage-sampler-v2.js` | 七阶段菜单/对象采样 |
| `artifacts/native-routing-research/rtti-vtables-v3.js` | 类名 → COL → 虚表反解 |
| `artifacts/native-routing-research/scene-table-dump-v1.js` | 场景红黑树遍历 + 查表返回 |
| `artifacts/native-routing-research/sibling-switch-v1.js` | L3 同层切换观测 |
| `artifacts/native-routing-research/confirm-caller-v1.js` | confirm 调用栈 |
| `artifacts/native-routing-research/readonly-page-watch-v1.js` | **零 hook 页面识别（待实机验证）** |
| `artifacts/native-routing-research/dump-runtime-code-v1.js` | 运行时 rwx 页转储 |
| `artifacts/native-routing-research/ghidra-scripts/DecompileAt.java` | 按地址反编译 |
| `artifacts/native-routing-research/find_callers.py` | 静态搜 call 站点（结论：这两个函数是虚表分发，无直接调用点） |
| `artifacts/native-routing-research/runtime-code/` | 三个运行时页快照（各 16KB） |

配套文档：`NATIVE_SELECTION_METHODS.md`（歌曲定位）、`NATIVE_PAGE_ROUTING_HANDOFF.md`（本轮任务清单）、`CUSTOMIZATION_*`（服装定位历史实验）。

## 9. 自动化复核进展（2026-10-31）

用户指定后续使用 AutoHotkey。已通过一次最小闭环和两次七阶段自然键盘流程，复核父页键1、模型键2、饰品键4；不再靠管理器扫描首命中，改从当前任务控制器+0x998读取并校验树成员及场景虚表。全程 Frida 只读，无 Hook 或原生函数主动调用。复用工具、真实截图及限制见 [AUTOMATED_PAGE_EXPERIMENTS.md](AUTOMATED_PAGE_EXPERIMENTS.md)。这不改变本报告中原生主动切页尚未打通的结论。

## 10. 后续原生主动调用已完成（2026-10-31）

第9节仅描述当时自然输入批次；后续已实际完成父页确认、同层子页切换、直接预览／更换，以及主页／选曲外层路由。`home-native-full-01`单会话14阶段通过，覆盖主页→自定义→模型／发型／饰品→主页→选曲→自定义→服装316定位→选曲。旧文中的“未打通”不能再作为当前状态。

最新接口、实机证据、失败记录与未完成边界见 [OUTER_NATIVE_ROUTING_VERIFIED.md](OUTER_NATIVE_ROUTING_VERIFIED.md) 和 [CURRENT_NATIVE_ROUTING_RESEARCH.md](CURRENT_NATIVE_ROUTING_RESEARCH.md)。产品DLL尚未集成本轮新路由，饰品目标定位及发型动作仍未认证。
