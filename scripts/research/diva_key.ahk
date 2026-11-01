#Requires AutoHotkey v2.0
#SingleInstance Off
; 参数依次为游戏进程号、白名单按键；一次执行仅发送一个按键，调用方验证页面。
try {
    if A_Args.Length != 2
        throw Error('必须提供游戏进程号和单次按键')
    targetPid := Integer(A_Args[1])
    targetKey := A_Args[2]
    allowed := Map('Esc', 'sc001', 'Enter', 'sc01C', 'R', 'sc013',
        'Up', 'sc148', 'Down', 'sc150', 'Left', 'sc14B', 'Right', 'sc14D', 'F3', 'sc03D',
        'F4', 'sc03E', 'Q', 'sc010', 'E', 'sc012')
    if !allowed.Has(targetKey)
        throw Error('按键不在实验白名单')
    if StrLower(ProcessGetName(targetPid)) != 'divamegamix.exe'
        throw Error('目标不是指定的 DIVA 游戏')
    windows := WinGetList('ahk_pid ' targetPid)
    if windows.Length != 1
        throw Error('游戏窗口不唯一，停止输入')
    target := 'ahk_id ' windows[1]
    WinActivate(target)
    if !WinWaitActive(target, , 3)
        throw Error('游戏没有取得前台焦点')
    Sleep(300)
    SetKeyDelay(30, 150)
    scan := allowed[targetKey]
    ; 使用真实键盘事件按下并保持 150 毫秒，避免游戏按帧轮询漏掉极短按键。
    SendEvent('{' scan ' down}')
    try Sleep(150)
    finally SendEvent('{' scan ' up}')
    FileAppend('已向游戏发送单次键盘事件；须独立核对页面。`n', '*', 'UTF-8')
    ExitApp(0)
} catch as failure {
    FileAppend('输入停止：' failure.Message '`n', '*', 'UTF-8')
    ExitApp(1)
}
