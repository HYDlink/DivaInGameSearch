#include "ui_theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace diva {
namespace {
// 参数 hex/alpha 为 0xRRGGBB 颜色和不透明度，返回 ImGui 浮点颜色。
constexpr ImVec4 Hex(unsigned hex,float alpha=1.0f) {
    return {((hex>>16)&0xFF)/255.0f,((hex>>8)&0xFF)/255.0f,(hex&0xFF)/255.0f,alpha};
}
// 参数 hex/alpha 为 0xRRGGBB 颜色和 0–255 不透明度，返回打包颜色。
constexpr ImU32 Hex32(unsigned hex,unsigned alpha=255) {
    return IM_COL32((hex>>16)&0xFF,(hex>>8)&0xFF,hex&0xFF,alpha);
}
// 参数 a/b/t 为两端颜色和比例，按通道线性混合，保留 a 的不透明度。
ImVec4 Mix(const ImVec4& a,const ImVec4& b,float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w};
}

// 珍珠白基础色，取自 New Classics 风格配色方案；名称与设计规范一致。
constexpr unsigned kWindowBg=0xF5F9FC,kContentBg=0xFFFFFF,kOuterBg=0xDDEAF0;
constexpr unsigned kPrimary=0x00BFCB,kPrimaryDark=0x277C90,kAccentPink=0xE55D9F;
constexpr unsigned kTextPrimary=0x244454,kTextSecondary=0x526B78,kTextOnPrimary=0x123D46;
constexpr unsigned kBorder=0xBBCDD7,kInactiveBg=0xE7EEF3,kHoverBg=0xD9F7F8;

const UiPalette kClassic{
    .resultText={0.53f,0.57f,0.61f,1},.secondaryText={0.57f,0.69f,0.72f,1},.groupLabel={0.75f,0.96f,0.92f,1},
    .tabBg={0.105f,0.12f,0.14f,1},.tabText={0.66f,0.71f,0.75f,1},
    .tabSelectedBg={0.20f,0.32f,0.30f,1},.tabSelectedText={0.63f,0.97f,0.86f,1},.tabHoveredBg={0.22f,0.27f,0.29f,1},
    .tabUnderline=IM_COL32(107,224,194,255),
    .songSelectedRow=IM_COL32(41,63,62,255),.songSelectedIndicator=0,
    .songTitleText={0.91f,0.93f,0.94f,1},.songAliasText={0.57f,0.69f,0.72f,1},.songMetaText={0.91f,0.93f,0.94f,1},
    .cardBg=IM_COL32(29,33,39,255),.cardSelectedBg=IM_COL32(38,52,53,255),.cardBorder=IM_COL32(53,61,69,255),
    .cardHoveredBorder=IM_COL32(99,115,122,255),.cardSelectedBorder=IM_COL32(107,224,194,255),
    .cardImageBg=IM_COL32(35,40,47,255),.cardMutedText=IM_COL32(120,143,154,255),.cardPlaceholderText=IM_COL32(141,163,174,255),
    .recommendBg=IM_COL32(24,43,43,240),.recommendText=IM_COL32(137,229,207,255),
    .browseBg=IM_COL32(24,30,36,220),.browseText=IM_COL32(150,165,175,255),.equippedText=IM_COL32(107,224,194,255),
    .cardRounding=8.0f,.cardImageRounding=7.0f,.badgeRounding=4.0f,
    .equipBg={0.15f,0.28f,0.25f,1},.equipHoveredBg={0.21f,0.39f,0.34f,1},.equipText={0.91f,0.93f,0.94f,1},.equipBorder=0,
};
const UiPalette kPearl{
    .resultText=Hex(kTextSecondary),.secondaryText=Hex(kTextSecondary),.groupLabel=Hex(kPrimaryDark),
    .tabBg=Hex(kInactiveBg),.tabText=Hex(kTextSecondary),
    .tabSelectedBg=Hex(kPrimary),.tabSelectedText=Hex(kContentBg),.tabHoveredBg=Hex(kHoverBg),
    .tabUnderline=Hex32(kPrimaryDark),
    // 选中行按参照图只换底色，不画行首指示条。
    .songSelectedRow=Hex32(0xDCF0F2),.songSelectedIndicator=0,
    .songTitleText=Hex(0x25485B),.songAliasText=Hex(0x577587),.songMetaText=Hex(0x315365),
    .cardBg=Hex32(kContentBg),.cardSelectedBg=Hex32(kContentBg),.cardBorder=Hex32(kBorder),
    .cardHoveredBorder=Hex32(0x7FDDE3),.cardSelectedBorder=Hex32(kPrimary),
    .cardImageBg=Hex32(0xEEF4F8),.cardMutedText=Hex32(kTextSecondary),.cardPlaceholderText=Hex32(kTextSecondary),
    .recommendBg=Hex32(kPrimary,235),.recommendText=Hex32(kTextOnPrimary),
    .browseBg=Hex32(kInactiveBg,230),.browseText=Hex32(kTextSecondary),.equippedText=Hex32(kAccentPink),
    .cardRounding=0.0f,.cardImageRounding=0.0f,.badgeRounding=0.0f,
    .equipBg=Hex(kAccentPink),.equipHoveredBg=Hex(0xF07DB3),.equipText=Hex(kContentBg),.equipBorder=0,
};

