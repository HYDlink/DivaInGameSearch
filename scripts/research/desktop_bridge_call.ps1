# 从 SSH 向桌面桥接提交一个任务并等待结果。参数 Action 为动作名，Value 为按键或流程名，TimeoutSec 为等待秒数。
param(
    [Parameter(Mandatory = $true)][ValidateSet('status', 'capture', 'key', 'experiment')][string]$Action,
    [string]$Value = '',
    [int]$TimeoutSec = 30
)
$ErrorActionPreference = 'Stop'

$Base = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path 'artifacts\desktop-bridge'
$Heartbeat = Join-Path $Base 'bridge.json'
$Utf8 = New-Object Text.UTF8Encoding($false)

if (-not (Test-Path -LiteralPath $Heartbeat) -or ((Get-Date) - (Get-Item -LiteralPath $Heartbeat).LastWriteTime).TotalSeconds -gt 10) {
    throw '桌面桥接没有运行：请用户在桌面窗口中启动 scripts\research\desktop_bridge.ps1'
}
$id = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $Action
$temp = Join-Path $Base "queue\$id.tmp"
[IO.File]::WriteAllText($temp, (@{ action = $Action; value = $Value } | ConvertTo-Json), $Utf8)
Move-Item -LiteralPath $temp -Destination (Join-Path $Base "queue\$id.json")

$resultPath = Join-Path $Base "results\$id.json"
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while (-not (Test-Path -LiteralPath $resultPath)) {
    if ((Get-Date) -gt $deadline) { throw "等待任务 $id 超时，任务可能仍在执行" }
    Start-Sleep -Milliseconds 200
}
[IO.File]::ReadAllText($resultPath, $Utf8)
