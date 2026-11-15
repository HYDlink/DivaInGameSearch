"""只读审计官方CPK中的发型／道具精灵映射，并解码少量实际缩略图供人工检查。"""
import argparse
import csv
import ctypes as ct
import hashlib
import json
from pathlib import Path
import re
import sys
import tomllib
from PIL import Image

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


class Bridge:
    """绑定现有NativeAOT资源桥接，仅在本工具进程读取资源，不接入游戏。"""

    def __init__(self, directory):
        """参数directory为已构建桥接和解码器所在目录；始终按绝对路径加载。"""
        self.decoder = ct.CDLL(str(directory / 'DivaTextureDecoder.dll'))
        self.dll = ct.CDLL(str(directory / 'MmlCpkBridge.dll'))
        declarations = {
            'DivaCpkOpen': ([ct.c_char_p], ct.c_void_p),
            'DivaCpkCount': ([ct.c_void_p], ct.c_int),
            'DivaCpkGetName': ([ct.c_void_p, ct.c_int, ct.c_void_p, ct.c_int], ct.c_int),
            'DivaCpkReadEntry': ([ct.c_void_p, ct.c_char_p, ct.POINTER(ct.c_void_p), ct.POINTER(ct.c_size_t)], ct.c_int),
            'DivaCpkClose': ([ct.c_void_p], None),
            'DivaCpkFree': ([ct.c_void_p], None),
            'DivaSpriteDatabaseList': ([ct.c_void_p, ct.c_size_t, ct.c_char_p, ct.c_void_p, ct.c_int], ct.c_int),
            'DivaFarcReadEntry': ([ct.c_void_p, ct.c_size_t, ct.c_char_p, ct.POINTER(ct.c_void_p), ct.POINTER(ct.c_size_t)], ct.c_int),
            'DivaSpriteDecode': ([ct.c_void_p, ct.c_size_t, ct.c_int, ct.POINTER(ct.c_void_p), ct.POINTER(ct.c_size_t), ct.POINTER(ct.c_int), ct.POINTER(ct.c_int)], ct.c_int),
        }
        for name, (arguments, result) in declarations.items():
            function = getattr(self.dll, name)
            function.argtypes, function.restype = arguments, result

    def take(self, function, *arguments):
        """参数function及arguments为桥接读取入口；有界复制并释放桥接分配的内存。"""
        pointer, size = ct.c_void_p(), ct.c_size_t()
        status = function(*arguments, ct.byref(pointer), ct.byref(size))
        try:
            if status or size.value > 64 * 1024 * 1024:
                raise RuntimeError(f'资源读取失败，返回码{status}，长度{size.value}')
            return ct.string_at(pointer, size.value)
        finally:
            if pointer.value:
                self.dll.DivaCpkFree(pointer)

    def names(self, handle):
        """参数handle为只读CPK句柄；限制目录大小后枚举精确路径。"""
        count = self.dll.DivaCpkCount(handle)
        if count < 0 or count > 300000:
            raise RuntimeError('CPK目录数量超限')
        for index in range(count):
            size = self.dll.DivaCpkGetName(handle, index, None, 0)
            if not 0 <= size <= 32768:
                raise RuntimeError('CPK条目名长度异常')
            buffer = ct.create_string_buffer(size + 1)
            if self.dll.DivaCpkGetName(handle, index, buffer, len(buffer)) != size:
                raise RuntimeError('CPK条目名读取失败')
            yield buffer.value.decode('utf-8')

    def sprites(self, data, pattern):
        """参数data/pattern为精灵数据库字节和名称子串；返回桥接解析的真实名称、集合文件与索引。"""
        function = self.dll.DivaSpriteDatabaseList
        size = function(data, len(data), pattern.encode(), None, 0)
        if size < 0 or size > 4 * 1024 * 1024:
            raise RuntimeError(f'精灵数据库查询失败：{size}')
        buffer = ct.create_string_buffer(size + 1)
        result = function(data, len(data), pattern.encode(), buffer, len(buffer))
        if result != size:
            raise RuntimeError('精灵列表读回长度不一致')
        return [line.split('\t') for line in buffer.value.decode('utf-8').splitlines()]

    def decode(self, data, index, output):
        """参数data/index为真实精灵集合及数据库索引；output为输出PNG文件，保存RGBA并返回像素哈希。"""
        pointer, size = ct.c_void_p(), ct.c_size_t()
        width, height = ct.c_int(), ct.c_int()
        result = self.dll.DivaSpriteDecode(data, len(data), index, ct.byref(pointer), ct.byref(size), ct.byref(width), ct.byref(height))
        try:
            if result or width.value <= 0 or height.value <= 0 or size.value != width.value * height.value * 4 or size.value > 64 * 1024 * 1024:
                raise RuntimeError(f'图片解码失败：{result}')
            pixels = ct.string_at(pointer, size.value)
            if output is not None:
                Image.frombytes('RGBA', (width.value, height.value), pixels).save(output)
            return {'宽': width.value, '高': height.value, '像素SHA256': hashlib.sha256(pixels).hexdigest()}
        finally:
            if pointer.value:
                self.dll.DivaCpkFree(pointer)


