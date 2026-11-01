"""稳定标题页的一次AHK自然输入与原生输入链对照，不强切菜单。"""
import argparse
from pathlib import Path
import subprocess
import time
from run_page_experiment import Experiment


def main():
    """参数为本轮PID、输出目录及AHK；只在标题页发送一次Enter并保存只读观察。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 当前进程参数不得复用旧PID。
    parser.add_argument('--pid', required=True, type=int, help='当前游戏PID')
    # 输出结果文件参数保存输入链事件及真实前后截图。
    parser.add_argument('--output', required=True, type=Path, help='独立证据目录')
    # 自然输入使用已有AHK单键脚本，不绕过游戏输入处理。
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('title_input_probe.js')
    options.timeout, options.flow = 35, '标题输入链只读观察及主页等待'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.record('观察启动', 数据=experiment.rpc('watchinput'))
        experiment.capture('01-标题输入前')
        result = subprocess.run([str(options.ahk), '/ErrorStdOut=UTF-8', str(Path(__file__).with_name('diva_key.ahk')),
                                 str(options.pid), 'Enter'], capture_output=True, text=True, encoding='utf-8', timeout=8)
        experiment.record('原有AHK单键输出', 返回码=result.returncode, 输出=result.stdout, 错误=result.stderr)
        if result.returncode:
            raise RuntimeError('原有AHK输入失败')
        time.sleep(2)
        experiment.record('原生输入链观察', 数据=experiment.rpc('inputevents'))
        experiment.capture('02-标题输入后')
        experiment.stage('03-原生主页就绪', '主页')
    except Exception as error:
        failure = str(error)
        print('输入观察停止：' + failure, flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
