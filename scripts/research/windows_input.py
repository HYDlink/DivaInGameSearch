"""Windows 输入与只读权限诊断；不更改令牌、沙箱或系统安全设置。"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import time
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

user = c.WinDLL('user32', use_last_error=True)
kernel = c.WinDLL('kernel32', use_last_error=True)
advapi = c.WinDLL('advapi32', use_last_error=True)
user.GetForegroundWindow.restype = w.HWND
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.SetForegroundWindow.argtypes = [w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]
user.GetWindowTextLengthW.argtypes = [w.HWND]
user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.ShowWindow.argtypes = [w.HWND, c.c_int]
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
advapi.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
advapi.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
advapi.GetSidSubAuthorityCount.argtypes = [c.c_void_p]
advapi.GetSidSubAuthorityCount.restype = c.POINTER(c.c_ubyte)
advapi.GetSidSubAuthority.argtypes = [c.c_void_p, w.DWORD]
advapi.GetSidSubAuthority.restype = c.POINTER(w.DWORD)


class Keyboard(c.Structure):
    """键盘事件字段按 Win64 KEYBDINPUT 定义，额外信息使用指针宽度。"""
    _fields_ = [('vk', w.WORD), ('scan', w.WORD), ('flags', w.DWORD),
                ('time', w.DWORD), ('extra', c.c_size_t)]


class Mouse(c.Structure):
    """完整鼠标布局仅用于保证 INPUT 联合体尺寸，不发送鼠标事件。"""
    _fields_ = [('dx', w.LONG), ('dy', w.LONG), ('data', w.DWORD),
                ('flags', w.DWORD), ('time', w.DWORD), ('extra', c.c_size_t)]


class Payload(c.Union):
    """Windows INPUT 数据联合体。"""
    _fields_ = [('keyboard', Keyboard), ('mouse', Mouse)]


class Input(c.Structure):
    """Windows INPUT，64 位平台必须为 40 字节。"""
    _fields_ = [('kind', w.DWORD), ('payload', Payload)]


user.SendInput.argtypes = [w.UINT, c.POINTER(Input), c.c_int]
user.SendInput.restype = w.UINT
ENUMPROC = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [ENUMPROC, w.LPARAM]


def process_info(pid):
    """参数 pid 为目标进程号；只读查询路径、完整性、提升状态及会话。"""
    result = {'进程号': pid}
    process = kernel.OpenProcess(0x1000, False, pid)
    if not process:
        return {**result, '错误': f'无法只读打开进程，错误码 {c.get_last_error()}'}
    token = w.HANDLE()
    try:
        length = w.DWORD(32768)
        name = c.create_unicode_buffer(length.value)
        if kernel.QueryFullProcessImageNameW(process, 0, name, c.byref(length)):
            result['路径'] = name.value
        session = w.DWORD()
        if kernel.ProcessIdToSessionId(pid, c.byref(session)):
            result['会话'] = session.value
        if not advapi.OpenProcessToken(process, 8, c.byref(token)):
            raise RuntimeError(f'令牌只读查询失败，错误码 {c.get_last_error()}')
        for kind, label in [(20, '已提升'), (26, 'UIAccess')]:
            value, length = w.DWORD(), w.DWORD()
            if advapi.GetTokenInformation(token, kind, c.byref(value), c.sizeof(value), c.byref(length)):
                result[label] = bool(value.value)
        length = w.DWORD()
        advapi.GetTokenInformation(token, 25, None, 0, c.byref(length))
        buffer = c.create_string_buffer(length.value)
        if advapi.GetTokenInformation(token, 25, buffer, length, c.byref(length)):
            sid = c.cast(buffer, c.POINTER(c.c_void_p))[0]
            count = advapi.GetSidSubAuthorityCount(sid)[0]
            rid = advapi.GetSidSubAuthority(sid, count - 1)[0]
            result['完整性RID'] = rid
            result['完整性'] = ('系统' if rid >= 0x4000 else '高' if rid >= 0x3000
                                else '中' if rid >= 0x2000 else '低')
        return result
    finally:
        if token: kernel.CloseHandle(token)
        kernel.CloseHandle(process)


def find_window(pid):
    """参数 pid 指定目标；要求该进程只有一个可见顶层窗口。"""
    matches = []

    # 参数 hwnd、unused 由系统枚举回调提供；只收集目标进程的可见窗口。
    @ENUMPROC
    def collect(hwnd, unused):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            text = c.create_unicode_buffer(user.GetWindowTextLengthW(hwnd) + 1)
            user.GetWindowTextW(hwnd, text, len(text))
            matches.append((hwnd, text.value))
        return True

    if not user.EnumWindows(collect, 0):
        raise RuntimeError('窗口枚举失败')
    if len(matches) != 1:
        raise RuntimeError(f'需要唯一可见目标窗口，实际 {len(matches)} 个')
    return matches[0]


def activate(pid):
    """参数 pid 为授权目标进程；激活后核对前台进程，不绕过前台限制。"""
    info = process_info(pid)
    if Path(info.get('路径', '')).name.lower() not in ('divamegamix.exe', 'notepad3.exe', 'notepad.exe'):
        raise RuntimeError('输入仅允许已授权的 DIVA 或记事本进程')
    hwnd, title = find_window(pid)
    user.ShowWindow(hwnd, 9)
    user.SetForegroundWindow(hwnd)
    time.sleep(0.35)
    foreground = w.DWORD()
    user.GetWindowThreadProcessId(user.GetForegroundWindow(), c.byref(foreground))
    if foreground.value != pid:
        raise RuntimeError('目标未取得前台焦点，停止发送输入')
    return {'窗口': int(hwnd), '标题': title, '前台进程': foreground.value}


def key_event(vk, release=False):
    """参数 vk、release 为虚拟键码和抬起标记；发送扫描码键盘事件并核对返回数量。"""
    scan = user.MapVirtualKeyW(vk, 4)
    event = Input(kind=1)
    event.payload.keyboard = Keyboard(0, scan & 0xFF,
        8 | (1 if scan & 0xFF00 else 0) | (2 if release else 0), 0, 0)
    if user.SendInput(1, c.byref(event), c.sizeof(event)) != 1:
        raise RuntimeError(f'SendInput 未提交事件，错误码 {c.get_last_error()}；可能被权限阻止')


def press(pid, key, hold=0.15):
    """参数 pid、key、hold 为目标、白名单按键及按住秒数；始终释放本次按下的键。"""
    keys = {'Enter': 13, 'Esc': 27, 'Up': 38, 'Down': 40, 'Left': 37, 'Right': 39,
            'R': 82, '1': 49, '2': 50, '3': 51, 'A': 65, 'B': 66, 'C': 67, 'Shift': 16}
    if key not in keys or not 0.05 <= hold <= 0.3:
        raise RuntimeError('按键或持续时间不在实验白名单中')
    activate(pid)
    key_event(keys[key])
    try: time.sleep(hold)
    finally: key_event(keys[key], True)
    time.sleep(0.2)


def main():
    """解析中文说明的诊断及独立按键参数，不保存记事本文档。"""
    parser = argparse.ArgumentParser(description=__doc__)
    # 输出参数指向 UTF-8 JSON 诊断文件，仅写指定研究产物目录。
    parser.add_argument('--output', type=Path, help='权限诊断结果 JSON 输出文件')
    parser.add_argument('--pids', nargs='+', type=int, help='需要只读检查的进程号')
    parser.add_argument('--pid', type=int, help='接收单次授权按键的目标进程号')
    parser.add_argument('--key', help='单次测试按键：Enter、Esc、上下左右、R、数字或 ABC')
    options = parser.parse_args()
    if options.pids:
        result = [process_info(pid) for pid in [os.getpid(), *options.pids]]
        text = json.dumps(result, ensure_ascii=False, indent=2)
        if options.output:
            options.output.parent.mkdir(parents=True, exist_ok=True)
            options.output.write_text(text, encoding='utf-8')
        print(text)
    if options.pid and options.key:
        press(options.pid, options.key)
        print('单次键盘事件已提交；实际效果必须由截图及页面状态另行确认。')


if __name__ == '__main__':
    try: main()
    except Exception as error:
        print(f'实验停止：{error}')
        raise SystemExit(1)
