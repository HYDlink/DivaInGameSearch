#include "ui.h"
#include <imgui.h>
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace diva {
// 在后台生成独立快照，不持有界面状态引用。
void View::Refresh() {
    if(pending.valid()) return;
    status="正在后台读取已启用曲包……";
    auto h=home,g=game;
    // 捕获路径副本，避免刷新任务与关闭界面竞争。
    pending=std::async(std::launch::async,[h,g]{ return LoadCatalog(h,g); });
}
// 发布后台曲库并刷新完整查询结果。
void View::Poll() {
    if(pending.valid() && pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto next=std::make_shared<Catalog>(pending.get());
            if(!hasInitialConfiguration) { initialConfiguration=next->configurationKey;hasInitialConfiguration=true; }
            next->requiresRestart=next->configurationKey!=initialConfiguration;
            catalog=next;
            status="曲库就绪："+std::to_string(catalog->songs.size())+" 首，索引 "+std::to_string((int)catalog->milliseconds)+" 毫秒。";
            if(!catalog->diagnostics.empty()) status+=" "+catalog->diagnostics.front();
            if(catalog->requiresRestart) status="曲包配置或数据库已变化，需重启游戏后定位；别名和收藏仍可使用。";
        } catch(...) { status="曲库读取失败，点击刷新重试。"; }
        dirty=true;
    }
    if(dirty && catalog) Requery();
}
// 保留仍在结果中的逻辑歌曲选择，避免使用易变化的行号。
void View::Requery() {
    query.text=input; results=Search(*catalog,query,favorites); dirty=false;
    auto it=std::find_if(results.begin(),results.end(),[&](const Result& r){ return catalog->songs[r.index].id==selectedId; });
    if(it==results.end()) selectedId=results.empty()?0:catalog->songs[results.front().index].id;
}
// 提交确切的风格和难度，缺失难度不降级；参数 song/difficulty 为歌曲与请求等级。
void View::Locate(const Song& song,int difficulty) {
    selectedId=song.id;
    if(catalog && catalog->requiresRestart) { status="当前索引与游戏启动时曲包不同，请重启游戏后再定位。";return; }
    bool available=song.charts[query.style][difficulty].available;
    if(!available) { status="该难度无生效谱面，请点击其他可用等级。"; return; }
    if(submit) submit({song.id,difficulty,query.style,++serial},available);
}
// 读取简单界面配置；只在显式刷新时更新，不改变输入文本和选择。
void View::ReloadStyle() {
    std::ifstream in(home/"settings.ini"); std::string line;
    while(std::getline(in,line)) {
        auto eq=line.find('='); if(eq==std::string::npos) continue;
        auto key=line.substr(0,eq),value=line.substr(eq+1);
        try {
            if(key=="shortcut_vk") shortcut=std::stoi(value,nullptr,0);
            if(key=="row_height") rowHeight=std::clamp(std::stof(value),56.0f,100.0f);
            if(key=="opacity") opacity=std::clamp(std::stof(value),0.7f,1.0f);
            if(key=="font_size") { float size=std::clamp(std::stof(value),16.0f,26.0f); if(size!=fontSize) { fontSize=size; fontDirty=true; } }
            if(key=="close_on_success") closeOnSuccess=std::stoi(value)!=0;
        } catch(...) {}
    }
    if(shortcut==0x47 || shortcut==0x4C || shortcut==0x53) { shortcut=0x46; status="配置快捷键与歌词 Mod 冲突，已采用 Ctrl+F。"; }
}
// 使用中日字体合并并建立青绿色主色调；参数 size 为基准字号。
void SetupStyle(float size) {
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    // 上下键和 Enter 由搜索列表统一处理，防止 ImGui 导航重复激活等级按钮。
    io.ConfigFlags&=~ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese()); builder.AddText("★☆·—↑↓…"); builder.BuildRanges(&ranges);
    wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows,MAX_PATH);
    auto font=fs::path(windows)/"Fonts/msyh.ttc";
    if(fs::exists(font)) io.Fonts->AddFontFromFileTTF(Utf8(font.wstring()).c_str(),size,nullptr,ranges.Data);
    else io.Fonts->AddFontDefault();
    auto& s=ImGui::GetStyle(); ImGui::StyleColorsDark();
    s.WindowPadding={24,22}; s.FramePadding={12,9}; s.ItemSpacing={10,10}; s.WindowRounding=14; s.FrameRounding=6;
    s.CellPadding={10,7}; s.ScrollbarSize=12;
    s.Colors[ImGuiCol_WindowBg]={0.025f,0.045f,0.065f,0.98f};
    s.Colors[ImGuiCol_Header]={0.03f,0.33f,0.32f,0.75f};
    s.Colors[ImGuiCol_HeaderHovered]={0.03f,0.48f,0.44f,0.8f};
    s.Colors[ImGuiCol_Button]={0.06f,0.22f,0.25f,1};
    s.Colors[ImGuiCol_ButtonHovered]={0.07f,0.42f,0.43f,1};
    s.Colors[ImGuiCol_CheckMark]={0.25f,0.92f,0.79f,1};
}
// 在固定宽度中省略长标题；参数 text/width 为文本与最大像素宽度。
static void ClippedText(const std::string& text,float width) {
    if(ImGui::CalcTextSize(text.c_str()).x<=width) { ImGui::TextUnformatted(text.c_str()); return; }
    std::string clipped=text;
    while(!clipped.empty() && ImGui::CalcTextSize((clipped+"…").c_str()).x>width) {
        size_t i=clipped.size()-1; while(i>0 && ((unsigned char)clipped[i]&0xC0)==0x80) --i; clipped.resize(i);
    }
    ImGui::TextUnformatted((clipped+"…").c_str());
}
// 绘制行首分组标签，以轻微叠绘加粗并突出操作类别；参数 text 为含冒号的中文标签。
static void ControlGroupLabel(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored({0.75f,0.96f,0.92f,1},"%s",text);
    auto position=ImGui::GetItemRectMin();position.x+=0.6f;
    ImGui::GetWindowDrawList()->AddText(position,IM_COL32(191,245,235,255),text);
    ImGui::SameLine();
}
// 绘制统一对齐列表并将名称和难度点击分离。
void View::Draw() {
    if(!open) return;
    auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(1280.0f,display.x-24),std::min(810.0f,display.y-24)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({display.x/2,display.y/2},ImGuiCond_FirstUseEver,{0.5f,0.5f});
    ImGui::SetNextWindowBgAlpha(opacity);
    if(!ImGui::Begin("DIVA 游戏内搜索###DivaSearch",&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    ImGui::TextColored({0.32f,0.94f,0.81f,1},"SONG FINDER"); ImGui::SameLine();
    ImGui::TextDisabled("原生曲库 / 按名称或等级定位 / Ctrl+%c",shortcut);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-220);
    if(focus) { ImGui::SetKeyboardFocusHere(); focus=false; }
    if(ImGui::InputTextWithHint("##查询","输入原名、英文、中文别名、假名、罗马音或 PVID",input,sizeof(input))) dirty=true;
    ImGui::SameLine();
    if(ImGui::RadioButton("歌曲",!query.modMode)) { query.modMode=false; dirty=true; }
    ImGui::SameLine(); if(ImGui::RadioButton("Mod",query.modMode)) { query.modMode=true; dirty=true; }
    const char* styles[]={"街机 ARCADE","主机 CONSOLE","混合 MIXED"};
    const char* difficulties[]={"EASY","NORMAL","HARD","EXTREME","EXTRA EXTREME"};
    const char* sorts[]={"相关度","PVID 升序","名称","等级 降序"};
    ControlGroupLabel("筛选：");
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("曲包"); ImGui::SameLine(); ImGui::SetNextItemWidth(170);
    if(ImGui::BeginCombo("##曲包筛选",query.pack.empty()?"全部来源":query.pack.c_str())) {
        if(ImGui::Selectable("全部来源",query.pack.empty())) { query.pack.clear(); dirty=true; }
        if(catalog) for(const auto& p:catalog->packs) if(ImGui::Selectable(p.c_str(),p==query.pack)) { query.pack=p; dirty=true; }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("风格"); ImGui::SameLine(); ImGui::SetNextItemWidth(170);
    if(ImGui::Combo("##风格筛选",&query.style,styles,3)) dirty=true;
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("含有难度"); ImGui::SameLine(); ImGui::SetNextItemWidth(165);
    if(ImGui::BeginCombo("##难度筛选",query.difficulty<0?"全部难度":difficulties[query.difficulty])) {
        if(ImGui::Selectable("全部难度",query.difficulty<0)) { query.difficulty=-1; dirty=true; }
        for(int d=0;d<5;++d) if(ImGui::Selectable(difficulties[d],query.difficulty==d)) { query.difficulty=d; dirty=true; }
        ImGui::EndCombo();
    }
    ControlGroupLabel("定位：");
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("点曲名 / Enter 使用"); ImGui::SameLine();
    ImGui::SetNextItemWidth(190); ImGui::Combo("##定位目标难度",&currentDifficulty,difficulties,5);
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("也可直接点击歌曲的等级按钮");
    ControlGroupLabel("排序：");
    ImGui::SetNextItemWidth(190); if(ImGui::Combo("##结果排序",&query.sort,sorts,4)) dirty=true;
    ImGui::SameLine(); if(ImGui::Button("刷新曲库")) Refresh();
    ImGui::SameLine(); if(ImGui::Button("重载样式")) ReloadStyle();
    if(dirty && catalog) Requery();
    ImGui::Separator(); ImGui::TextDisabled("%zu 首结果  ·  %s",results.size(),composing?"输入法组词中":"↑↓ 选择  /  Enter 定位  /  Esc 关闭");
    if(move && !results.empty() && catalog) {
        auto it=std::find_if(results.begin(),results.end(),[&](const Result& r){ return catalog->songs[r.index].id==selectedId; });
        int at=it==results.end()?0:(int)(it-results.begin());
        at=std::clamp(at+move,0,(int)results.size()-1); selectedId=catalog->songs[results[at].index].id;
    }
    if(requestEscape && !composing) open=false;
    if(requestEnter && !composing && catalog) for(const auto& r:results) if(catalog->songs[r.index].id==selectedId) { Locate(catalog->songs[r.index],currentDifficulty); break; }
    requestEnter=requestEscape=false;
    const ImVec4 colors[]={{0.3f,0.7f,1,1},{0.4f,0.85f,0.52f,1},{1,0.74f,0.25f,1},{1,0.35f,0.43f,1},{0.76f,0.52f,1,1}};
    float width=ImGui::GetContentRegionAvail().x;
    float diffWidth=std::clamp(width*0.073f,64.0f,92.0f);
    if(ImGui::BeginTable("歌曲列表",7,ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH, {0,ImGui::GetContentRegionAvail().y-62})) {
        ImGui::TableSetupColumn("曲名 / 别名 · 来源",ImGuiTableColumnFlags_WidthStretch);
        const char* headers[]={"EASY","NORMAL","HARD","EXTREME","EX EXT"};
        for(int d=0;d<5;++d) ImGui::TableSetupColumn(headers[d],ImGuiTableColumnFlags_WidthFixed,diffWidth);
        ImGui::TableSetupColumn("PVID",ImGuiTableColumnFlags_WidthFixed,78); ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin((int)results.size(),rowHeight);
        // 键盘选中在裁剪范围外时先滚动到目标行，下帧由裁剪器绘制。
        if(move) for(size_t i=0;i<results.size();++i) if(catalog->songs[results[i].index].id==selectedId) ImGui::SetScrollY(std::max(0.0f,(float)i*rowHeight-120));
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto& song=catalog->songs[results[row].index]; ImGui::PushID(song.id);
            ImGui::TableNextRow(0,rowHeight); if(song.id==selectedId) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(12,69,71,200));
            ImGui::TableSetColumnIndex(0); auto pos=ImGui::GetCursorScreenPos();
            auto nameWidth=ImGui::GetContentRegionAvail().x;
            if(ImGui::InvisibleButton("名称",{nameWidth,rowHeight-16})) Locate(song,currentDifficulty);
            bool hovered=ImGui::IsItemHovered(); ImGui::SetCursorScreenPos(pos);
            ClippedText(song.name,nameWidth);
            ImGui::PushStyleColor(ImGuiCol_Text,{0.57f,0.69f,0.72f,1});
            auto alias=song.aliases.empty()?song.english:song.aliases;
            if(auto separator=alias.find('|');separator!=std::string::npos) alias.resize(separator);
            if(alias==song.name) alias.clear();
            ClippedText((alias.empty()?"":alias+" · ")+song.source,nameWidth); ImGui::PopStyleColor();
            if(hovered) {
                ImGui::BeginTooltip(); ImGui::TextUnformatted(song.name.c_str()); ImGui::TextUnformatted(song.english.c_str()); ImGui::TextUnformatted(song.reading.c_str()); ImGui::TextUnformatted(song.aliases.c_str());
                for(const auto& p:song.sources) ImGui::Text("关联来源：%s",p.c_str()); ImGui::EndTooltip();
            }
            for(int d=0;d<5;++d) {
                ImGui::TableSetColumnIndex(d+1); const auto& c=song.charts[query.style][d];
                ImGui::PushID(d); ImGui::PushStyleColor(ImGuiCol_Text,colors[d]);
                // 难度按钮使用独立不透明底色和彩色边框，避免被选中行的青绿色底色淹没。
                const auto& tint=colors[d];
                ImGui::PushStyleColor(ImGuiCol_Button,{0.09f+tint.x*0.06f,0.10f+tint.y*0.06f,0.14f+tint.z*0.06f,1});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.16f+tint.x*0.18f,0.17f+tint.y*0.18f,0.21f+tint.z*0.18f,1});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0.20f+tint.x*0.25f,0.21f+tint.y*0.25f,0.25f+tint.z*0.25f,1});
                ImGui::PushStyleColor(ImGuiCol_Border,{tint.x,tint.y,tint.z,c.available?0.65f:0.15f});
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.0f);
                char label[40]; if(c.available) snprintf(label,sizeof(label),"%.1f",c.level); else snprintf(label,sizeof(label),"—");
                ImGui::BeginDisabled(!c.available);
                if(ImGui::Button(label,{diffWidth-8,rowHeight-20})) Locate(song,d);
                ImGui::EndDisabled(); ImGui::PopStyleVar(); ImGui::PopStyleColor(5);
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip(); ImGui::TextUnformatted(c.available?c.source.c_str():"该风格没有生效谱面"); if(c.available) ImGui::TextUnformatted(c.script.c_str()); ImGui::EndTooltip();
                } ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(6); ImGui::Text("%04d",song.id); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    move=0;
    ImGui::TextWrapped("%s",status.c_str());
    ImGui::End();
}
}
