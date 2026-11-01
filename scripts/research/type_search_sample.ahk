#Requires AutoHotkey v2.0
#SingleInstance Off
; 参数：当前PID、记录按钮X/Y、查询框X/Y；坐标只可来自已查看的本轮D3D截图。
try {
    if A_Args.Length != 5
        throw Error('必须提供PID、记录按钮与查询框坐标')
    targetPid := Integer(A_Args[1])
    if StrLower(ProcessGetName(targetPid)) != 'divamegamix.exe'
        throw Error('目标不是DIVA游戏')
    windows := WinGetList('ahk_pid ' targetPid)
    if windows.Length != 1
        throw Error('游戏窗口不唯一')
    target := 'ahk_id ' windows[1]
    WinActivate(target)
    if !WinWaitActive(target, , 3)
        throw Error('游戏未取得前台焦点')
    CoordMode('Mouse', 'Client')
    SetKeyDelay(20, 40)
    ; 记录开始后不截图、不附加探针、不写文件，只发送普通搜索字符；不提交定位或演奏。
    MouseClick('Left', Integer(A_Args[2]), Integer(A_Args[3]))
    started := A_TickCount
    Sleep(500)
    MouseClick('Left', Integer(A_Args[4]), Integer(A_Args[5]))
    Sleep(1500)
    SendEvent('^a{Backspace}')
    for character in StrSplit('miku') {
        SendText(character)
        Sleep(450)
    }
    Sleep(700)
    SendEvent('^a{Backspace}')
    Sleep(500)
    for character in StrSplit('project') {
        SendText(character)
        Sleep(100)
    }
    Sleep(700)
    SendEvent('^a{Backspace}')
    Sleep(Max(0, 17000 - (A_TickCount - started)))
    FileAppend('逐字输入采样完成；输入间隔含450毫秒与100毫秒两组，未触发定位。`n','*','UTF-8')
    ExitApp(0)
} catch as failure {
    FileAppend('逐字输入采样停止：' failure.Message '`n','*','UTF-8')
    ExitApp(1)
}
