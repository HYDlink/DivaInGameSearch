# 更新已部署的本项目 DLL 和版本声明，保留所有用户设置及全局加载配置。
param(
  # 已授权部署的游戏目录。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory,
  # 用户明确要求时仅更新快捷键值，其余设置保持原样。
  [Nullable[int]]$ShortcutVk=$null
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
if(Get-Process DivaMegaMix -ErrorAction SilentlyContinue){ throw '游戏仍在运行，已停止更新，没有强制结束进程。' }
$target=[IO.Path]::GetFullPath((Join-Path $GameDirectory 'mods/DivaInGameSearch'))
if([IO.Path]::GetDirectoryName($target) -ne [IO.Path]::GetFullPath((Join-Path $GameDirectory 'mods'))){ throw '本 Mod 目录校验失败。' }
if(-not (Test-Path -LiteralPath (Join-Path $target 'DivaInGameSearch.dll'))){ throw '没有找到本 Mod 的既有部署。' }
$backup=Join-Path $projectRoot ('artifacts/deployment/update-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$files=@('DivaInGameSearch.dll','MmlCpkBridge.dll','DivaTextureDecoder.dll','config.toml')
$backupFiles=@($files | Where-Object { Test-Path -LiteralPath (Join-Path $target $_) })
$newFiles=@($files | Where-Object { -not (Test-Path -LiteralPath (Join-Path $target $_)) })
if($null -ne $ShortcutVk){ $backupFiles+=@('settings.ini') }
$preserved=@('aliases.toml','settings.ini','favorites.txt')
$originalSettings=@{}
foreach($file in $preserved){
  $path=Join-Path $target $file
  if(Test-Path -LiteralPath $path){ $originalSettings[$file]=(Get-FileHash $path).Hash }
}
$loaderConfig=Join-Path $GameDirectory 'config.toml'
$loaderHash=(Get-FileHash $loaderConfig).Hash
foreach($file in $backupFiles){ Copy-Item -LiteralPath (Join-Path $target $file) -Destination $backup }
if(Test-Path -LiteralPath (Join-Path $target 'search-diagnostics.log')){ Copy-Item -LiteralPath (Join-Path $target 'search-diagnostics.log') -Destination $backup }
try{
  foreach($file in $files){
    $source=Join-Path $projectRoot "artifacts/release/DivaInGameSearch/$file"
    $destination=Join-Path $target $file
    Copy-Item -LiteralPath $source -Destination ($destination+'.new')
    if((Get-FileHash $source).Hash -ne (Get-FileHash ($destination+'.new')).Hash){ throw "更新校验失败：$file" }
    Move-Item -LiteralPath ($destination+'.new') -Destination $destination -Force
  }
  if($null -ne $ShortcutVk){
    $settingsPath=Join-Path $target 'settings.ini'
    $settingsText=[IO.File]::ReadAllText($settingsPath,[Text.Encoding]::UTF8)
    if([regex]::Matches($settingsText,'(?m)^shortcut_vk\s*=.*$').Count -ne 1){ throw '快捷键配置项不是唯一值，已停止更新。' }
    $settingsText=[regex]::Replace($settingsText,'(?m)^shortcut_vk\s*=.*$',('shortcut_vk=0x{0:X2}' -f [int]$ShortcutVk))
    $settingsText=$settingsText.Replace('# Ctrl+K；使用 Win32 虚拟键值，禁止与歌词 Mod 的 Ctrl+F/G/L/S 重复。','# Ctrl+F；用户已迁移卡拉 OK Mod 的原有快捷键，使用 Win32 虚拟键值。')
    [IO.File]::WriteAllText($settingsPath,$settingsText,[Text.UTF8Encoding]::new($false))
    $originalSettings['settings.ini']=(Get-FileHash $settingsPath).Hash
  }
  foreach($file in $originalSettings.Keys){ if((Get-FileHash (Join-Path $target $file)).Hash -ne $originalSettings[$file]){ throw "用户设置意外变更：$file" } }
  if((Get-FileHash $loaderConfig).Hash -ne $loaderHash){ throw '全局加载器配置意外变化。' }
  $record=[ordered]@{ 时间=(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'); 版本='1.0.6'; 部署目录=$target; 备份=$backup; 更新文件=$backupFiles; 快捷键更新=$ShortcutVk; 预期设置哈希=$originalSettings; 全局配置哈希=$loaderHash; DLL哈希=(Get-FileHash (Join-Path $target 'DivaInGameSearch.dll')).Hash; 结果='文件更新与读回校验通过，实际游戏效果待重启复测' }
  $record | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $backup 'update.json')
  Write-Output '已更新本 Mod 至 1.0.6，默认快捷键为 Ctrl+F。'
  Write-Output "旧版 DLL 和诊断备份：$backup"
  Write-Output '别名、收藏、其他界面设置和全局曲包加载配置保持不变。'
}catch{
  foreach($file in $backupFiles){ Copy-Item -LiteralPath (Join-Path $backup $file) -Destination (Join-Path $target $file) -Force }
  # 只回滚本次新增且内容仍匹配本次发行包的文件，避免覆盖外部新修改。
  foreach($file in $newFiles){
    $destination=Join-Path $target $file
    $source=Join-Path $projectRoot "artifacts/release/DivaInGameSearch/$file"
    if((Test-Path -LiteralPath $destination) -and (Test-Path -LiteralPath $source) -and (Get-FileHash -LiteralPath $destination).Hash -eq (Get-FileHash -LiteralPath $source).Hash){ Remove-Item -LiteralPath $destination }
  }
  throw
}
