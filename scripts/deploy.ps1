# 部署本项目自己的独立 Mod，并只在加载列表末尾追加本 Mod。
param(
  # 用户已经确认的游戏安装目录。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
if(Get-Process DivaMegaMix -ErrorAction SilentlyContinue){ throw '游戏正在运行，已停止部署以避免替换已加载的 DLL；没有结束游戏进程。' }
$gameRoot=[IO.Path]::GetFullPath($GameDirectory)
$modsRoot=Join-Path $gameRoot 'mods'
$target=[IO.Path]::GetFullPath((Join-Path $modsRoot 'DivaInGameSearch'))
if([IO.Path]::GetDirectoryName($target) -ne [IO.Path]::GetFullPath($modsRoot)){ throw '目标目录校验失败。' }
if(-not (Test-Path -LiteralPath (Join-Path $gameRoot 'DivaMegaMix.exe'))){ throw '指定目录没有游戏可执行文件。' }
if(Test-Path -LiteralPath $target){ throw '独立 Mod 目录已存在，请先核对现有部署记录，禁止盲目覆盖。' }
$source=Join-Path $projectRoot 'artifacts/release/DivaInGameSearch'
$configPath=Join-Path $gameRoot 'config.toml'
$original=[IO.File]::ReadAllText($configPath,[Text.Encoding]::UTF8)
$originalHash=(Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash
$pattern='(?ms)^priority\s*=\s*\[(?<items>.*?)\]'
$matchesFound=[regex]::Matches($original,$pattern)
if($matchesFound.Count -ne 1){ throw '加载器 priority 数组不是预期的唯一数组，已停止部署。' }
$match=$matchesFound[0]
$items=$match.Groups['items'].Value
if($items -match '"DivaInGameSearch"'){ throw '加载列表已经存在本 Mod，需先核对残留部署。' }
$newItems=$items.TrimEnd()
if($newItems.Trim().Length -gt 0){ $newItems+=', ' }
$newItems+='"DivaInGameSearch"'
$replacement='priority = ['+$newItems+']'
$updated=$original.Substring(0,$match.Index)+$replacement+$original.Substring($match.Index+$match.Length)
$timestamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $projectRoot "artifacts/deployment/$timestamp"
New-Item -ItemType Directory -Force $backup | Out-Null
Copy-Item -LiteralPath $configPath -Destination (Join-Path $backup 'config.toml.before')
[IO.File]::WriteAllText((Join-Path $backup 'config.toml.after'),$updated,[Text.UTF8Encoding]::new($false))
$files=@('DivaInGameSearch.dll','config.toml','settings.ini','aliases.toml','data/base_pv_db.txt','data/dlc_pv_db.txt')
$stage=Join-Path $modsRoot "DivaInGameSearch.deploy-$timestamp"
if(Test-Path -LiteralPath $stage){ throw '暂存目录已存在，已停止部署。' }
New-Item -ItemType Directory -Path $stage | Out-Null
try{
  foreach($relative in $files){
    $inputFile=Join-Path $source $relative
    $outputFile=Join-Path $stage $relative
    New-Item -ItemType Directory -Force (Split-Path $outputFile -Parent) | Out-Null
    Copy-Item -LiteralPath $inputFile -Destination $outputFile
    if((Get-FileHash -LiteralPath $inputFile).Hash -ne (Get-FileHash -LiteralPath $outputFile).Hash){ throw "部署文件校验失败：$relative" }
  }
  if((Get-FileHash -LiteralPath $configPath).Hash -ne $originalHash){ throw '加载器配置在部署过程中发生变化，已停止修改。' }
  Move-Item -LiteralPath $stage -Destination $target
  [IO.File]::WriteAllText($configPath,$updated,[Text.UTF8Encoding]::new($false))
  $deployedHash=(Get-FileHash -LiteralPath $configPath).Hash
  $record=[ordered]@{ 时间=(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'); 游戏目录=$gameRoot; 独立Mod目录=$target; 配置备份=(Join-Path $backup 'config.toml.before'); 部署前配置哈希=$originalHash; 部署后配置哈希=$deployedHash; 部署DLL哈希=(Get-FileHash (Join-Path $target 'DivaInGameSearch.dll')).Hash; 文件=$files; 状态='已部署并追加启用，尚未实机验收' }
  $record | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $backup 'deployment.json')
  Write-Output "已部署并启用：$target"
  Write-Output "配置备份与部署记录：$backup"
  Write-Output '原有曲包顺序保持不变，只追加 DivaInGameSearch。默认快捷键：Ctrl+F。'
}catch{
  # 仅恢复本次确实写入的加载器配置，不覆盖其他进程的新配置。
  if((Test-Path -LiteralPath $target) -and [IO.File]::ReadAllText($configPath) -eq $updated){ Copy-Item -LiteralPath (Join-Path $backup 'config.toml.before') -Destination $configPath -Force }
  throw
}
