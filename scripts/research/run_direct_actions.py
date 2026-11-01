"""不改变原生高亮，脚本直接预览316、更换316并恢复原装备0。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def wait_action(experiment, mode, target):
    """参数 experiment、mode、target 为会话、动作和目标；高亮必须始终保持原服装0。"""
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        state = experiment.rpc('details')
        if state['page'] != '模型' or state['module']['id'] != 0 or state['module']['index'] != 0:
            raise RuntimeError('直接动作改变了原生列表高亮或离开模型页')
        internal = state['internalState']['words'][2]
        if mode == 'preview' and internal == 2 and state['controllerFields']['0x320'] == target:
            return state
        if mode == 'equip' and internal == 0 and state['sceneFields']['0x60'] == -1 \
                and state['sceneFields']['0x2c'] == (156 if target == 316 else 0) \
                and state['controllerFields']['0x320'] == target:
            return state
        time.sleep(0.15)
    raise RuntimeError(f'直接目标动作没有确认：{mode} / {target}')


def main():
    """从已确认恢复的默认装备0起步，全程不发送更换确认键，仅用 Esc 退出预览。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出目录保存直接目标动作的排队、实际调用、跨帧读回及截图证据。
    parser.add_argument('--output', required=True, type=Path, help='直接动作复现实验输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径，仅用于退出预览')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '不改变高亮的原生预览与更换'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        initial = experiment.rpc('details')
        if initial['page'] != '模型' or initial['module']['id'] != 0 or initial['sceneFields']['0x2c'] != 0 \
                or initial['sceneFields']['0x60'] != -1 or initial['controllerFields']['0x320'] != 0:
            raise RuntimeError('默认装备及列表高亮0起点未验证')
        experiment.record('原装备及高亮', 数据=initial)
        experiment.capture('01-高亮0原装备0')
        experiment.record('直接预览排队', 数据=experiment.rpc('armaction', {'id': 316, 'action': 'preview'}))
        state = wait_action(experiment, 'preview', 316)
        experiment.record('直接预览读回', 数据=state)
        time.sleep(0.8)
        experiment.capture('02-高亮0直接预览316')
        experiment.record('直接预览调用', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        key(experiment, 'Esc')
        wait_action(experiment, 'equip', 0)
        experiment.capture('03-退出预览恢复0')
        experiment.record('直接更换排队', 数据=experiment.rpc('armaction', {'id': 316, 'action': 'equip'}))
        state = wait_action(experiment, 'equip', 316)
        experiment.record('直接更换读回', 数据=state)
        time.sleep(0.8)
        experiment.capture('04-高亮0直接更换316')
        experiment.record('直接更换调用', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        experiment.record('恢复排队', 数据=experiment.rpc('armaction', {'id': 0, 'action': 'equip'}))
        state = wait_action(experiment, 'equip', 0)
        experiment.record('原装备恢复读回', 数据=state)
        time.sleep(0.8)
        experiment.capture('05-高亮与原装备均恢复0')
        experiment.record('恢复调用', 数据=experiment.rpc('events'))
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
