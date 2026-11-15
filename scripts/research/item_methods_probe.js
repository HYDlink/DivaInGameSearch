// 与只读page_probe.js合并；默认只读，显式armlocate／armaction用于用户授权的受控原生实验，不供产品加载。
const originalItemDetails = rpc.exports.details;
let itemObservers = [];
let itemEvents = [];
let itemLocateObserver = null;
let itemLocateTimer = null;
let observedSaveRecord = null;

// 参数 object/length 为已认证对象和限定字节数；读取失败保留空值，不扫描进程。
function itemWords(object, length) {
    const values = {};
    for (let offset = 0; offset < length; offset += 4)
        values['0x' + offset.toString(16)] = read(object.add(offset), 'int');
    return values;
}

// 保存当前活动场景有限字段及指针邻域，候选容器不视为已认证列表。
function itemMethodDetails() {
    const result = originalItemDetails();
    if (!result.nodeValid || ![1, 2, 3, 4].includes(result.sceneKey)) return result;
    const scene = ptr(result.scene);
    result.itemSceneWords = itemWords(scene, 0x340);
    // 发型与道具的独立候选向量来自自然操作差分；逐项校验边界后读取，不将其提前认定为生产ABI。
    const hair = result.sceneKey === 3;
    const begin = read(scene.add(hair ? 0x108 : 0x48));
    const end = read(scene.add(hair ? 0x110 : 0x50));
    const index = read(scene.add(hair ? 0x138 : 0x78), 'int');
    const count = read(scene.add(hair ? 0x144 : 0x84), 'int');
    result.itemControlTransition = read(scene.add((hair ? 0x138 : 0x78) + 0xac), 'int');
    if ([3, 4].includes(result.sceneKey) && begin && end && count > 0 && count < 1000 && end.sub(begin).toInt32() === count * 8) {
        const records = [];
        for (let number = 0; number < count; number++) {
            const record = read(begin.add(number * 8));
            records.push({index: number, address: addressText(record), id: read(record, 'int')});
        }
        result.itemList = {begin: begin.toString(), end: end.toString(), index, count, records,
            selected: index >= 0 && index < count ? records[index] : null};
    }
    result.itemPointerCandidates = {};
    for (const offset of [0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80]) {
        const candidate = read(scene.add(offset));
        if (!candidate || candidate.isNull()) continue;
        const range = Process.findRangeByAddress(candidate);
        if (!range || !range.protection.includes('r') || candidate.add(128).compare(range.base.add(range.size)) > 0) continue;
        result.itemPointerCandidates['0x' + offset.toString(16)] = {address: candidate.toString(), words: itemWords(candidate, 128)};
    }
    const configuration = read(ptr(result.controller).add(0x18));
    if (configuration && !configuration.isNull()) result.itemConfigurationWords = itemWords(configuration, 0x180);
    const controller = ptr(result.controller);
    result.currentSlot = read(controller.add(0x3c), 'int');
    result.slotCount = read(controller.add(0x38), 'int');
    result.sourcePvid = read(controller.add(8), 'int');
    result.scope = read(imageBase.add(0x12b5a74), 'int');
    result.slotModels = Array.from({length: 6}, (_, slot) => itemWords(controller.add(0x320 + slot * 0xe0), 0x30));
    if (observedSaveRecord && observedSaveRecord.controller === result.controller)
        result.observedSavedSlot = {slot: observedSaveRecord.slot, words: itemWords(observedSaveRecord.pointer, 0x1c)};
    const overrides = read(controller.add(0x10));
    if (overrides && !overrides.isNull()) result.overrideSlots = Array.from({length: 6}, (_, slot) => itemWords(overrides.add(slot * 0x1c), 0x1c));
    return result;
}

// 卸载本脚本自己的观察器，保留正式 Mod 和其他工具的 Hook。
function cancelItemObservers() {
    for (const observer of itemObservers) observer.detach();
    itemObservers = [];
    if (itemLocateObserver) itemLocateObserver.detach();
    if (itemLocateTimer) clearTimeout(itemLocateTimer);
    itemLocateObserver = itemLocateTimer = null;
    return {说明: '本轮自然调用观察已停止'};
}