// 发行默认珍珠白；旧 settings.ini 没有 ui_theme 时同样使用。
std::atomic<UiTheme> currentTheme{UiTheme::Pearl};

// 参数 s 为样式；Classic 原样复现 1.2.x 的深色青绿配色和圆角。
void ApplyClassic(ImGuiStyle& s) {
    ImGui::StyleColorsDark(&s);
    s.WindowPadding={24,18}; s.FramePadding={12,8}; s.ItemSpacing={10,10}; s.WindowRounding=12; s.FrameRounding=5;
    s.CellPadding={10,7}; s.ScrollbarSize=12;
    s.Colors[ImGuiCol_Text]={0.91f,0.93f,0.94f,1};
    s.Colors[ImGuiCol_TextDisabled]={0.53f,0.57f,0.61f,1};
    s.Colors[ImGuiCol_WindowBg]={0.065f,0.073f,0.086f,1};
    s.Colors[ImGuiCol_ChildBg]={0,0,0,0};
    s.Colors[ImGuiCol_PopupBg]={0.095f,0.107f,0.123f,1};
    s.Colors[ImGuiCol_TitleBg]={0.085f,0.096f,0.11f,1};
    s.Colors[ImGuiCol_TitleBgActive]=s.Colors[ImGuiCol_TitleBg];
    s.Colors[ImGuiCol_Border]={0.22f,0.25f,0.28f,0.7f};
    s.Colors[ImGuiCol_Separator]={0.22f,0.25f,0.28f,0.7f};
    s.Colors[ImGuiCol_FrameBg]={0.12f,0.137f,0.157f,1};
    s.Colors[ImGuiCol_FrameBgHovered]={0.17f,0.20f,0.22f,1};
    s.Colors[ImGuiCol_FrameBgActive]={0.18f,0.23f,0.25f,1};
    s.Colors[ImGuiCol_Header]={0.18f,0.29f,0.29f,1};
    s.Colors[ImGuiCol_HeaderHovered]={0.21f,0.34f,0.33f,1};
    s.Colors[ImGuiCol_HeaderActive]={0.22f,0.39f,0.36f,1};
    s.Colors[ImGuiCol_Button]={0.15f,0.175f,0.195f,1};
    s.Colors[ImGuiCol_ButtonHovered]={0.22f,0.27f,0.29f,1};
    s.Colors[ImGuiCol_ButtonActive]={0.22f,0.35f,0.33f,1};
    s.Colors[ImGuiCol_CheckMark]={0.42f,0.88f,0.76f,1};
    s.Colors[ImGuiCol_SliderGrab]=s.Colors[ImGuiCol_CheckMark];
    s.Colors[ImGuiCol_NavHighlight]=s.Colors[ImGuiCol_CheckMark];
    s.Colors[ImGuiCol_ScrollbarBg]={0,0,0,0};
    s.Colors[ImGuiCol_ScrollbarGrab]={0.25f,0.29f,0.32f,1};
    s.Colors[ImGuiCol_TableHeaderBg]={0.105f,0.12f,0.14f,1};
    s.Colors[ImGuiCol_TableRowBgAlt]={1,1,1,0.025f};
}
// 参数 s 为样式；珍珠白主体、初音青标题栏，窗口与控件一律直角，对齐游戏界面的方正风格。
void ApplyPearl(ImGuiStyle& s) {
    ImGui::StyleColorsLight(&s);
    s.WindowPadding={24,18}; s.FramePadding={12,8}; s.ItemSpacing={10,10}; s.CellPadding={10,7}; s.ScrollbarSize=12;
    s.WindowRounding=0; s.ChildRounding=0; s.FrameRounding=0; s.PopupRounding=0; s.GrabRounding=0; s.ScrollbarRounding=0; s.TabRounding=0;
    s.WindowBorderSize=1; s.FrameBorderSize=1; s.PopupBorderSize=1;
    auto* c=s.Colors;
    c[ImGuiCol_Text]=Hex(kTextPrimary);
    c[ImGuiCol_TextDisabled]=Hex(0x7F95A1);
    c[ImGuiCol_WindowBg]=Hex(kWindowBg);
    c[ImGuiCol_ChildBg]={0,0,0,0};
    c[ImGuiCol_PopupBg]=Hex(kContentBg);
    c[ImGuiCol_TitleBg]=Hex(kPrimary);
    c[ImGuiCol_TitleBgActive]=c[ImGuiCol_TitleBg];
    c[ImGuiCol_TitleBgCollapsed]=c[ImGuiCol_TitleBg];
    c[ImGuiCol_Border]=Hex(kBorder);
    c[ImGuiCol_BorderShadow]={0,0,0,0};
    c[ImGuiCol_Separator]=Hex(kBorder);
    c[ImGuiCol_SeparatorHovered]=Hex(kPrimary);
    c[ImGuiCol_SeparatorActive]=Hex(kPrimaryDark);
    c[ImGuiCol_FrameBg]=Hex(kContentBg);
    c[ImGuiCol_FrameBgHovered]=Hex(kHoverBg);
    c[ImGuiCol_FrameBgActive]=Hex(kHoverBg);
    c[ImGuiCol_Header]=Hex(kHoverBg);
    c[ImGuiCol_HeaderHovered]=Hex(0xC6EFF2);
    c[ImGuiCol_HeaderActive]=Hex(0xB3E8EC);
    c[ImGuiCol_Button]=Hex(kInactiveBg);
    c[ImGuiCol_ButtonHovered]=Hex(kHoverBg);
    c[ImGuiCol_ButtonActive]=Hex(0xB3E8EC);
    c[ImGuiCol_CheckMark]=Hex(kPrimary);
    c[ImGuiCol_SliderGrab]=Hex(kPrimary);
    c[ImGuiCol_SliderGrabActive]=Hex(kPrimaryDark);
    c[ImGuiCol_NavHighlight]=Hex(kPrimaryDark);
    c[ImGuiCol_ScrollbarBg]={0,0,0,0};
    c[ImGuiCol_ScrollbarGrab]=Hex(kBorder);
    c[ImGuiCol_ScrollbarGrabHovered]=Hex(0x9DB5C2);
    c[ImGuiCol_ScrollbarGrabActive]=Hex(kPrimaryDark);
    c[ImGuiCol_ResizeGrip]=Hex(kPrimary,0.25f);
    c[ImGuiCol_ResizeGripHovered]=Hex(kPrimary,0.6f);
    c[ImGuiCol_ResizeGripActive]=Hex(kPrimaryDark);
    c[ImGuiCol_TableHeaderBg]=Hex(0xE4ECF1);
    // 表头下沿与行间线同色，按参照图不再加深。
    c[ImGuiCol_TableBorderStrong]=Hex(0xDBE5EB);
    c[ImGuiCol_TableBorderLight]=Hex(0xDBE5EB);
    c[ImGuiCol_TableRowBg]=Hex(kContentBg);
    c[ImGuiCol_TableRowBgAlt]=Hex(0xF7F9FB);
    c[ImGuiCol_TextSelectedBg]=Hex(kPrimary,0.30f);
    c[ImGuiCol_ModalWindowDimBg]=Hex(kOuterBg,0.5f);
}
// 参数 draw/min/max/skew 为绘制列表、按钮矩形和水平斜切量；生成向右倾斜的平行四边形，对应游戏角色按钮与“Filter”标签。
void SlantPath(ImDrawList* draw,const ImVec2& min,const ImVec2& max,float skew) {
    draw->PathLineTo({min.x+skew,min.y}); draw->PathLineTo({max.x,min.y});
    draw->PathLineTo({max.x-skew,max.y}); draw->PathLineTo({min.x,max.y});
}
// 参数 height 为形状高度；斜切量随高度变化，保持游戏界面约 18° 的倾角。
float SlantFor(float height) { return std::round(height*0.32f); }
// 参数 height 为标签高度；返回标签形状探出按钮矩形的宽度，即半个斜切量。
float TabOverhang(float height) { return std::round(SlantFor(height)*0.5f); }
// 参数 draw/min/max/cut 为绘制列表、按钮矩形和切角边长；生成左上、右下各切去 45° 角的切角矩形。
void ChamferPath(ImDrawList* draw,const ImVec2& min,const ImVec2& max,float cut) {
    draw->PathLineTo({min.x+cut,min.y}); draw->PathLineTo({max.x,min.y});
    draw->PathLineTo({max.x,max.y-cut}); draw->PathLineTo({max.x-cut,max.y});
    draw->PathLineTo({min.x,max.y}); draw->PathLineTo({min.x,min.y+cut});
}
// 参数 height 为形状高度；切角取高度的四分之一，按钮大小变化时比例不变。
float ChamferFor(float height) { return std::max(4.0f,std::round(height*0.25f)); }
// 禁用按钮的底、边和字；文字仍保持可读，不靠整体变淡表示禁用。
constexpr unsigned kDisabledBg=0xEDF2F5,kDisabledBorder=0xD3DEE5,kDisabledText=0x6B8290;
// 参数 label/size/background 为按钮及其背景绘制回调；按钮文字画在上层通道，形状在下层，二者不互相覆盖。
// 处于 BeginDisabled 中时改用禁用配色并恢复不透明，避免整体变淡后白字压在浅粉底上看不清。
template<class Background> bool ShapedButton(const char* label,const ImVec2& size,Background background) {
    auto* draw=ImGui::GetWindowDrawList();
    const bool disabled=(ImGui::GetCurrentContext()->CurrentItemFlags&ImGuiItemFlags_Disabled)!=0;
    if(disabled) {ImGui::PushStyleVar(ImGuiStyleVar_Alpha,1.0f);ImGui::PushStyleColor(ImGuiCol_Text,Hex(kDisabledText));}
    ImDrawListSplitter splitter; splitter.Split(draw,2); splitter.SetCurrentChannel(draw,1);
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0,0,0,0));
    // 去掉上下内边距再居中，按钮矮于“字高+内边距”时文字也不会被挤到下方。
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0.5f,0.5f});
    const bool pressed=ImGui::Button(label,size);
    ImGui::PopStyleVar(3); ImGui::PopStyleColor(3);
    splitter.SetCurrentChannel(draw,0);
    if(disabled) {
        const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();const float cut=ChamferFor(max.y-min.y);
        ChamferPath(draw,min,max,cut); draw->PathFillConvex(Hex32(kDisabledBg));
        ChamferPath(draw,min,max,cut); draw->PathStroke(Hex32(kDisabledBorder),ImDrawFlags_Closed,1.0f);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
    } else background(draw,ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),ImGui::IsItemHovered(),ImGui::IsItemActive());
    splitter.Merge(draw);
    return pressed;
}
}

