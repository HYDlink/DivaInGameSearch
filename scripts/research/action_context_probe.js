// 与现有页面探针共用会话；默认仅读取已知对象的有界字段。
let ownershipListener = null;
let ownershipTimer = null;
let ownershipEvents = [];

// 读取当前场景关联配置及六组模型候选字段；未验证字段仍标为原始数据。
function actionContext() {
    const result = details();
    if (!result.nodeValid) return result;
    const controller = ptr(result.controller);
    result.rawControllerWords = {};
    for (let offset = 0; offset < 0x320; offset += 4)
        result.rawControllerWords['0x' + offset.toString(16)] = read(controller.add(offset), 'int');
    for (let offset = 0x860; offset < 0x998; offset += 4)
        result.rawControllerWords['0x' + offset.toString(16)] = read(controller.add(offset), 'int');
    result.rawSceneWords = {};
    for (let offset = 0; offset < 0x70; offset += 4)
        result.rawSceneWords['0x' + offset.toString(16)] = read(ptr(result.scene).add(offset), 'int');
    result.rawConfigurationWords = {};
    for (let offset = 0; offset < 0x100; offset += 4)
        result.rawConfigurationWords['0x' + offset.toString(16)] = read(ptr(result.config).add(offset), 'int');
    result.modelRows = Array.from({length: 6}, (_, index) => ({index,
        model: read(controller.add(0x320 + index * 0xE0), 'int'),
        itemWords: [4, 8, 12, 16, 20].map(offset => read(controller.add(0x320 + index * 0xE0 + offset), 'int'))}));
    result.rawTaskSource = read(imageBase.add(0xCC6F168), 'int');
    // 已由681A90静态链定位的原始范围选择值；映射须再用F4截图核对。
    result.rawScopeSelector = read(imageBase.add(0x12B5A74), 'int');
    if (result.module?.begin) {
        result.modelRecords = Array.from({length: Math.min(result.module.count, 8)}, (_, index) => {
            const record = read(ptr(result.module.begin).add(index * 8));
            return {index, id: read(record, 'int'), character: read(record.add(8), 'int')};
        });
    }
    return result;
}
// 清理本探针自己的单Hook和计时器，保留游戏及其他Mod调用链。
function stopOwnershipObservation() {
    if (ownershipListener) ownershipListener.detach();
    if (ownershipTimer) clearTimeout(ownershipTimer);
    ownershipListener = ownershipTimer = null;
    return ownershipEvents;
}
// 自然菜单期间只观察拥有判定的输入/返回，不调用判定器，也不更改解锁数据。
function observeOwnership() {
    stopOwnershipObservation();ownershipEvents = [];
    const initial = details();
    if (initial.page !== '模型' || initial.internalState.words[2] !== 0 || initial.sceneFields['0x60'] !== -1)
        throw new Error('拥有判定观察需要空闲模型列表');
    ownershipListener = Interceptor.attach(imageBase.add(0x1DA550), {
        // 参数 values 是自然菜单调用的记录；只接收已核实菜单代码的调用者。
        onEnter(values) {
            this.match = this.returnAddress.compare(imageBase.add(0x695010)) >= 0
                && this.returnAddress.compare(imageBase.add(0x695250)) < 0;
            if (!this.match) return;
            this.record = values[0];
            this.beforeByte = values[0].readU8();
        },
        // 参数 value 为原生拥有判定返回值，关联当前菜单稳定目标与线程。
        onLeave(value) {
            if (!this.match || ownershipEvents.length >= 16) return;
            ownershipEvents.push({说明: '自然菜单拥有判定', record: this.record.toString(), firstByte: this.beforeByte,
                result: value.toInt32() & 0xFF, thread: Process.getCurrentThreadId(), context: actionContext()});
        }
    });
    ownershipTimer = setTimeout(stopOwnershipObservation, 15000);
    return {说明: '单Hook观察拥有判定，15秒自动清理'};
}
rpc.exports = {...rpc.exports, actioncontext: actionContext, observeownership: observeOwnership, ownershipresult: stopOwnershipObservation};