// 参数 settings.rvas 为静态分析确认的入口，最多16个；只记录参数及前后快照，不修改寄存器或返回值。
function observeItemMethods(settings) {
    cancelItemObservers();
    itemEvents = [];
    const rvas = settings.rvas || [];
    if (rvas.length > 16) throw new Error('观察入口数量超限');
    for (const rva of rvas) {
        if (rva === 0x1d5e30) {
            itemObservers.push(Interceptor.attach(imageBase.add(rva), {
                // 参数args是自然装备配置getter的来源歌曲与槽位；只读观察当前事务，不主动调用getter。
                onEnter(args) {
                    const current = snapshot();
                    this.accept = current.nodeValid && [3, 4].includes(current.sceneKey)
                        && args[1].toInt32() === read(ptr(current.controller).add(8), 'int')
                        && args[2].toInt32() === read(ptr(current.controller).add(0x3c), 'int');
                    this.slot = args[2].toInt32(); this.controller = current.controller;
                },
                // 参数result为原生返回的装备记录；记录真实保存配置，和临时显示字段分开。
                onLeave(result) {
                    if (!this.accept || result.isNull()) return;
                    observedSaveRecord = {controller: this.controller, slot: this.slot, pointer: result};
                    if (itemEvents.length < 100) itemEvents.push({说明: '自然getter返回独立装备记录', rva: '0x1d5e30',
                        slot: this.slot, words: itemWords(result, 0x1c), thread: Process.getCurrentThreadId()});
                }
            }));
            continue;
        }
        itemObservers.push(Interceptor.attach(imageBase.add(rva), {
            // 参数 args 为原生自然调用参数；仅在当前活动发型／道具场景记录。
            onEnter(args) {
                const current = snapshot();
                this.capture = [3, 4].includes(current.sceneKey) && itemEvents.length < 100;
                if (!this.capture) return;
                this.event = {时间: new Date().toISOString(), rva: '0x' + rva.toString(16),
                    thread: Process.getCurrentThreadId(), args: [0, 1, 2, 3].map(index => args[index].toString()),
                    before: itemMethodDetails()};
            },
            // 参数 retval 为真实返回值；独立快照用于对照，不以返回成功判定定位或装备。
            onLeave(retval) {
                if (!this.capture) return;
                this.event.result = retval.toString();
                this.event.after = itemMethodDetails();
                itemEvents.push(this.event);
            }
        }));
    }
    return {说明: '只观察自然调用，未主动执行原生入口', 入口数: rvas.length,
        当前快照: itemMethodDetails()};
}
rpc.exports.details = itemMethodDetails;
rpc.exports.observe = observeItemMethods;
// 返回最多100条自然调用记录，由外部实验结束时落盘。
rpc.exports.events = function () { return itemEvents; };
rpc.exports.cancel = cancelItemObservers;

// 仅采样卸除事务所需已认证字段，避免轮询时重复读取无关指针邻域及完整目录。
rpc.exports.removalstate = function () {
    const result = snapshot();
    if (!result.nodeValid || ![3, 4].includes(result.sceneKey)) return result;
    const scene = ptr(result.scene), controller = ptr(result.controller);
    const hair = result.sceneKey === 3;
    result.config = addressText(read(controller.add(0x18)));
    result.scope = read(imageBase.add(0x12b5a74), 'int');
    result.currentSlot = read(controller.add(0x3c), 'int');
    result.sourcePvid = read(controller.add(8), 'int');
    result.itemSceneWords = {};
    for (const offset of [0x10, 0x14, 0x18, 0x30, 0x38]) result.itemSceneWords['0x' + offset.toString(16)] = read(scene.add(offset), 'int');
    const state = read(scene.add(hair ? 0x3a0 : 0x300));
    result.internalState = {manager: addressText(state), words: [null, null, state ? read(state.add(0x18), 'int') : null]};
    const control = scene.add(hair ? 0x138 : 0x78);
    result.itemControlTransition = read(control.add(0xac), 'int');
    const index = read(control, 'int'), count = read(control.add(0xc), 'int');
    const begin = read(scene.add(hair ? 0x108 : 0x48)), end = read(scene.add(hair ? 0x110 : 0x50));
    if (begin && end && count > 0 && count < 1000 && end.sub(begin).toInt32() === count * 8 && index >= 0 && index < count) {
        const record = read(begin.add(index * 8));
        result.itemList = {begin: addressText(begin), end: addressText(end), index, count,
            selected: {index, address: addressText(record), id: read(record, 'int')}};
    }
    result.slotModels = [];
    for (let slot = 0; slot < 6; slot++) result.slotModels.push(itemWords(controller.add(0x320 + slot * 0xe0), 0x18));
    return result;
};

