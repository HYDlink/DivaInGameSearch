# 只读检查本机游戏构建与 New Classics 文件签名，不加载 DLL、不访问游戏进程。
param(
  # 待核查的游戏根目录。
  [Parameter(Mandatory=$true)]
  [string]$GameDirectory
)
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$exe=Join-Path $GameDirectory 'DivaMegaMix.exe'
$stream=[IO.File]::OpenRead($exe)
try{
  $reader=[IO.BinaryReader]::new($stream)
  $stream.Position=60
  $pe=$reader.ReadInt32()
  $stream.Position=$pe+8
  $stamp=$reader.ReadUInt32()
  $stream.Position=$pe+24+56
  $size=$reader.ReadUInt32()
}finally{ $stream.Dispose() }
if($stamp -ne 1666920875 -or $size -ne 602353664){ throw '游戏 PE 构建标记不在当前支持名单。' }
Write-Output "游戏构建标记已匹配：$stamp，映像大小 $size。"
Write-Output "游戏文件 SHA-256：$((Get-FileHash $exe -Algorithm SHA256).Hash)"
$nc=Join-Path $GameDirectory 'mods/New Classics/NewClassics.dll'
if(Test-Path -LiteralPath $nc){
  $bytes=[IO.File]::ReadAllBytes($nc)
  [byte[]]$pattern=0x74,0x0B,0x8B,0x88,0,0,0,0,0x83,0xF9,0x03,0x75,0x02,0x33,0xC9,0x89,0x0D,0,0,0,0,0x40,0x0F,0xB6,0xC7,0x48,0x8B
  $mask='xxxx????xxxxxxxxx????xxxxxx'
  $hits=[Collections.Generic.List[int]]::new()
  $position=0
  while($position -lt $bytes.Length-$pattern.Length){
    $position=[Array]::IndexOf($bytes,[byte]0x74,$position)
    if($position -lt 0 -or $position+$pattern.Length -gt $bytes.Length){ break }
    $match=$true
    for($j=0;$j -lt $pattern.Length;$j++){
      if($mask[$j] -eq 'x' -and $bytes[$position+$j] -ne $pattern[$j]){ $match=$false; break }
    }
    if($match){ $hits.Add($position) }
    $position++
  }
  if($hits.Count -ne 1){ throw "New Classics 风格签名命中 $($hits.Count) 次，必须唯一。" }
  Write-Output ('New Classics 风格签名唯一命中，文件偏移：0x{0:X}。' -f $hits[0])
  Write-Output "New Classics 文件 SHA-256：$((Get-FileHash $nc -Algorithm SHA256).Hash)"
}
Write-Output '静态检查完成；运行时仍须核对输入签名、数据页与真实菜单，不把静态检查视作游戏验收。'
