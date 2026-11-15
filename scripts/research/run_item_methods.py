"""按明确计划用AHK及显式授权的原生实验验证发型和道具；每步断言并保存截图与有限字段。"""
import argparse
import json
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def main():
    """读取带来源断言的操作计划；任何步骤失败立即停止并分离探针，不自动恢复装备。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 每轮重新查询的游戏进程，禁止沿用历史进程号。
    parser.add_argument('--pid', type=int, required=True, help='当前游戏进程号')
    # 输出结果文件参数：保存本轮截图、字段时序、脚本哈希及清理状态，不覆盖既有目录。
    parser.add_argument('--output', type=Path, required=True, help='尚不存在的证据输出目录')
    # 使用用户指定的现有 AutoHotkey，不安装其他输入后端。
    parser.add_argument('--ahk', type=Path, required=True, help='AutoHotkey v2路径')
    # 计划列明每个步骤的来源页、单个按键、目标页和可选字段断言，禁止盲发连续确认。
    parser.add_argument('--plan', type=Path, required=True, help='UTF-8实验计划JSON')
    options = parser.parse_args()
    plan = json.loads(options.plan.read_text(encoding='utf-8-sig'))
    options.agent = Path(__file__).with_name('item_methods_probe.js')
    options.timeout, options.flow = 15, plan['说明']
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.record('观察器初始化', 数据=experiment.rpc('observe', {'rvas': plan.get('入口', [])}))
        for number, step in enumerate(plan['步骤'], 1):
            before = experiment.wait(step['来源'])
            for field, expected in step.get('断言', {}).items():
                if before.get(field) != expected:
                    raise RuntimeError(f'步骤{number}来源字段不符：{field}')
            if step.get('按键'):
                key(experiment, step['按键'])
            if '原生定位ID' in step:
                experiment.record('原生定位实验提交', 数据=experiment.rpc('armlocate', {'id': step['原生定位ID']}))
                deadline = time.monotonic() + options.timeout
                while time.monotonic() < deadline:
                    current = experiment.rpc('details')
                    if current.get('page') != step['目标']:
                        raise RuntimeError(f'步骤{number}原生定位期间离页')
                    if current.get('itemList', {}).get('selected', {}).get('id') == step['原生定位ID']:
                        time.sleep(0.2)
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f'步骤{number}原生定位稳定ID读回超时')
            if '原生动作' in step:
                previous_events = len(experiment.rpc('events'))
                experiment.record('原生动作实验提交', 数据=experiment.rpc('armaction', {'id': step['目标ID'], 'action': step['原生动作']}))
                deadline = time.monotonic() + options.timeout
                while time.monotonic() < deadline:
                    events = experiment.rpc('events')[previous_events:]
                    if any(event.get('action') == step['原生动作'] and event.get('accepted') for event in events):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f'步骤{number}原生动作未被状态机受理')
            experiment.wait(step['目标'])
            if '内部状态' in step:
                deadline = time.monotonic() + options.timeout
                while time.monotonic() < deadline:
                    current = experiment.rpc('details')
                    if current.get('page') != step['目标']:
                        raise RuntimeError(f'步骤{number}等待业务状态时离页，停止后续输入')
                    equipment_matches = all(current['slotModels'][current['currentSlot']].get(field) == expected
                                            for field, expected in step.get('装备字段', {}).items())
                    if current.get('internalState', {}).get('words', [None] * 3)[2] == step['内部状态'] and equipment_matches:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f'步骤{number}等待内部状态超时，停止后续输入')
            name = f"{number:02d}-{step['名称']}"
            experiment.stage(name, step['目标'])
            details = experiment.rpc('details')
            experiment.record('定制自然操作字段', 阶段=name, 数据=details)
            if '目标ID' in step and details.get('itemList', {}).get('selected', {}).get('id') != step['目标ID']:
                raise RuntimeError(f'步骤{number}高亮稳定ID不符，停止后续输入')
            if '内部状态' in step and details.get('internalState', {}).get('words', [None] * 3)[2] != step['内部状态']:
                raise RuntimeError(f'步骤{number}内部状态不符，停止后续输入')
            for field, expected in step.get('装备字段', {}).items():
                if details['slotModels'][details['currentSlot']].get(field) != expected:
                    raise RuntimeError(f'步骤{number}当前槽位装备字段{field}不符，停止后续输入')
        experiment.record('定制自然调用时序', 数据=experiment.rpc('events'))
    except Exception as error:
        failure = str(error)
        print(f'实验停止：{failure}', flush=True)
    finally:
        try:
            if experiment.process:
                experiment.record('结束调用时序', 数据=experiment.rpc('events'))
                experiment.rpc('cancel')
        finally:
            experiment.close(failure)
    print(f'实验记录：{experiment.output}', flush=True)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