// 参数settings.id为当前真实装备ID；只允许发型搭配模型或当前部位卸除，不购买、不直接写装备字段。
rpc.exports.armremove = function (settings) {
    if (itemLocateObserver) throw new Error('仍有原生实验等待执行');
    const initial = itemMethodDetails();
    const hair = initial.sceneKey === 3;
    const part = hair ? 4 : initial.itemSceneWords?.['0x30'];
    const field = '0x' + (4 + part * 4).toString(16);
    if (![3, 4].includes(initial.sceneKey) || initial.scope !== 3 || !Number.isInteger(part) || part < 0 || part > 4
        || initial.internalState?.words[2] !== 0 || initial.itemControlTransition !== -1
        || initial.transition !== 1 || initial.target !== initial.head
        || !Number.isInteger(settings.id) || settings.id < 0
        || initial.slotModels?.[initial.currentSlot]?.[field] !== settings.id
        || initial.itemList?.selected?.id !== settings.id)
        throw new Error('卸除要求空闲单曲页面、高亮当前真实装备、有效部位及稳定控件');
    const target = ptr(initial.itemList.selected.address);
    const callbackRva = hair ? 0x68ac40 : 0x68f430;
    const command = hair ? 6 : 5;
    itemLocateObserver = Interceptor.attach(imageBase.add(0x66b2d0), {
        // 参数args为当前真实状态机帧，禁止在RPC线程直接执行游戏逻辑。
        onEnter(args) { this.matches = args[0].toString() === initial.internalState.manager; },
        // 原生帧末重新验证来源、部位及实际装备，且只执行一次自然菜单已观察到的命令。
        onLeave() {
            if (!this.matches || !itemLocateObserver) return;
            itemLocateObserver.detach(); itemLocateObserver = null;
            if (itemLocateTimer) clearTimeout(itemLocateTimer);
            itemLocateTimer = null;
            const before = itemMethodDetails();
            const scene = ptr(initial.scene);
            const binding = read(scene.add(hair ? 0x378 : 0x2d8));
            if (before.scene !== initial.scene || before.controller !== initial.controller || before.config !== initial.config
                || before.sourcePvid !== initial.sourcePvid || before.scope !== initial.scope
                || before.currentSlot !== initial.currentSlot || before.slotCount !== initial.slotCount
                || before.transition !== 1 || before.target !== before.head
                || before.internalState?.words[2] !== 0 || before.itemControlTransition !== -1
                || (!hair && before.itemSceneWords['0x30'] !== part)
                || before.slotModels?.[before.currentSlot]?.[field] !== settings.id
                || before.itemList?.begin !== initial.itemList.begin || before.itemList?.end !== initial.itemList.end
                || before.itemList?.selected?.address !== target.toString() || read(target, 'int') !== settings.id
                || !binding || !read(binding.add(8))?.equals(scene)
                || !read(binding.add(0x10))?.equals(imageBase.add(callbackRva))) {
                itemEvents.push({说明: '卸除取消：来源、装备或回调绑定变化'}); return;
            }
            scene.add(0x38).writeS32(before.itemList.index);
            scene.add(0x40).writePointer(target);
            const callback = new NativeFunction(imageBase.add(callbackRva), 'void', ['pointer', 'int']);
            callback(scene, command);
            const next = read(scene.add(0x10), 'int');
            if (next !== 3) {
                itemEvents.push({说明: '卸除停止：回调未进入已认证加载状态', next}); return;
            }
            const transition = new NativeFunction(imageBase.add(0x66b3a0), 'uint8', ['pointer', 'int']);
            const accepted = transition(ptr(initial.internalState.manager), next);
            itemEvents.push({说明: '原生卸除已提交，必须跨帧验证', action: '卸除', command, part,
                id: settings.id, next, accepted, before, after: itemMethodDetails()});
        }
    });
    // 超时只移除本次观察器，不发送备用输入、不重试或写装备。
    itemLocateTimer = setTimeout(function () {
        if (itemLocateObserver) itemLocateObserver.detach();
        itemLocateObserver = itemLocateTimer = null;
        itemEvents.push({说明: '卸除等待原生帧超时'});
    }, 5000);
    return {说明: '等待原生帧末执行卸除', command, part, id: settings.id};
};

