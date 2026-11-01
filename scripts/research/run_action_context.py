"""自然模型菜单的拥有判定与槽位原始字段对照；不预览、不换装、不购买。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def main():
    """参数由命令行提供；先核对稳定选曲，再逐步进入与退出菜单，保存截图及独立读回。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 目标进程参数必须每轮重新查询，不能沿用历史PID。
    parser.add_argument('--pid', required=True, type=int, help='本轮实际游戏进程号')
    # 输出结果文件参数：每轮独立目录保存逐阶段截图、字段和本探针事件，不覆盖历史证据。
    parser.add_argument('--output', required=True, type=Path, help='本轮证据目录，必须不存在')
    # 原生自然按键由用户指定的AutoHotkey工具发送。
    parser.add_argument('--ahk', required=True, type=Path, help='已安装的AutoHotkey v2程序')
    options = parser.parse_args()
    options.timeout, options.flow = 12, '自然菜单拥有判定与配置对照'
    experiment, failure = Experiment(options), None
    agent = experiment.output / 'action-agent.js'
    agent.write_text(Path(__file__).with_name('route_actions.js').read_text(encoding='utf-8') + '\n'
                     + Path(__file__).with_name('action_context_probe.js').read_text(encoding='utf-8'), encoding='utf-8')
    options.agent = agent
    try:
        experiment.start()
        initial_page = experiment.sample()['page']
        if initial_page == '歌曲':
            experiment.stage('01-选曲起点', '歌曲')
            experiment.stage('02-自然父页', '自定义父页', 'R')
            experiment.parent_cursor(0)
            experiment.stage('03-自然模型列表', '模型', 'Enter')
        elif initial_page == '模型':
            experiment.stage('01-当前模型起点', '模型')
        else:
            raise RuntimeError('观察起点必须是稳定选曲或模型列表')
        baseline = experiment.rpc('actioncontext')
        experiment.record('模型配置原始基线', 数据=baseline)
        # 只从当前真实分类取非装备索引的样本，不切角色或槽位，不假定任何ID已拥有。
        candidates = [row for row in baseline['modelRecords'] if row['index'] != baseline['sceneFields']['0x2c']]
        if not candidates:
            raise RuntimeError('当前分类没有可对照的非装备条目')
        target = candidates[0]['id']
        experiment.rpc('armlocate', {'id': target})
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if experiment.sample().get('module', {}).get('id') == target:
                break
            time.sleep(0.1)
        else:
            raise RuntimeError('观察目标没有在原生列表中高亮')
        experiment.rpc('cancel')
        experiment.capture('04-菜单前高亮')
        experiment.rpc('observeownership')
        before = experiment.rpc('details')
        if before['internalState']['words'][2] != 0 or before['sceneFields']['0x60'] != -1:
            raise RuntimeError('打开菜单前页面不是空闲列表')
        after = key(experiment, 'Enter')
        if after['internalState']['words'][2] != 1 or after['menuTargetId'] != target:
            raise RuntimeError('自然菜单未绑定目标，停止且不发送确认')
        experiment.record('自然拥有判断', 数据=experiment.rpc('ownershipresult'))
        experiment.record('菜单配置原始字段', 数据=experiment.rpc('actioncontext'))
        experiment.capture('05-自然菜单只读')
        after = key(experiment, 'Esc')
        if after['internalState']['words'][2] != 0:
            raise RuntimeError('取消菜单后未恢复列表')
        experiment.stage('06-模型列表恢复', '模型')
        if initial_page == '歌曲':
            experiment.stage('07-自然返回父页', '自定义父页', 'Esc')
            experiment.stage('08-自然返回选曲', '歌曲', 'Esc')
    except Exception as error:
        failure = str(error)
        print('观察停止：' + failure, flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
