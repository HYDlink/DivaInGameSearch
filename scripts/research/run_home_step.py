"""从经截图确认的主页执行单步观察，不在未知光标下连续确认。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment
from run_preview_observation import key


def main():
    """单次Down或Enter后立即读回并截图；Enter必须指定预期目标页。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前DIVA进程号')
    # 输出目录独立保存该次主页步骤的输入前后与截图。
    parser.add_argument('--output', required=True, type=Path, help='单次主页步骤输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2可执行文件')
    parser.add_argument('--step', choices=['down', 'song', 'custom'], required=True, help='单步下移、确认进入歌曲或自定义')
    options = parser.parse_args()
    options.timeout, options.flow = 15, '主页单步浏览与读回'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        if experiment.sample()['page'] != '主页': raise RuntimeError('当前不在已认证主页')
        key(experiment, 'Down' if options.step == 'down' else 'Enter')
        target = {'down': '主页', 'song': '歌曲', 'custom': '自定义父页'}[options.step]
        experiment.wait(target)
        time.sleep(0.5)
        experiment.capture('01-主页单步后')
        experiment.record('主页单步结果', 数据=experiment.rpc('details'))
    except Exception as error:
        failure = str(error)
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
