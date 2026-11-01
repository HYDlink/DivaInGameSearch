"""已读图确认后发送一次AHK界面操作，保留操作前后真实游戏截图与中文输出。"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import sys
from capture_d3d_frame import capture as capture_frame

ROOT = Path(__file__).resolve().parents[2]
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def capture(pid, path):
    """参数pid/path为当前游戏和输出PNG；只读D3D后缓冲，失败不继续输入。"""
    return capture_frame(pid, path)


def main():
    """解析目标、单步操作与输出目录；本脚本不将按键成功冒充业务完成。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 当前PID每轮重新查询。
    parser.add_argument('--pid', required=True, type=int, help='当前游戏PID')
    # AHK程序使用已安装版本，不自动下载依赖。
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2路径')
    # 操作参数只允许单步输入或正常关闭窗口。
    parser.add_argument('--operation', required=True, choices=['shortcut', 'text', 'click', 'key', 'close'], help='单步操作')
    # 文本/按键参数仅由已核对的当前页面决定。
    parser.add_argument('--value', default='', help='查询文本或白名单按键')
    # 客户区坐标参数只能来自已查看的当前截图。
    parser.add_argument('--x', type=int, help='点击客户区X')
    parser.add_argument('--y', type=int, help='点击客户区Y')
    # 点击次数限制为单击或双击。
    parser.add_argument('--count', type=int, choices=[1, 2], default=1, help='点击次数')
    # 等待参数只用于外部自测，产品帧回调不使用Sleep。
    parser.add_argument('--delay', type=float, default=0.7, help='输入后截图等待秒数，0至10')
    # 输出结果文件参数：独立目录保存before/after PNG与result.json，不覆盖既有证据。
    parser.add_argument('--output', required=True, type=Path, help='本项目内尚不存在的证据目录')
    options = parser.parse_args()
    output = options.output.resolve()
    output.relative_to(ROOT)
    output.mkdir(parents=True, exist_ok=False)
    record = {'目标PID': options.pid, '操作': options.operation, '参数': options.value, '结果': '未执行'}
    try:
        record['操作前截图'] = capture(options.pid, output / 'before.png')
        command = [str(options.ahk), '/ErrorStdOut=UTF-8', str(Path(__file__).with_name('game_ui.ahk')),
                   str(options.pid), options.operation]
        if options.operation in ('text', 'key'):
            command.append(options.value)
        elif options.operation == 'click':
            if options.x is None or options.y is None:
                raise ValueError('点击必须提供客户区坐标')
            command.extend([str(options.x), str(options.y), str(options.count)])
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        record.update({'AHK返回码': result.returncode, '标准输出': result.stdout, '错误输出': result.stderr})
        if result.returncode:
            raise RuntimeError('AHK单步操作失败：' + result.stdout + result.stderr)
        time.sleep(max(0, min(10, options.delay)))
        if options.operation != 'close':
            record['操作后截图'] = capture(options.pid, output / 'after.png')
        record['结果'] = '输入完成，业务行为待独立读回及读图核对'
        print('已保存单步输入与实际游戏前后截图，尚未宣称业务通过。', flush=True)
        return 0
    except Exception as error:
        record.update({'结果': '停止', '原因': str(error)})
        print('单步自测停止：' + str(error), flush=True)
        return 1
    finally:
        (output / 'result.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    raise SystemExit(main())
