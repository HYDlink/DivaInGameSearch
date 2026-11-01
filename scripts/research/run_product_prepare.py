"""部署前自然退出自定义并核查来源；只读Frida和AHK，不调用动作探针。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def main():
    """参数指定当前PID、输出目录和AHK；逐层自然返回后停在选曲，不强制关闭。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 目标进程每轮重新查询。
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出结果文件参数保存自然退出前后截图、页面和保存中边界。
    parser.add_argument('--output', required=True, type=Path, help='独立退出证据目录')
    # 按键沿用已验证的AutoHotkey工具。
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2路径')
    options = parser.parse_args()
    options.timeout, options.flow = 15, '部署前自然退出'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        initial = experiment.sample()
        if initial['page'] == '模型':
            experiment.stage('01-模型起点', '模型')
            experiment.stage('02-自然返回父页', '自定义父页', 'Esc')
        if experiment.sample()['page'] == '自定义父页':
            experiment.stage('03-自然恢复选曲', '歌曲', 'Esc')
        if experiment.sample()['page'] != '歌曲':
            raise RuntimeError('当前不是可自然关闭的选曲来源，停止后续关闭操作')
        experiment.stage('04-选曲可操作', '歌曲')
    except Exception as error:
        failure = str(error)
        print('退出准备停止：' + failure, flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
