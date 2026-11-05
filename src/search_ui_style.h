#pragma once
#include <imgui.h>
namespace diva {
inline const ImVec4 SearchResultTextColor{0.53f,0.57f,0.61f,1};
inline const ImVec4 SearchSecondaryTextColor{0.57f,0.69f,0.72f,1};
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
    ImGui::AlignTextToFramePadding();ImGui::TextColored({0.75f,0.96f,0.92f,1},"%s",text);
    auto position=ImGui::GetItemRectMin();position.x+=0.6f;
    ImGui::GetWindowDrawList()->AddText(position,IM_COL32(191,245,235,255),text);ImGui::SameLine();
}
}
