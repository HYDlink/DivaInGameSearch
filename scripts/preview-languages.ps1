# 用共用原生界面为每种语言和页面各截一张图，检查布局和文案；只读游戏目录，不启动或修改游戏。
param(
  # 本机游戏目录，只读扫描配置和元数据。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$outputRoot=Join-Path $projectRoot 'artifacts/languages'
New-Item -ItemType Directory -Force $outputRoot | Out-Null
$pages=[ordered]@{ songs=''; modules='--customization'; hair='--hair'; accessories='--accessories' }
foreach($language in 'en','ja','qps','zh-CN'){
  foreach($page in $pages.Keys){
    $output=Join-Path $outputRoot "$language-$page.png"
    $arguments='--smoke --language '+$language+' '+$pages[$page]+' --home "'+$projectRoot+'" --game "'+$GameDirectory+'" --output "'+$output+'"'
    $process=Start-Process -FilePath (Join-Path $projectRoot 'build/release/SearchPreview.exe') -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(120000)){ throw "预览超时：$language $page；保留进程以便诊断，没有强制结束。" }
    if($process.ExitCode -ne 0){ throw "预览失败：$language $page，退出码 $($process.ExitCode)。" }
    Write-Output "已生成 $output"
  }
}
Write-Output '语言预览完成。'
