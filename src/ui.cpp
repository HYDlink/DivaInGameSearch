#include "ui.h"
#include "search_ui_style.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace diva {
// 在后台生成独立快照，不持有界面状态引用。
void View::Refresh() {
    if(pending.valid()) return;
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    status="正在后台读取已启用曲包……";
    auto h=home,g=game;
    // 捕获路径副本，避免刷新任务与关闭界面竞争。
    pending=std::async(std::launch::async,[h,g]{ return LoadCatalog(h,g); });
}
// 发布后台曲库并刷新完整查询结果。
void View::Poll() {
    ConsumeSearchReply(performanceExport,[&](std::string result){status=std::move(result);},[&]{status="性能报告导出失败。";});
    customization.songCatalog=catalog;customization.songCriteria.style=query.style;customization.songFavorites=favorites;
    customization.composing=composing; customization.Poll();
    if(pending.valid() && pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto next=std::make_shared<Catalog>(pending.get());
            if(!hasInitialConfiguration) { initialConfiguration=next->configurationKey;hasInitialConfiguration=true; }
            next->requiresRestart=next->configurationKey!=initialConfiguration;
            // 新快照索引不可复用旧结果，正在运行的旧查询由版本号丢弃。
            catalog=next; results.clear();++songCatalogGeneration;ResetSongBodyClick();
            if(invalidateSongNavigation) invalidateSongNavigation();
            status="曲库就绪："+std::to_string(catalog->songs.size())+" 首，索引 "+std::to_string((int)catalog->milliseconds)+" 毫秒。";
            if(!catalog->diagnostics.empty()) status+=" "+catalog->diagnostics.front();
            if(catalog->requiresRestart) status="曲包配置或数据库已变化，需重启游戏后定位；别名和收藏仍可使用。";
        } catch(...) { status="曲库读取失败，点击刷新重试。"; }
        dirty=true;
    }
    if(dirty) QueueSearch(false);
    ConsumeSearchReply(searching,[&](SearchReply reply) {
            if(reply.revision==queryRevision) {
                const auto publishStart=PerformanceNow();
                results=std::move(reply.results); appliedRevision=reply.revision;
                auto it=std::find_if(results.begin(),results.end(),[&](const Result& r){ return catalog->songs[r.index].id==selectedId; });
                if(it==results.end()) selectedId=results.empty()?0:catalog->songs[results.front().index].id;
                scrollSelection=true;
                performance->Query("歌曲",reply.revision,reply.timing,PerformanceNow()-publishStart);
            }
    },[&] {
            if(searchingRevision==queryRevision) {
                status="查询失败，请修改查询或刷新曲库重试。";
                results.clear(); selectedId=0; appliedRevision=queryRevision; requestEnter=false;
            }
    });
    if(catalog && !searching.valid() && SearchDue(queryRevision,appliedRevision,queryDue,composing)) {
        auto snapshot=catalog; auto criteria=query; auto savedFavorites=favorites; auto revision=queryRevision;
        // 参数均按值捕获，后台任务不访问 UI 或游戏内存。
        searchingRevision=revision;
        auto timing=searchTiming;timing.submitted=PerformanceNow();
        searching=searchExecutor->Submit([snapshot,criteria,savedFavorites,revision,timing]() mutable {
            timing.started=PerformanceNow();auto results=Search(*snapshot,criteria,savedFavorites);timing.finished=PerformanceNow();
            return SearchReply{revision,std::move(results),timing};
        });
    }
}
// 查询版本仅在条件变化时递增，输入法组合期间不启动后台搜索。
void View::QueueSearch(bool debounce) {
    query.modMode=false;
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    query.text=input; dirty=false; ++queryRevision;
    queryDue=SearchDeadline(debounce);
    searchTiming.changed=PerformanceNow();searchTiming.due=searchTiming.changed+(debounce?kSearchDebounce.count():0);
}
// 查询包括尚未调度的配置变更，防止 Enter 使用旧结果。
bool View::SearchPending() const { return dirty || appliedRevision!=queryRevision; }
// 显式查询也走后台路径。
void View::Requery() {
    QueueSearch(false); Poll();
}
// 上下保留难度偏好；左右跳过无谱面的难度，到边界停止。
void View::Navigate() {
    if(move || difficultyMove) {ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();}
    if(!catalog || results.empty()) { move=difficultyMove=0; return; }
    auto it=std::find_if(results.begin(),results.end(),[&](const Result& r){ return catalog->songs[r.index].id==selectedId; });
    int at=it==results.end()?0:(int)(it-results.begin());
    if(move) {
        at=(int)std::clamp((int64_t)at+move,int64_t(0),(int64_t)results.size()-1);
        selectedId=catalog->songs[results[at].index].id; scrollSelection=true;
    }
    const auto& charts=catalog->songs[results[at].index].charts[query.style];
    int direction=difficultyMove<0?-1:1;
    // 最多只有五个难度；即使积累很多重复消息也不在一帧循环过量。
    for(int steps=std::min(5,std::abs(difficultyMove));steps>0;--steps) {
        int next=currentDifficulty+direction;
        while(next>=0 && next<5 && !charts[next].available) next+=direction;
        if(next<0 || next>=5) break;
        currentDifficulty=next;
    }
    move=difficultyMove=0;
}
// 模式互切只影响当前领域，不会把定制详情请求交给歌曲定位器。
void View::SetMode(SearchMode next) {
    if(mode==next) return;
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    customization.HandleEscape();
    mode=next; ClearActions(); focus=true; listNavigation=true;
    horizontalNavigation=mode==SearchMode::Songs;
    if(mode!=SearchMode::Songs) customization.SetKind(mode==SearchMode::Modules?CustomizationKind::Module:CustomizationKind::Accessory);
}
// 输入宿主在打开时调用，角色未知与已验证读取保持不同语义。
void View::OnOpened() {
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    customization.OnOpen(TryReadCustomizationContext());
    focus=true; listNavigation=true; horizontalNavigation=mode==SearchMode::Songs; ClearActions();
}
// 参数 preferred/context 为已识别的当前页面和可选角色；打开时覆盖上次领域，保留各领域查询。
void View::ApplyOpeningContext(SearchMode preferred,std::optional<CustomizationContext> context) {
    SetMode(preferred);
    if(context) customization.OnOpen(context);
    focus=true;listNavigation=true;horizontalNavigation=mode==SearchMode::Songs;ClearActions();
}
// 只清理尚未处理的 UI 动作，不改变条件或歌曲目标难度。
void View::ClearActions() { requestEnter=requestEscape=false; move=difficultyMove=0; itemMoves.clear(); }
// 提交确切的风格和难度，缺失难度不降级；参数 song/difficulty 为歌曲与请求等级。
void View::Locate(const Song& song,int difficulty,SongOrigin origin) {
    selectedId=song.id;
    if(SearchPending()) { status="正在更新结果，请稍候再定位。"; return; }
    if(catalog && catalog->requiresRestart) { status="当前索引与游戏启动时曲包不同，请重启游戏后再定位。";return; }
    bool available=song.charts[query.style][difficulty].available;
    if(!available) { status="该难度无生效谱面，请按左右键选择其他可用难度。"; return; }
    Request request{song.id,difficulty,query.style,++serial};request.origin=origin;
    request.queryRevision=queryRevision;request.catalogGeneration=songCatalogGeneration;
    if(origin==SongOrigin::BodyDoubleClick) {request.firstClickPvid=songClickPvid;request.firstClickRevision=songClickRevision;}
    if(origin!=SongOrigin::BodySingleClick) ResetSongBodyClick();
    if(submit) submit(request,available);
}
// 首击只保留稳定身份和时序，不能由Enter或难度按钮继承跨页权限。
void View::ResetSongBodyClick() {songClickPvid=0;songClickRevision=songClickGeneration=0;songClickTime=-1;}
// 参数song为名称主体；仅左键双击且首击身份、修订、目录、时间和距离全部匹配时跨页。
void View::ClickSongBody(const Song& song) {
    auto& io=ImGui::GetIO();const auto now=ImGui::GetTime();
    if(!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || composing || imeGuard || SearchPending()) {ResetSongBodyClick();return;}
    const float dx=io.MousePos.x-songClickX,dy=io.MousePos.y-songClickY;
    const bool twice=ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && songClickPvid==song.id
        && songClickRevision==queryRevision && songClickGeneration==songCatalogGeneration && songClickTime>=0
        && now-songClickTime<=io.MouseDoubleClickTime && dx*dx+dy*dy<=io.MouseDoubleClickMaxDist*io.MouseDoubleClickMaxDist;
    if(twice) {Locate(song,currentDifficulty,SongOrigin::BodyDoubleClick);return;}
    songClickPvid=song.id;songClickRevision=queryRevision;songClickGeneration=songCatalogGeneration;
    songClickTime=now;songClickX=io.MousePos.x;songClickY=io.MousePos.y;
    Locate(song,currentDifficulty,SongOrigin::BodySingleClick);
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
    // Tab 可访问筛选和工具；搜索框内方向键由列表统一处理。
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese()); builder.AddText("★☆·—↑↓←→…"); builder.BuildRanges(&ranges);
    wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows,MAX_PATH);
    auto font=fs::path(windows)/"Fonts/msyh.ttc";
    if(fs::exists(font)) io.Fonts->AddFontFromFileTTF(Utf8(font.wstring()).c_str(),size,nullptr,ranges.Data);
    else io.Fonts->AddFontDefault();
    auto& s=ImGui::GetStyle(); ImGui::StyleColorsDark();
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
// 参数 label/selected/width 为分段名称、当前状态和宽度；保留原生按钮的键盘导航与焦点描边。
static bool SearchSegment(const char* label,bool selected,float width) {
    ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(0.20f,0.32f,0.30f,1):ImVec4(0.105f,0.12f,0.14f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,selected?ImVec4(0.63f,0.97f,0.86f,1):ImVec4(0.66f,0.71f,0.75f,1));
    const bool pressed=ImGui::Button(label,{width,36});
    if(selected) {
        auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled({min.x+14,max.y-3},{max.x-14,max.y-1},IM_COL32(107,224,194,255),1);
    }
    ImGui::PopStyleColor(2);return pressed;
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
    DrawSearchGroupLabel(text);
}
// 旧版 ImGui 默认只 Tab 到文本输入框；复用其公开焦点 API 所用的导航请求标志。
void EnableControlTabNavigation() {
    auto& g=*GImGui;
    if((g.NavMoveFlags&ImGuiNavMoveFlags_Tabbing) && ImGui::IsKeyPressed(ImGuiKey_Tab) && !g.IO.KeyCtrl && !g.IO.KeyAlt) {
        g.NavMoveFlags|=ImGuiNavMoveFlags_FocusApi;
        if(!g.IO.KeyShift && g.NavId) g.NavTabbingDir=1;
    }
}
// 绘制统一对齐列表并将名称和难度点击分离。
void View::Draw() {
    if(!open) return;
    if(composing || imeGuard || ImGui::GetIO().AppFocusLost || ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) ResetSongBodyClick();
    auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(1280.0f,display.x-24),std::min(860.0f,display.y-24)},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({display.x/2,display.y/2},ImGuiCond_FirstUseEver,{0.5f,0.5f});
    ImGui::SetNextWindowBgAlpha(opacity);
    if(!ImGui::Begin("DIVA 游戏内搜索###DivaSearch",&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    // 外部左键关闭搜索；下拉弹层与输入法候选仍先处理自身输入，宿主保留本次鼠标释放隔离。
    if(ImGui::GetCurrentWindow()->WasActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !composing && !imeGuard
        && !ImGui::GetCurrentWindow()->Rect().Contains(ImGui::GetIO().MousePos)
        && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)) {
        open=false;ResetSongBodyClick();customization.ResetBodyClick();ClearActions();ImGui::End();return;
    }
    EnableControlTabNavigation();
    if(SearchSegment("歌曲##领域",mode==SearchMode::Songs,100)) SetMode(SearchMode::Songs);
    ImGui::SameLine(0,4); if(SearchSegment("服装##领域",mode==SearchMode::Modules,100)) SetMode(SearchMode::Modules);
    // 饰品与性能诊断尚不面向玩家开放；保留底层实现，不提供界面入口。
    if(ImGui::BeginPopup("搜索性能监测")) {
        const auto& f=performance->last;
        ImGui::Text("当前防抖：%lld 毫秒",static_cast<long long>(kSearchDebounce.count()));
        ImGui::Text("帧间隔 %.2f ms / 本Mod回调 %.2f ms",f.gap,f.total);
        ImGui::Text("输入锁 %.2f ms / 界面构建 %.2f ms / DX11提交 %.2f ms",f.inputLock,f.draw,f.render);
        ImGui::Text("纹理上传 %u 张 / %.2f ms",f.uploads,f.upload);
        ImGui::TextWrapped("先记录，再逐字输入复现卡顿。记录只保存在内存，不采集查询文字；导出后可分析防抖、排队、查询、发布和绘制各段。");
        if(ImGui::Button("开始15秒记录")) {performance->Start();ImGui::CloseCurrentPopup();}
        ImGui::Text("已记录 %zu 帧 / %zu 条已发布查询%s",performance->frames.size(),performance->queries.size(),performance->recording?"（记录中）":"");
        ImGui::BeginDisabled(performance->recording || performance->frames.empty() || performanceExport.valid());
        if(ImGui::Button("导出报告")) {
            auto frames=performance->frames;auto queries=performance->queries;auto path=home/"search-performance.tsv";
            // 参数均为样本副本，后台格式化与写入，不访问实时UI或游戏对象。
            performanceExport=searchExecutor->Submit([frames=std::move(frames),queries=std::move(queries),path] {
                std::ofstream out(path,std::ios::binary|std::ios::trunc);
                out<<"\xEF\xBB\xBF"<<PerformanceTrace::Report(frames,queries);out.close();
                return out?"性能报告已保存："+Utf8(path.wstring()):"性能报告写入失败。";
            });ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();ImGui::EndPopup();
    }
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    if(mode!=SearchMode::Songs) {
        customization.EnsureLoaded(home,game); customization.composing=composing;
        customization.Poll();
        if(focus) { customization.State().focus=true; focus=false; }
        if(!composing && !imeGuard) {
            for(const auto& [action,repeats]:itemMoves) for(int i=0;i<std::min(repeats,1000);++i) customization.Navigate(action);
            if(requestEnter) customization.RequestDetails();
            if(requestEscape && !customization.HandleEscape()) open=false;
        }
        ClearActions(); customization.Draw();
        listNavigation=customization.WantsListNavigation();
        horizontalNavigation=customization.WantsHorizontalNavigation();
        ImGui::End(); return;
    }
    horizontalNavigation=true;
    ImGui::SetNextItemWidth(-1);
    if(focus) { ImGui::SetKeyboardFocusHere(); focus=false; }
    if(ImGui::InputTextWithHint("##查询","输入原名、英文、中文别名、假名、罗马音或 PVID",input,sizeof(input))) QueueSearch(true);
    bool searchActive=ImGui::IsItemActive();
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
    ControlGroupLabel("排序：");
    ImGui::SetNextItemWidth(190); if(ImGui::Combo("##结果排序",&query.sort,sorts,4)) dirty=true;
    ImGui::SameLine(); if(ImGui::Button("刷新曲库")) Refresh();
    ImGui::SameLine(); if(ImGui::Button("重载样式")) ReloadStyle();
    // Enter 跳过防抖，但仍不等待后台；查询完成后才提交最新结果。
    if(requestEnter) queryDue=std::chrono::steady_clock::time_point{};
    Poll();
    ImGui::Separator(); ImGui::TextColored(SearchResultTextColor,"%zu 首结果%s  ·  %s",results.size(),SearchPending()?"（搜索中…）":"",composing?"输入法组词中":"↑↓ 选择（可长按） / ←→ 难度 / Enter 定位 / Esc 关闭 / Tab 控件");
    Navigate();
    if(requestEscape && !composing) { open=false; requestEnter=false; }
    if(requestEnter && !composing && catalog && !SearchPending()) {
        for(const auto& r:results) if(catalog->songs[r.index].id==selectedId) { Locate(catalog->songs[r.index],currentDifficulty); break; }
        requestEnter=false;
    }
    requestEscape=false;
    const ImVec4 colors[]={{0.3f,0.7f,1,1},{0.4f,0.85f,0.52f,1},{1,0.74f,0.25f,1},{1,0.35f,0.43f,1},{0.76f,0.52f,1,1}};
    float width=ImGui::GetContentRegionAvail().x;
    if(songListWidth!=width) {ResetSongBodyClick();songListWidth=width;}
    bool songBodyHit=false;
    float diffWidth=std::clamp(width*0.073f,64.0f,92.0f);
    // 结果由搜索框方向键操作；Tab 只在查询、筛选和工具之间循环。
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    if(ImGui::BeginTable("歌曲列表",7,ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH, {0,ImGui::GetContentRegionAvail().y-62})) {
        const float scroll=ImGui::GetScrollY();
        if(songListScroll!=scroll || ImGui::GetIO().MouseWheel!=0) ResetSongBodyClick();
        songListScroll=scroll;
        ImGui::TableSetupColumn("曲名 / 别名 · 来源",ImGuiTableColumnFlags_WidthStretch);
        const char* headers[]={"EASY","NORMAL","HARD","EXTREME","EX EXT"};
        for(int d=0;d<5;++d) ImGui::TableSetupColumn(headers[d],ImGuiTableColumnFlags_WidthFixed,diffWidth);
        ImGui::TableSetupColumn("PVID",ImGuiTableColumnFlags_WidthFixed,78); ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin((int)results.size(),rowHeight);
        // 键盘选中在裁剪范围外时先滚动到目标行，下帧由裁剪器绘制。
        if(scrollSelection) for(size_t i=0;i<results.size();++i) if(catalog->songs[results[i].index].id==selectedId) {
            float top=(float)i*rowHeight,bottom=top+rowHeight;
            float scroll=ImGui::GetScrollY(),visible=ImGui::GetWindowHeight()-ImGui::GetFrameHeightWithSpacing();
            if(top<scroll) ImGui::SetScrollY(top);
            else if(bottom>scroll+visible) ImGui::SetScrollY(std::max(0.0f,bottom-visible));
        }
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto& song=catalog->songs[results[row].index]; ImGui::PushID(song.id);
            ImGui::TableNextRow(0,rowHeight); if(song.id==selectedId) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(41,63,62,255));
            ImGui::TableSetColumnIndex(0); auto pos=ImGui::GetCursorScreenPos();
            auto nameWidth=ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("名称",{nameWidth,rowHeight-16});
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                songBodyHit=true;ClickSongBody(song);
            }
            bool hovered=ImGui::IsItemHovered(); ImGui::SetCursorScreenPos(pos);
            ClippedText(song.name,nameWidth);
            ImGui::PushStyleColor(ImGuiCol_Text,SearchSecondaryTextColor);
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
                bool selected=song.id==selectedId && d==currentDifficulty;
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,selected?3.0f:1.0f);
                char label[40]; if(c.available) snprintf(label,sizeof(label),"%.1f",c.level); else snprintf(label,sizeof(label),"—");
                ImGui::BeginDisabled(!c.available);
                if(ImGui::Button(label,{diffWidth-8,rowHeight-20})) Locate(song,d,SongOrigin::DifficultyButton);
                ImGui::EndDisabled(); ImGui::PopStyleVar(); ImGui::PopStyleColor(5);
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip(); ImGui::TextUnformatted(c.available?c.source.c_str():"该风格没有生效谱面"); if(c.available) ImGui::TextUnformatted(c.script.c_str()); ImGui::EndTooltip();
                } ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(6); ImGui::Text("%04d",song.id); ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopItemFlag();
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !songBodyHit) ResetSongBodyClick();
    scrollSelection=false;
    listNavigation=!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && (searchActive || !ImGui::IsAnyItemFocused());
    ImGui::TextWrapped("%s",status.c_str());
    ImGui::End();
}
}
