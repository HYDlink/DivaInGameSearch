"""模型候选菜单的分阶段观察：仅打开已知目标，不自动购买。"""
import argparse
from pathlib import Path
import subprocess
import time
from run_page_experiment import Experiment


def main():
    """参数选择独立输出目录；第一轮只定位并打开菜单，供读图核对。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='本次游戏进程号')
    # 输出参数保存每阶段操作、原生字段和截图，必须使用不存在的目录。
    parser.add_argument('--output', required=True, type=Path, help='本次菜单观察输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 程序路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '候选菜单只读观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        if experiment.sample()['page'] == '歌曲': experiment.stage('01-父页', '自定义父页', 'R')
        experiment.wait('自定义父页')
        experiment.parent_cursor(0)
        experiment.rpc('armparent', {'cursor': 0, 'animate': True})
        experiment.stage('02-原生进入模型', '模型')
        experiment.record('进入事件', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        experiment.rpc('armlocate', {'id': 316})
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if experiment.sample().get('module', {}).get('id') == 316: break
            time.sleep(0.1)
        else: raise RuntimeError('原生定位316超时')
        experiment.stage('03-无上之选高亮', '模型')
        experiment.record('定位事件', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        before = experiment.rpc('details')
        if before['module']['id'] != 316 or before['sceneFields']['0x60'] != -1:
            raise RuntimeError('打开菜单前目标或弹层状态改变')
        result = subprocess.run([str(options.ahk), '/ErrorStdOut=UTF-8',
                                str(Path(__file__).with_name('diva_key.ahk')), str(options.pid), 'Enter'],
                               capture_output=True, text=True, encoding='utf-8', timeout=8)
        if result.returncode: raise RuntimeError('打开目标菜单输入失败')
        time.sleep(0.65)
        after = experiment.rpc('details')
        experiment.record('候选菜单', 数据=after)
        if after['page'] != '模型' or after['module']['id'] != 316 or after['sceneFields']['0x60'] != after['module']['index']:
            raise RuntimeError('候选菜单目标未被独立验证')
        experiment.capture('04-候选菜单待识别')
    except Exception as error:
        failure = str(error)
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
