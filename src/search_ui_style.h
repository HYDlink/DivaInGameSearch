#pragma once
#include <imgui.h>
#include "ui_theme.h"
namespace diva {
// 结果计数与第二行说明文字，随界面主题变化。
inline const ImVec4& SearchResultTextColor() {return Palette().resultText;}
inline const ImVec4& SearchSecondaryTextColor() {return Palette().secondaryText;}
// 宿主对搜索框的编辑请求：全选文字，以及按字符移动光标的步数（负数向左）。
struct SearchTextRequest {
    bool selectAll=false;
    int cursorMove=0;
};
// 参数data为输入框回调，UserData指向SearchTextRequest；组件激活后执行并清除请求，光标按UTF-8字符移动。
inline int ApplySearchTextRequest(ImGuiInputTextCallbackData* data) {
    auto* request=static_cast<SearchTextRequest*>(data->UserData);
    if(request->selectAll) {data->SelectAll();request->selectAll=false;}
    if(!request->cursorMove) return 0;
    int position=data->CursorPos;
    const auto continuation=[&](int at){return (static_cast<unsigned char>(data->Buf[at])&0xC0)==0x80;};
    for(;request->cursorMove<0;++request->cursorMove) if(position>0) {--position;while(position>0 && continuation(position))--position;}
    for(;request->cursorMove>0;--request->cursorMove) if(position<data->BufTextLen) {++position;while(position<data->BufTextLen && continuation(position))++position;}
    data->CursorPos=data->SelectionStart=data->SelectionEnd=position;
    return 0;
}
// 参数text为歌曲／定制搜索共用的条件标签，以同一颜色和轻微叠绘强调分组。
inline void DrawSearchGroupLabel(const char* text) {
    const auto& color=Palette().groupLabel;
    ImGui::AlignTextToFramePadding();ImGui::TextColored(color,"%s",text);
    auto position=ImGui::GetItemRectMin();position.x+=0.6f;
    ImGui::GetWindowDrawList()->AddText(position,ImGui::ColorConvertFloat4ToU32(color),text);ImGui::SameLine();
}
}
