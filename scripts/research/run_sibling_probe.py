"""观察F3同层循环的原生调用者，并验证Esc回父页。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def main():
    """只从模型列表执行已验证F3循环，所有页面断言通过才发送下一键。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int, help='当前游戏进程号')
    # 输出目录独立保存调用者、切页前后和截图。
    parser.add_argument('--output', required=True, type=Path, help='同层切换观察输出目录')
    parser.add_argument('--ahk', required=True, type=Path, help='AutoHotkey v2 路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 12, 'F3同层原生调用者观察'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-模型起点', '模型')
        experiment.rpc('observesibling')
        for name, page in [('02-发型', '发型'), ('03-饰品', '饰品'), ('04-模型', '模型')]:
            experiment.stage(name, page, 'F3')
            experiment.record('原生切页记录', 数据=experiment.rpc('events'))
            experiment.record('目标详细状态', 数据=experiment.rpc('details'))
        experiment.stage('05-父页', '自定义父页', 'Esc')
        experiment.record('原生返回记录', 数据=experiment.rpc('events'))
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
