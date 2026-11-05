#pragma once
#include <imgui.h>
namespace diva {
inline const ImVec4 SearchResultTextColor{0.53f,0.57f,0.61f,1};
inline const ImVec4 SearchSecondaryTextColor{0.57f,0.69f,0.72f,1};
// 参数data为输入框回调，UserData指向全选请求标记；组件激活后执行一次全选并清除标记。
inline int SelectSearchTextCallback(ImGuiInputTextCallbackData* data) {
    auto* requested=static_cast<bool*>(data->UserData);
    if(*requested) {data->SelectAll();*requested=false;}
    return 0;
}
// 参数text为歌曲／定制搜索共用的条件标签，以同一颜色和轻微叠绘强调分组。
inline void DrawSearchGroupLabel(const char* text) {
    ImGui::AlignTextToFramePadding();ImGui::TextColored({0.75f,0.96f,0.92f,1},"%s",text);
    auto position=ImGui::GetItemRectMin();position.x+=0.6f;
    ImGui::GetWindowDrawList()->AddText(position,IM_COL32(191,245,235,255),text);ImGui::SameLine();
}
}
