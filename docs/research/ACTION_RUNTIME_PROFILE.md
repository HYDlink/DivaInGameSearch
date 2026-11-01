# 服装动作运行时守卫补证与产品接线

日期：2026-10-31。以下地址只适用于 SHA256 `6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D`。堆地址、PID及线程每轮动态取得。本文区分研究脚本与产品DLL；原始证据均留在忽略目录，不随公开源码提交。

## 已完成的本轮研究

- REA只读入口核查 `ev_69fb6acef1ccf919dabd186fa4a523780c5fb47587ed1afe1bce1046206d96b0`，原始记录 `artifacts/action-profile-readonly.json`。确认外层/内部状态入口形状，选曲入口已有其他Mod跳转，产品保留其Hook链。会话已关闭；没有把该次观察记成产品动作成功。
- `artifacts/action-context-natural-02`：当前KAITO列表中原生高亮从45移到267，打开自然菜单。原生拥有判断 `0x1DA550(record)` 接收字节12并返回1，实际菜单截图显示“更换／预览”；已打开检查。只取消菜单，没有装备或购买。入口 `0x1D5C90(save,id)` 沿DML原有Hook查找服装记录，`0x1DA550` 是游戏自己的拥有判定；不得用目录、图片或0/316白名单替代。
- `artifacts/action-slots-natural-01`：自然E将 `controller+0x3C` 从0变1，画面歌者1/KAITO切到歌者2/初音；Q恢复0，六组模型数组保持一致。截图已打开检查。`controller+0x38` 为本轮3歌者数量，`+0x320+slot*0xE0` 为每槽模型数据起点；槽位语义由自然切换与静态数组访问交叉核对，不能将所有历史 `rawContext` 继续只解释成0。
- `artifacts/action-scope-natural-01` 至 `04`：自然F4依次观察全曲A/B/C并恢复曲目单独设置。控制器及 `+0x18` 配置指针不变，不能单靠指针取消旧范围请求。静态 `0x681A90` 由 `0x1D6510()` 的保存数据对象 `+0x169410` 循环范围；对应只读RVA为 `0x12B5A74`。本轮直接读回并读图确认共通C值2、单曲值3。A/B截图及循环逻辑存在，但首批产品动作仅开放直接认证的单曲值3，其他范围明确禁用。
- `artifacts/action-guards-static.log`、`action-config-static.log`、`action-scope-static.log`、`action-equipped-static.log` 为项目内独立Ghidra数据库的定向静态分析。反编译混合跳转或坏指令不作为可调用入口。`0x66B2D0` 和 `0x2C4A40` 的实际返回类型为void；产品Hook只保留返回寄存器，不依赖其值。

首次 `action-context-natural-01` 因新RPC未登记停止于模型列表，未继续盲发按键；已修正白名单，第二轮通过。每轮会话均独立关闭并分离。

## 产品当前实现与边界

`customization_action.h` 保存动作、来源、稳定ID、查询修订、目录、明确槽位与范围。网关独立计算是否跨页，只有真实主体双击链允许Locate跨页；Preview/Equip即使伪造双击来源也不导航。

`native_page_router.*` 从类型化外层字段、当前控制器、虚表与有界场景树取活动页，在外层/选曲/内部状态帧末推进协议。路由自身预期换对象不会盲目取消，用户改曲目、范围、槽位或页面会使旧请求失效。父页仅认证模型光标0；不改写用户父页光标。

`module_action.*` 在匹配模型状态机的游戏线程缓存原生拥有状态并执行动作，渲染线程不调用原生拥有函数。只对当前真实分类中、与当前歌者相同人物的唯一普通服装操作，不自动切分类或换歌者。首批只开放曲目单独设置；共通范围、特殊模型、目标缺失、未知拥有/范围/槽位均拒绝换装。命令仅7预览或3普通更换，不调用0/1。

成功依据加载完成后的真实装备索引映射、稳定ID、当前槽模型和内部状态跨帧一致，同时核对其他槽位。预览依据内部状态2及实际目标模型；退出后核对原装备和原模型再恢复搜索。用户改装备、范围或槽位后不反向写旧装备；执行器没有后台反向换装路径。

正式DLL已有主页／选曲双击路由、初音槽0单曲范围直接预览／换装及恢复的产品证据；逐条记录见 ../ACCEPTANCE.md。其他人物、更多槽位、共通范围及复杂搜索恢复不由这些样本推定。

## 真实D3D截图与启动阶段纠正

早期 actions-title-* 的失败原因是测试流程把外层子模式2/3的启动鸣谢当成真正标题，第一次Enter只跳过开场，随后没有在3/4标题确认。最终以实际图片区分阶段，第二次自然Enter已正常进入42/43主页；不能把早期失败报告成游戏初始化故障或新Hook回归。title_input_probe.js已把观察起点改为真正标题3/4。

早期动作截图是NativeCapture的GDI客户区读取，随后按用户要求补做真实D3D采集。桌面复制曾返回DXGI_ERROR_WAIT_TIMEOUT，未降级冒充通过；未采用的桌面复制代码已移除。capture_d3d_frame.py／d3d_frame_probe.js只在本Mod公开OnFrame原调用完成后复制实际交换链后缓冲，以工具自己的staging纹理读回，不改游戏输入、对象字段或装备。每张图单次Hook并立即卸载，退出worker时再次unload/detach；不需要给产品DLL增加截图入口。PNG只是BMP读回的无损格式转换，不是重绘画面。

本轮 actions-d3d-preview-*、actions-d3d-equip-*、actions-song-* 的前后PNG已打开核对，并结合独立只读actioncontext。产品导航和动作始终由正式DLL受理，观察脚本没有代为调用导航／装备函数。

## 复用命令

以下为Git Bash格式。`DIVA_PID`每轮重新查询；输出目录须尚不存在，AHK变量指向已安装AutoHotkey v2。

```bash
python scripts/research/run_action_context.py \
  --pid "$DIVA_PID" \
  --output artifacts/下一轮-自然菜单拥有对照 \
  --ahk "$AHK_EXE"

python scripts/research/run_action_scope.py \
  --pid "$DIVA_PID" \
  --output artifacts/下一轮-歌者槽位对照 \
  --ahk "$AHK_EXE" \
  --flow slots

python scripts/research/run_product_prepare.py \
  --pid "$DIVA_PID" \
  --output artifacts/下一轮-部署前自然退出 \
  --ahk "$AHK_EXE"

python scripts/research/capture_d3d_frame.py \
  --pid "$DIVA_PID" \
  --output artifacts/下一轮-真实D3D帧.png
```
