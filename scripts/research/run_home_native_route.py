"""通过正式模式请求从主页进入自定义并恢复，完整模式再进入选曲。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment


def submit(experiment, command, parameters, name, page):
    """参数为当前实验、已登记原生命令、参数、阶段名和目标页；每步独立断言。"""
    experiment.record('原生路由排队', 命令=command, 数据=experiment.rpc(command, parameters))
    experiment.stage(name, page)
    experiment.record('原生路由时序', 阶段=name, 数据=experiment.rpc('events'))
    experiment.rpc('cancel')


def main():
    """最小模式验证主页往返；完整模式覆盖子页循环、选曲与服装定位。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 进程号参数在每次游戏重启后重新查询。
    parser.add_argument('--pid', type=int, required=True, help='当前游戏进程号')
    # 输出结果文件参数：每次使用新目录保存逐阶段截图及日志。
    parser.add_argument('--output', type=Path, required=True, help='本轮独立输出目录')
    # AHK只供父页光标准备使用，主页模式请求不发送按键。
    parser.add_argument('--ahk', type=Path, required=True, help='AutoHotkey v2程序路径')
    # 流程参数默认最小闭环，完整验证必须显式选择。
    parser.add_argument('--flow', choices=['minimal', 'full'], default='minimal', help='主页最小闭环或完整原生导航')
    options = parser.parse_args()
    full = options.flow == 'full'
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '主页原生完整导航' if full else '主页原生最小闭环'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-主页起点', '主页')
        submit(experiment, 'armhome', {'mode': 7}, '02-原生主页自定义', '自定义父页')
        if full:
            experiment.parent_cursor(0)
            submit(experiment, 'armparent', {'cursor': 0, 'animate': True}, '03-原生模型', '模型')
            for name, page, target in [('04-原生发型', '发型', 3), ('05-原生饰品', '饰品', 4),
                                       ('06-原生回模型', '模型', 2), ('07-原生回父页', '自定义父页', 1)]:
                submit(experiment, 'armnavigation', {'target': target}, name, page)
        submit(experiment, 'armfinish', {}, '08-原生返回主页', '主页')
        if full:
            submit(experiment, 'armhome', {'mode': 2}, '09-原生主页进入选曲', '歌曲')
            submit(experiment, 'armactivatesong', {}, '10-原生选曲自定义', '自定义父页')
            experiment.parent_cursor(0)
            submit(experiment, 'armparent', {'cursor': 0, 'animate': True}, '11-歌曲来源模型', '模型')
            submit(experiment, 'armlocate', {'id': 316}, '12-原生定位316', '模型')
            if experiment.sample().get('module', {}).get('id') != 316:
                raise RuntimeError('服装目标未命中，停止')
            submit(experiment, 'armnavigation', {'target': 1}, '13-回歌曲来源父页', '自定义父页')
            submit(experiment, 'armfinish', {}, '14-原生返回选曲', '歌曲')
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