std::optional<UiTheme> ParseUiTheme(std::string_view text) {
    if(text=="classic") return UiTheme::Classic;
    if(text=="pearl") return UiTheme::Pearl;
    return std::nullopt;
}
void SetUiTheme(UiTheme theme) { currentTheme=theme; }
const char* UiThemeName(UiTheme theme) { return theme==UiTheme::Pearl?"pearl":"classic"; }
UiTheme CurrentUiTheme() { return currentTheme; }
const UiPalette& Palette() { return currentTheme==UiTheme::Pearl?kPearl:kClassic; }
void ApplyUiTheme(ImGuiStyle& style) {
    if(currentTheme==UiTheme::Pearl) ApplyPearl(style); else ApplyClassic(style);
}

DifficultyColors DifficultyButtonColors(int difficulty,const ImVec4& tint,bool available) {
    if(currentTheme!=UiTheme::Pearl)
        return {tint,{0.09f+tint.x*0.06f,0.10f+tint.y*0.06f,0.14f+tint.z*0.06f,1},
                {0.16f+tint.x*0.18f,0.17f+tint.y*0.18f,0.21f+tint.z*0.18f,1},
                {0.20f+tint.x*0.25f,0.21f+tint.y*0.25f,0.25f+tint.z*0.25f,1},
                {tint.x,tint.y,tint.z,available?0.65f:0.15f}};
    // 文字、底色和边框取自珍珠白参照图；悬浮与按下在底色上逐级加深。
    struct Chip { unsigned text,bg,border; };
    static constexpr Chip chips[]={{0x287FC0,0xE6F4FF,0x79BFFF},{0x329253,0xEAF8EE,0x83DCA1},{0xB87817,0xFFF5E5,0xF4C06B},
                                   {0xC73650,0xFFF0F3,0xF192A4},{0x8454B7,0xF6EFFF,0xB999E9}};
    const auto& chip=chips[std::clamp(difficulty,0,4)];
    const ImVec4 bg=Hex(chip.bg),border=Hex(chip.border);
    return {Hex(chip.text),bg,Mix(bg,border,0.30f),Mix(bg,border,0.55f),{border.x,border.y,border.z,available?1.0f:0.35f}};
}

