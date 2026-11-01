# DIVA 自动页面实验：AutoHotkey + 只读 Frida

本文件描述早期只读批次。后续已主动调用原生接口并完成14阶段端到端验证，最新结论见 [OUTER_NATIVE_ROUTING_VERIFIED.md](OUTER_NATIVE_ROUTING_VERIFIED.md)；不要沿用下文历史段落的“原生主动调用未完成”作为当前状态。

更新：2026-10-31，亚洲／上海时间。用户已明确要求后续 DIVA 键盘实验统一使用 AutoHotkey，不再让用户逐键充当操作员。

## 已完成的实际验证

已实现可重复运行的本地工具，并在当前游戏构建完成一次最小闭环和两次七阶段流程。每轮只有一个 Frida Session，结束均正常卸载脚本并分离。最终 `full-02` 的全部九张图片（七个主阶段及两次光标移动）已逐张打开，页面与字段一致。

| 实验 | 操作序列 | 结果 |
| --- | --- | --- |
| `minimal-01` | 歌曲 → R → 自定义父页 → Esc → 歌曲 | 三阶段断言与截图通过 |
| `full-01` | 歌曲 → 父页 → 模型 → 父页 → 饰品 → 父页 → 歌曲 | 七阶段通过；另含两次逐键光标断言 |
| `full-02` | 相同流程，增加输入前来源守卫及更严格选曲判据 | 再次通过，九张图片均复核 |

原始结果位于仓库忽略目录 `artifacts/native-routing-research/20261031/`，每轮独立子目录保存 `events.jsonl`、`result.json` 和按阶段命名的 PNG。`events.jsonl` 包含 UTC 时间、相对时间、输入前后、快照、截图起止时间及会话清理。原始私人日志不提交 Git。

本轮游戏 PID 为 384，仅供追溯；下次运行必须重新查询。构建 SHA-256：`6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D`。完整流程往返保持同一歌曲 PVID 10083，模型页读取到 Module ID 0，画面为“初音未来(-2)”；后缀来自已有裙长 Mod，不能当作 ID。

这证明自动操作和读回流程已经工作，不等于 C++ 插件的“任意页面原生跳转”已经实现。当前只支持已认证的歌曲页／自定义父页作为实验起点；模型和饰品是流程内部目标，不自动处理未知起点、弹窗、演奏、暂停、主菜单或保存确认。

## 诊断结论

- Computer Use 的窗口激活成功，但 DIVA 两次 Enter 均未得到页面变化。记事本被系统重定向到 Notepad3，真实键盘输入最终为 `ABC23`，数字 1 丢失，用户及截图均确认；因此不是“完全无法输入”，也不能记为可靠通过。
- Computer Use 对 Notepad3 出现过 `coordinate input geometry is unavailable` 和 `FrameArrived timed out`。其输入／截图表现存在问题，但本轮没有定位辅助组件内部根因。
- 只读令牌检查显示 DIVA、Codex、两个 Computer Use 辅助进程、Notepad3 及诊断 Python 都在会话 1，完整性 RID 8192（中），未提升、UIAccess=false。本次排除“中完整性辅助进程向高完整性游戏发送输入”这一具体解释，不能泛化排除所有输入过滤。
- 配置已有 `[windows] sandbox = "elevated"`；这不是游戏或输入助手获得管理员权限的证明。本轮没有修改该配置或系统安全策略。
- 已安装的 AutoHotkey v2.0.17 使用 `SendEvent` 扫描码，按下保持 150ms 再抬起，实机能驱动 DIVA；已由一键返回、完整闭环及重复运行证实。其成功与短按失败相符，但不足以单独证明根因一定是按键时长。

独立权限记录：`artifacts/native-routing-research/20261031/integrity.json`。记事本截图：同目录 `notepad-computer-use.png`。首次独立 `ahk-escape.png` 截图早于 AHK 完成，仍为父页，不能作为成功图；后续编排器使用 `subprocess.run` 等待 AHK 退出，再读回并截图，修复了该时序问题。

## 工具组成

| 文件 | 作用 |
| --- | --- |
| `scripts/research/run_page_experiment.py` | 流程、超时、输入前守卫、阶段断言、截图、日志与清理 |
| `scripts/research/diva_key.ahk` | 激活指定 PID，要求唯一游戏窗口及前台焦点；单次发送白名单按键并释放 |
| `scripts/research/frida_page_worker.mjs` | 复用本机 REA 所带 Frida，在整轮中保留一个 Session，RPC 只读采样 |
| `scripts/research/page_probe.js` | 已有字段的只读验证，不 Hook、不调用 NativeFunction、不写内存、不扫堆 |
| `scripts/research/windows_input.py` | 令牌完整性／会话诊断；此前准备的 Python 输入备用方法未实机验证，正式流程不使用它 |
| `scripts/research/test_page_experiment.py` | 四项离线边界检查，验证错误来源、错误光标和过渡中拒绝输入 |

