#pragma once
#include <Windows.h>
#include <filesystem>
#include <optional>
#include <string_view>

namespace diva {
// Windows scRGB 与 Luma SDR 模式的标准参考白，不是经验亮度补偿。
inline constexpr float kSdrReferenceWhiteNits=80.0f;
struct UiColorProfile {
    // Luma 已保存的显示模式：0 为 SDR，1 为 HDR，2 为 SDR 内容映射到 HDR。
    std::optional<int> displayMode;
    // 用户在 Luma 中设置的界面白和场景白，单位尼特。
    std::optional<float> uiWhite,sceneWhite;
    // Luma 当前官方实现缺省使用系统 SDR 白亮度；显式关闭时采用自身亮度配置。
    bool useOsWhite=true;
    // 当前游戏窗口所在显示器的系统白亮度；仅后台查询，不改变系统设置。
    std::optional<float> osWhite;
};
struct UiWhiteDecision {
    float nits=kSdrReferenceWhiteNits;
    const char* source="未取得有效 Luma 配置，使用标准 SDR 参考白";
};
// 参数 text 为 ReShade.ini 快照；只解析 [Luma] 的显示模式和亮度，不写回配置。
UiColorProfile ParseUiColorProfile(std::string_view text);
// 参数 file/window 为明确配置路径和游戏窗口；只能从后台任务调用文件与显示器查询。
UiColorProfile ReadUiColorProfile(const std::filesystem::path& file,HWND window);
// 参数 requested/profile 为本 Mod 手动覆盖值（0=自动）和实际 Luma 配置快照。
UiWhiteDecision ResolveUiWhite(float requested,const UiColorProfile& profile);
}
