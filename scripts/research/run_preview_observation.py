"""从已读图确认的两项候选菜单观察预览及退出，不购买或更换。"""
import argparse
from pathlib import Path
import subprocess
import time
from run_page_experiment import Experiment


def key(experiment, name):
    """参数 experiment、name 为当前实验及单键；只调用已安装 AHK，调用后立即取样。"""
    before = experiment.rpc('details')
    experiment.record('候选操作前', 按键=name, 数据=before)
    result = subprocess.run([str(experiment.options.ahk), '/ErrorStdOut=UTF-8',
                             str(Path(__file__).with_name('diva_key.ahk')), str(experiment.options.pid), name],
                            capture_output=True, text=True, encoding='utf-8', timeout=8)
    if result.returncode: raise RuntimeError('候选操作输入失败：' + result.stdout)
    time.sleep(0.5)
    after = experiment.rpc('details')
    experiment.record('候选操作后', 按键=name, 数据=after)
    return after


def main():
    """只接受已经确认ID316的两项菜单作为起点，逐步核验预览状态。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出目录保存预览前后与退出恢复的独立图片及字段日志。
    parser.add_argument('--output', required=True, type=Path, help='预览观察结果输出目录，必须不存在')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '两项菜单候选预览观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        initial = experiment.rpc('details')
        if initial['page'] != '模型' or initial['module']['id'] != 316 or initial['menuTargetId'] != 316 \
                or initial['internalState']['words'][2] != 1 or initial['sceneFields']['0x60'] != 156:
            raise RuntimeError('起点不是已确认的316候选菜单')
        experiment.record('菜单起点', 数据=initial)
        experiment.rpc('observemodule')
        after = key(experiment, 'Down')
        if after['internalState']['words'][2] != 1 or after['menuTargetId'] != 316:
            raise RuntimeError('菜单下移后上下文改变')
        experiment.capture('01-选择预览')
        key(experiment, 'Enter')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            preview = experiment.rpc('details')
            if preview['internalState']['words'][2] == 2: break
            time.sleep(0.15)
        else: raise RuntimeError('没有进入候选预览状态2')
        time.sleep(0.8)
        experiment.record('候选预览', 数据=experiment.rpc('details'))
        experiment.capture('02-候选316预览')
        key(experiment, 'Esc')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            after = experiment.rpc('details')
            if after['internalState']['words'][2] == 0 and after['sceneFields']['0x60'] == -1: break
            time.sleep(0.15)
        else: raise RuntimeError('退出预览没有恢复普通列表')
        time.sleep(0.6)
        experiment.record('退出预览恢复', 数据=experiment.rpc('details'))
        experiment.capture('03-退出预览恢复原装备')
        experiment.record('原生状态时序', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
    except Exception as error:
        failure = str(error)
        try: experiment.record('停止前事件', 数据=experiment.rpc('events'))
        except Exception: pass
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
