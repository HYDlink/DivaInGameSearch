# 检查玩家可见代码里没有硬编码中文；新增文案应写入 locales/*.toml，见 docs/I18N.md。
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$files='src/ui.cpp','src/customization_ui.cpp','src/customization.cpp','src/customization_action.h','src/customization_item_locator.cpp','src/selector.cpp','src/module_action.cpp','src/module_locator.cpp','src/native_page_router.cpp','src/recommendation.cpp','src/recommendation_runtime.cpp'
# 这些行只面向开发者：性能录制、内部 ImGui ID、静态断言和未认证 Profile 的日志细节。
$allowedLine='performance->|性能|BeginTable\("|InvisibleButton\("|static_assert|Profile|协议未登记|noun=|窗口消息线程|^\s*//'
$failures=@()
foreach($file in $files){
  $number=0
  foreach($line in [IO.File]::ReadAllLines((Resolve-Path $file).Path,[Text.UTF8Encoding]::new($false))){
    ++$number
    if($line -match $allowedLine){ continue }
    foreach($literal in [regex]::Matches($line,'"[^"]*[\u4e00-\u9fff][^"]*"')){
      if($literal.Value -match '^"##' -or $literal.Value -match '###'){ continue }
      $failures+="${file}:${number}: $($literal.Value)"
    }
  }
}
if($failures.Count){
  $failures | ForEach-Object { Write-Output $_ }
  throw "发现 $($failures.Count) 处硬编码中文。"
}
Write-Output '文案硬编码检查通过。'
