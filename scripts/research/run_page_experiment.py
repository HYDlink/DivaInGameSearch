"""用 AutoHotkey、单一只读 Frida Session 与原生截图执行有状态断言的页面实验。"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import time

from windows_input import process_info

ROOT = Path(__file__).resolve().parents[2]
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def utc_now():
    """返回 UTC 时间，方便把输入、快照和截图放在同一时间轴中。"""
    return dt.datetime.now(dt.timezone.utc).isoformat()


class Experiment:
    """一轮实验持有一个 Frida 会话；页面未知或断言失败即停止后续输入。"""

    def __init__(self, options):
        """参数 options 包含目标进程、流程、输出目录及现有 AHK 程序。"""
        self.options = options
        self.output = options.output.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.log = (self.output / 'events.jsonl').open('w', encoding='utf-8')
        self.events = queue.Queue()
        self.process = None
        self.sequence = 0
        self.stages = []
        self.start_time = time.monotonic()
        self.last_snapshot = None

    def record(self, event, **data):
        """参数 event、data 为中文事件名称和可序列化内容；逐条立即落盘。"""
        if {'时间', '经过秒', '事件'} & data.keys():
            raise ValueError('日志附加数据不能覆盖时间或事件名称')
        self.log.write(json.dumps({'时间': utc_now(), '经过秒': round(time.monotonic() - self.start_time, 3),
                                   '事件': event, **data}, ensure_ascii=False) + '\n')
        self.log.flush()

    def start(self):
        """核对构建哈希后附加一次；整轮使用相同 Node 工作进程及 Frida Session。"""
        info = process_info(self.options.pid)
        executable = Path(info.get('路径', ''))
        if executable.name.lower() != 'divamegamix.exe':
            raise RuntimeError('目标进程不是 DIVA')
        with executable.open('rb') as source:
            digest = hashlib.file_digest(source, 'sha256').hexdigest().upper()
        if digest != '6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D':
            raise RuntimeError('游戏构建不匹配，拒绝套用已有字段')
        self.record('环境', 游戏=info, 输入后端='AutoHotkey SendEvent 扫描码，按住150毫秒',
                    AutoHotkey=str(self.options.ahk), 构建SHA256=digest,
                    脚本SHA256={name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
                        for name in ['page_probe.js', 'frida_page_worker.mjs', 'diva_key.ahk', 'run_page_experiment.py']})
        if getattr(self.options, 'agent', None):
            self.record('扩展探针版本', 文件=Path(self.options.agent).name,
                        SHA256=hashlib.sha256(Path(self.options.agent).read_bytes()).hexdigest())
        command = [shutil.which('node') or 'node', str(Path(__file__).with_name('frida_page_worker.mjs')), str(self.options.pid)]
        if getattr(self.options, 'agent', None): command.append(str(self.options.agent))
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, encoding='utf-8', cwd=ROOT)

        # 从工作进程持续收集协议消息，避免 RPC 阻塞导致实验失去超时控制。
        def receive():
            """读取当前会话的 stdout 协议，遇到退出时向主线程报告。"""
            for line in self.process.stdout:
                try: self.events.put(json.loads(line))
                except json.JSONDecodeError: self.events.put({'event': '协议错误', '原文': line})
            self.events.put({'event': '工作进程退出'})

        threading.Thread(target=receive, daemon=True).start()
        ready = self.events.get(timeout=20)
        if ready.get('event') != '就绪':
            raise RuntimeError(f'Frida 启动失败：{ready}')
        self.record('会话就绪', 工作进程=self.process.pid, 信息=ready)

    def sample(self):
        """通过当前会话 RPC 读取快照，五秒内无应答立即停止。"""
        self.last_snapshot = self.rpc('snapshot')
        return self.last_snapshot

    def rpc(self, command, parameters=None):
        """参数 command、parameters 为已登记探针方法及参数；复用同一会话并设置应答超时。"""
        if time.monotonic() - self.start_time > 90:
            raise RuntimeError('整轮超过90秒预算，停止操作')
        self.sequence += 1
        self.process.stdin.write(json.dumps({'id': self.sequence, 'command': command, 'parameters': parameters or {}}) + '\n')
        self.process.stdin.flush()
        response = self.events.get(timeout=5)
        if response.get('id') != self.sequence or 'snapshot' not in response:
            raise RuntimeError(f'只读采样失败：{response}')
        return response['snapshot']

    def wait(self, page, cursor=None):
        """参数 page、cursor 为期望页面及可选父页光标；连续三次满足且无挂起切页才通过。"""
        deadline, stable, last_key = time.monotonic() + self.options.timeout, 0, None
        last_emit = 0
        while time.monotonic() < deadline:
            snapshot = self.sample()
            key = json.dumps({k: v for k, v in snapshot.items() if k != 'timestamp'}, sort_keys=True)
            if key != last_key or time.monotonic() - last_emit >= 1:
                self.record('只读快照', 数据=snapshot)
                last_emit = time.monotonic()
                last_key = key
            settled = page in ('主页', '歌曲') or (snapshot['transition'] == 1 and snapshot['target'] == snapshot['head'])
            match = snapshot['page'] == page and settled and (cursor is None or snapshot['parentCursor'] == cursor)
            if page == '模型':
                match = match and snapshot.get('module', {}).get('id') is not None
            stable = stable + 1 if match else 0
            if stable >= 3:
                return snapshot
            time.sleep(0.15)
        raise RuntimeError(f'页面断言超时：预期 {page}，父页光标 {cursor}，实际 {self.last_snapshot}')

    def capture(self, name):
        """参数 name 为阶段名；复用 NativeCapture 并限制等待，图片仍须最终打开核对。"""
        output = self.output / f'{name}.png'
        begin = utc_now()
        result = subprocess.run([str(ROOT / 'build/release/NativeCapture.exe'), str(self.options.pid), str(output)],
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=8)
        if result.returncode or not output.is_file():
            raise RuntimeError(f'阶段截图失败：{result.stdout} {result.stderr}')
        self.record('截图', 阶段=name, 开始=begin, 完成=utc_now(), 文件=str(output), 输出=result.stdout.strip())
        return output.name

    def stage(self, name, page, key=None, cursor=None):
        """参数 name、page、key、cursor 为阶段标识、目标页、单键及可选光标断言。"""
        if key:
            before = self.sample()
            # 参数 before、page、key 描述真实来源、目标及单键，防止 Enter 落到服装更换菜单。
            self.validate_input(before, page, key)
            self.record('输入前', 阶段=name, 按键=key, 数据=before)
            result = subprocess.run([str(self.options.ahk), '/ErrorStdOut=UTF-8',
                                     str(Path(__file__).with_name('diva_key.ahk')), str(self.options.pid), key],
                                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=8)
            self.record('输入后', 阶段=name, 按键=key, 返回码=result.returncode,
                        输出=result.stdout.strip(), 错误=result.stderr.strip())
            if result.returncode:
                raise RuntimeError(f'AutoHotkey 输入失败：{result.stdout} {result.stderr}')
        snapshot = self.wait(page, cursor)
        time.sleep(0.45)
        picture = self.capture(name)
        # 截图后再独立读回，避免截图期间离页仍被记为通过。
        after = self.wait(page, cursor)
        self.stages.append({'阶段': name, '结果': '通过', '期望页面': page, '按键': key,
                            '快照': snapshot, '截图后快照': after, '截图': picture})
        self.record('阶段通过', 阶段=name, 页面=page)
        print(f'阶段通过：{name} → {page}', flush=True)
        return after

    @staticmethod
    def validate_input(before, destination, key):
        """参数 before、destination、key 为新鲜快照、目标页和单键；只允许已验证浏览边。"""
        source = before['page']
        if source not in ('歌曲', '主页') and (before.get('transition') != 1 or before.get('target') != before.get('head')):
            raise RuntimeError('输入前仍在页面过渡中，停止操作')
        allowed = (key == 'R' and source == '歌曲' and destination == '自定义父页')
        allowed |= (key == 'Esc' and ((source == '自定义父页' and destination == '歌曲')
                    or (source in ('模型', '饰品') and destination == '自定义父页')))
        allowed |= (key in ('Up', 'Down') and source == destination == '自定义父页')
        allowed |= (key == 'Enter' and source == '自定义父页'
                    and {0: '模型', 2: '饰品'}.get(before.get('parentCursor')) == destination)
        allowed |= (key == 'F3' and {'模型': '发型', '发型': '饰品', '饰品': '模型'}.get(source) == destination)
        allowed |= (key == 'Esc' and source == '歌曲' and destination == '主页')
        allowed |= (key in ('Up', 'Down') and source == destination == '主页')
        if not allowed:
            raise RuntimeError(f'拒绝未经验证的按键路径：{source} / {key} / {destination}')

    def parent_cursor(self, destination):
        """参数 destination 为父页目标光标；每移动一次都先核对读回，禁止盲目连按。"""
        current = self.wait('自定义父页')['parentCursor']
        if current not in range(6) or destination not in range(6):
            raise RuntimeError('父页光标超出已知范围')
        while current != destination:
            following = current + (1 if current < destination else -1)
            self.stage(f'光标-{len(self.stages):02d}-{following}', '自定义父页',
                       'Down' if following > current else 'Up', following)
            current = following

    def run(self):
        """先允许从已认证父页归一化到歌曲，再执行最小闭环或七阶段完整浏览。"""
        initial = self.sample()
        self.record('起点', 数据=initial)
        if self.options.flow == 'inspect':
            self.capture('00-只读起点')
            return
        if initial['page'] == '自定义父页':
            self.stage('准备-父页返回歌曲', '歌曲', 'Esc')
        elif initial['page'] != '歌曲':
            raise RuntimeError('起点仅支持已认证歌曲列表或自定义父页；未知起点停止')
        self.stage('01-歌曲列表', '歌曲')
        self.stage('02-自定义父页', '自定义父页', 'R')
        if self.options.flow == 'minimal':
            self.stage('03-返回歌曲列表', '歌曲', 'Esc')
            return
        self.parent_cursor(0)
        self.stage('03-模型子页', '模型', 'Enter')
        if self.options.flow == 'cycle':
            self.stage('04-F3发型', '发型', 'F3')
            self.stage('05-F3饰品', '饰品', 'F3')
            self.stage('06-F3模型', '模型', 'F3')
            self.stage('07-返回父页', '自定义父页', 'Esc')
            self.stage('08-返回歌曲', '歌曲', 'Esc')
            return
        self.stage('04-返回自定义父页', '自定义父页', 'Esc')
        self.parent_cursor(2)
        self.stage('05-饰品子页', '饰品', 'Enter')
        self.stage('06-返回自定义父页', '自定义父页', 'Esc')
        self.stage('07-返回歌曲列表', '歌曲', 'Esc')

    def close(self, failure=None):
        """参数 failure 为可空失败原因；失败只截图与记录，不继续按键或强制恢复游戏。"""
        if failure:
            self.record('实验停止', 原因=failure, 最后快照=self.last_snapshot)
            try: self.capture('失败-停止现场')
            except Exception as error: self.record('失败现场截图异常', 原因=str(error))
        cleanup = '未启动会话'
        if self.process:
            try:
                if self.process.poll() is None:
                    self.process.stdin.write('{"command":"close"}\n')
                    self.process.stdin.flush()
                    self.process.stdin.close()
                    self.process.wait(timeout=10)
                cleanup = '已关闭并分离' if self.process.returncode == 0 else '工作进程异常退出，需检查清理'
            except Exception as error:
                cleanup = f'分离未确认：{error}'
        result = {'结果': '失败' if failure else '通过', '流程': self.options.flow, '结束时间': utc_now(),
                  '原因': failure, 'Frida清理': cleanup, '阶段': self.stages,
                  '说明': ('扩展探针流程；是否调用原生接口及写入字段以事件记录为准。' if getattr(self.options, 'agent', None)
                           else '只读 Frida + AHK 浏览。') + '页面断言与截图人工复核分开记录。'}
        (self.output / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        self.record('结束', 结果=result['结果'], 清理=cleanup)
        self.log.close()


def main():
    """解析带中文帮助的运行参数；任一阶段失败返回非零退出码。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True, help='本次实际运行的 DIVA 进程号')
    parser.add_argument('--flow', choices=['inspect', 'minimal', 'full', 'cycle'], default='minimal', help='只读、最小闭环、七阶段或F3循环流程')
    # 输出目录参数：每次运行创建独立目录，保存逐阶段截图、时间轴与最终结果，不覆盖旧实验。
    parser.add_argument('--output', type=Path, required=True, help='本轮截图和 JSON 日志的独立输出目录，必须尚不存在')
    parser.add_argument('--ahk', type=Path, required=True, help='现有 AutoHotkey v2 可执行文件的完整路径')
    parser.add_argument('--timeout', type=float, default=12, help='每个页面断言最多等待的秒数')
    options = parser.parse_args()
    experiment = Experiment(options)
    failure = None
    try:
        experiment.start()
        experiment.run()
    except Exception as error:
        failure = str(error)
        print(f'实验停止：{failure}', flush=True)
    finally:
        experiment.close(failure)
    print(f'实验结果已保存：{experiment.output}', flush=True)
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
