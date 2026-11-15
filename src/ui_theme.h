#pragma once
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>

namespace diva {
// 界面主题：Classic 为 1.2.x 原有深色青绿，Pearl 为参考 New Classics 的珍珠白。
enum class UiTheme { Classic, Pearl };

// 主题中由代码直接绘制、不经 ImGuiStyle 的颜色；Classic 各值与改造前的硬编码一致。
struct UiPalette {
    ImVec4 resultText,secondaryText,groupLabel;
    // 顶部领域标签与道具部位子标签。
    ImVec4 tabBg,tabText,tabSelectedBg,tabSelectedText,tabHoveredBg;
    ImU32 tabUnderline;
    // 歌曲表选中行底色与行首指示条（指示条透明则不绘制）。
    ImU32 songSelectedRow,songSelectedIndicator;
    // 歌曲表的曲名、第二行（别名 · 来源）和表头、PVID 文字。
    ImVec4 songTitleText,songAliasText,songMetaText;
    // 服装卡片：底、边框、图片区、角标与已装备标记。
    ImU32 cardBg,cardSelectedBg,cardBorder,cardHoveredBorder,cardSelectedBorder;
    ImU32 cardImageBg,cardMutedText,cardPlaceholderText;
    ImU32 recommendBg,recommendText,browseBg,browseText,equippedText;
    float cardRounding,cardImageRounding,badgeRounding;
    // 换装主按钮。
    ImVec4 equipBg,equipHoveredBg,equipText;
    ImU32 equipBorder;
};

// 参数 text 为 settings.ini 中的 ui_theme 值（classic／pearl）；未知值返回空以保留当前主题。
std::optional<UiTheme> ParseUiTheme(std::string_view text);
// 参数 theme 为新主题；只更新全局选择，调用方负责在安全帧边界重新套用样式（View::styleDirty）。
void SetUiTheme(UiTheme theme);
// 参数 theme 为主题；返回写入 settings.ini 的名称。
const char* UiThemeName(UiTheme theme);
UiTheme CurrentUiTheme();
const UiPalette& Palette();
// 参数 style 为当前 ImGui 样式；写入当前主题的窗口、控件颜色与圆角。
void ApplyUiTheme(ImGuiStyle& style);
// 参数 tint 为难度主色（保持 EASY 蓝、NORMAL 绿、HARD 黄、EXTREME 红、EX EXT 紫语义）；
// difficulty 为 0–4 难度序号，Pearl 按序号取参照图配色；返回该难度按钮的文字、底色、悬浮、按下和边框颜色。available 为该谱面是否存在。
struct DifficultyColors { ImVec4 text,bg,hovered,active,border; };
DifficultyColors DifficultyButtonColors(int difficulty,const ImVec4& tint,bool available);

// 参数 label/selected/size 为标签文字、选中状态和尺寸；Pearl 绘制游戏风格的斜切平行四边形，
// Classic 保留原圆角按钮加底部选中线。仍使用 ImGui::Button，键盘导航与焦点不变。
bool ThemedTabButton(const char* label,bool selected,const ImVec2& size);
// 参数 height 为标签高度；Pearl 标签形状左右各探出按钮矩形的宽度，Classic 为 0。首个标签前右移这么多，左下角才与下方控件对齐。
float ThemedTabOverhang(float height);
// 参数 label/size 为主操作文字和尺寸；Pearl 绘制粉底白字的切角矩形，Classic 保留原圆角按钮。
bool ThemedPrimaryButton(const char* label,const ImVec2& size);
// 参数 label/size 为次要操作文字和尺寸；Pearl 绘制浅底深字描边的切角矩形，与换装按钮成对，Classic 为普通按钮。
bool ThemedSecondaryButton(const char* label,const ImVec2& size);
// 参数 title/storage 为窗口标题和名称缓冲；返回主窗口 Begin 名称。Pearl 隐藏 ImGui 自带标题，改由 DrawThemedWindowFrame 绘制。
const char* ThemedWindowName(const char* title,std::string& storage);
// 参数 title 为窗口标题；在主窗口 Begin 之后调用。Pearl 绘制白色标题、标题下斜纹、粉色色块与底边色带，Classic 不绘制。
void DrawThemedWindowFrame(const char* title);
}
