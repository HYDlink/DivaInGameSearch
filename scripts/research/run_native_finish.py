"""按实际来源验证原生Finish返回；模型起点先原生退回父页。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def main():
    """只接受模型或父页，按外层来源断言返回歌曲或主页，不发送Esc。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 目标参数必须为当前游戏进程。
    parser.add_argument('--pid', type=int, required=True, help='本次游戏进程号')
    # 输出结果文件参数保存本轮调用、读回和截图。
    parser.add_argument('--output', type=Path, required=True, help='独立实验结果目录')
    # 参数为基础实验器所需的现有AHK路径，本流程不发送按键。
    parser.add_argument('--ahk', type=Path, required=True, help='现有AutoHotkey v2路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '原生退出并按来源恢复'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        initial = experiment.sample()
        if initial['page'] not in ['模型', '自定义父页'] or initial['menu'][0] not in [5, 36]:
            raise RuntimeError('当前来源不符合原生退出实验条件')
        target = '歌曲' if initial['menu'][0] == 5 else '主页'
        experiment.stage('01-退出起点', initial['page'])
        if initial['page'] == '模型':
            experiment.record('返回父页排队', 数据=experiment.rpc('armnavigation', {'target': 1}))
            experiment.stage('02-原生返回父页', '自定义父页')
            experiment.record('父页返回时序', 数据=experiment.rpc('events'))
            experiment.rpc('cancel')
        experiment.record('退出来源', 预期=target, 数据=experiment.rpc('outerinfo'))
        experiment.record('原生退出排队', 数据=experiment.rpc('armfinish'))
        experiment.stage('03-原生恢复来源', target)
        experiment.record('原生退出时序', 数据=experiment.rpc('events'))
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