float ThemedTabOverhang(float height) { return currentTheme==UiTheme::Pearl?TabOverhang(height):0.0f; }

bool ThemedTabButton(const char* label,bool selected,const ImVec2& size) {
    const auto& palette=Palette();
    if(currentTheme!=UiTheme::Pearl) {
        ImGui::PushStyleColor(ImGuiCol_Button,selected?palette.tabSelectedBg:palette.tabBg);
        ImGui::PushStyleColor(ImGuiCol_Text,selected?palette.tabSelectedText:palette.tabText);
        const bool pressed=ImGui::Button(label,size);
        if(selected) {
            const float inset=size.y>=36?14.0f:12.0f;
            auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled({min.x+inset,max.y-3},{max.x-inset,max.y-1},palette.tabUnderline,1);
        }
        ImGui::PopStyleColor(2); return pressed;
    }
    ImGui::PushStyleColor(ImGuiCol_Text,selected?palette.tabSelectedText:palette.tabText);
    const bool pressed=ShapedButton(label,size,[&](ImDrawList* draw,ImVec2 min,ImVec2 max,bool hovered,bool active) {
        // 平行四边形以按钮矩形为中线，左下、右上各探出半个斜切量；相邻标签的斜边间距就等于按钮间距，点击区域与形状基本重合。
        const float skew=SlantFor(max.y-min.y),half=TabOverhang(max.y-min.y);
        min.x-=half; max.x+=half;
        const ImVec4 fill=selected?palette.tabSelectedBg:active?ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive):hovered?palette.tabHoveredBg:palette.tabBg;
        SlantPath(draw,min,max,skew); draw->PathFillConvex(ImGui::GetColorU32(fill));
        if(!selected) {SlantPath(draw,min,max,skew);draw->PathStroke(ImGui::GetColorU32(ImGuiCol_Border),ImDrawFlags_Closed,1.0f);}
        // 选中标签底边加深青色粗线，像游戏选中按钮的厚描边。
        else draw->AddLine({min.x+1.0f,max.y-1.5f},{max.x-skew-1.0f,max.y-1.5f},ImGui::GetColorU32(ImGuiCol_NavHighlight),3.0f);
    });
    ImGui::PopStyleColor(); return pressed;
}

