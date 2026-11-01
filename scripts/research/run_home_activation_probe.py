"""从已截图确认的主页自定义入口观察任务参数与返回来源。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment
from run_preview_observation import key


def main():
    """主页自定义高亮经截图确认后，单Hook观察Enter并检查Esc是否回主页。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出参数记录主页来源的任务参数、页面与截图。
    parser.add_argument('--output', required=True, type=Path, help='主页激活观察输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '主页自定义激活只读观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-主页自定义起点', '主页')
        experiment.rpc('observeactivation')
        key(experiment, 'Enter')
        experiment.stage('02-主页进入自定义', '自定义父页')
        experiment.record('主页激活调用', 数据=experiment.rpc('events'))
        experiment.record('主页来源任务', 数据=experiment.rpc('outerinfo'))
        key(experiment, 'Esc')
        experiment.stage('03-返回主页', '主页')
        experiment.record('返回来源任务', 数据=experiment.rpc('outerinfo'))
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
