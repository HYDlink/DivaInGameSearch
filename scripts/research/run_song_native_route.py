"""从稳定选曲页通过原生接口进入自定义、模型并定位；失败即停。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def main():
    """保持同一会话验证原生R等效调用与服装ID定位，各阶段独立截图。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 参数为当前进程号，禁止沿用重启前的PID。
    parser.add_argument('--pid', type=int, required=True, help='本次游戏进程号')
    # 输出结果文件参数：新建目录保存阶段截图、时间轴和最终状态。
    parser.add_argument('--output', type=Path, required=True, help='本轮独立证据目录')
    # AHK仅供准备父页光标；原生激活和定位不发送R或Enter。
    parser.add_argument('--ahk', type=Path, required=True, help='AutoHotkey v2程序路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '选曲原生激活并定位服装'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-选曲起点', '歌曲')
        experiment.record('原生激活排队', 数据=experiment.rpc('armactivatesong'))
        experiment.stage('02-原生进入自定义', '自定义父页')
        experiment.record('原生激活事件', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        experiment.parent_cursor(0)
        experiment.record('进入模型排队', 数据=experiment.rpc('armparent', {'cursor': 0, 'animate': True}))
        experiment.stage('03-原生进入模型', '模型')
        experiment.record('父页确认事件', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        experiment.record('服装定位排队', 数据=experiment.rpc('armlocate', {'id': 316}))
        experiment.stage('04-原生定位后', '模型')
        located = experiment.sample()
        if located.get('module', {}).get('id') != 316:
            raise RuntimeError('页面正确但目标服装ID未定位，停止后续动作')
        experiment.record('服装定位核验', 数据=located, 调用事件=experiment.rpc('events'))
        experiment.rpc('cancel')
    except Exception as error:
        failure = str(error)
        try: experiment.record('停止前事件', 数据=experiment.rpc('events'))
        except Exception: pass
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    print(f'实验记录：{experiment.output}', flush=True)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
