# 仅组装玩家安装所需文件与许可证；源码和开发资料通过GitHub单独提供。
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$releaseRoot=Join-Path $projectRoot 'artifacts/release'
$modRoot=Join-Path $releaseRoot 'DivaInGameSearch'
if(Test-Path -LiteralPath $modRoot){
  $resolvedTarget=[IO.Path]::GetFullPath($modRoot)
  $expectedParent=[IO.Path]::GetFullPath($releaseRoot)
  if([IO.Path]::GetDirectoryName($resolvedTarget) -ne $expectedParent -or -not $resolvedTarget.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){ throw '发行暂存目录超出本项目，已拒绝清理。' }
  Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}
New-Item -ItemType Directory -Force $modRoot,(Join-Path $modRoot 'data'),(Join-Path $modRoot 'licenses') | Out-Null
foreach($file in @('DivaInGameSearch.dll','DivaTextureDecoder.dll')){
  $inputFile=Join-Path $projectRoot "build/release/$file"
  if(-not (Test-Path -LiteralPath $inputFile)){ throw "缺少已构建文件：$file" }
  Copy-Item -LiteralPath $inputFile -Destination $modRoot -Force
}
$assetBridge=Join-Path $projectRoot 'build/release/module-assets/MmlCpkBridge.dll'
if(-not (Test-Path -LiteralPath $assetBridge)){ throw '缺少 NativeAOT 官方 CPK 读取组件 MmlCpkBridge.dll。' }
Copy-Item -LiteralPath $assetBridge -Destination $modRoot -Force
Copy-Item -LiteralPath config/config.toml -Destination (Join-Path $modRoot 'config.toml') -Force
Copy-Item -LiteralPath settings.ini,LICENSE,THIRD_PARTY_NOTICES.md -Destination $modRoot -Force
Copy-Item -LiteralPath data/base_pv_db.txt,data/dlc_pv_db.txt -Destination (Join-Path $modRoot 'data') -Force
Copy-Item -LiteralPath third_party/vendor/imgui/LICENSE.txt -Destination (Join-Path $modRoot 'licenses/ImGui.txt') -Force
Copy-Item -LiteralPath third_party/vendor/detours-source/LICENSE.md -Destination (Join-Path $modRoot 'licenses/Detours.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/DivaModLoader.md -Destination (Join-Path $modRoot 'licenses/DivaModLoader.md') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/toml++.txt -Destination (Join-Path $modRoot 'licenses/toml++.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/NewClassics.txt -Destination (Join-Path $modRoot 'licenses/NewClassics.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/ReDIVA.txt -Destination (Join-Path $modRoot 'licenses/ReDIVA.txt') -Force
Copy-Item -LiteralPath third_party/vendor/licenses/QuantumSwitch.txt,third_party/vendor/licenses/MikuMikuSwitchPlugin.txt -Destination (Join-Path $modRoot 'licenses') -Force
Copy-Item -LiteralPath third_party/vendor/miniz/LICENSE -Destination (Join-Path $modRoot 'licenses/miniz.txt') -Force
Copy-Item -LiteralPath third_party/vendor/mml-cpk/LICENSE.md -Destination (Join-Path $modRoot 'licenses/MikuMikuLibrary.md') -Force
Copy-Item -LiteralPath third_party/vendor/mml-cpk/COPYRIGHT.md -Destination (Join-Path $modRoot 'licenses/MikuMikuLibrary-Dependencies.md') -Force
# 仅提取 bcdec 的许可证文本，避免将源码放入玩家安装包。
$bcdecSource=[IO.File]::ReadAllText((Join-Path $projectRoot 'third_party/vendor/bcdec/bcdec.h'))
$licenseStart=$bcdecSource.LastIndexOf('/* LICENSE:')
if($licenseStart -lt 0){ throw '未找到 bcdec 许可证。' }
[IO.File]::WriteAllText((Join-Path $modRoot 'licenses/bcdec.txt'),$bcdecSource.Substring($licenseStart),[Text.UTF8Encoding]::new($false))
# 显式允许清单防止源码、开发文档、截图、研究脚本或预览程序再次混入玩家包。
$allowed=@('DivaInGameSearch.dll','MmlCpkBridge.dll','DivaTextureDecoder.dll','config.toml','settings.ini','LICENSE','THIRD_PARTY_NOTICES.md','data/base_pv_db.txt','data/dlc_pv_db.txt','licenses/ImGui.txt','licenses/Detours.md','licenses/DivaModLoader.md','licenses/toml++.txt','licenses/NewClassics.txt','licenses/ReDIVA.txt','licenses/QuantumSwitch.txt','licenses/MikuMikuSwitchPlugin.txt','licenses/miniz.txt','licenses/MikuMikuLibrary.md','licenses/MikuMikuLibrary-Dependencies.md','licenses/bcdec.txt')
$actual=@(Get-ChildItem -LiteralPath $modRoot -Recurse -File | ForEach-Object {[IO.Path]::GetRelativePath($modRoot,$_.FullName).Replace('\','/')})
if(@(Compare-Object $allowed $actual).Count){throw '安装包文件与允许清单不一致，已拒绝打包。'}
$zip=Join-Path $releaseRoot 'DivaInGameSearch-1.2.4-win64.zip'
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -Force
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText(($zip+'.sha256'),(Get-FileHash $zip -Algorithm SHA256).Hash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Write-Output "玩家安装包已生成：$zip"
Write-Output "安装包 SHA-256：$((Get-FileHash $zip -Algorithm SHA256).Hash)"
Write-Output '仅含三个DLL、必要配置、两份检索数据和许可证；不含源码、开发文档、截图、研究脚本或预览程序。'
