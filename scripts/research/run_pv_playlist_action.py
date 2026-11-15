"""播放列表自定义页的受控动作研究：验证原生预览、预览退出与更换实际写入哪个配置对象。"""
import argparse
from pathlib import Path
import time

from run_page_experiment import Experiment


def snapshot_words(state):
    """参数state为探针只读采样；取出当前槽位的六项独立记录，便于逐项比较。"""
    slot = state['slot']
    return list(state['overrideSlots'][slot]) if state['slot'] is not None and state['overrideSlots'] else None


def display_words(state):
    """参数state为探针只读采样；取出当前槽位的临时显示配置，和独立记录分开记录。"""
    slot = state['slot']
    return list(state['displaySlots'][slot]) if state['slot'] is not None and state['displaySlots'] else None


def wait_state(experiment, description, predicate, seconds=12):
    """参数description/predicate为阶段说明与采样判据；连续三次满足才返回，超时保留最后采样。"""
    deadline, stable, last = time.monotonic() + seconds, 0, None
    while time.monotonic() < deadline:
        last = experiment.rpc('pvstate')
        stable = stable + 1 if predicate(last) else 0
        if stable >= 3:
            return last
        time.sleep(0.1)
    return None


def checkpoint(experiment, name, state):
    """参数name/state为阶段名与当前采样；截图前后各读回一次，图片须人工打开核对。"""
    experiment.capture(name)
    after = experiment.rpc('pvstate')
    experiment.record('阶段截图', 阶段=name, 独立记录=snapshot_words(state), 显示配置=display_words(state),
                      截图后独立记录=snapshot_words(after), 截图后显示配置=display_words(after))
    print('阶段截图：' + name, flush=True)
    return after


def ownership(experiment):
    """在真实模型状态帧查询当前列表条目的拥有状态；只读，不更换或购买。"""
    experiment.rpc('pvownership')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = experiment.rpc('pvownershipresult')
        if result.get('result'):
            return result['result']
        if any('失败' in str(event.get('说明')) for event in result.get('events', [])):
            raise RuntimeError('拥有查询失败：' + repr(result['events']))
        time.sleep(0.1)
    raise RuntimeError('拥有查询超时')


def actions(experiment):
    """返回本探针累计的受控事件，失败时也保留现场。"""
    return experiment.rpc('pvevents')


