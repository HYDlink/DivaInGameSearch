# 使用本机 MSVC 构建；全部缓存和输出保存在当前项目。
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
New-Item -ItemType Directory -Force (Join-Path $projectRoot 'build/tmp') | Out-Null
$env:TEMP=Join-Path $projectRoot 'build/tmp'
$env:TMP=$env:TEMP
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){ $installation=& $vswhere -latest -products '*' -property installationPath }
if(-not $installation){ throw '未找到 MSVC C++ 工具链。' }
$env:VSLANG='2052'
$toolset=Get-ChildItem (Join-Path $installation 'VC/Tools/MSVC') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'lib/x64/libcmt.lib') } | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot='C:/Program Files (x86)/Windows Kits/10'
$sdk=Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if(-not $toolset -or -not $sdk){ throw '未找到完整 MSVC 或 Windows SDK。' }
$env:PATH=(Join-Path $toolset.FullName 'bin/Hostx64/x64')+';'+(Join-Path $sdkRoot "bin/$($sdk.Name)/x64")+';'+$env:PATH
$env:INCLUDE=(Join-Path $toolset.FullName 'include')+';'+(($('ucrt','shared','um','winrt','cppwinrt') | ForEach-Object { Join-Path $sdk.FullName $_ }) -join ';')
$env:LIB=(Join-Path $toolset.FullName 'lib/x64')+';'+(Join-Path $sdkRoot "Lib/$($sdk.Name)/ucrt/x64")+';'+(Join-Path $sdkRoot "Lib/$($sdk.Name)/um/x64")
Write-Output '正在配置本地 C++ 工程……'
& cmake -S . -B build/release -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){ throw '工程配置失败。' }
Write-Output '正在构建原生 DLL、共用界面预览和验证程序……'
& cmake --build build/release --config Release
if($LASTEXITCODE -ne 0){ throw '原生构建失败。' }
Write-Output '正在运行核心自动验收……'
& ctest --test-dir build/release -C Release --output-on-failure
if($LASTEXITCODE -ne 0){ throw '自动验收失败。' }
Write-Output '构建与核心自动验收完成。'
