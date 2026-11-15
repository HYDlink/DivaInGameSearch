# 用共用原生界面为每套主题和页面各截一张图，对比珍珠白与原深色配色；只读游戏目录，不启动或修改游戏。
param(
  # 本机游戏目录，只读扫描配置和元数据。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory,
  # 界面语言，默认简中。
  [string]$Language='zh-CN'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$outputRoot=Join-Path $projectRoot 'artifacts/themes'
New-Item -ItemType Directory -Force $outputRoot | Out-Null
$pages=[ordered]@{ songs=''; modules='--customization'; hair='--hair'; accessories='--accessories' }
foreach($theme in 'pearl','classic'){
  foreach($page in $pages.Keys){
    $output=Join-Path $outputRoot "$theme-$page.png"
    $arguments='--smoke --theme '+$theme+' --language '+$Language+' '+$pages[$page]+' --home "'+$projectRoot+'" --game "'+$GameDirectory+'" --output "'+$output+'"'
    $process=Start-Process -FilePath (Join-Path $projectRoot 'build/release/SearchPreview.exe') -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(120000)){ throw "预览超时：$theme $page；保留进程以便诊断，没有强制结束。" }
    if($process.ExitCode -ne 0){ throw "预览失败：$theme $page，退出码 $($process.ExitCode)。" }
    Write-Output "已生成 $output"
  }
}
Write-Output '主题预览完成。'
