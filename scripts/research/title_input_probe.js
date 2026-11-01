// 有界观察已有输入获取器，不调用游戏函数、不修改输入状态；15秒自动卸载本Hook。
let inputListener = null, inputTimer = null;
let inputRows = [], inputCounts = {}, inputSeen = new Set();
// 停止本探针并返回有界事件，不撤销其他Mod的输入链。
function stopInputProbe() {
    if (inputListener) inputListener.detach();
    if (inputTimer) clearTimeout(inputTimer);
    inputListener = inputTimer = null;
    return {说明: '自然输入只读观察', counts: inputCounts, rows: inputRows, page: snapshot()};
}
// 仅接受真实标题页；只记录原生函数实际返回的指针与阻塞位，不伪造输入返回值。
function watchTitleInput() {
    const initial = snapshot();
    if (initial.menu[0] !== 3 || initial.menu[1] !== 4 || initial.menu[2] !== 1)
        throw new Error('观察起点不是真正标题页；2/3为启动鸣谢，应先跳过开场并读图确认');
    stopInputProbe();inputRows = [];inputCounts = {};inputSeen = new Set();
    inputListener = Interceptor.attach(imageBase.add(0x2AC970), {
        // 参数args是游戏实际请求的玩家索引；不修改参数。
        onEnter(args) { this.player = args[0].toInt32(); },
        // 参数result是原输入链真实返回；有界记录头部变化与已知阻塞字段。
        onLeave(result) {
            const label = String(this.player);
            inputCounts[label] = (inputCounts[label] || 0) + 1;
            if (result.isNull() || inputRows.length >= 24) return;
            const row = {player: this.player, pointer: result.toString(), blocked: result.add(0x1A28).readU8(),
                words: Array.from({length: 12}, (_, index) => result.add(index * 4).readU32())};
            const text = JSON.stringify(row);
            if (!inputSeen.has(text)) {inputSeen.add(text);inputRows.push(row);}
        }
    });
    inputTimer = setTimeout(stopInputProbe, 15000);
    return {说明: '标题输入观察已启动，15秒自动卸载'};
}
// 按已静态核实的子模式表读取标题/开场的三个回调，不执行回调。
function titleProfile() {
    const rows = [];
    for (let index = 0; index < 47; index++) {
        const row = imageBase.add(0xBF6EE0 + index * 0x20), key = row.readS32();
        if (key === 2 || key === 3)
            rows.push({key, entries: [8, 0x10, 0x18].map(offset => row.add(offset).readPointer().sub(imageBase).toString())});
    }
    const taskTable = imageBase.add(0x12B6C00).readPointer();
    const taskRange = Process.findRangeByAddress(taskTable);
    return {rows, page: snapshot(), startupFlags: [0x12B6CA8, 0x12B6CA9, 0x12B6CAA, 0x12B6CAB].map(rva => imageBase.add(rva).readU8()),
        taskTable: taskTable.toString(),
        taskEntries: taskRange ? Array.from({length: 8}, (_, index) => {
            const address = taskTable.add(index * 8).readPointer();
            return {rva: address.sub(imageBase).toString(), executable: Process.findRangeByAddress(address)?.protection.includes('x') || false};
        }) : []};
}
rpc.exports = {...rpc.exports, watchinput: watchTitleInput, inputevents: stopInputProbe, titleprofile: titleProfile};