def mod_roots(game):
    """参数game为游戏目录；遵守当前DML优先级、enabled和include，仅返回目录内的有效根。"""
    configuration = tomllib.loads((game / 'config.toml').read_text(encoding='utf-8-sig'))
    if not configuration.get('enabled', True):
        return []
    mods = (game / configuration.get('mods', 'mods')).resolve()
    mods.relative_to(game.resolve())
    order = configuration.get('priority') or sorted(path.name for path in mods.iterdir() if path.is_dir())
    roots = []
    for directory in order:
        path = (mods / directory).resolve()
        path.relative_to(mods)
        if not (path / 'config.toml').is_file():
            continue
        current = tomllib.loads((path / 'config.toml').read_text(encoding='utf-8-sig'))
        if not current.get('enabled', True):
            continue
        for include in current.get('include', []):
            root = (path / include).resolve()
            root.relative_to(path)
            if root.is_dir() and root not in roots:
                roots.append(root)
    return roots


def main():
    """读取当前目录审计及官方精灵数据库，输出覆盖率；少量抽样不冒充全部图片解码。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 游戏目录仅用于只读访问根目录的官方CPK，不递归扫描其他Mod或导出全量资产。
    parser.add_argument('--game', type=Path, required=True, help='游戏根目录')
    # 桥接参数指定已有构建，不下载或编译依赖。
    parser.add_argument('--bridge', type=Path, required=True, help='桥接DLL目录')
    # 目录参数是CustomizationAudit输出的UTF-8表，提供全部目标稳定ID和领域。
    parser.add_argument('--catalog', type=Path, required=True, help='定制目录审计TSV')
    # 输出结果文件参数：新目录保存映射TSV、覆盖摘要JSON及抽样PNG，不覆盖旧证据。
    parser.add_argument('--output', type=Path, required=True, help='尚不存在的资源审计输出目录')
    # 全量验证参数只解码并记录摘要，不保存每张图片，避免重复导出整套资源。
    parser.add_argument('--verify-all', action='store_true', help='验证全部目录目标两种风格的实际解码，FT缺专用图时核对通用图回退')
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=False)
    bridge = Bridge(options.bridge.resolve())
    catalogs = list(csv.DictReader(options.catalog.open(encoding='utf-8-sig'), delimiter='\t'))
    targets = {int(row['编号']): row for row in catalogs if row['领域'] != '模型'}
    handles, files, rows = {}, {}, []
    try:
        roots = mod_roots(options.game.resolve())
        databases = []
        for database_name in ['mod_spr_db.bin', 'spr_db.bin']:
            for root in roots:
                path = root / 'rom/2d' / database_name
                if path.is_file():
                    databases.append((str(path), str(path), path.read_bytes()))
                    # 普通库同逻辑路径只接受最高优先来源；追加库保留全部候选及来源。
                    if database_name == 'spr_db.bin':
                        break
        for path in sorted(options.game.glob('*.cpk')):
            handle = bridge.dll.DivaCpkOpen(str(path.resolve()).encode('utf-8'))
            if not handle:
                raise RuntimeError(f'无法打开CPK：{path.name}')
            handles[path.name] = handle
            files[path.name] = list(bridge.names(handle))
        for cpk, names in files.items():
            for name in names:
                if not name.lower().endswith('/spr_db.bin'):
                    continue
                data = bridge.take(bridge.dll.DivaCpkReadEntry, handles[cpk], name.encode())
                databases.append((cpk, name, data))
        raw_rows = []
        for cpk, name, data in databases:
                for sprite, collection, index in bridge.sprites(data, 'CMNITM'):
                    raw_rows.append({'精灵': sprite, '集合': collection, '索引': int(index), '来源': cpk, '数据库路径': name})
                    match = re.fullmatch(r'SPR_CMNITM_THMB(\d+)_ITM_IMG(_FT)?', sprite)
                    if match:
                        item_id = int(match[1])
                        rows.append({'编号': item_id, '领域': targets.get(item_id, {}).get('领域', '不在有效目录'),
                                     '名称': targets.get(item_id, {}).get('名称', ''), '风格': 'FT' if match[2] else 'MEGA39',
                                     '精灵': sprite, '集合': collection, '索引': int(index), '数据库CPK': cpk, '数据库路径': name})
        (options.output / '定制精灵原始候选.json').write_text(json.dumps(raw_rows, ensure_ascii=False, indent=2), encoding='utf-8')
        with (options.output / '精灵映射.tsv').open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else ['编号'], delimiter='\t')
            writer.writeheader()
            writer.writerows(rows)
        samples = []
        # 每个领域／部位和风格各抽一个稳定样本；数据库命中全集与抽样解码分别报告。
        chosen = set()
        for row in rows:
            item = targets.get(row['编号'])
            if not item:
                continue
            category = (item['领域'], item['部位令牌'], row['风格'])
            if category in chosen:
                continue
            basename = Path(row['集合']).stem.lower()
            suffix = '/rom/2d/' + basename + '.farc'
            candidates = [(cpk, name) for cpk, names in files.items() for name in names
                          if ('/' + name.lower()).endswith(suffix)]
            local = next((root / 'rom/2d' / (basename + '.farc') for root in roots
                          if (root / 'rom/2d' / (basename + '.farc')).is_file()), None)
            if not candidates and local is None:
                continue
            if local:
                cpk, name = '启用Mod', str(local)
                archive = local.read_bytes()
            else:
                cpk, name = candidates[0]
                archive = bridge.take(bridge.dll.DivaCpkReadEntry, handles[cpk], name.encode())
            binary = bridge.take(bridge.dll.DivaFarcReadEntry, archive, len(archive), (basename + '.bin').encode())
            output = options.output / f"{item['领域']}-{item['部位令牌']}-{row['编号']}-{row['风格']}.png"
            sample = dict(row, 资源CPK=cpk, 资源路径=name, 图片=str(output.resolve()))
            sample.update(bridge.decode(binary, row['索引'], output))
            samples.append(sample)
            chosen.add(category)
        coverage = []
        for domain in ['发型', '自定义道具']:
            for style in ['MEGA39', 'FT']:
                wanted = {key for key, item in targets.items() if item['领域'] == domain}
                found = {row['编号'] for row in rows if row['风格'] == style}
                coverage.append({'领域': domain, '风格': style, '目标数量': len(wanted), '数据库命中': len(wanted & found), '未命中': sorted(wanted - found)})
        full = []
        if options.verify_all:
            lookup = {}
            for row in rows:
                lookup.setdefault((row['编号'], row['风格']), row)
            archive_paths = {}
            for cpk, names in files.items():
                for name in names:
                    if '/rom/2d/' in '/' + name.lower() and name.lower().endswith('.farc'):
                        archive_paths.setdefault(Path(name).stem.lower(), (cpk, name))

            def decode_candidate(candidate):
                """参数candidate为精灵数据库真实命中；只按启用资源优先级读取对应集合并解码，不修改来源文件。"""
                basename = Path(candidate['集合']).stem.lower()
                local = next((root / 'rom/2d' / (basename + '.farc') for root in roots
                              if (root / 'rom/2d' / (basename + '.farc')).is_file()), None)
                if local:
                    archive = local.read_bytes()
                    resource = str(local)
                else:
                    cpk, name = archive_paths[basename]
                    archive = bridge.take(bridge.dll.DivaCpkReadEntry, handles[cpk], name.encode())
                    resource = cpk + '/' + name
                binary = bridge.take(bridge.dll.DivaFarcReadEntry, archive, len(archive), (basename + '.bin').encode())
                return dict(bridge.decode(binary, candidate['索引'], None), 精灵=candidate['精灵'], 资源=resource)

            for number, (item_id, item) in enumerate(targets.items(), 1):
                for style in ['MEGA39', 'FT']:
                    row = lookup.get((item_id, style))
                    fallback = row is None and style == 'FT'
                    if fallback:
                        row = lookup.get((item_id, 'MEGA39'))
                    entry = {'编号': item_id, '领域': item['领域'], '部位': item['部位令牌'], '请求风格': style, '回退通用图': fallback}
                    try:
                        if row is None:
                            raise RuntimeError('精灵数据库没有目标或通用回退')
                        entry.update(decode_candidate(row))
                        entry['结果'] = '通过'
                    except Exception as error:
                        # FT专用条目损坏时可验证同ID通用精灵，但原始失败必须保留，不冒充FT原图成功。
                        common = lookup.get((item_id, 'MEGA39')) if style == 'FT' and not fallback else None
                        if common is not None:
                            try:
                                entry.update(decode_candidate(common))
                                entry.update({'结果': '通过', '回退通用图': True, 'FT原始失败': str(error)})
                            except Exception as fallback_error:
                                entry.update({'结果': '失败', '原因': str(error), '通用回退失败': str(fallback_error)})
                        else:
                            entry.update({'结果': '失败', '原因': str(error)})
                    full.append(entry)
                if number % 100 == 0:
                    print(f'全量图片验证进度：{number}/{len(targets)}项。', flush=True)
            (options.output / '全部图片解码.json').write_text(json.dumps(full, ensure_ascii=False, indent=2), encoding='utf-8')
        report = {'范围': '官方CPK及启用Mod的实际精灵数据库候选；精确来源及冲突仍须产品加载器验证，不代表游戏原生3D预览', '覆盖': coverage, '解码样本': samples,
                  '全量解码': {'执行': options.verify_all, '组合数': len(full), '通过': sum(row['结果'] == '通过' for row in full), '失败': sum(row['结果'] != '通过' for row in full)}}
        (options.output / '资源结论.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        for row in coverage:
            print(f"{row['领域']}／{row['风格']}：数据库命中{row['数据库命中']}/{row['目标数量']}，未命中{len(row['未命中'])}项", flush=True)
        print(f'抽样解码完成：{len(samples)}张。', flush=True)
        if full:
            print(f"全量图片解码：通过{report['全量解码']['通过']}，失败{report['全量解码']['失败']}。", flush=True)
        return 1 if any(row['结果'] != '通过' for row in full) else 0
    finally:
        for handle in handles.values():
            bridge.dll.DivaCpkClose(handle)


if __name__ == '__main__':
    raise SystemExit(main())