def main():
    """参数均有中文帮助；默认先预览、再退出预览、再更换并恢复原配置。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True, help='本次游戏进程号')
    # 输出参数：独立目录保存时间轴、截图与最终结果，目录必须尚不存在。
    parser.add_argument('--output', type=Path, required=True, help='本轮证据输出目录')
    parser.add_argument('--ahk', type=Path, required=True, help='现有AutoHotkey v2程序')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('pv_playlist_action_probe.js')
    options.timeout, options.flow, options.budget = 12, '播放列表自定义动作研究', 180
    experiment, failure = Experiment(options), None
    baseline, record_id, changed = None, None, False
    try:
        experiment.start()
        baseline = experiment.rpc('pvstate')
        if baseline['page'] != '模型' or baseline['menu'] != [6, 7, 1] or baseline['scope'] != 3 \
                or baseline['internal'] != 0 or not baseline['overrideSlots']:
            raise RuntimeError('需要播放列表自定义的模型列表空闲状态：' + repr(baseline))
        record_id = snapshot_words(baseline)[0]
        experiment.record('基线', 数据=baseline, 独立模型=record_id)
        checkpoint(experiment, '01-基线模型列表', baseline)
        if display_words(baseline)[0] != record_id:
            raise RuntimeError('基线临时显示与独立记录不一致，先核对现场')
        info = ownership(experiment)
        experiment.record('拥有查询结果', 数据={k: v for k, v in info.items() if k != 'ids'})
        owned = [item for item in info['ids'] if item in set(info['owned']) and item != record_id]
        if not owned:
            raise RuntimeError('当前列表没有可安全试验的其他已拥有模型')
        target = owned[0]
        experiment.record('选定目标', 目标ID=target, 已拥有数量=len(info['owned']))
        # 预览：临时显示应改变，独立记录必须保持原值。
        experiment.rpc('pvarmaction', {'id': target, 'action': 'preview'})
        previewing = wait_state(experiment, '预览进入临时状态', lambda state: state['internal'] == 2)
        if previewing is None:
            experiment.record('预览未进入', 探针事件=actions(experiment))
            raise RuntimeError('预览未进入临时状态')
        experiment.record('预览采样', 数据=previewing, 探针事件=actions(experiment))
        if display_words(previewing)[0] != target:
            raise RuntimeError('预览未改变临时显示配置')
        if snapshot_words(previewing)[0] != record_id:
            raise RuntimeError('预览改变了独立记录，停止后续动作')
        checkpoint(experiment, '02-原生预览', previewing)
        # 退出预览：显示与独立记录都应回到原值。
        experiment.rpc('pvexitmodelpreview')
        exited = wait_state(experiment, '预览退出回到空闲列表', lambda state: state['internal'] == 0)
        if exited is None:
            experiment.record('预览退出未完成', 探针事件=actions(experiment))
            raise RuntimeError('预览退出未回到空闲列表')
        experiment.record('预览退出采样', 数据=exited, 探针事件=actions(experiment))
        if snapshot_words(exited)[0] != record_id or display_words(exited)[0] != record_id:
            raise RuntimeError('预览退出后配置未恢复')
        checkpoint(experiment, '03-退出预览', exited)
        # 更换：独立记录应在原生加载完成后等于目标。
        changed = True
        experiment.rpc('pvarmaction', {'id': target, 'action': 'equip'})
        equipped = wait_state(experiment, '更换完成后独立记录等于目标',
                              lambda state: state['internal'] == 0 and snapshot_words(state)[0] == target, 20)
        if equipped is None:
            last = experiment.rpc('pvstate')
            experiment.record('更换未写入独立记录', 数据=last, 探针事件=actions(experiment))
            raise RuntimeError('更换没有改变独立记录；已记录临时显示与独立记录差异')
        experiment.record('更换采样', 数据=equipped, 探针事件=actions(experiment))
        if display_words(equipped)[0] != target:
            raise RuntimeError('更换后临时显示与独立记录不一致')
        checkpoint(experiment, '04-原生更换', equipped)
        # 恢复原配置：重新更换回原模型，并核对六个槽位其他字段未变。
        experiment.rpc('pvarmaction', {'id': record_id, 'action': 'equip'})
        restored = wait_state(experiment, '恢复原模型',
                              lambda state: state['internal'] == 0 and snapshot_words(state)[0] == record_id, 20)
        if restored is None:
            raise RuntimeError('恢复原模型超时，现场保留')
        experiment.record('恢复采样', 数据=restored, 探针事件=actions(experiment))
        if display_words(restored)[0] != record_id or snapshot_words(restored) != snapshot_words(baseline):
            raise RuntimeError('恢复后独立记录与原基线不一致')
        changed = False
        checkpoint(experiment, '05-恢复原配置', restored)
    except Exception as error:
        failure = str(error)
        print('播放列表动作研究停止：' + failure, flush=True)
    finally:
        try:
            if experiment.process:
                try:
                    if changed:
                        # 只恢复本次已改变的独立记录；不写配置字段，仍走原生更换。
                        experiment.record('尝试恢复原模型', 目标=record_id)
                        experiment.rpc('pvarmaction', {'id': record_id, 'action': 'equip'})
                        recovered = wait_state(experiment, '失败后恢复原模型',
                                               lambda state: state['internal'] == 0 and snapshot_words(state)[0] == record_id, 20)
                        experiment.record('恢复结果', 数据=recovered, 成功=recovered is not None)
                    experiment.record('结束时序', 数据=experiment.rpc('pvevents'))
                    experiment.rpc('pvcancel')
                except Exception as error:
                    experiment.record('结束采样受限', 原因=str(error))
        finally:
            experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())

