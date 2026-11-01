"""对已确认拥有的316执行正常更换，再恢复原装备0，记录原生状态。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def wait_list(experiment, module_id):
    """参数 experiment、module_id 为当前会话及目标；要求列表恢复且装备关联索引等于目标索引。"""
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        snapshot = experiment.rpc('details')
        if snapshot['page'] == '模型' and snapshot['internalState']['words'][2] == 0 \
                and snapshot['sceneFields']['0x60'] == -1 and snapshot['module']['id'] == module_id \
                and snapshot['sceneFields']['0x2c'] == snapshot['module']['index']:
            return snapshot
        time.sleep(0.2)
    raise RuntimeError('装备结果没有由原生列表与关联索引确认')


def main():
    """仅允许默认0装备、316高亮的已认证场景；没有原装备证据就停止。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出参数保存更换、恢复的截图和状态事件，不覆盖先前实验。
    parser.add_argument('--output', required=True, type=Path, help='更换与恢复实验输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='已安装 AutoHotkey v2 路径')
    parser.add_argument('--callback', action='store_true', help='观察已绑定操作回调，并先记录候选预览命令')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '已拥有服装更换及恢复观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        initial = experiment.rpc('details')
        if options.callback and initial['page'] == '模型' and initial['module']['id'] == 0 \
                and initial['sceneFields']['0x2c'] == 0 and initial['sceneFields']['0x60'] == -1:
            experiment.rpc('armlocate', {'id': 316})
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                if experiment.sample()['module']['id'] == 316: break
                time.sleep(0.15)
            else: raise RuntimeError('回调观察前定位316失败')
            experiment.rpc('cancel')
            initial = experiment.rpc('details')
        if initial['page'] != '模型' or initial['module']['id'] != 316 \
                or initial['sceneFields']['0x2c'] != 0 or initial['sceneFields']['0x60'] != -1:
            raise RuntimeError('原装备0或候选316的起点未验证')
        experiment.record('原装备证据', 数据=initial)
        if options.callback:
            experiment.rpc('observeaction')
            after = key(experiment, 'Enter')
            if after['internalState']['words'][2] != 1 or after['menuTargetId'] != 316:
                raise RuntimeError('预览回调观察的菜单起点不符')
            key(experiment, 'Down')
            key(experiment, 'Enter')
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if experiment.rpc('details')['internalState']['words'][2] == 2: break
                time.sleep(0.15)
            else: raise RuntimeError('预览回调没有进入预览态')
            experiment.capture('00-回调预览已进入')
            key(experiment, 'Esc')
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if experiment.rpc('details')['sceneFields']['0x60'] == -1: break
                time.sleep(0.15)
            else: raise RuntimeError('预览退出未恢复列表')
        else: experiment.rpc('observemodule')
        after = key(experiment, 'Enter')
        if after['internalState']['words'][2] != 1 or after['menuTargetId'] != 316:
            raise RuntimeError('没有打开已确认的316操作菜单')
        experiment.capture('01-已拥有316更换菜单')
        key(experiment, 'Enter')
        after = wait_list(experiment, 316)
        experiment.record('更换316结果', 数据=after)
        time.sleep(0.7)
        experiment.capture('02-更换316后')
        experiment.record('更换原生时序', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        experiment.rpc('armlocate', {'id': 0})
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if experiment.sample().get('module', {}).get('id') == 0: break
            time.sleep(0.15)
        else: raise RuntimeError('恢复前定位原服装失败')
        experiment.rpc('cancel')
        experiment.rpc('observeaction' if options.callback else 'observemodule')
        after = key(experiment, 'Enter')
        if after['internalState']['words'][2] != 1 or after['menuTargetId'] != 0:
            raise RuntimeError('原服装0恢复菜单未验证')
        experiment.capture('03-原服装恢复菜单')
        key(experiment, 'Enter')
        after = wait_list(experiment, 0)
        experiment.record('恢复原装备0结果', 数据=after)
        time.sleep(0.7)
        experiment.capture('04-原装备恢复')
        experiment.record('恢复原生时序', 数据=experiment.rpc('events'))
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
