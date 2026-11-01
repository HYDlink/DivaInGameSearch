"""自然切换歌者槽位或F4范围，只读比较配置身份；不装备、不购买。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment
from run_preview_observation import key
import time


def wait_ready(experiment):
    """参数experiment为当前会话；等待真实模型列表及内部状态恢复，页面存在不等于加载完成。"""
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        state = experiment.rpc('actioncontext')
        if state['page'] == '模型' and state.get('module', {}).get('id') is not None \
                and state['internalState']['words'][2] == 0 and state['sceneFields']['0x60'] == -1:
            return state
        time.sleep(0.15)
    raise RuntimeError('自然切换后真实列表没有就绪')


def main():
    """通过当前游戏、证据目录、AHK和对照类型参数运行一次有界自然观察。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 目标参数每次重新查询。
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出结果文件参数：独立保存前后截图与原始字段，不覆盖上一轮。
    parser.add_argument('--output', required=True, type=Path, help='尚不存在的本轮证据目录')
    # 自然输入工具参数，不进入产品运行依赖。
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2路径')
    # 类型参数分别验证槽位环回与一次范围切换；范围切换后需读图再决定恢复操作。
    parser.add_argument('--flow', choices=['slots', 'scope'], required=True, help='槽位环回或F4单步范围对照')
    options = parser.parse_args()
    options.timeout = 12
    experiment, failure = Experiment(options), None
    agent = experiment.output / 'scope-agent.js'
    agent.write_text(Path(__file__).with_name('action_context_probe.js').read_text(encoding='utf-8'), encoding='utf-8')
    options.agent = agent
    try:
        experiment.start()
        experiment.stage('01-模型基线', '模型')
        before = wait_ready(experiment)
        if before['internalState']['words'][2] != 0 or before['sceneFields']['0x60'] != -1:
            raise RuntimeError('起点不是正常模型列表')
        experiment.record('原始配置基线', 数据=before)
        if options.flow == 'slots':
            original = before['controllerFields']['0x3c']
            count = before['rawControllerWords']['0x38']
            if count not in range(2, 7) or original not in range(count):
                raise RuntimeError('当前槽位数量或候选活动槽位不可验证')
            key(experiment, 'E')
            after = wait_ready(experiment)
            experiment.record('歌者切换候选', 数据=after)
            if after['controllerFields']['0x3c'] != (original + 1) % count:
                raise RuntimeError('自然E没有呈现预期槽位变化，停止且不盲发恢复')
            experiment.capture('02-自然下一歌者')
            key(experiment, 'Q')
            restored = wait_ready(experiment)
            experiment.record('歌者恢复候选', 数据=restored)
            if restored['controllerFields']['0x3c'] != original or restored['modelRows'] != before['modelRows']:
                raise RuntimeError('槽位或装备数组没有独立恢复')
            experiment.capture('03-自然恢复歌者')
        else:
            key(experiment, 'F4')
            after = wait_ready(experiment)
            time.sleep(0.7)
            experiment.record('F4单步原始对照', 数据=after)
            experiment.capture('02-F4范围待识别')
    except Exception as error:
        failure = str(error)
        print('配置观察停止：' + failure, flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
