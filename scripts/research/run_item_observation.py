"""复用已验证原生导航，批量只读观察饰品左右定位和部位切换。"""
import argparse
from pathlib import Path
from run_page_experiment import Experiment
from run_home_native_route import submit
from run_preview_observation import key


def observe_step(experiment, name, button=None):
    """参数为当前实验、阶段名和可选方向键；只在饰品页发送不含确认的自然输入。"""
    experiment.wait('饰品')
    if button:
        key(experiment, button)
    experiment.stage(name, '饰品')
    current = experiment.rpc('details')
    experiment.record('饰品候选布局', 阶段=name, 数据=current)
    return current


def main():
    """同一会话采集当前场景有限字段，观察后原生退出；不预览、不装备、不购买。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 目标进程参数必须为当前运行实例。
    parser.add_argument('--pid', type=int, required=True, help='当前游戏进程号')
    # 输出结果文件参数保存逐阶段字段及原生截图。
    parser.add_argument('--output', type=Path, required=True, help='饰品只读实验的新目录')
    # 自然方向键由用户指定的AHK后端发送。
    parser.add_argument('--ahk', type=Path, required=True, help='AutoHotkey v2程序路径')
    options = parser.parse_args()
    options.agent = Path(__file__).with_name('route_actions.js').resolve()
    options.timeout, options.flow = 15, '饰品候选字段只读对照'
    experiment, failure = Experiment(options), None
    try:
        experiment.start()
        experiment.stage('01-选曲起点', '歌曲')
        submit(experiment, 'armactivatesong', {}, '02-自定义父页', '自定义父页')
        experiment.parent_cursor(0)
        submit(experiment, 'armparent', {'cursor': 0, 'animate': True}, '03-模型', '模型')
        submit(experiment, 'armnavigation', {'target': 4}, '04-饰品', '饰品')
        baseline = observe_step(experiment, '05-饰品基线')
        if baseline['itemCandidateWords']['0x30'] != 0 or baseline['itemCandidateWords']['0x78'] != 0:
            raise RuntimeError('本轮恢复序列只接受已观察过的头部首项基线')
        observe_step(experiment, '06-饰品右移', 'Right')
        observe_step(experiment, '07-部位下移', 'Down')
        observe_step(experiment, '08-部位恢复', 'Up')
        observe_step(experiment, '09-饰品左移环绕', 'Left')
        restored = observe_step(experiment, '10-饰品右移恢复', 'Right')
        if any(restored['itemCandidateWords'][field] != baseline['itemCandidateWords'][field]
               for field in ['0x30', '0x78', '0x84']):
            raise RuntimeError('方向键序列没有恢复基线部位和高亮，停止退出')
        experiment.record('饰品部位高亮已恢复', 数据=restored['itemCandidateWords'])
        submit(experiment, 'armnavigation', {'target': 1}, '11-返回父页', '自定义父页')
        submit(experiment, 'armfinish', {}, '12-返回选曲', '歌曲')
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
