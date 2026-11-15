# 独立 UI 色彩适配测试

开发测试基于 1.2.6，用户实机确认后按其明确要求纳入 1.2.7 正式本地安装包。实现位于 `src/ui_color_output.*`；没有修改第三方 ImGui 后端，也没有 hook、设置或改写 Luma／ReShade 的对象。

## 分流与配置

`settings.ini` 使用 `ui_color_output=auto`、`ui_white_nits=auto`。旧配置缺少这两项时使用相同默认值；上一个测试包如果显式写入 `ui_white_nits=203`，该值属于手动覆盖，需改成 `auto` 才会跟随 Luma。

| 配置 | 行为 |
| --- | --- |
| auto | 仅官方命名的 DIVA Luma 模块加匹配的十位／FP16 后缓冲启用 HDR10／scRGB；其他情况沿用原路径 |
| sdr | 强制使用原有 ImGui 直绘，可用于对比和回退 |
| hdr10 | 仅在实际十位后缓冲上执行 PQ／BT.2020 合成；不匹配格式回退原路径 |
| scrgb | 仅在实际 FP16 后缓冲上执行线性 BT.709 合成；不匹配格式回退原路径 |

DXGI 的 `IDXGISwapChain3` 没有 `GetColorSpace1()`。`CheckColorSpaceSupport()` 只能报告能力，不能报告当前模式。自动分流是基于已知 Luma 实现的推断，不是通用 HDR 检测；不能仅凭十位格式把普通 SDR 输出判作 HDR。重命名或不同实现由玩家根据实际 Output 显式指定。

已知模块命名来自 Luma 官方工程的 `$(SolutionName)-$(ProjectName).addon`；自动识别 `.addon` 与 `.addon64`。官方源码的 OutputHandling 将 HDR10、scRGB 与 SDR8 区分处理，Display Mode 为 SDR 也可能输出 HDR10。参考：