// 参数settings.id为已在当前角色／当前部位原生列表唯一出现的稳定ID；只在匹配状态机帧末调用已静态核对的原生控件函数。
rpc.exports.armlocate = function (settings) {
    if (itemLocateObserver) throw new Error('仍有定位实验等待原生帧');
    const initial = itemMethodDetails();
    if (![3, 4].includes(initial.sceneKey) || !initial.itemList || initial.scope !== 3
        || initial.internalState.words[2] !== 0) throw new Error('需要已核对单曲范围的空闲发型／道具列表');
    const matches = initial.itemList.records.filter(record => record.id === settings.id);
    if (matches.length !== 1) throw new Error('目标稳定ID不存在或重复');
    const targetIndex = matches[0].index;
    const state = initial.internalState.manager;
    itemLocateObserver = Interceptor.attach(imageBase.add(0x66b2d0), {
        // 参数args为真实状态机帧；仅接受本次活动页面的状态机对象。
        onEnter(args) { this.matches = args[0].toString() === state; },
        // 在原生状态帧末再次验证页面、槽位、列表和高亮来源后调用，不写高亮缓存字段。
        onLeave() {
            if (!this.matches || !itemLocateObserver) return;
            itemLocateObserver.detach(); itemLocateObserver = null;
            if (itemLocateTimer) clearTimeout(itemLocateTimer);
            itemLocateTimer = null;
            const before = itemMethodDetails();
            if (before.scene !== initial.scene || before.controller !== initial.controller || before.config !== initial.config
                || before.currentSlot !== initial.currentSlot || before.scope !== 3 || before.internalState.words[2] !== 0
                || before.sourcePvid !== initial.sourcePvid || before.slotCount !== initial.slotCount
                || before.itemList?.begin !== initial.itemList.begin || before.itemList?.selected?.id !== initial.itemList.selected.id) {
                itemEvents.push({说明: '原生定位取消：来源上下文变化'}); return;
            }
            const scene = ptr(before.scene);
            const control = scene.add(before.sceneKey === 3 ? 0x138 : 0x78);
            if (read(control.add(0xac), 'int') !== -1) {
                itemEvents.push({说明: '原生定位取消：控件仍在过渡'}); return;
            }
            const select = new NativeFunction(imageBase.add(0x22d250), 'void', ['pointer', 'int']);
            // 只更新当前卡片显示；688780／68C750会重建列表并重置高亮，不能用于定位后的刷新。
            const refresh = new NativeFunction(imageBase.add(before.sceneKey === 3 ? 0x6891f0 : 0x68d180), 'void', ['pointer', 'int']);
            select(control, targetIndex);
            refresh(scene, 3);
            itemEvents.push({说明: '原生帧末控件定位已调用，尚需跨帧读回及截图', id: settings.id, targetIndex,
                thread: Process.getCurrentThreadId(), before, after: itemMethodDetails()});
        }
    });
    // 超时只撤销本轮等待，不盲发重试。
    itemLocateTimer = setTimeout(function () {
        if (itemLocateObserver) itemLocateObserver.detach();
        itemLocateObserver = itemLocateTimer = null;
        itemEvents.push({说明: '原生定位实验等待帧超时'});
    }, 5000);
    return {说明: '已等待匹配原生状态帧', id: settings.id, targetIndex};
};

