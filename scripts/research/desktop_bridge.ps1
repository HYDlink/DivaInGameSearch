# DIVA 桌面桥接：由用户在桌面手动启动，替 SSH（会话 0）执行固定动作；关闭窗口即停止。
# 只接受 status、capture、key、experiment 四种动作，不执行任意命令，也不修改系统设置。
$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Base = Join-Path $Root 'artifacts\desktop-bridge'
$Queue = Join-Path $Base 'queue'
$Results = Join-Path $Base 'results'
$Runs = Join-Path $Base 'runs'
$Heartbeat = Join-Path $Base 'bridge.json'
$Ahk = Join-Path $env:USERPROFILE 'scoop\apps\AutoHotkey-Portable\2.0.17\AutoHotkey64.exe'
$Capture = Join-Path $Root 'build\release\NativeCapture.exe'
$KeyScript = Join-Path $Root 'scripts\research\diva_key.ahk'
$ExperimentScript = Join-Path $Root 'scripts\research\run_page_experiment.py'
$Utf8 = New-Object Text.UTF8Encoding($false)
$NamePattern = '^[A-Za-z0-9_-]{1,64}$'
# Enter 可能触发更换或购买，只能经由带页面断言的实验流程发送。
$AllowedKeys = @('Esc', 'R', 'Up', 'Down', 'Left', 'Right', 'Q', 'E', 'F3', 'F4')
$AllowedFlows = @('inspect', 'minimal', 'full')

Add-Type -Namespace DivaBridge -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
'@

# 以先写临时文件再改名的方式写入 JSON。参数 Path 为目标文件，Value 为要序列化的对象。
function Write-JsonFile([string]$Path, $Value) {
    $temp = "$Path.tmp"
    [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 5), $Utf8)
    Move-Item -LiteralPath $temp -Destination $Path -Force
}

# 查询本次运行的唯一游戏进程号；没有或不唯一时报错，不猜测目标。
function Get-GamePid {
    $games = @(Get-Process DivaMegaMix -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw "需要恰好一个 DivaMegaMix 进程，当前为 $($games.Count) 个" }
    return $games[0].Id
}

# 运行固定工具并限时等待。参数 File 为可执行文件，Arguments 为已校验的参数，TimeoutSec 为时限秒数。
function Invoke-Tool([string]$File, [string[]]$Arguments, [int]$TimeoutSec) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $File
    $info.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $info.WorkingDirectory = $Root
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $Utf8
    $info.StandardErrorEncoding = $Utf8
    $info.EnvironmentVariables['PYTHONUTF8'] = '1'
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSec * 1000)
    if (-not $finished) {
        # 只结束本工具及其子进程；游戏不是它的子进程，不受影响。
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        $process.WaitForExit(5000) | Out-Null
    }
    $out = ''; $err = ''
    if ($stdout.Wait(2000)) { $out = $stdout.Result }
    if ($stderr.Wait(2000)) { $err = $stderr.Result }
    $code = $null
    if ($finished) { $code = $process.ExitCode }
    return @{
        ok = ($finished -and $code -eq 0)
        timedOut = (-not $finished)
        exitCode = $code
        stdout = $out.Substring(0, [Math]::Min(8000, $out.Length))
        stderr = $err.Substring(0, [Math]::Min(8000, $err.Length))
    }
}

# 汇报桥接所在会话、游戏进程和当前前台进程，供调用方判断能否操作。
function Get-BridgeStatus {
    $games = @(Get-Process DivaMegaMix -ErrorAction SilentlyContinue)
    $foregroundPid = [uint32]0
    [DivaBridge.Native]::GetWindowThreadProcessId([DivaBridge.Native]::GetForegroundWindow(), [ref]$foregroundPid) | Out-Null
    $foreground = Get-Process -Id $foregroundPid -ErrorAction SilentlyContinue
    return @{
        ok = $true
        session = (Get-Process -Id $PID).SessionId
        gamePids = @($games | ForEach-Object { $_.Id })
        foregroundProcess = $(if ($foreground) { $foreground.ProcessName } else { $null })
        ahk = (Test-Path -LiteralPath $Ahk)
        nativeCapture = (Test-Path -LiteralPath $Capture)
    }
}

# 按动作名分发任务。参数 Id 为任务号，Job 为任务对象（action、value）。
function Invoke-Job([string]$Id, $Job) {
    switch ([string]$Job.action) {
        'status' { return Get-BridgeStatus }
        'capture' {
            $output = Join-Path $Runs "$Id.png"
            $result = Invoke-Tool $Capture @([string](Get-GamePid), $output) 15
            $result.image = $output
            return $result
        }
        'key' {
            if ($AllowedKeys -cnotcontains [string]$Job.value) { throw "按键不在桥接白名单：$($Job.value)" }
            return Invoke-Tool $Ahk @('/ErrorStdOut=UTF-8', $KeyScript, [string](Get-GamePid), [string]$Job.value) 15
        }
        'experiment' {
            if ($AllowedFlows -cnotcontains [string]$Job.value) { throw "流程不在桥接白名单：$($Job.value)" }
            $output = Join-Path $Runs $Id
            $python = (Get-Command python).Source
            $result = Invoke-Tool $python @($ExperimentScript, '--pid', [string](Get-GamePid), '--flow', [string]$Job.value, '--output', $output, '--ahk', $Ahk) 240
            $result.output = $output
            return $result
        }
        default { throw "未知动作：$($Job.action)" }
    }
}

if ((Get-Process -Id $PID).SessionId -eq 0) { throw '桥接必须在桌面窗口中启动，不能在 SSH 里运行' }
$mutex = New-Object Threading.Mutex($false, 'Local\DivaDesktopBridge')
if (-not $mutex.WaitOne(0)) { throw '已有一个桥接在运行' }
foreach ($directory in @($Queue, $Results, $Runs)) { New-Item -ItemType Directory -Force $directory | Out-Null }
# 启动时清掉上次遗留的任务，避免旧按键在新一轮被执行。
Get-ChildItem -LiteralPath $Queue -File | Remove-Item -Force

Write-Host "DIVA 桌面桥接已启动（会话 $((Get-Process -Id $PID).SessionId)）。关闭此窗口即停止。"
$lastBeat = [DateTime]::MinValue
while ($true) {
    if (([DateTime]::UtcNow - $lastBeat).TotalSeconds -ge 2) {
        Write-JsonFile $Heartbeat @{ pid = $PID; session = (Get-Process -Id $PID).SessionId; utc = [DateTime]::UtcNow.ToString('o') }
        $lastBeat = [DateTime]::UtcNow
    }
    $file = Get-ChildItem -LiteralPath $Queue -Filter '*.json' -File | Sort-Object Name | Select-Object -First 1
    if (-not $file) { Start-Sleep -Milliseconds 300; continue }
    $id = $file.BaseName
    $job = $null
    try {
        if ($id -notmatch $NamePattern) { throw '任务号不合法' }
        $job = [IO.File]::ReadAllText($file.FullName, $Utf8) | ConvertFrom-Json
        Remove-Item -LiteralPath $file.FullName -Force
        Write-Host "[$(Get-Date -Format HH:mm:ss)] $id $($job.action) $($job.value)"
        $result = Invoke-Job $id $job
    } catch {
        Remove-Item -LiteralPath $file.FullName -Force -ErrorAction SilentlyContinue
        $result = @{ ok = $false; error = $_.Exception.Message }
        Write-Host "  失败：$($_.Exception.Message)"
    }
    Write-JsonFile (Join-Path $Results "$($id -replace '[^A-Za-z0-9_-]', '_').json") $result
}
