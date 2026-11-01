'use strict';
// 只读当前已知入口的指令与自定义根，不安装 Hook、不调用原生函数、不写内存。
const targetModule = Process.getModuleByName('DivaMegaMix.exe');
const entryRvas = [0x2C4A40, 0x6EDC40, 0x66B2D0, 0x2C4BB0, 0x695250];
const entries = entryRvas.map(rva => {
    let address = targetModule.base.add(rva);
    const instructions = [];
    for (let index = 0; index < (rva === 0x695250 ? 180 : 12); index++) {
        const instruction = Instruction.parse(address);
        instructions.push({rva: address.sub(targetModule.base).toString(), text: instruction.toString()});
        address = instruction.next;
        if (instruction.mnemonic === 'ret') break;
    }
    return {rva, instructions};
});
send({说明: '已知入口只读核查', base: targetModule.base.toString(), entries,
    controller: targetModule.base.add(0xCC6F178).readPointer().toString(),
    menu: [0, 0x1C, 0x20, 0x28].map(offset => targetModule.base.add(0xCC61078 + offset).readS32())});
