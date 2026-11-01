# 组装自包含 Mod 与可重建源码；所有输出均在本项目 artifacts/release。
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$releaseRoot=Join-Path $projectRoot 'artifacts/release'
$modRoot=Join-Path $releaseRoot 'DivaInGameSearch'
$sourceRoot=Join-Path $modRoot 'source'
if(Test-Path -LiteralPath $modRoot){
  $resolvedTarget=[IO.Path]::GetFullPath($modRoot)
  $expectedParent=[IO.Path]::GetFullPath($releaseRoot)
  if([IO.Path]::GetDirectoryName($resolvedTarget) -ne $expectedParent -or -not $resolvedTarget.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){ throw '发行暂存目录超出本项目，已拒绝清理。' }
  Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
New-Item -ItemType Directory -Force $modRoot,$sourceRoot,(Join-Path $modRoot 'data'),(Join-Path $modRoot 'licenses'),(Join-Path $sourceRoot 'third_party/vendor') | Out-Null
foreach($file in @('DivaInGameSearch.dll','SearchPreview.exe','DivaTextureDecoder.dll')){
  $inputFile=Join-Path $projectRoot "build/release/$file"
  if(-not (Test-Path -LiteralPath $inputFile)){ throw "缺少已构建文件：$file" }
  Copy-Item -LiteralPath $inputFile -Destination $modRoot -Force
}
$assetBridge=Join-Path $projectRoot 'build/release/module-assets/MmlCpkBridge.dll'
if(-not (Test-Path -LiteralPath $assetBridge)){ throw '缺少 NativeAOT 官方 CPK 读取组件 MmlCpkBridge.dll。' }
Copy-Item -LiteralPath $assetBridge -Destination $modRoot -Force
Copy-Item -LiteralPath config/config.toml -Destination (Join-Path $modRoot 'config.toml') -Force
Copy-Item -LiteralPath settings.ini,aliases.toml,README.md,LICENSE,THIRD_PARTY_NOTICES.md -Destination $modRoot -Force
Copy-Item -LiteralPath docs -Destination $modRoot -Recurse -Force
New-Item -ItemType Directory -Force (Join-Path $modRoot 'design') | Out-Null
Copy-Item -LiteralPath design/INTERACTION_SPEC.md,design/CUSTOMIZATION_ACTIONS_SPEC.md -Destination (Join-Path $modRoot 'design') -Force
Copy-Item -Path data/*.txt -Destination (Join-Path $modRoot 'data') -Force
Copy-Item -LiteralPath third_party/vendor/imgui/LICENSE.txt -Destination (Join-Path $modRoot 'licenses/ImGui.txt') -Force
Copy-Item -LiteralPath third_party/vendor/detours-source/LICENSE.md -Destination (Join-Path $modRoot 'licenses/Detours.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/DivaModLoader.md -Destination (Join-Path $modRoot 'licenses/DivaModLoader.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/toml++.txt -Destination (Join-Path $modRoot 'licenses/toml++.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/NewClassics.txt -Destination (Join-Path $modRoot 'licenses/NewClassics.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/QuantumSwitch.txt,third_party/vendor/licenses/MikuMikuSwitchPlugin.txt -Destination (Join-Path $modRoot 'licenses') -Force
Copy-Item -LiteralPath third_party/vendor/miniz/LICENSE -Destination (Join-Path $modRoot 'licenses/miniz.txt') -Force
Copy-Item -LiteralPath third_party/vendor/mml-cpk/LICENSE.md -Destination (Join-Path $modRoot 'licenses/MikuMikuLibrary.md') -Force
Copy-Item -LiteralPath third_party/vendor/mml-cpk/COPYRIGHT.md -Destination (Join-Path $modRoot 'licenses/MikuMikuLibrary-Dependencies.md') -Force
Copy-Item -LiteralPath third_party/vendor/bcdec/bcdec.h -Destination (Join-Path $modRoot 'licenses/bcdec.h') -Force
# 公开包只附整理后的验收说明，避免把个人路径、运行日志或曲包审计上传。
# toml++ 的单头文件含完整 MIT 文本，源码包保留该文件。
Copy-Item -LiteralPath CMakeLists.txt,AGENTS.md,README.md,LICENSE,THIRD_PARTY_NOTICES.md,settings.ini,aliases.toml -Destination $sourceRoot -Force
foreach($folder in @('src','scripts','config','data','docs')){
  Copy-Item -LiteralPath $folder -Destination $sourceRoot -Recurse -Force
}
# 只清理本轮发行暂存区的Python缓存，避免打包本机字节码与其中的绝对路径。
foreach($cache in @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'scripts') -Directory -Recurse -Filter '__pycache__')){
  $cachePath=[IO.Path]::GetFullPath($cache.FullName)
  if(-not $cachePath.StartsWith([IO.Path]::GetFullPath($sourceRoot)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){ throw '缓存路径超出发行源码暂存区，已拒绝清理。' }
  Remove-Item -LiteralPath $cachePath -Recurse -Force
}
New-Item -ItemType Directory -Force (Join-Path $sourceRoot 'design') | Out-Null
Copy-Item -LiteralPath design/INTERACTION_SPEC.md,design/CUSTOMIZATION_ACTIONS_SPEC.md -Destination (Join-Path $sourceRoot 'design') -Force
New-Item -ItemType Directory -Force (Join-Path $sourceRoot 'tests') | Out-Null
Copy-Item -Path tests/*.cpp -Destination (Join-Path $sourceRoot 'tests') -Force
Copy-Item -LiteralPath third_party/vendor/imgui,third_party/vendor/toml,third_party/vendor/licenses,third_party/vendor/miniz,third_party/vendor/mml-cpk,third_party/vendor/bcdec -Destination (Join-Path $sourceRoot 'third_party/vendor') -Recurse -Force
$detoursRoot=Join-Path $sourceRoot 'third_party/vendor/detours-source'
New-Item -ItemType Directory -Force $detoursRoot | Out-Null
Copy-Item -LiteralPath third_party/vendor/detours-source/src -Destination $detoursRoot -Recurse -Force
Copy-Item -LiteralPath third_party/vendor/detours-source/LICENSE.md -Destination $detoursRoot -Force
$utf8=[Text.UTF8Encoding]::new($false)
$dependencies=Get-ChildItem (Join-Path $sourceRoot 'third_party/vendor') -Recurse -File | Sort-Object FullName | ForEach-Object {
  (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash+'  '+[IO.Path]::GetRelativePath($sourceRoot,$_.FullName).Replace('\','/')
}
[IO.File]::WriteAllLines((Join-Path $modRoot 'dependencies.sha256'),$dependencies,$utf8)
$manifest=Get-ChildItem $modRoot -Recurse -File | Where-Object { $_.Name -ne 'manifest.sha256' } | Sort-Object FullName | ForEach-Object {
  (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash+'  '+[IO.Path]::GetRelativePath($modRoot,$_.FullName).Replace('\','/')
}
[IO.File]::WriteAllLines((Join-Path $modRoot 'manifest.sha256'),$manifest,$utf8)
$zip=Join-Path $releaseRoot 'DivaInGameSearch-1.0.11-win64.zip'
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -Force
[IO.File]::WriteAllText(($zip+'.sha256'),(Get-FileHash $zip -Algorithm SHA256).Hash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Write-Output "发行包已生成：$zip"
Write-Output "发行包 SHA-256：$((Get-FileHash $zip -Algorithm SHA256).Hash)"
Write-Output '本包包含 DLL、可选原生预览、配置、元数据、许可证和可重建源码；没有修改游戏或其他 Mod。'
