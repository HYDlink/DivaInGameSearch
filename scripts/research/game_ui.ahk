#Requires AutoHotkey v2.0
#SingleInstance Off
; 参数为当前PID、单步操作及坐标/文本；上层实验器先核对页面和截图，不盲发业务确认。
try {
    if A_Args.Length < 2
        throw Error('必须提供当前游戏PID与单步操作')
    targetPid := Integer(A_Args[1])
    operation := A_Args[2]
    if StrLower(ProcessGetName(targetPid)) != 'divamegamix.exe'
        throw Error('目标不是DIVA游戏')
    windows := WinGetList('ahk_pid ' targetPid)
    if windows.Length != 1
        throw Error('游戏窗口不唯一，停止输入')
    target := 'ahk_id ' windows[1]
    if operation == 'close' {
        ; 用户已明确授权正常关闭游戏；只发送窗口关闭，不强制结束进程。
        WinClose(target)
        if !WinWaitClose(target, , 25)
            throw Error('游戏未在时限内正常关闭，保留现场，不强制结束')
    } else {
        WinActivate(target)
        if !WinWaitActive(target, , 3)
            throw Error('游戏未取得前台焦点')
        ; 外部自测等待游戏重新取得输入设备；不把该等待加入产品帧回调。
        Sleep(700)
        SetKeyDelay(30, 100)
        if operation == 'shortcut' {
            SendEvent('{Ctrl down}f{Ctrl up}')
        } else if operation == 'text' {
            if A_Args.Length != 3
                throw Error('文本操作必须提供查询文本')
            SendEvent('^a')
            SendText(A_Args[3])
        } else if operation == 'click' {
            ; 鼠标双击不用键盘的100毫秒按压延迟；否则两次down实际间隔可能超过ImGui双击阈值。
            SetKeyDelay(0, -1)
            SetMouseDelay(-1)
            if A_Args.Length != 5
                throw Error('点击必须提供客户区X、Y与次数')
            x := Integer(A_Args[3]), y := Integer(A_Args[4]), count := Integer(A_Args[5])
            WinGetClientPos(, , &width, &height, target)
            if x < 0 || y < 0 || x >= width || y >= height || (count != 1 && count != 2)
                throw Error('点击坐标或次数超出当前游戏客户区')
            CoordMode('Mouse', 'Client')
            MouseMove(x, y, 0)
            Loop count {
                SendEvent('{LButton down}')
                try Sleep(60)
                finally SendEvent('{LButton up}')
                if A_Index < count
                    Sleep(100)
            }
        } else if operation == 'key' {
            if A_Args.Length != 3
                throw Error('单键操作必须提供按键名称')
            allowed := Map('Esc','sc001','Enter','sc01C','Tab','sc00F','Space','sc039',
                'Up','sc148','Down','sc150','Left','sc14B','Right','sc14D','R','sc013',
                'Q','sc010','E','sc012','F3','sc03D','F4','sc03E')
            if !allowed.Has(A_Args[3])
                throw Error('按键不在自测白名单')
            scan := allowed[A_Args[3]]
            SendEvent('{' scan ' down}')
            try Sleep(200)
            finally SendEvent('{' scan ' up}')
        } else {
            throw Error('未知单步操作')
        }
    }
    FileAppend('单步游戏窗口操作完成；业务结果须独立读回与截图核对。`n','*','UTF-8')
    ExitApp(0)
} catch as failure {
    FileAppend('游戏操作停止：' failure.Message '`n','*','UTF-8')
    ExitApp(1)
}
