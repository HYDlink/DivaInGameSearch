# 在本项目内执行曲库审计、真实 DLL 回调与原生截图验证。
param(
  # 本机游戏目录，只读扫描配置和元数据，不修改或启动游戏。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
New-Item -ItemType Directory -Force artifacts | Out-Null
& './build/release/SearchTests.exe' $projectRoot | Tee-Object -FilePath artifacts/core-tests.log
if($LASTEXITCODE -ne 0){ throw '核心验证失败。' }
& './build/release/InputCheck.exe' $projectRoot | Tee-Object -FilePath artifacts/input-tests.log
if($LASTEXITCODE -ne 0){ throw '输入协议验证失败。' }
& './build/release/CatalogCheck.exe' $projectRoot $GameDirectory 'artifacts/catalog-audit.tsv' | Tee-Object -FilePath artifacts/catalog-summary.log
if($LASTEXITCODE -ne 0){ throw '曲库审计失败。' }
& './build/release/DllCheck.exe' (Join-Path $projectRoot 'build/release/DivaInGameSearch.dll') | Tee-Object -FilePath artifacts/dll-verification.log
if($LASTEXITCODE -ne 0){ throw '实际 DLL 回调验证失败。' }
$arguments='--smoke --home "'+$projectRoot+'" --game "'+$GameDirectory+'" --output "'+(Join-Path $projectRoot 'artifacts/native-preview.png')+'"'
$process=Start-Process -FilePath (Join-Path $projectRoot 'build/release/SearchPreview.exe') -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(60000)){ throw '原生预览验证超时；保留进程以便诊断，没有强制结束。' }
if($process.ExitCode -ne 0){ throw '原生预览截图验证失败。' }
Get-Content -Encoding UTF8 artifacts/preview-verification.log
Write-Output '自动验证完成；真实游戏中的 IME 候选窗和选曲仍须单独验收。'