- [Luma DIVA 输出实现](https://github.com/Filoppi/Luma-Framework/blob/main/Source/Games/Project%20Diva%20Mega%20Mix/main.cpp)
- [IDXGISwapChain3 接口](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nn-dxgi1_4-idxgiswapchain3)
- [微软 HDR 与 scRGB 说明](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)

## 合成边界

普通 SDR 保留原调用顺序，不分配 HDR 中间纹理。HDR 打开搜索时才创建本 Mod 的资源：先把现有 ImGui 输出画到透明 SDR 图层，再还原预乘颜色、解码 sRGB，按参考白转为线性亮度。HDR10 对 UI 做 BT.709 至 BT.2020 转换，解码游戏副本 PQ 后在线性空间合成，最后编码 PQ；scRGB 使用 1.0=80 尼特的标准尺度。

亮度决策独立在 `src/ui_color_profile.*`。使用已有后台执行器每秒读取游戏目录 `ReShade.ini` 的 `[Luma]` 已保存配置，游戏帧只消费就绪快照：DisplayMode=0 按 Luma 规定使用标准 80 尼特；HDR 模式使用 UIPaperWhite，SDRInHDR 使用 ScenePaperWhite；UseOSReferenceWhiteLevel 开启（当前官方默认）时通过 Windows 显示路径查询游戏窗口所在显示器的 SDR 白亮度。查询失败、未知模式或无效字段使用标准 SDR 参考白，并明确记入诊断，不伪装成自动识别成功。手动数值覆盖仍可用（80–400）。不写 Luma 配置，不调用其私有内存或新增 ReShade hook。

这是对已保存配置的只读兼容，不是读取 Luma 当前 shader 常量：自定义 ReShade 配置文件名、尚未保存的设置、多运行时或分支实现不能保证同步，应检查日志来源或显式指定。模式规则依据官方 `Source/Core/core.hpp` 的 LoadConfigs／OnDisplayModeChanged，Windows 白亮度 API 的 1000 单位对应 80 尼特。

### 本轮修正证据

用户 2026-11-15 15:44:32.280 的日志确认实际启用了 HDR10，后缓冲格式 24、界面白 203 尼特、返回成功。截图深灰原值 `(35,40,47)` 变成约 `(58,65,76)`，与 SDR 80 尼特参考下放大 `203/80` 的结果吻合。这支持亮度基准不匹配，不能仅凭截图宣称所有实机问题已解决。旧测试只验证输出是 203 尼特，遗漏了 SDR 素材在目标参考白下的实际层次；新增回归先在旧默认值上失败，再验证深灰、肤色、青色和六档白衣亮部往返。

完全透明像素直接 discard，游戏未被界面覆盖的像素不经历色彩往返。资源不保存后缓冲引用，尺寸／格式变化时重建，设备重绑时释放；所有改写的着色阶段、动态链接实例、纹理、常量、混合、深度、视口、光栅化、图元与输出目标状态会恢复。只支持单采样目标；适配准备失败不向游戏目标提交半成品，以一次中文诊断说明，可通过 sdr 回退。

### 1.2.7 同版本背景透明观感修正

HDR 线性混合使相同 `opacity=0.97` 在亮底上显得比旧 SDR 更透明。本次不调整透明度数值或白亮度解析，而由 View 每帧提供主窗口实际逻辑边界；宿主按 DisplayPos 和 FramebufferScale 转成后缓冲坐标，只在该边界内且图层 alpha 小于 1 的像素复现旧 SDR 的透底混合。

具体使用既有 SDR 图层的预乘颜色，加上在扩展 sRGB 空间表示的游戏透底项，再回到 HDR10／scRGB 输出。负值和超白通过有符号扩展保留，避免为兼容背景而裁掉游戏 HDR 高光。完全透明像素、边界外像素、不透明卡片、缩略图及不透明文字继续沿用此前路径。文字抗锯齿等部分覆盖位置的透底随背景一起变化，文字基色、图片采样和参考白规则不变；不是根据像素亮暗猜测图片。

`UiColorOutputCheck` 新增真实旧 SDR 后端对照，覆盖 HDR10／scRGB、80／203 尼特、0.70／0.97／1.00 不透明度、两档缩放和白／灰／彩色游戏底图，共 72 组。窗口背景采样对照旧 SDR；不透明图片、叠在不透明卡片上的透明图片边缘、不透明文字白及窗口外像素，与不启用兼容的 HDR 输出逐像素一致。原有图片灰阶、肤色、白衣亮部和亮度配置测试保留。

共用构建目录若被独立预览占用，可从已完整验证的隔离目录封包：`./scripts/package.ps1 -BuildDirectory build/hdr-background`。版本仍为 1.2.7；原包保存在本地验收目录，截图与自动测试不补记为新的实机通过。

HDR 模式增加一份游戏副本、一个八位 SDR 图层与全屏合成，存在额外 GPU 带宽和显存开销；关闭面板不提交这些绘制。没有更改游戏帧等待策略，没有 CPU 像素读回或强制 Flush。

## 复现

```powershell
./scripts/build.ps1
./build/release/UiColorOutputCheck.exe
./scripts/package.ps1 -TestVariant hdr-ui-auto-white
```

本轮测试包单独生成到 `artifacts/test-releases/hdr-ui-auto-white/`，不会覆盖普通发行目录或上一个测试包。内含简中测试说明。

`UiColorOutputCheck` 使用真实 WARP 设备和默认 ImGui DX11 后端读回 HDR10／scRGB 纹理，检查标准白、灰阶、基色、肤色和青色图片、半透明合成、未覆盖区域位值、缩放重建和状态恢复。普通 SDR 的共用预览前后截图另作对比。

离屏模拟没有加载 Luma，不等同实机通过。仍需反馈用户核对旧版与新版实际截图、Luma 的版本和 Output、窗口／全屏、分辨率切换、其他覆盖层及显示器观感；失败时保留“界面色彩：”诊断。实际游戏截图按 `docs/GAME_SCREENSHOT_GUIDE.md` 执行。
