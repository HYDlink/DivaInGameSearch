"""复用 AutoHotkey 实验宿主，定向观察父页自然确认路径。"""
import argparse
from pathlib import Path
import time
from run_page_experiment import Experiment


def main():
    """解析实验进程、输出目录和 AHK 路径，自动完成自然确认与返回。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='本次实际 DIVA 进程号')
    # 输出目录用于本次探针时序、只读状态与截图，必须独立创建，避免覆盖证据。
    parser.add_argument('--output', required=True, type=Path, help='实验日志和阶段截图输出目录，必须不存在')
    parser.add_argument('--ahk', required=True, type=Path, help='已安装 AutoHotkey v2 路径')
    parser.add_argument('--native', action='store_true', help='自然确认采样通过后，在真实状态帧边界调用一次确认函数')
    parser.add_argument('--animate', action='store_true', help='复现正常Enter的确认动画与父页确认子状态，等待原生状态机切页')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout = 12
    options.flow = '父页确认定向观察'
    experiment = Experiment(options)
    failure = None
    try:
        experiment.start()
        initial = experiment.sample()
        if initial['page'] == '歌曲': experiment.stage('01-进入父页', '自定义父页', 'R')
        else: experiment.stage('01-父页起点', '自定义父页')
        experiment.parent_cursor(0)
        for address in [0x69B040, 0x69BA10, 0x69A100]:
            experiment.record('运行时反汇编', RVA=hex(address), 数据=experiment.rpc('code', {'rva': address}))
        experiment.record('观察启动', 数据=experiment.rpc('observe', {'rva': 0x69BA10}))
        experiment.stage('02-自然进入模型', '模型', 'Enter')
        experiment.record('自然调用记录', 数据=experiment.rpc('events'))
        experiment.record('模型详细状态', 数据=experiment.rpc('details'))
        experiment.rpc('cancel')
        experiment.stage('03-返回父页', '自定义父页', 'Esc')
        if options.native:
            experiment.record('原生确认排队', 数据=experiment.rpc('armparent', {'cursor': 0, 'animate': options.animate}))
            experiment.stage('04-脚本原生进入模型', '模型')
            experiment.record('原生调用记录', 数据=experiment.rpc('events'))
            experiment.rpc('cancel')
            experiment.stage('05-返回父页', '自定义父页', 'Esc')
    except Exception as error:
        failure = str(error)
        try: experiment.record('失败前调用记录', 数据=experiment.rpc('events'))
        except Exception: pass
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
