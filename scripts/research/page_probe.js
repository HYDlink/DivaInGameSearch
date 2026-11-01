'use strict';
// 只读页面快照：不安装拦截器，不调用游戏原生函数，不写游戏内存。
const image = Process.getModuleByName('DivaMegaMix.exe');
const imageBase = image.base;
const sceneVtables = [0xCC4A80, 0xCC5170, 0xCC4BB0, 0xCC4678,
    0xCC48C8, 0xCC5318, 0xCC5020, 0xCC3AB0, 0xCC4560];

// 参数 address、kind 为读取地址与类型；不可读时返回空值。
function read(address, kind = 'pointer') {
    try {
        if (!address || address.isNull()) return null;
        return kind === 'int' ? address.readS32() : address.readPointer();
    } catch (_) { return null; }
}
// 参数 value 为可空指针，返回适合日志保存的地址。
function addressText(value) { return value ? value.toString() : null; }
// 参数 object、rva 为候选对象和已知虚表；只读验证身份。
function hasVtable(object, rva) {
    const actual = read(object);
    return actual !== null && actual.equals(imageBase.add(rva));
}
// 参数 node、head 为当前树节点与哨兵；验证节点属于有界场景树。
function treeContains(node, head) {
    const queue = [read(head.add(8))];
    const visited = new Set();
    while (queue.length && visited.size < 32) {
        const current = queue.pop();
        if (!current || current.isNull() || current.equals(head)) continue;
        const text = current.toString();
        if (visited.has(text)) continue;
        visited.add(text);
        if (current.equals(node)) return true;
        queue.push(read(current), read(current.add(0x10)));
    }
    return false;
}
// 每次按真实任务控制器重新取得管理器，拒绝驻留对象扫描的首命中结果。
function snapshot() {
    const menu = [0xCC61094, 0xCC61098, 0xCC610A0].map(rva => read(imageBase.add(rva), 'int'));
    const controller = read(imageBase.add(0xCC6F178));
    const manager = controller && !controller.isNull() ? read(controller.add(0x998)) : null;
    const managerValid = hasVtable(manager, 0xCC4E38);
    const head = managerValid ? read(manager.add(8)) : null;
    const node = managerValid ? read(manager.add(0x18)) : null;
    const target = managerValid ? read(manager.add(0x20)) : null;
    let sceneKey = null, scene = null, nodeValid = false;
    if (head && !head.isNull() && node && !node.isNull() && !node.equals(head)) {
        sceneKey = read(node.add(0x20), 'int');
        scene = read(node.add(0x28));
        nodeValid = sceneKey >= 0 && sceneKey < sceneVtables.length
            && hasVtable(scene, sceneVtables[sceneKey]) && treeContains(node, head);
    }
    const song = read(imageBase.add(0xCC5EF18));
    const member = read(imageBase.add(0x6EFE8C), 'int');
    const pvid = song && !song.isNull() && member >= 0x9C8 && member <= 0x100000
        ? read(song.add(member - 0x9C8), 'int') : null;
    const songState = song && !song.isNull() ? read(song.add(0x6C), 'int') : null;
    const taskFields = {};
    for (const offset of [0x18, 0x24, 0x28, 0x50, 0x54])
        taskFields['0x' + offset.toString(16)] = read(imageBase.add(0xCC6F100 + offset), 'int');
    let page = '未知';
    const names = ['载入', '自定义父页', '模型', '发型', '饰品', 'T恤', '按键音', '设置', '结束'];
    if (nodeValid) page = names[sceneKey];
    // 驻留的选曲对象不能覆盖仍活动的自定义场景；只在无活动自定义节点时判定选曲。
    if ((!controller || controller.isNull())
        && menu[0] === 5 && menu[1] === 6 && menu[2] === 1
        && songState === 6 && pvid > 0 && pvid < 10000000) page = '歌曲';
    if ((!controller || controller.isNull()) && (!song || song.isNull())
        && menu[0] === 42 && menu[1] === 43 && menu[2] === 1) page = '主页';
    const result = {timestamp: new Date().toISOString(), page, menu, taskFields,
        controller: addressText(controller), manager: addressText(manager), managerValid,
        node: addressText(node), target: addressText(target), head: addressText(head),
        transition: managerValid ? read(manager.add(0x28), 'int') : null,
        sceneKey, nodeValid, scene: addressText(scene), song: addressText(song), songState, pvid,
        parentCursor: nodeValid && sceneKey === 1 ? read(scene.add(0x28), 'int') : null};
    if (nodeValid && sceneKey === 2) {
        const begin = read(scene.add(0x70)), end = read(scene.add(0x78));
        const count = read(scene.add(0x11C), 'int'), index = read(scene.add(0x110), 'int');
        result.module = {begin: addressText(begin), end: addressText(end), count, index,
            category: controller ? read(controller.add(0x40), 'int') : null};
        if (begin && end && count > 0 && count <= 10000 && end.sub(begin).toInt32() === count * 8
            && index >= 0 && index < count) {
            const record = read(begin.add(index * 8));
            result.module.id = read(record, 'int');
        }
    }
    return result;
}
// 参数 table 为虚表，返回有界函数 RVA 清单，便于复用静态分析。
function tableEntries(table) {
    const entries = [];
    for (let slot = 0; slot < 7; slot++) {
        const entry = read(table.add(slot * 8));
        if (!entry || entry.compare(imageBase) < 0 || entry.compare(imageBase.add(image.size)) >= 0) break;
        const range = Process.findRangeByAddress(entry);
        if (!range || !range.protection.includes('x')) break;
        entries.push(entry.sub(imageBase).toString());
    }
    return entries;
}
// 参数 stateManager 为已知场景持有的内部状态机，读取有界树；不扫堆、不调用原生函数。
function stateTree(stateManager) {
    if (!stateManager || stateManager.isNull()) return null;
    const head = read(stateManager.add(8));
    if (!head || head.isNull()) return null;
    const queue = [read(head.add(8))], seen = new Set(), rows = [];
    while (queue.length && seen.size < 16) {
        const node = queue.pop();
        if (!node || node.isNull() || node.equals(head) || seen.has(node.toString())) continue;
        seen.add(node.toString());
        const value = read(node.add(0x28)), table = read(value);
        rows.push({key: read(node.add(0x20), 'int'), node: node.toString(), object: addressText(value),
            vtable: table ? table.sub(imageBase).toString() : null, entries: table ? tableEntries(table) : []});
        queue.push(read(node), read(node.add(0x10)));
    }
    return {manager: stateManager.toString(), vtable: addressText(read(stateManager)),
        entries: read(stateManager) ? tableEntries(read(stateManager)) : [],
        words: [0x10, 0x14, 0x18, 0x1C].map(offset => read(stateManager.add(offset), 'int')), rows};
}
// 输出已识别场景的状态机树与父页字段，供后续定向观测使用。
function details() {
    const result = snapshot();
    if (!result.nodeValid) return result;
    const scene = ptr(result.scene);
    if (result.sceneKey === 4) {
        // 饰品布局尚未认证：只读当前已校验场景的前0x120字节，不把值命名为已确认字段。
        result.itemCandidateWords = {};
        for (let offset = 0x14; offset < 0x120; offset += 4)
            result.itemCandidateWords['0x' + offset.toString(16)] = read(scene.add(offset), 'int');
    }
    result.sceneFields = {};
    for (const offset of [0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x60, 0x64])
        result.sceneFields['0x' + offset.toString(16)] = read(scene.add(offset), 'int');
    const offset = {1: 0x178, 2: 0x330, 3: 0x3A0, 4: 0x300}[result.sceneKey];
    if (offset) result.internalState = stateTree(read(scene.add(offset)));
    const table = read(scene);
    result.sceneEntries = table ? tableEntries(table) : [];
    const controller = ptr(result.controller);
    result.controllerFields = {};
    for (const offset of [0x3C, 0x40, 0x80, 0x320, 0x338])
        result.controllerFields['0x' + offset.toString(16)] = read(controller.add(offset), 'int');
    result.config = addressText(read(controller.add(0x18)));
    const record = read(scene.add(0x68));
    result.menuTargetRecord = addressText(record);
    if (result.module && result.module.begin && result.module.index >= 0)
        result.selectedRecord = addressText(read(ptr(result.module.begin).add(result.module.index * 8)));
    result.menuTargetId = record ? read(record, 'int') : null;
    const popup = read(controller.add(0x990));
    result.popup = popup ? {object: popup.toString(), vtable: addressText(read(popup)),
        entries: read(popup) ? tableEntries(read(popup)) : []} : null;
    if ([2, 4].includes(result.sceneKey)) {
        const callback = read(scene.add(result.sceneKey === 2 ? 0x308 : 0x2D8));
        result.actionCallback = callback && !callback.isNull() ? {object: callback.toString(),
            entries: read(callback) ? tableEntries(read(callback)) : [],
            boundValues: [8, 0x10].map(offset => addressText(read(callback.add(offset))))} : null;
    }
    return result;
}
// 只读已知静态自定义任务的虚表与已登记字段，不扫描进程。
function outerInfo() {
    const task = imageBase.add(0xCC6F100), table = read(task);
    return {snapshot: snapshot(), task: task.toString(), vtable: addressText(table),
        entries: table && !table.isNull() ? tableEntries(table) : [],
        menuEngine: Array.from({length: 13}, (_, index) => read(imageBase.add(0xCC61078 + index * 4), 'int')),
        modeTable: Array.from({length: 12}, (_, index) => {
            const row = imageBase.add(0xBF74C0 + index * 0x88);
            return {mode: read(row, 'int'), first: read(row.add(0x20), 'int'), next: read(row.add(0x24), 'int')};
        }),
        source68: read(task.add(0x68), 'int'), context70: addressText(read(task.add(0x70)))};
}
rpc.exports = { snapshot, details, outerinfo: outerInfo };
