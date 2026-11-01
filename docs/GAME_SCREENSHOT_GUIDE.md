# Project DIVA：游戏窗口截图复用指南

适用：Windows 上运行的《Project DIVA Mega Mix+》，用于 Mod 开发和实机验收。这个方案是游戏外的窗口截图工具，不需要修改游戏或给每个 Mod 增加截图代码。

## 当时怎么做到的

常规 Windows.Graphics.Capture 接口连续超时后，开发工具 `NativeCapture.exe` 通过 Win32 查找指定 PID 的可见、无 owner 顶层窗口，取得游戏客户区大小，然后选择：

| 条件 | 实际调用 | 注意事项 |
| --- | --- | --- |
| 游戏进程在前台 | `GetDC(NULL)` + `ClientToScreen` + `BitBlt(SRCCOPY \| CAPTUREBLT)` | 从屏幕上的客户区矩形取像素；遮挡、通知和覆盖层可能进入图片 |
| 游戏进程不在前台 | `PrintWindow(hwnd, dc, 2)` | 参数 2 为 `PW_RENDERFULLCONTENT`；请求目标窗口绘制，是否得到完整游戏帧取决于窗口和渲染环境 |

随后用 `GetDIBits` 读出 32 位像素，设置不透明 alpha，再用 Windows Imaging Component（WIC）编码成 PNG。工具启用 Per-Monitor DPI Awareness V2，避免将高 DPI 下的逻辑尺寸直接当作物理像素。

两种分支都曾在该开发环境获得有效的 **3840×2160 游戏画面**。这不是对所有显卡、全屏模式或后台窗口的保证。返回成功、PNG 存在，不等于图片一定正确，必须打开检查。

实现源码：[tests/native_capture.cpp](../tests/native_capture.cpp)。截图不注入 DLL、不读写游戏业务内存、不模拟按键、不切换菜单。后台 `PrintWindow` 仍是对窗口发起绘制请求，并不保证目标程序完全不受调用开销影响。

## 直接使用

先在自己的工作目录创建输出文件夹，获取**本次运行**的游戏 PID，再调用工具：

```powershell
New-Item -ItemType Directory -Force ./captures | Out-Null
Get-Process DivaMegaMix | Select-Object Id,ProcessName
./NativeCapture.exe 12345 ./captures/selection-before.png
```

把示例 `12345` 换成刚查到的 PID。游戏重启后必须重新查询，不能沿用旧对话中的 PID。如果存在多个游戏进程，应先确认目标，不随意取第一个。当前工具会选择该 PID 下首个符合条件的窗口；多窗口情形需要人工核对。

已有本项目构建环境时，运行文件位于 `build/release/NativeCapture.exe`：

```powershell
./build/release/NativeCapture.exe 12345 ./artifacts/selection-before.png
```

## 沙箱和权限

实际排错中，同一工具在隔离环境内无法枚举目标窗口，获得访问交互桌面的执行权限后才成功截图。这里涉及的是工具运行环境是否能看到游戏所在桌面，**不等于每台机器都必须以管理员身份运行**。

在 Codex 等沙箱中，如果明明有游戏窗口却提示“没有找到目标游戏窗口”，先核对 PID、执行用户及交互桌面是否相同；需要时按宿主的审批机制申请针对截图命令的权限。不要修改系统安全设置，也不要为了截图强制结束游戏。

## 截图后必须检查

1. 打开输出 PNG，确认它是当前游戏窗口，而不是黑图、旧帧、桌面或错误弹窗。
2. 核对要验证的界面、歌曲、难度、角色或服装确实可见。保存操作前、操作后的两张图片；等待动画稳定后再取图。
3. 如果涉及定位或装备，再和独立读回字段、原生高亮或日志交叉确认。封面、预览、当前选中和已装备可能不是同一数据。
4. 记录游戏构建、Mod 版本、截图分支及必要的复现步骤。截图不能证明不可见的存档写入或长期稳定性。

前台分支若被其他窗口遮挡，会拍到遮挡内容；后台分支可能黑屏、画面滞后或挂起。`PrintWindow` 的现有工具调用没有内部超时，调用卡住时应由外部执行工具设置时限，不要重复堆积进程。最小化、独占全屏和设备切换时尤其要重新确认图片。

现有实现对后台窗口使用客户区尺寸的位图，但 `PrintWindow` 没有指定 `PW_CLIENTONLY`；无边框窗口曾验证可用，带标题栏窗口可能出现非客户区、偏移或裁切。需要普适窗口支持时，应单独完善该分支，不能把当前结果推广到所有窗口。

图片里的坐标是原图像素。若对话工具把 3840×2160 缩为 2048×1152 展示，展示坐标不能直接用于点击；另需核对窗口边框、DPI 和自动化接口坐标系。截图指南本身不授权任何点击或按键操作。

## 独立构建，不依赖本项目

复用包包含源码、独立 `CMakeLists.txt`、预编译 EXE 和 SHA-256 清单。需要 Windows SDK、MSVC C++ 工具链和 CMake。在 Visual Studio 的开发者 PowerShell 中执行：

```powershell
cmake -S . -B build -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/NativeCapture.exe 12345 ./captures/selection-after.png
```

使用前创建 `captures` 文件夹。只链接系统的 `user32`、`gdi32`、`ole32`、`windowscodecs`，不需要下载截图库。源码沿用本项目 AGPL-3.0-or-later 许可证；同包保留 LICENSE。

## 给其他开发对话的简短交接

> 请先阅读本指南，使用随包 NativeCapture.exe 按当前 DivaMegaMix PID 截图。先只截图并打开检查，再决定下一步。若沙箱无法访问游戏窗口，核对交互桌面权限；若普通截图接口已连续超时，不反复重试。前台使用 BitBlt，后台尝试 PrintWindow，两者都必须检查图片。涉及 Mod 可见行为的实机验收，保留操作前后截图，并与实际高亮或独立字段交叉验证；不要把预览程序截图冒充游戏截图。未经授权不点击、不演奏、不装备、不写存档、不结束游戏。

原始截图可能含 Steam 通知、其他覆盖层或个人信息，公开前应检查；不要把整套本机日志和截图自动上传。

## 1.0.8自测：实际D3D11后缓冲

用户指定AHK+D3D自测时，可用scripts/research/capture_d3d_frame.py。它在正式Mod公开OnFrame结束后只复制一帧实际交换链，创建自己的staging纹理并读回，结束立即卸载Hook。不会修改游戏菜单、装备或输入，不用独立WARP Preview替代游戏。需要已安装Frida运行时及Pillow；必须选当前PID。run_ui_step.py已复用这一分支保存每次AHK前后帧。BMP与PNG是同一帧的无损编码转换，仍须打开图片及独立读回。

```bash
python scripts/research/capture_d3d_frame.py \
  --pid "$DIVA_PID" \
  --output artifacts/下一轮-D3D游戏帧.png
```

桌面复制曾在远程环境返回DXGI_ERROR_WAIT_TIMEOUT，未算截图成功；早期NativeCapture的GDI组仍按实际分支标记。
