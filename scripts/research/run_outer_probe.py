"""自动采集选曲R激活自定义任务的有限调用者和进入返回状态。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment


def main():
    """只在已认证选曲页起步，单Hook观察正常R，再Esc恢复选曲。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出参数保存本轮任务激活前后、调用栈及图片。
    parser.add_argument('--output', required=True, type=Path, help='外层任务观察输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径')
    parser.add_argument('--activation', action='store_true', help='观察实际激活接口参数，替代任务获取器')
    parser.add_argument('--home', action='store_true', help='选曲返回主页并下移一项后停止，供截图核对')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '选曲R外层任务只读观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-选曲起点', '歌曲')
        experiment.record('进入前任务', 数据=experiment.rpc('outerinfo'))
        experiment.rpc('observeactivation' if options.activation else 'observeouter')
        time.sleep(0.4)
        experiment.stage('02-R进入自定义', '自定义父页', 'R')
        experiment.record('进入后任务', 数据=experiment.rpc('outerinfo'))
        experiment.record('任务调用者', 数据=experiment.rpc('events'))
        experiment.stage('03-Esc返回选曲', '歌曲', 'Esc')
        experiment.record('返回后任务', 数据=experiment.rpc('outerinfo'))
        experiment.record('返回阶段调用者', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        if options.home:
            experiment.stage('04-返回主页', '主页', 'Esc')
            experiment.stage('05-主页下移一项', '主页', 'Down')
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
