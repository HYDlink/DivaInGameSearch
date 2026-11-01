"""记录父页正常返回选曲，再单次返回主页；未知主页只记录不继续输入。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def main():
    """从已确认父页起步，主页身份交由本轮原始字段与截图核对。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出目录保存正常退出调用与主页未知状态，不覆盖既有实验。
    parser.add_argument('--output', required=True, type=Path, help='主页单次观察输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '主页路径单次观察，未知起点后停止'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-父页起点', '自定义父页')
        experiment.rpc('observesibling')
        experiment.stage('02-正常返回歌曲', '歌曲', 'Esc')
        experiment.record('退出自定义请求', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        key(experiment, 'Esc')
        time.sleep(1)
        experiment.record('待认证主页状态', 数据=experiment.rpc('details'))
        experiment.capture('03-单次返回主页待认证')
    except Exception as error:
        failure = str(error)
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
