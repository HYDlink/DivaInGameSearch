'use strict';
// 播放列表自定义动作研究探针：默认只读；仅在用户授权的受控实验中于真实模型状态帧末提交一次原生动作。
// 与 page_probe.js、item_methods_probe.js 在同一脚本上下文加载，复用其 snapshot、details 与地址读取。
let pvListener = null;
let pvEvents = [];
let pvResult = null;

// 参数值为地址文本或可空指针；统一转成地址文本，空值返回空。页面快照里的地址已经是字符串。
function pvAddress(value) {
    if (!value) return null;
    if (typeof value === 'string') return value;
    return value.isNull() ? null : value.toString();
}

// 返回当前模型页可执行原生动作的前置条件；不满足时返回中文原因。
function pvModelGuard() {
    if (pvEvents.length > 200) pvEvents.splice(0, pvEvents.length - 200);
    const current = details();
    if (current.page !== '模型') return {reason: '当前不是模型页', snapshot: current};
    if (current.transition !== 1 || current.target !== current.head) return {reason: '场景仍在过渡或存在挂起目标', snapshot: current};
    if (!current.internalState || current.internalState.words[2] !== 0) return {reason: '模型页内部状态不是空闲列表', snapshot: current};
    if (current.sceneFields['0x60'] !== -1) return {reason: '原生动作菜单或目标未清空', snapshot: current};
    if (!current.actionCallback || current.actionCallback.boundValues[0] !== current.scene
        || current.actionCallback.boundValues[1] !== imageBase.add(0x695250).toString())
        return {reason: '动作回调未绑定当前模型场景', snapshot: current};
    return {reason: null, snapshot: current};
}

// 在下一个真实模型状态帧返回后查询当前列表全部条目的拥有状态；只调用只读判定函数。
rpc.exports.pvownership = function () {
    if (pvListener) { pvListener.detach(); pvListener = null; }
    pvEvents = []; pvResult = null;
    const guard = pvModelGuard();
    if (guard.reason) throw new Error(guard.reason);
    const initial = guard.snapshot;
    const begin = ptr(initial.module.begin);
    const ids = [];
    for (let index = 0; index < initial.module.count; index++) {
        const record = read(begin.add(index * 8));
        const id = record && !record.isNull() ? read(record, 'int') : null;
        if (Number.isInteger(id) && id >= 0) ids.push(id);
    }
    if (!ids.length || ids.length > 4096) throw new Error('当前真实列表条目数量无效');
    const save = new NativeFunction(imageBase.add(0x1D6510), 'pointer', []);
    const findRecord = new NativeFunction(imageBase.add(0x1D5C90), 'pointer', ['pointer', 'int']);
    const readOwned = new NativeFunction(imageBase.add(0x1DA550), 'uint8', ['pointer']);
    let armed = true;
    pvEvents.push({说明: '已排队拥有查询', count: ids.length, scene: initial.scene});
    pvListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为内部状态更新器；只接受当前模型页持有的状态机。
        onEnter(values) { this.match = armed && values[0].toString() === initial.internalState.manager; },
        // 状态更新完成后核对页面未变，再批量读取拥有标记。
        onLeave() {
            if (!this.match) return;
            armed = false; pvListener.detach(); pvListener = null;
            const fresh = details();
            if (fresh.scene !== initial.scene || fresh.page !== '模型') {
                pvEvents.push({说明: '拥有查询取消：页面改变'});
                return;
            }
            try {
                const handle = save();
                if (!handle || handle.isNull()) throw new Error('保存对象不可读');
                const owned = [];
                for (const id of ids) {
                    const record = findRecord(handle, id);
                    if (record.isNull()) continue;
                    const flag = readOwned(record);
                    if (flag <= 1 && flag === 1) owned.push(id);
                }
                pvResult = {ids, owned, sampledAt: new Date().toISOString(), scene: initial.scene,
                    controller: initial.controller, menu: initial.menu, sourcePvid: initial.sourcePvid};
                pvEvents.push({说明: '拥有查询完成', count: owned.length});
            } catch (error) { pvEvents.push({说明: '拥有查询失败', error: String(error)}); }
        }
    });
    return {说明: '等待模型状态帧', count: ids.length};
};
rpc.exports.pvownershipresult = function () { return {events: pvEvents, result: pvResult}; };

