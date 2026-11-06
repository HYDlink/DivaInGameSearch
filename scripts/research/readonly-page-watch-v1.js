'use strict';

// 零插桩页面识别：只扫描一次管理器虚表定位对象，之后每 200 毫秒只读“当前场景键”，
// 页面变化时输出。全程不安装 hook、不调用游戏函数、不写内存。
const mod = Process.getModuleByName('DivaMegaMix.exe');
const base = mod.base;
const MANAGER_VTABLE_RVA = 0xCC4E38;
const PAGE_NAMES = {
  0: 'Loading',
  1: '自定义父页 TopMenu',
  2: '模型子页 Module',
  3: '发型子页 Hairstyle',
  4: '自定义道具子页 Item',
  5: 'T恤子页 Tshirt',
  6: '按键音设定 Sound',
  7: '游戏与操作设置 Config',
  8: 'Finish',
};
const started = Date.now();

function safeRead(address, reader) {
  try {
    if (Process.findRangeByAddress(address) === null) return null;
    return reader(address);
  } catch (e) {
    return null;
  }
}
const readI32 = a => safeRead(a, x => x.readS32());
const readPtr = a => safeRead(a, x => x.readPointer());

// 参数 无；扫描管理器实例（只读扫描，不安装 hook）。
function findManager() {
  const pattern = base.add(MANAGER_VTABLE_RVA).toMatchPattern();
  for (const r of Process.enumerateRanges('rw-')) {
    if (r.size > 96 * 1024 * 1024) continue;
    try {
      for (const m of Memory.scanSync(r.base, r.size, pattern)) {
        if (m.address.and(7).isNull()) return m.address;
      }
    } catch (e) {
      // 段被释放属正常情况。
    }
  }
  return null;
}

const manager = findManager();
// 参数 无；读取当前场景键与页面名。
function pageNow() {
  if (!manager) return { '键': null, '页面': '未找到管理器' };
  const node = readPtr(manager.add(0x18));
  const key = node ? readI32(node.add(0x20)) : null;
  return { '键': key, '页面': PAGE_NAMES[key] || ('未知键 ' + key) };
}

let last = '';
send({ '类型': '就绪', '管理器': manager ? manager.toString() : null, '当前': pageNow() });

setInterval(() => {
  const now = pageNow();
  const key = JSON.stringify(now);
  if (key === last) return;
  last = key;
  send({ '类型': '页面变化', '经过毫秒': Date.now() - started, '当前': now });
}, 200);
