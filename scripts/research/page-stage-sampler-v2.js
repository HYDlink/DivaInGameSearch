'use strict';

// 页面阶段采样 v2：重启后不依赖任何堆地址，只读已有来源的判据字段。
// 记录菜单三态、选曲对象守卫、自定义控制器及其虚表、任务字段，用于区分七阶段。
// 只读；不安装 hook、不写内存、不调用游戏函数。
const mod = Process.getModuleByName('DivaMegaMix.exe');
const base = mod.base;
const modEnd = base.add(mod.size);
const started = Date.now();

// 参数 address 为候选地址、reader 为读取函数；未映射返回 null。
function safeRead(address, reader) {
  try {
    if (Process.findRangeByAddress(address) === null) return null;
    return reader(address);
  } catch (e) {
    return null;
  }
}
const readI32 = address => safeRead(address, a => a.readS32());
const readPtr = address => safeRead(address, a => a.readPointer());

// 参数 p 为指针；模块内返回 RVA，否则返回 null，用于识别对象类型而不依赖具体堆地址。
function moduleOffset(p) {
  if (!p) return null;
  if (p.compare(base) < 0 || p.compare(modEnd) >= 0) return null;
  return '0x' + p.sub(base).toString(16);
}

// 采样一次完整判据；返回可比较的指纹对象。
function sample() {
  const songObject = readPtr(base.add(0xCC5EF18));
  const member = readI32(base.add(0x6EFE8C));
  let visiblePvid = null;
  let songState = null;
  if (songObject) {
    songState = readI32(songObject.add(0x6C));
    if (member && member >= 0x9C8 && member <= 0x100000) visiblePvid = readI32(songObject.add(member - 0x9C8));
  }
  const controller = readPtr(base.add(0xCC6F100).add(0x78));
  const controllerVtable = controller ? readPtr(controller) : null;
  return {
    '菜单': {
      'current': readI32(base.add(0xCC61094)),
      'next': readI32(base.add(0xCC61098)),
      'command': readI32(base.add(0xCC610A0)),
    },
    '选曲对象存在': songObject !== null,
    '选曲状态+0x6C': songState,
    '可见PVID': visiblePvid,
    '自定义控制器': controller ? 'present' : null,
    '控制器虚表RVA': moduleOffset(controllerVtable),
    '任务字段': {
      '0x18': readI32(base.add(0xCC6F100).add(0x18)),
      '0x24': readI32(base.add(0xCC6F100).add(0x24)),
      '0x28': readI32(base.add(0xCC6F100).add(0x28)),
      '0x50': readI32(base.add(0xCC6F100).add(0x50)),
      '0x54': readI32(base.add(0xCC6F100).add(0x54)),
    },
  };
}

let timer = null;
let stopped = false;
let samples = 0;
let pendingKey = '';
let pendingCount = 0;
let emittedKey = '';
let lastHeartbeat = started;
let latest = null;

// 参数 type/data 为中文事件类型与数据；带相对毫秒，便于和玩家步骤对齐。
function emit(type, data) {
  send({ '类型': type, '经过毫秒': Date.now() - started, '采样次数': samples, '数据': data });
}

function stop(reason) {
  if (stopped) return;
  stopped = true;
  if (timer !== null) clearInterval(timer);
  emit('采样结束', { '原因': reason, '最后指纹': latest });
}

function tick() {
  if (stopped) return;
  samples++;
  try {
    latest = sample();
  } catch (e) {
    stop('采样失败：' + String(e));
    return;
  }
  const key = JSON.stringify(latest);
  if (key === pendingKey) pendingCount++;
  else { pendingKey = key; pendingCount = 1; }
  // 连续三次一致才记录，过滤过渡动画的中间帧。
  if (pendingCount === 3 && key !== emittedKey) {
    emittedKey = key;
    emit('稳定指纹', latest);
  }
  const now = Date.now();
  if (now - lastHeartbeat >= 15000) {
    lastHeartbeat = now;
    emit('心跳', { '控制器': latest['自定义控制器'], '菜单': latest['菜单'] });
  }
  if (now - started >= 55000) stop('本段时限结束');
}

try {
  latest = sample();
  emit('采样就绪', {
    '主模块': base.toString(),
    '初始指纹': latest,
    '限制': '只记录已有来源的判据；未登记的子页面对象仍属未知',
  });
  timer = setInterval(tick, 250);
} catch (e) {
  send({ '类型': '采样启动失败', '原因': String(e) });
}