// 参数 settings.id 为目标稳定服装、settings.action 为 preview 或 equip；帧末提交一次原生动作，不直接写配置。
rpc.exports.pvarmaction = function (settings) {
    if (pvListener) throw new Error('仍有等待执行的原生查询或动作');
    pvEvents = []; pvResult = null;
    const command = settings.action === 'preview' ? 7 : settings.action === 'equip' ? 3 : null;
    if (!command) throw new Error('只开放预览或更换');
    if (!Number.isInteger(settings.id) || settings.id < 0) throw new Error('目标ID无效');
    const guard = pvModelGuard();
    if (guard.reason) throw new Error(guard.reason);
    const initial = guard.snapshot;
    const scene = ptr(initial.scene);
    const begin = ptr(initial.module.begin);
    const matches = [];
    for (let index = 0; index < initial.module.count; index++) {
        const record = read(begin.add(index * 8));
        if (record && !record.isNull() && read(record, 'int') === settings.id) matches.push({index, record});
    }
    if (matches.length !== 1) throw new Error('目标未在当前真实列表中唯一匹配');
    const target = matches[0];
    const callback = new NativeFunction(imageBase.add(0x695250), 'void', ['pointer', 'int']);
    const switchState = new NativeFunction(imageBase.add(0x66B3A0), 'uint8', ['pointer', 'int']);
    let armed = true;
    pvEvents.push({说明: '已排队受控原生动作', id: settings.id, index: target.index, command});
    pvListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为内部状态更新器；只匹配当前模型页状态机。
        onEnter(values) { this.match = armed && values[0].toString() === initial.internalState.manager; },
        // 同帧暂存真实目标并复现自然菜单的加载协议；失败只在空闲态恢复目标字段。
        onLeave() {
            if (!this.match) return;
            armed = false; pvListener.detach(); pvListener = null;
            const fresh = details();
            if (fresh.scene !== initial.scene || fresh.controller !== initial.controller
                || fresh.menu.join('/') !== initial.menu.join('/') || fresh.page !== '模型') {
                pvEvents.push({说明: '受控动作取消：上下文改变'});
                return;
            }
            const oldIndex = scene.add(0x60).readS32(), oldRecord = scene.add(0x68).readPointer();
            try {
                scene.add(0x60).writeS32(target.index); scene.add(0x68).writePointer(target.record);
                callback(scene, command);
                const next = scene.add(0x14).readS32();
                if (next !== 3 && !(command === 7 && next === 2)) throw new Error('回调未认可加载状态:' + next);
                pvEvents.push({说明: '原生回调已准备加载', next, operation: scene.add(0x18).readS32()});
                const accepted = switchState(ptr(initial.internalState.manager), next);
                pvEvents.push({说明: '原生状态切换已提交', accepted, command,
                    snapshot: details()});
            } catch (error) {
                const internal = read(ptr(initial.internalState.manager).add(0x18), 'int');
                if (internal === 0) { scene.add(0x60).writeS32(oldIndex); scene.add(0x68).writePointer(oldRecord); }
                pvEvents.push({说明: '受控动作失败', error: String(error)});
            }
        }
    });
    return {说明: '等待模型状态帧提交', id: settings.id, index: target.index, command};
};
// 参数为空；在真实模型状态帧末复现产品使用的原生预览退出分支（模型页 6963E0 对应协议）。
rpc.exports.pvexitmodelpreview = function () {
    if (pvListener) throw new Error('仍有等待执行的原生查询或动作');
    pvEvents = [];
    const initial = details();
    if (initial.page !== '模型' || !initial.internalState || initial.internalState.words[2] !== 2)
        throw new Error('当前不是已确认的模型临时预览');
    const scene = ptr(initial.scene), state = ptr(initial.internalState.manager);
    const switchState = new NativeFunction(imageBase.add(0x66B3A0), 'uint8', ['pointer', 'int']);
    let armed = true;
    pvEvents.push({说明: '已排队原生预览退出', scene: initial.scene});
    pvListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为内部状态更新器；只匹配当前模型页状态机。
        onEnter(values) { this.match = armed && values[0].toString() === initial.internalState.manager; },
        // 与产品 InvokePreviewExit 同序：先按直连标记准备字段，再请求原生返回状态。
        onLeave() {
            if (!this.match) return;
            armed = false; pvListener.detach(); pvListener = null;
            const fresh = snapshot();
            if (fresh.scene !== initial.scene || fresh.controller !== initial.controller) {
                pvEvents.push({说明: '预览退出取消：上下文改变'});
                return;
            }
            try {
                const direct = scene.add(0x28).readU8();
                const next = direct ? 0 : 3;
                pvEvents.push({说明: '预览退出字段', direct, next});
                if (direct) scene.add(0x28).writeU8(0);
                else { scene.add(0x1C).writeS32(2); scene.add(0x18).writeS32(0); }
                const accepted = switchState(state, next);
                pvEvents.push({说明: '预览退出已提交', accepted, snapshot: details()});
            } catch (error) { pvEvents.push({说明: '预览退出失败', error: String(error)}); }
        }
    });
    return {说明: '等待模型状态帧退出预览'};
};
rpc.exports.pvevents = function () { return pvEvents; };
rpc.exports.pvcancel = function () {
    if (pvListener) { pvListener.detach(); pvListener = null; }
    return {说明: '已停止本探针的等待'};
};
// 只读返回当前模型列表的全部稳定 ID 与分类，供恢复原模型前定位所属分类。
rpc.exports.pvlist = function () {
    const current = details();
    if (current.page !== '模型') throw new Error('当前不是模型页');
    const begin = ptr(current.module.begin);
    const ids = [];
    for (let index = 0; index < current.module.count; index++) {
        const record = read(begin.add(index * 8));
        ids.push(record && !record.isNull() ? read(record, 'int') : null);
    }
    return {category: current.controllerFields['0x40'], count: current.module.count,
        highlight: current.module.index, highlightId: current.module.id, ids};
};
// 参数 settings.direction 为相邻分类方向；在真实模型状态帧末调用已认证的分类入口一步，随后由调用方读回。
rpc.exports.pvcategory = function (settings) {
    if (pvListener) throw new Error('仍有等待执行的原生查询或动作');
    pvEvents = [];
    const direction = settings.direction;
    if (direction !== 1 && direction !== -1) throw new Error('只允许相邻分类一步');
    const guard = pvModelGuard();
    if (guard.reason) throw new Error(guard.reason);
    const initial = guard.snapshot;
    const scene = ptr(initial.scene);
    const move = new NativeFunction(imageBase.add(0x694D00), 'void', ['pointer', 'int']);
    let armed = true;
    pvEvents.push({说明: '已排队分类切换', direction, category: initial.controllerFields['0x40']});
    pvListener = Interceptor.attach(imageBase.add(0x66B2D0), {
        // 参数 values 为内部状态更新器；只匹配当前模型页状态机。
        onEnter(values) { this.match = armed && values[0].toString() === initial.internalState.manager; },
        // 直接调用模型页已认证的分类入口，不写控制器字段。
        onLeave() {
            if (!this.match) return;
            armed = false; pvListener.detach(); pvListener = null;
            const fresh = details();
            if (fresh.scene !== initial.scene || fresh.page !== '模型') {
                pvEvents.push({说明: '分类切换取消：页面改变'});
                return;
            }
            try {
                move(scene, direction);
                pvEvents.push({说明: '分类切换已提交', direction, accepted: true});
            } catch (error) { pvEvents.push({说明: '分类切换失败', error: String(error)}); }
        }
    });
    return {说明: '等待模型状态帧切换分类', direction};
};
// 参数 controller 为已核验活动控制器，返回六个槽位的独立记录字（0x1C 步长）；不可读时返回空。
function pvOverrideSlots(controller) {
    const table = read(controller.add(0x10));
    if (!table || table.isNull()) return null;
    return Array.from({length: 6}, (_, slot) => Array.from({length: 7},
        (_, word) => read(table.add(slot * 0x1C + word * 4), 'int')));
}
// 参数 controller 为已核验活动控制器，返回六个槽位的临时显示配置（0xE0 步长，前六项）。
function pvDisplaySlots(controller) {
    return Array.from({length: 6}, (_, slot) => Array.from({length: 6},
        (_, word) => read(controller.add(0x320 + slot * 0xE0 + word * 4), 'int')));
}
// 参数 settings.pvid 为目标歌曲；只读 PV 数据库中的演出槽位，用于核对歌者解析来源。
rpc.exports.pvperformers = function (settings) {
    const pvid = settings.pvid;
    if (!Number.isInteger(pvid)) throw new Error('目标PVID无效');
    const database = imageBase.add(0x1753780);
    const begin = read(database.add(0x98)), end = read(database.add(0xA0));
    if (!begin || !end || begin.isNull() || end.isNull() || end.compare(begin) < 0) throw new Error('PV数据库未就绪');
    const count = end.sub(begin).toInt32() / 8;
    if (count <= 0 || count > 100000) throw new Error('PV数据库边界不符');
    for (let index = 0; index < count; index++) {
        const entry = read(begin.add(index * 8));
        if (!entry || entry.isNull() || read(entry, 'int') !== pvid) continue;
        const slotBegin = read(entry.add(0x98)), slotEnd = read(entry.add(0xA0));
        if (!slotBegin || !slotEnd || slotEnd.compare(slotBegin) < 0) return {pvid, available: false, reason: '演出槽位边界不符'};
        const bytes = slotEnd.sub(slotBegin).toInt32();
        if (bytes % 0x44 || bytes / 0x44 > 6) return {pvid, available: false, reason: '演出槽位步长不符'};
        const performers = [];
        for (let slot = 0; slot < bytes / 0x44; slot++) {
            const at = slotBegin.add(slot * 0x44);
            performers.push({slot, type: read(at, 'int'), character: read(at.add(4), 'int'),
                costume: read(at.add(8), 'int'),
                costumesByDifficulty: [0, 1, 2, 3, 4].map(n => read(at.add(0xC + n * 4), 'int'))});
        }
        return {pvid, available: true, performers};
    }
    return {pvid, available: false, reason: '数据库中没有该歌曲'};
};
// 只读采样播放列表上下文的独立记录与临时显示配置，不安装 Hook。
rpc.exports.pvstate = function () {
    const current = details();
    const controller = current.controller ? ptr(current.controller) : null;
    return {page: current.page, menu: current.menu, sceneKey: current.sceneKey,
        scene: current.scene, controller: current.controller, sourcePvid: current.sourcePvid,
        slot: read(controller.add(0x3C), 'int'), slotCount: read(controller.add(0x38), 'int'),
        scope: read(imageBase.add(0x12B5A74), 'int'),
        internal: current.internalState ? current.internalState.words[2] : null,
        sceneFields: current.sceneFields, category: read(controller.add(0x40), 'int'),
        highlight: current.module ? {index: current.module.index, id: current.module.id, count: current.module.count} : null,
        displaySlots: pvDisplaySlots(controller), overrideSlots: pvOverrideSlots(controller),
        transition: current.itemControlTransition, target: pvAddress(current.target), head: pvAddress(current.head)};
};
