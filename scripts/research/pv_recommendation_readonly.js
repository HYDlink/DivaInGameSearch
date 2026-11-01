'use strict';

// 参数 address 为读取地址；返回可读页，失败保留明确原因，不申请或修改页面权限。
function readable(address) {
    const range = Process.findRangeByAddress(address);
    if (!range || !range.protection.includes('r')) throw new Error('目标地址不在可读页');
    return range;
}

// 参数 address 为MSVC字符串对象；只读取有界UTF-8文本，不执行游戏获取器。
function readString(address) {
    readable(address);
    const length = address.add(16).readU64().toNumber();
    const capacity = address.add(24).readU64().toNumber();
    if (length < 0 || length > 384 || capacity < length || capacity > 1048576) return null;
    const data = capacity < 16 ? address : address.readPointer();
    readable(data);
    return length === 0 ? '' : data.readUtf8String(length);
}

// 参数 base/rva/count 为模块基址、参考入口及最多指令数；仅解码，不调用或安装Hook。
function decode(base, rva, count) {
    const instructions = [];
    let address = base.add(rva);
    for (let i = 0; i < count; i++) {
        try {
            readable(address);
            const instruction = Instruction.parse(address);
            instructions.push({rva: address.sub(base).toString(), text: instruction.toString()});
            address = instruction.next;
            if (instruction.mnemonic === 'ret' || instruction.mnemonic === 'jmp') break;
        } catch (error) { instructions.push({error: String(error)}); break; }
    }
    return instructions;
}

// 参数 entry 为当前PV对象；按源码及定向静态分析核对的向量读取演出槽位，不修改推荐或装备。
function readPerformers(entry) {
    const begin = entry.add(0x98).readPointer();
    const end = entry.add(0xA0).readPointer();
    const bytes = Number(end.sub(begin).toString());
    // 商业PC的访问器0x4BEEE0使用0x44步长，不能照搬ReDIVA参考结构的0x40。
    const stride = 0x44;
    if (bytes < 0 || bytes % stride !== 0 || bytes / stride > 6) return {available: false, reason: '演出槽位向量边界不符'};
    const performers = [];
    for (let slot = 0; slot < bytes / stride; slot++) {
        const performer = begin.add(slot * stride);
        const costumes = [];
        for (let difficulty = 0; difficulty < 5; difficulty++) costumes.push(performer.add(0xC + difficulty * 4).readS32());
        performers.push({slot, type: performer.readS32(), character: performer.add(4).readS32(), costume: performer.add(8).readS32(), pvCostumesZeroBased: costumes, raw20: performer.add(0x20).readU32(), raw2C: performer.add(0x2C).readS32()});
    }
    return {available: true, count: performers.length, performers};
}

// 参数 base 为本次模块基址；按独立静态分析的PVDB有序指针表读取少量对象，不依赖当前选曲页。
function sampleEntries(base) {
    // 先核对本次实际获取器的RIP相对LEA，避免在不匹配构建上盲读固定对象。
    const getter = base.add(0x4BBA20);
    if (getter.readU8() !== 0x48 || getter.add(1).readU8() !== 0x8D || getter.add(2).readU8() !== 0x05) return {available: false, reason: 'PVDB获取器签名不符'};
    const observedDatabase = getter.add(7).add(getter.add(3).readS32());
    const database = base.add(0x1753780);
    if (!observedDatabase.equals(database)) return {available: false, reason: 'PVDB实际对象来源与已核查布局不符'};
    const begin = database.add(0x98).readPointer();
    const end = database.add(0xA0).readPointer();
    const bytes = Number(end.sub(begin).toString());
    if (bytes < 0 || bytes % 8 !== 0 || bytes / 8 > 100000) return {available: false, reason: 'PVDB指针表边界不符'};
    const count = bytes / 8;
    if (count === 0) return {available: false, reason: 'PVDB尚未加载，不把未就绪记为没有推荐'};
    const entries = [];
    const wanted = new Set([1,268,637,638,800]);
    for (let index = 0; index < count && index < 4096; index++) {
        const entry = begin.add(index * 8).readPointer();
        const id = entry.readS32();
        if (!wanted.has(id)) continue;
        entries.push({id, entry: entry.toString(), name: readString(entry.add(8)), performers: readPerformers(entry)});
    }
    return {available: true, count, entries};
}

// 仅检查明确游戏主模块并发送一次结果；不改变页面、服装、装备或存档，不注册观察点。
function main() {
    const module = Process.mainModule;
    if (module.name.toLowerCase() !== 'divamegamix.exe') throw new Error('目标不是已指定的DIVA游戏');
    const report = {purpose: '歌曲推荐服装只读入口研究', pid: Process.id, module: module.name, base: module.base.toString(), timestamp: new Date().toISOString(), effects: '短时附加并加载只读脚本；无Hook、无游戏函数调用、无业务字段写入'};
    report.lookup = decode(module.base, 0x4BC500, 48);
    report.databaseGetter = decode(module.base, 0x4BBA20, 8);
    try { report.samples = sampleEntries(module.base); }
    catch (error) { report.samples = {available: false, reason: String(error)}; }
    send(report);
}

try { main(); } catch (error) { send({error: String(error)}); }
