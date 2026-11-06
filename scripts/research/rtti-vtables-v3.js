'use strict';

// 页面类虚表反解 v3：先由类型名找到 COL，再由“vtable-8 保存 COL 指针”反解虚表地址。
// 用 CustomizeSelectorSceneModule 自检：结果必须是已确认的 0xCC4BB0。只读；不写内存、不安装 hook。
const mod = Process.getModuleByName('DivaMegaMix.exe');
const base = mod.base;

// 参数 text 为 ASCII 字符串；返回 Frida 扫描模式。
function asPattern(text) {
  const bytes = [];
  for (let i = 0; i < text.length; i++) bytes.push(text.charCodeAt(i));
  return bytes.map(b => b.toString(16).padStart(2, '0')).join(' ');
}

// 参数 value 为 32 位整数；返回小端 4 字节模式。
function u32Pattern(value) {
  return [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff]
    .map(b => b.toString(16).padStart(2, '0')).join(' ');
}

// 参数 value 为 NativePointer；返回其 64 位小端字节模式。
function u64Pattern(value) {
  // 直接使用 NativePointer 自带模式，避免误读目标地址处的内容。
  return value.toMatchPattern();
}

// 参数 address 为 NativePointer；读取以 0 结尾的 ASCII 字符串。
function readCString(address, limit) {
  const bytes = [];
  for (let i = 0; i < limit; i++) {
    let b;
    try { b = address.add(i).readU8(); } catch (e) { break; }
    if (b === 0 || b < 0x20 || b > 0x7e) break;
    bytes.push(b);
  }
  return bytes.length ? String.fromCharCode.apply(null, bytes) : null;
}

// 在映像范围内扫描模式，返回命中地址数组。
function scanImage(patternText, fromRva, toRva, limit) {
  const hits = [];
  const step = 0x100000;
  for (let rva = fromRva; rva < toRva && hits.length < limit; rva += step) {
    const size = Math.min(step, toRva - rva);
    try {
      for (const m of Memory.scanSync(base.add(rva), size, patternText)) {
        hits.push(m.address);
        if (hits.length >= limit) break;
      }
    } catch (e) {
      // 未映射段跳过。
    }
  }
  return hits;
}

// 第一步：扫类型名，得到每个类的类型描述符 RVA。
const nameHits = scanImage(asPattern('.?AVCustomizeSelectorScene'), 0xE00000, 0x1400000, 60);
const descriptors = [];
for (const hit of nameHits) {
  const name = readCString(hit, 200);
  if (!name) continue;
  const descriptorRva = (hit.sub(0x10).sub(base).toInt32()) >>> 0;
  descriptors.push({ name, descriptorRva });
}

// 第二步：描述符 RVA 出现在 COL+0x0C，据此得到 COL 地址。
const results = [];
for (const d of descriptors) {
  const colFieldHits = scanImage(u32Pattern(d.descriptorRva), 0x800000, 0x1000000, 4);
  const colAddresses = colFieldHits.map(h => h.sub(0x0C));
  // 第三步：扫描“vtable-8 处保存 COL 指针”的位置，虚表 = 该位置 + 8。
  const vtables = [];
  for (const col of colAddresses) {
    const colPointerHits = scanImage(u64Pattern(col), 0x800000, 0x1000000, 4);
    for (const hit of colPointerHits) {
      const vtable = hit.add(8);
      let firstEntry = null;
      try { firstEntry = vtable.readPointer(); } catch (e) { firstEntry = null; }
      const firstRva = firstEntry && firstEntry.compare(base) > 0 ? firstEntry.sub(base).toInt32() >>> 0 : null;
      if (firstRva === null || firstRva > 0x2000000) continue;
      vtables.push({ '虚表RVA': '0x' + vtable.sub(base).toString(16), '首项RVA': '0x' + firstRva.toString(16) });
    }
  }
  results.push({ '类名': d.name, '虚表': vtables });
}

send({ '类数': results.length, '类表': results });
