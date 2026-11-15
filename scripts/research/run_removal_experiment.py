"""实机对照自然卸除与注入卸除；每轮只测一个部位，结束恢复进入本轮时的六项配置。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def configuration(details):
    """参数details为新鲜快照；只取当前槽位模型及五类定制ID，避免把动画数据当装备。"""
    slot = details['slotModels'][details['currentSlot']]
    return {hex(offset): slot[hex(offset)] for offset in range(0, 0x18, 4)}


def settled(experiment, field=None, expected=None):
    """参数field/expected为可选装备断言；连续三次正常列表且控件稳定才返回。"""
    deadline, stable = time.monotonic() + 12, 0
    while time.monotonic() < deadline:
        details = experiment.rpc('removalstate')
        match = details['page'] == experiment.removal_page and details['scope'] == 3 \
            and details['controller'] == experiment.removal_controller \
            and details['currentSlot'] == experiment.removal_slot \
            and details['sourcePvid'] == experiment.removal_pvid \
            and details['internalState']['words'][2] == 0 and details['itemControlTransition'] == -1
        if field is not None:
            match = match and configuration(details)[field] == expected
        stable = stable + 1 if match else 0
        if stable >= 3:
            return details
        time.sleep(0.1)
    raise RuntimeError('卸除实验等待稳定列表或目标装备字段超时')


def checkpoint(experiment, name, expected):
    """参数name/expected为阶段名和六项预期；截图前后均验证，变化超出目标即停止。"""
    details = settled(experiment)
    if configuration(details) != expected:
        raise RuntimeError('阶段六项配置不符：' + name)
    experiment.capture(name)
    after = settled(experiment)
    if configuration(after) != expected:
        raise RuntimeError('截图期间六项配置变化：' + name)
    experiment.record('卸除阶段通过', 阶段=name, 配置=expected, 数据=after)
    experiment.stages.append({'阶段': name, '结果': '通过', '配置': expected, '截图': name + '.png'})
    print('卸除阶段通过：' + name, flush=True)
    return after


def locate(experiment, target):
    """参数target为当前真实列表唯一稳定ID；复用已认证控件定位并跨帧确认。"""
    experiment.rpc('armlocate', {'id': target})
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        details = settled(experiment)
        if details['itemList']['selected']['id'] == target:
            return
        time.sleep(0.1)
    raise RuntimeError('定位当前装备失败')


def equip(experiment, target, field):
    """参数target/field为已验证拥有的样本及对应字段；通过自然菜单更换，不购买。"""
    locate(experiment, target)
    before = settled(experiment)
    key(experiment, 'Enter')
    menu = experiment.rpc('removalstate')
    if menu['internalState']['words'][2] != 1 or menu['itemSceneWords']['0x38'] != before['itemList']['index']:
        raise RuntimeError('自然更换菜单上下文不符')
    experiment.capture('菜单-更换-' + str(target))
    key(experiment, 'Enter')
    settled(experiment, field, target)


def main():
    """参数均有中文帮助；只开放已核对拥有的五个样本，不将实验白名单当产品拥有判定。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True, help='本次游戏进程号')
    # 输出结果文件参数：保存时序、自然调用、截图和最终恢复断言；目录必须尚不存在。
    parser.add_argument('--output', type=Path, required=True, help='本轮证据输出目录')
    parser.add_argument('--ahk', type=Path, required=True, help='现有AutoHotkey v2程序')
    parser.add_argument('--part', choices=['hair', 'head', 'face', 'chest', 'back'], required=True, help='本轮验证部位')
    # compare会恢复进入本轮时的配置；remove仅卸除指定部位当前真实装备，成功后保留卸除结果。
    parser.add_argument('--mode', choices=['compare', 'remove'], default='compare', help='对照并恢复，或只执行原生卸除')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('item_methods_probe.js')
    options.timeout, options.flow = 12, '自然卸除与注入卸除逐部位对照'
    if options.mode == 'remove':
        options.flow = '单次原生卸除并保留结果'
    options.budget = 180
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        part, target = {'hair': (4, 666), 'head': (0, 42), 'face': (1, 0), 'chest': (2, 57), 'back': (3, 70)}[options.part]
        expected_page = '发型' if part == 4 else '饰品'
        initial = experiment.rpc('removalstate')
        if initial['page'] == '发型' and expected_page == '饰品':
            experiment.stage('准备-进入饰品', '饰品', 'F3')
        initial = experiment.rpc('removalstate')
        if initial['page'] != expected_page or initial['scope'] != 3 or initial['internalState']['words'][2] != 0:
            raise RuntimeError('需要对应的空闲单曲发型或饰品列表')
        experiment.removal_page, experiment.removal_controller = expected_page, initial['controller']
        experiment.removal_slot, experiment.removal_pvid = initial['currentSlot'], initial['sourcePvid']
        if part != 4:
            for _ in range(4):
                current = settled(experiment)['itemSceneWords']['0x30']
                if current == part:
                    break
                key(experiment, 'Down')
                after = settled(experiment)['itemSceneWords']['0x30']
                if after != (current + 1) % 4:
                    raise RuntimeError('部位下移未生效，停止')
            if settled(experiment)['itemSceneWords']['0x30'] != part:
                raise RuntimeError('部位不匹配')
        baseline = configuration(settled(experiment))
        field = hex(4 + part * 4)
        if options.mode == 'remove':
            checkpoint(experiment, '01-单次卸除前', baseline)
            if baseline[field] == -1:
                experiment.record('无需卸除', 部位=options.part, 配置=baseline)
                print('该部位未设置，无需卸除。', flush=True)
                return 0
            locate(experiment, baseline[field])
            experiment.record('单次卸除提交', 数据=experiment.rpc('armremove', {'id': baseline[field]}))
            settled(experiment, field, -1)
            checkpoint(experiment, '02-单次注入卸除完成', dict(baseline, **{field: -1}))
            return 0
        if baseline[field] not in [-1, target]:
            raise RuntimeError('当前装备不属于本轮已认证样本，保留现场')
        experiment.rpc('observe', {'rvas': [0x68ac40 if part == 4 else 0x68f430, 0x1d5e30]})
        checkpoint(experiment, '01-原配置', baseline)
        if baseline[field] == -1:
            equip(experiment, target, field)
        locate(experiment, target)
        equipped = dict(baseline, **{field: target})
        checkpoint(experiment, '02-卸除前', equipped)
        key(experiment, 'Enter')
        menu = experiment.rpc('removalstate')
        if menu['internalState']['words'][2] != 1 or menu['itemSceneWords']['0x38'] != menu['itemList']['index']:
            raise RuntimeError('当前装备卸除菜单上下文不符')
        experiment.capture('03-当前装备唯一菜单')
        key(experiment, 'Enter')
        settled(experiment, field, -1)
        removed = dict(baseline, **{field: -1})
        checkpoint(experiment, '04-自然卸除', removed)
        events = experiment.rpc('events')
        command = 6 if part == 4 else 5
        if not any(int(event.get('args', ['0', '0'])[1], 16) == command
                   and event.get('before', {}).get('sceneKey') == (3 if part == 4 else 4) for event in events):
            raise RuntimeError('没有捕获预期自然卸除回调，禁止注入复现')
        equip(experiment, target, field)
        checkpoint(experiment, '05-恢复样本供注入', equipped)
        experiment.record('注入卸除提交', 数据=experiment.rpc('armremove', {'id': target}))
        settled(experiment, field, -1)
        checkpoint(experiment, '06-注入卸除', removed)
        if baseline[field] != -1:
            equip(experiment, target, field)
        checkpoint(experiment, '07-本轮原配置恢复', baseline)
        experiment.record('完整卸除调用时序', 数据=experiment.rpc('events'))
    except Exception as error:
        failure = str(error)
        print('卸除实验停止：' + failure, flush=True)
    finally:
        try:
            if experiment.process:
                try:
                    experiment.record('卸除结束时序', 数据=experiment.rpc('events'))
                    experiment.rpc('cancel')
                except Exception as error:
                    experiment.record('结束采样受限', 原因=str(error))
        finally:
            experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