运行依赖为已安装的 Python 3.11+、Node、REA npm 包内的 Frida、AutoHotkey v2 及 `build/release/NativeCapture.exe`；没有新增下载或安装。Frida 默认从 `%APPDATA%/npm/node_modules/rea-agents` 加载，也可用 `DIVA_REA_ROOT` 指向已有安装。本工具直接复用该 Frida 依赖，不通过 REA MCP 创建会话，所以这些新日志没有伪造的 REA Evidence ID；已保存构建哈希、脚本哈希、会话工作进程和时间轴。

## 页面断言与新增确认

复用既有研究，不重新寻找服装定位函数。每次采样从 `base+0xCC6F178` 读当前控制器，再从 `controller+0x998` 取得管理器，验证其虚表为 `base+0xCC4E38`。不使用扫描后第一个虚表命中。

| 字段／条件 | 本轮证据与用途 |
| --- | --- |
| `manager+0x18` → 节点 `+0x20` | 父页 1、模型 2、饰品 4，与两轮自然输入及截图一致 |
| `manager+8` | 红黑树哨兵；有界遍历证明当前节点属于该树，再核对场景虚表 |
| `manager+0x20` / `+0x28` | 无挂起目标时目标等于哨兵、过渡状态为 1；连续三次满足才继续 |
| 父页 `scene+0x28` | 下移两次读到 0→1→2，对应模型→发型→自定义道具；光标不符禁止 Enter |
| 模型页 `+0x70/+0x78/+0x110/+0x11C` | 校验真实列表边界、位置、数量一致，再读记录首 int32 ID；ID 0 与画面一致 |
| 控制器零值 + 选曲对象 | 离开自定义后控制器为 0；联合全局 5/6/1、song+0x6C==6、有效 PVID 判选曲 |
| 全局 5/6/1 | 各页均相同，再次排除其单独判页用途 |

没有新增候选函数 RVA，也没有主动调用 `0x6968D0`、`0x69BA10` 或写菜单状态。本轮从父页进入子页依靠 AHK 驱动正常输入，未解决旧报告中的原生主动调用失败。

每步发送前核验来源：只有歌曲页可按 R，只有父页匹配目标光标时可按 Enter，Esc 仅允许已验证的返回边。模型／饰品列表上的 Enter 被拒绝，避免触发更换或购买。每步超时默认 12 秒，整轮预算 90 秒；出错留现场、写失败结果，不盲按或强制回退。截图进程有八秒外部超时。普通截图仍依赖实际桌面可见性；不能把 PNG 存在直接当作图像验收。

**自然返回选曲时截图出现“保存中”**。工具没有直接调用装备、购买或存档函数，但正常游戏操作可能自行保存，不能承诺全流程绝不写存档。当前还没有覆盖保存中标记的独立内存判据，选曲页识别通过不意味着已证明磁盘保存完成。

## 复用命令

先在 PowerShell 查询当前 PID：`Get-Process DivaMegaMix | Select-Object Id,ProcessName`。以下按用户要求用反斜杠分行，适用于 Git Bash；PowerShell 运行时应改用其续行符或合成一行。示例 PID 必须替换为本次实际值，输出目录必须尚不存在；不要同时运行两个实验或另一套动态探针。

```bash
python scripts/research/run_page_experiment.py \
  --pid 384 \
  --flow minimal \
  --output artifacts/native-routing-research/下一轮-最小闭环 \
  --ahk 'C:/Users/50886/scoop/apps/AutoHotkey-Portable/2.0.17/AutoHotkey64.exe'

python scripts/research/run_page_experiment.py \
  --pid 384 \
  --flow full \
  --output artifacts/native-routing-research/下一轮-七阶段 \
  --ahk 'C:/Users/50886/scoop/apps/AutoHotkey-Portable/2.0.17/AutoHotkey64.exe'

python scripts/research/test_page_experiment.py

python scripts/research/windows_input.py \
  --pids 384 \
  --output artifacts/native-routing-research/下一轮-权限.json
```

在 Codex 沙箱中若无法访问交互桌面或进程，针对本次实验命令申请执行权限，说明是 AHK 浏览、只读 Frida 和截图。不要因此改系统安全设置或关闭沙箱。AHK 当前只接受 Esc、Enter、R、方向键；尚未实现和验收 1+3 组合，后续需要时在相同单键／组合键保护及页面断言下扩展。

后续研究可直接在这套时间轴中增加有限字段或已有静态候选的观测，不再要求用户逐步操作。原生跨页接口的确认、槽位／范围保持、加载与保存阻塞、其他起点仍是独立待办。
