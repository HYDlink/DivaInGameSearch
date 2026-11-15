let playlistObserver = null;
let playlistEvents = [];
// 参数 settings.id 为本次受控实验的目标 PVID；仅在已确认的歌曲选择帧调用一次列表高亮入口。
rpc.exports.armlocate = function (settings) {
    const before = rpc.exports.observe();
    if (before.menu.join('/') !== '6/7/1' || before.songState !== 6 || before.pvid <= 0)
        throw new Error('当前不是已观察的播放列表歌曲选择状态');
    let index = before.list.ids.indexOf(settings.id);
    if (index < 0 && !settings.all) throw new Error('目标歌曲不在当前列表中');
    if (index >= 0 && before.list.ids.lastIndexOf(settings.id) !== index) throw new Error('目标歌曲不唯一');
    if (playlistObserver) playlistObserver.detach();
    playlistEvents = [];
    const object = ptr(before.song);
    playlistObserver = Interceptor.attach(imageBase.add(0x6edc40), {
        // 参数 args 为选曲任务；只在相同任务的原生帧执行一次。
        onEnter(args) {
            if (!args[0].equals(object)) return;
            const now = snapshot();
            if (now.menu.join('/') !== '6/7/1' || now.pvid !== before.pvid || now.song !== before.song) return;
            playlistObserver.detach(); playlistObserver = null;
            // 受控实验只更改选曲任务当前 PVID，并调用已有选曲刷新及高亮入口。
            const refresh = new NativeFunction(imageBase.add(0x6f2f40), 'void', ['pointer', 'int']);
            if (index < 0 && settings.all) {
                const mode = read(object.add(0x262c0), 'int');
                if (mode < 0 || mode > 3) { playlistEvents.push({说明:'排序模式无效'}); return; }
                const folder = object.add(0x262ac + mode * 4);
                folder.writeS32([10, 19, 6, 5][mode]);
                object.add(0x26240).writePointer(folder);
                object.add(0x262bc).writeS32(1);
                refresh(object, 0);
                index = rpc.exports.observe().list.ids.indexOf(settings.id);
                if (index < 0) { playlistEvents.push({说明:'全曲分组中没有目标'}); return; }
            }
            object.add(0x262a4).writeS32(settings.id);
            refresh(object, 0);
            const call = new NativeFunction(imageBase.add(0x217500), 'void', ['pointer', 'int', 'int']);
            call(object.add(0x25930), index, 0);
            playlistEvents.push({说明: '已调用原生列表高亮，等待独立读回', id: settings.id, index});
        }
    });
    return {说明: '等待选曲任务帧', id: settings.id, index};
};
// 观察游戏自然操作时的列表高亮调用；不调用原生函数或写内存。
rpc.exports.watchinput = function () {
    playlistEvents = [];
    if (playlistObserver) playlistObserver.detach();
    playlistObserver = Interceptor.attach(imageBase.add(0x217500), {
        // 参数 args 包含列表对象与游戏传入的目标索引。
        onEnter(args) {
            const song = read(imageBase.add(0xCC5EF18));
            if (song && !song.isNull() && args[0].equals(song.add(0x25930)))
                playlistEvents.push({ index: args[1].toInt32(), third: args[2].toInt32(), before: snapshot() });
        }
    });
    return {说明: '已开始观察原生高亮调用'};
};
// 返回观察事件并撤销本次观察器。
rpc.exports.inputevents = function () {
    if (playlistObserver) playlistObserver.detach();
    playlistObserver = null;
    return {事件: playlistEvents, 当前: snapshot()};
};
// 只读播放列表中的歌曲任务与已知生效列表，参数由既有工作进程提供。
rpc.exports.observe = function () {
    const result = snapshot();
    const song = read(imageBase.add(0xCC5EF18));
    if (!song || song.isNull()) return result;
    result.words = {};
    for (let offset = 0x25930; offset < 0x25a20; offset += 4)
        result.words['0x' + offset.toString(16)] = read(song.add(offset), 'int');
    for (const offset of [0x25920, 0x25928, 0x25930, 0x25934, 0x25938, 0x2593c, 0x25940, 0x25944, 0x25948, 0x2594c, 0x262a4, 0x262c0, 0x262c8, 0x262cc])
        result.words['0x' + offset.toString(16)] = read(song.add(offset), 'int');
    const vector = read(song.add(0x25a20));
    const begin = vector && !vector.isNull() ? read(vector) : null;
    const end = vector && !vector.isNull() ? read(vector.add(8)) : null;
    result.list = { vector: addressText(vector), begin: addressText(begin), end: addressText(end), ids: [] };
    if (!begin || !end || end.compare(begin) < 0) return result;
    const bytes = end.sub(begin).toInt32();
    if (bytes < 0 || bytes % 8 || bytes > 80000) return result;
    for (let i = 0; i < bytes / 8; i++) {
        const record = read(begin.add(i * 8));
        const data = record && !record.isNull() ? read(record) : null;
        const entry = data && !data.isNull() ? read(data) : null;
        result.list.ids.push(entry && !entry.isNull() ? read(entry, 'int') : null);
    }
    return result;
};