bool ThemedPrimaryButton(const char* label,const ImVec2& size) {
    const auto& palette=Palette();
    if(currentTheme!=UiTheme::Pearl) {
        ImGui::PushStyleColor(ImGuiCol_Button,palette.equipBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,palette.equipHoveredBg);
        const bool pressed=ImGui::Button(label,size);
        ImGui::PopStyleColor(2); return pressed;
    }
    ImGui::PushStyleColor(ImGuiCol_Text,palette.equipText);
    const bool pressed=ShapedButton(label,size,[&](ImDrawList* draw,ImVec2 min,ImVec2 max,bool hovered,bool active) {
        ChamferPath(draw,min,max,ChamferFor(max.y-min.y));
        draw->PathFillConvex(ImGui::GetColorU32(hovered||active?palette.equipHoveredBg:palette.equipBg));
    });
    ImGui::PopStyleColor(); return pressed;
}

bool ThemedSecondaryButton(const char* label,const ImVec2& size) {
    if(currentTheme!=UiTheme::Pearl) return ImGui::Button(label,size);
    const auto& palette=Palette();
    // 浅底配深色正文字，悬浮与按下只换浅青底色。
    ImGui::PushStyleColor(ImGuiCol_Text,Hex(kTextPrimary));
    const bool pressed=ShapedButton(label,size,[&](ImDrawList* draw,ImVec2 min,ImVec2 max,bool hovered,bool active) {
        const float cut=ChamferFor(max.y-min.y);
        const ImVec4 fill=active?ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive):hovered?palette.tabHoveredBg:palette.tabBg;
        ChamferPath(draw,min,max,cut); draw->PathFillConvex(ImGui::GetColorU32(fill));
        ChamferPath(draw,min,max,cut); draw->PathStroke(ImGui::GetColorU32(ImGuiCol_Border),ImDrawFlags_Closed,1.0f);
    });
    ImGui::PopStyleColor(); return pressed;
}

