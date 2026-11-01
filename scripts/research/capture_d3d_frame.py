"""通过本Mod公开帧回调只读复制一帧真实D3D11后缓冲；结束卸载采集探针。"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def capture(pid, output):
    """参数pid为本轮游戏进程；output为项目artifacts下尚不存在的PNG输出文件。"""
    output = Path(output).resolve()
    output.relative_to(ROOT / 'artifacts')
    bitmap = output.with_suffix('.bmp')
    if output.exists() or bitmap.exists():
        raise ValueError('截图输出已存在，拒绝覆盖证据')
    output.parent.mkdir(parents=True, exist_ok=True)
    commands = '\n'.join([json.dumps({'id': 1, 'command': 'captureframe', 'parameters': {'output': str(bitmap), 'root': str(ROOT / 'artifacts')}}),
                          json.dumps({'command': 'close'})]) + '\n'
    result = subprocess.run(['node', str(ROOT / 'scripts/research/frida_page_worker.mjs'), str(pid),
                             str(ROOT / 'scripts/research/d3d_frame_probe.js')], input=commands,
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=15)
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    reply = next((row for row in rows if row.get('id') == 1), {})
    if result.returncode or 'error' in reply or not reply.get('snapshot') or not bitmap.is_file():
        raise RuntimeError('D3D后缓冲采集失败：' + json.dumps(rows, ensure_ascii=False) + result.stderr)
    with Image.open(bitmap) as image:
        image.convert('RGB').save(output)
    output.with_suffix('.json').write_text(json.dumps(reply['snapshot'], ensure_ascii=False, indent=2), encoding='utf-8')
    return 'D3D11真实交换链截图已保存，采集探针已卸载；仍须打开图片检查。'


def main():
    """解析当前PID及输出文件参数，不启动游戏或发送输入。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 当前游戏进程参数，每次重启须重新查询。
    parser.add_argument('--pid', required=True, type=int, help='当前游戏PID')
    # 输出结果文件参数：PNG及同名BMP、JSON均保存于本项目artifacts，不覆盖既有证据。
    parser.add_argument('--output', required=True, type=Path, help='新PNG输出文件')
    options = parser.parse_args()
    print(capture(options.pid, options.output), flush=True)


if __name__ == '__main__':
    main()
