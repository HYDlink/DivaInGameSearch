"""注入脚本完成父页进入与模型／发型／饰品切页，跨页步骤不发送确认或F3。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def main():
    """从已认证父页执行有界原生跨页循环，每步独立读回与截图。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出参数保存脚本调用、前后状态和截图证据，必须为独立新目录。
    parser.add_argument('--output', required=True, type=Path, help='原生跨页实验输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径，只在准备父菜单光标时使用')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, '脚本原生跨页循环'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-父页起点', '自定义父页')
        experiment.parent_cursor(0)
        experiment.record('进入模型排队', 数据=experiment.rpc('armparent', {'cursor': 0, 'animate': True}))
        experiment.stage('02-原生进入模型', '模型')
        experiment.record('进入模型时序', 数据=experiment.rpc('events'))
        experiment.rpc('cancel')
        for name, page, target in [('03-原生发型', '发型', 3), ('04-原生饰品', '饰品', 4),
                                    ('05-原生模型', '模型', 2), ('06-原生父页', '自定义父页', 1)]:
            experiment.record('同层排队', 数据=experiment.rpc('armnavigation', {'target': target}))
            experiment.stage(name, page)
            experiment.record('同层读回时序', 数据=experiment.rpc('events'))
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
