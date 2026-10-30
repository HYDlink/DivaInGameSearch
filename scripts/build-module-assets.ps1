# 使用 scripts/build.ps1 已准备的 MSVC 和 Windows SDK 环境构建 NativeAOT CPK 桥接。
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot

if(-not (Get-Command dotnet -ErrorAction SilentlyContinue)){ throw '未找到 .NET SDK 8；官方 CPK 读取桥接无法构建。' }
if(-not $env:INCLUDE -or -not $env:LIB){ throw '请从 scripts/build.ps1 调用本脚本，以复用 MSVC PATH、INCLUDE 和 LIB。' }

$project=Join-Path $projectRoot 'third_party/vendor/mml-cpk/AotBridge/MmlCpkBridge.csproj'
$output=Join-Path $projectRoot 'build/release/module-assets'
$nativeOutput=Join-Path $projectRoot 'build/release'
$cacheInfo=& dotnet nuget locals global-packages --list
$packageCache=($cacheInfo -split ':\s*',2)[1]
if(-not $packageCache -or -not (Test-Path -LiteralPath $packageCache)){ throw '未找到 NuGet 包缓存；请先配置 MikuMikuLibrary NativeAOT 构建依赖。' }
New-Item -ItemType Directory -Force $output | Out-Null
New-Item -ItemType Directory -Force $nativeOutput | Out-Null
Write-Output '正在使用固定的 MikuMikuLibrary NativeAOT 桥接读取官方 CPK……'
& dotnet restore $project -r win-x64 -p:PublishAot=true -p:NativeLib=Shared -p:IlcUseEnvironmentalTools=true --source $packageCache
if($LASTEXITCODE -ne 0){
  Write-Output '本地 NuGet 缓存缺少构建包，尝试使用已配置的在线 NuGet 源……'
  & dotnet restore $project -r win-x64 -p:PublishAot=true -p:NativeLib=Shared -p:IlcUseEnvironmentalTools=true
  if($LASTEXITCODE -ne 0){ throw 'NativeAOT 依赖还原失败。' }
}
& dotnet publish $project -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:NativeLib=Shared -p:IlcUseEnvironmentalTools=true --no-restore -o $output
if($LASTEXITCODE -ne 0){ throw 'MikuMikuLibrary NativeAOT 桥接构建失败。' }
$bridge=Join-Path $output 'MmlCpkBridge.dll'
if(-not (Test-Path -LiteralPath $bridge)){ throw 'NativeAOT 未生成 MmlCpkBridge.dll。' }
Copy-Item -LiteralPath $bridge -Destination (Join-Path $nativeOutput 'MmlCpkBridge.dll') -Force
Write-Output 'MikuMikuLibrary NativeAOT CPK／精灵桥接已复制到 build/release，与宿主 DLL 同目录。'