// 参数settings.action/id为已看图核实菜单准备过的目标与动作；先自然取消菜单，再从空闲列表提交，避免残留弹层。
rpc.exports.armaction = function (settings) {
    if (itemLocateObserver) throw new Error('仍有原生实验等待执行');
    const initial = itemMethodDetails();
    const hair = initial.sceneKey === 3;
    const commands = hair ? {预览: 7, 更换: 3} : {预览: 7, 更换: 4, 卸除: 5};
    const command = commands[settings.action];
    if (![3, 4].includes(initial.sceneKey) || command === undefined || initial.scope !== 3
        || initial.internalState.words[2] !== 0 || initial.itemList?.selected?.id !== settings.id)
        throw new Error('需要已认证单曲范围、目标已准备且菜单已自然取消的空闲列表；拒绝未知命令');
    const part = hair ? 4 : initial.itemSceneWords['0x30'];
    const equipped = initial.slotModels[initial.currentSlot]['0x' + (4 + part * 4).toString(16)];
    if (settings.action === '卸除' && equipped !== settings.id) throw new Error('卸除目标不是当前槽位实际装备');
    if (settings.action === '更换' && equipped === settings.id) throw new Error('目标已装备，禁止将更换误用为卸除');
    const scene = ptr(initial.scene);
    const target = ptr(initial.itemList.selected.address);
    if (read(target, 'int') !== settings.id) throw new Error('原生列表真实目标记录与稳定ID不符');
    // 本研究工具只允许已逐项看图确认拥有菜单的代表样本；产品必须另接实时拥有判定，不能复制此样本白名单。
    const verifiedSamples = hair ? [500, 616] : [0, 41, 57, 70];
    if (!verifiedSamples.includes(settings.id)) throw new Error('该目标尚不属于本轮已核对的研究样本');
    const callbackRva = hair ? 0x68ac40 : 0x68f430;
    const callback = read(scene.add(hair ? 0x378 : 0x2d8));
    if (!callback || !read(callback.add(8))?.equals(scene) || !read(callback.add(0x10))?.equals(imageBase.add(callbackRva)))
        throw new Error('菜单回调绑定与已观察ABI不符');
    itemLocateObserver = Interceptor.attach(imageBase.add(0x66b2d0), {
        // 参数args为原生状态机对象，仅允许目标菜单所在状态机。
        onEnter(args) { this.matches = args[0].toString() === initial.internalState.manager; },
        // 帧末重新核对目标、来源、范围与槽位后只提交一次已认证命令，后续由原生跨帧推进。
        onLeave() {
            if (!this.matches || !itemLocateObserver) return;
            itemLocateObserver.detach(); itemLocateObserver = null;
            if (itemLocateTimer) clearTimeout(itemLocateTimer);
            itemLocateTimer = null;
            const before = itemMethodDetails();
            if (before.scene !== initial.scene || before.controller !== initial.controller || before.config !== initial.config
                || before.currentSlot !== initial.currentSlot || before.sourcePvid !== initial.sourcePvid
                || before.scope !== 3 || before.internalState.words[2] !== 0 || before.itemList?.selected?.id !== settings.id
                || before.itemList?.begin !== initial.itemList.begin || before.itemList?.selected?.address !== target.toString()
                || read(target, 'int') !== settings.id) {
                itemEvents.push({说明: '原生动作取消：菜单目标或上下文已变化'}); return;
            }
            const originalTargetIndex = read(scene.add(0x38), 'int');
            const originalTargetRecord = read(scene.add(0x40));
            // +0x38/+0x40是自然确认菜单已经观察过的目标序号与记录；仅准备动作目标，不写装备或拥有状态。
            scene.add(0x38).writeS32(before.itemList.index);
            scene.add(0x40).writePointer(target);
            const call = new NativeFunction(imageBase.add(callbackRva), 'void', ['pointer', 'int']);
            call(scene, command);
            // 回调仅准备下一状态；按生产模型动作同样的状态机协议显式受理，不能把回调返回当动作成功。
            // 发型／道具+0x10是待进入状态，+0x14是加载完成后的返回状态；两者不可套用模型页布局。
            const next = read(scene.add(0x10), 'int');
            if (![2, 3].includes(next)) {
                scene.add(0x38).writeS32(originalTargetIndex);
                scene.add(0x40).writePointer(originalTargetRecord);
                itemEvents.push({说明: '原生回调未给出已认证下一状态', next}); return;
            }
            const transition = new NativeFunction(imageBase.add(0x66b3a0), 'uint8', ['pointer', 'int']);
            const accepted = transition(ptr(initial.internalState.manager), next);
            itemEvents.push({说明: '受控原生动作已提交一次，后续须独立读回', action: settings.action, command, id: settings.id,
                next, accepted, thread: Process.getCurrentThreadId(), before, after: itemMethodDetails()});
        }
    });
    // 超时停止本轮动作，不执行重试或后台恢复。
    itemLocateTimer = setTimeout(function () {
        if (itemLocateObserver) itemLocateObserver.detach();
        itemLocateObserver = itemLocateTimer = null;
        itemEvents.push({说明: '原生动作等待匹配帧超时'});
    }, 5000);
    return {说明: '等待已认证菜单帧末提交', action: settings.action, command, id: settings.id};
};