const char* ThemedWindowName(const char* title,std::string& storage) {
    storage=currentTheme==UiTheme::Pearl?std::string():std::string(title);
    storage+="###DivaSearch";
    return storage.c_str();
}

void DrawThemedWindowFrame(const char* titleText) {
    if(currentTheme!=UiTheme::Pearl) return;
    auto* window=ImGui::GetCurrentWindow();
    auto* draw=window->DrawList;
    const ImRect rect=window->Rect(),title=window->TitleBarRect();
    const float height=title.GetHeight(),padding=ImGui::GetStyle().FramePadding.x;
    const ImVec2 textSize=ImGui::CalcTextSize(titleText);
    draw->PushClipRect(rect.Min,rect.Max,false);
    // 标题文字压在白色斜纹上，斜纹向右逐渐淡出，对应游戏顶栏横幅；避开右侧关闭按钮。
    const float stripeRight=std::min(title.Min.x+padding*2+textSize.x+180,title.Max.x-height-8);
    draw->PushClipRect(title.Min,{stripeRight,title.Max.y},true);
    for(float x=title.Min.x;x<stripeRight+height;x+=12) {
        const float fade=std::clamp(1.0f-(x-title.Min.x)/(stripeRight-title.Min.x),0.0f,1.0f);
        draw->AddQuadFilled({x,title.Min.y},{x+5,title.Min.y},{x+5-height,title.Max.y},{x-height,title.Max.y},Hex32(kContentBg,static_cast<unsigned>(70*fade)));
    }
    draw->PopClipRect();
    // ImGui 自带标题已隐藏，这里绘制白色标题并轻微叠绘加粗。
    const ImVec2 textPos(title.Min.x+padding,title.Min.y+(height-textSize.y)*0.5f);
    draw->AddText(textPos,Hex32(kContentBg),titleText);
    draw->AddText({textPos.x+0.6f,textPos.y},Hex32(kContentBg),titleText);
    // 标题栏底部：深青细线加左端粉色斜块，呼应游戏顶栏的色块分割。
    draw->AddRectFilled({title.Min.x,title.Max.y-2},{title.Max.x,title.Max.y},Hex32(kPrimaryDark));
    const float h=6;
    draw->AddQuadFilled({title.Min.x,title.Max.y},{title.Min.x+150,title.Max.y},{title.Min.x+150-h,title.Max.y+h},{title.Min.x,title.Max.y+h},Hex32(kAccentPink));
    // 窗口底边粉色操作条色带，对应游戏底部按键提示栏。
    draw->AddRectFilled({rect.Min.x,rect.Max.y-5},{rect.Max.x,rect.Max.y},Hex32(kAccentPink));
    draw->PopClipRect();
}
}
