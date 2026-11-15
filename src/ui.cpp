#include "ui.h"
#include "search_ui_style.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cmath>
#include <cfloat>

namespace diva {
// 在后台生成独立快照，不持有界面状态引用。
void View::ExportPerformance() {
    auto frames=performance->frames;auto queries=performance->queries;
    SYSTEMTIME time{};GetLocalTime(&time);
    char stamp[32];snprintf(stamp,sizeof(stamp),"%02d%02d%02d",time.wHour,time.wMinute,time.wSecond);
    auto path=home/("search-performance-"+std::string(stamp)+".tsv");
    const auto messageThread=performance->messageThread.load(),frameThread=performance->frameThread.load();
    const auto note="窗口消息线程 "+std::to_string(messageThread)+"，帧回调线程 "+std::to_string(frameThread)+"（"+(messageThread==frameThread?"相同":"不同")+"）";
    // 参数均为样本副本，后台格式化与写入，不访问实时UI或游戏对象。
    performanceExport=searchExecutor->Submit([frames=std::move(frames),queries=std::move(queries),path,note] {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out<<"\xEF\xBB\xBF"<<PerformanceTrace::Report(frames,queries,note);out.close();
        return out?"性能报告已保存："+Utf8(path.wstring()):"性能报告写入失败。";
    });
}
void View::Refresh() {
    if(pending.valid()) return;
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    status=Tr(Tx::song_status_loading);
    auto h=home,g=game;
    // 捕获路径副本，避免刷新任务与关闭界面竞争。
    pending=std::async(std::launch::async,[h,g]{ return LoadCatalog(h,g); });
}
// 发布后台曲库并刷新完整查询结果。
void View::Poll() {
    ConsumeSearchReply(performanceExport,[&](std::string result){status=result;customization.status=std::move(result);},[&]{status=customization.status="性能报告导出失败。";});
    if(requestPerformanceRecord) {
        requestPerformanceRecord=false;
        if(!performance->recording) {performance->Start();performanceAutoExport=true;status=customization.status="正在录制性能数据（15 秒），请开始输入。";}
    }
    if(performanceAutoExport && !performance->recording && !performanceExport.valid()) {performanceAutoExport=false;ExportPerformance();}
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
            status=Trf(Tx::song_status_ready,{{"count",std::to_string(catalog->songs.size())},{"ms",std::to_string((int)catalog->milliseconds)}});
            if(!catalog->diagnostics.empty()) status+=" "+catalog->diagnostics.front();
            if(catalog->requiresRestart) status=Tr(Tx::song_status_restart);
        } catch(...) { status=Tr(Tx::song_status_load_failed); }
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
                status=Tr(Tx::song_status_query_failed);
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
    if(move || difficultyMove) {enterPressTick=0;enterDouble=false;ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();}
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
CustomizationDomain SearchModeDomain(SearchMode mode) {
    return mode==SearchMode::Hair?CustomizationDomain::Hair:mode==SearchMode::Accessories?CustomizationDomain::Accessories:CustomizationDomain::Models;
}
// 模式互切只影响当前领域，不会把定制详情请求交给歌曲定位器，也不触发原生跳页。
void View::SetMode(SearchMode next) {
    if(mode==next) return;
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    customization.HandleEscape();
    mode=next; ClearActions(); enterPressTick=0; focus=true; listNavigation=true;
    horizontalNavigation=mode==SearchMode::Songs;
    if(mode!=SearchMode::Songs) customization.SetDomain(SearchModeDomain(mode));
}
// 顺序与顶部标签一致；Tab 不再遍历控件，只在主标签之间循环。
void View::CycleMode(int step) {
    const int next=(static_cast<int>(mode)+step+4)%4;
    SetMode(static_cast<SearchMode>(next));
}
// 输入宿主在打开时调用，角色未知与已验证读取保持不同语义。
void View::OnOpened() {
    ResetSongBodyClick();if(invalidateSongNavigation) invalidateSongNavigation();
    customization.OnOpen(TryReadCustomizationContext());
    enterPressTick=0; RequestSearchFocus(); listNavigation=true; horizontalNavigation=mode==SearchMode::Songs; ClearActions();
}
// 参数 preferred/context 为已识别的当前页面和可选角色；打开时覆盖上次领域，保留各领域查询。
void View::ApplyOpeningContext(SearchMode preferred,std::optional<CustomizationContext> context) {
    SetMode(preferred);
    if(context) customization.OnOpen(context);
    focus=true;listNavigation=true;horizontalNavigation=mode==SearchMode::Songs;ClearActions();
}
// 只清理尚未处理的 UI 动作，不改变条件或歌曲目标难度。
void View::ClearActions() { requestEnter=requestEscape=enterDouble=requestPreview=requestEquip=false; move=difficultyMove=tabMove=0; textRequest.cursorMove=0; itemMoves.clear(); }
// 第二下 Enter 可能落在定位成功、面板关闭之后，enterGuardUntil 让宿主把它隔离，避免漏给游戏开始演奏。
void View::PressEnter(uint64_t now) {
    enterGuardUntil=now+kDoubleEnterMs;
    const bool twice=enterPressTick && now-enterPressTick<=kDoubleEnterMs;
    // 第三下重新计起，连续敲击不会累积出新的跨页。
    enterPressTick=twice?0:now;
    if(!twice) {enterPvid=0;enterRevision=0;}
    // 已在目标页时第一下已原位定位，第二下不重复提交。
    if(twice && onTargetPage && onTargetPage(mode)) return;
    enterDouble=enterDouble || twice;
    requestEnter=true;
}
// 焦点已在搜索框时 ImGui 不会重新激活，全选由输入框回调完成。
void View::RequestSearchFocus() { focus=true; textRequest.selectAll=true; }
// 提交确切的风格和难度，缺失难度不降级；参数 song/difficulty 为歌曲与请求等级。
void View::Locate(const Song& song,int difficulty,SongOrigin origin) {
    selectedId=song.id;
    if(SearchPending()) { status=Tr(Tx::song_status_updating); return; }
    if(catalog && catalog->requiresRestart) { status=Tr(Tx::song_status_index_stale);return; }
    bool available=song.charts[query.style][difficulty].available || (songLocateWithoutChart && songLocateWithoutChart());
    if(!available) { status=Tr(Tx::song_status_no_chart); return; }
    Request request{song.id,difficulty,query.style,++serial};request.origin=origin;
    request.queryRevision=queryRevision;request.catalogGeneration=songCatalogGeneration;
    if(origin==SongOrigin::BodyDoubleClick) {request.firstClickPvid=songClickPvid;request.firstClickRevision=songClickRevision;}
    if(origin!=SongOrigin::BodySingleClick) ResetSongBodyClick();
    if(submit) submit(request,available);
}
// 首击只保留稳定身份和时序，单次Enter或难度按钮不能继承跨页权限。
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
            if(key=="row_height") rowHeight=std::clamp(std::stof(value),44.0f,100.0f);
            if(key=="opacity") opacity=std::clamp(std::stof(value),0.7f,1.0f);
            if(key=="font_size") { float size=std::clamp(std::stof(value),16.0f,26.0f); if(size!=fontSize) { fontSize=size; fontDirty=true; } }
            if(key=="close_on_success") closeOnSuccess=std::stoi(value)!=0;
            if(key=="ui_theme") {
                std::string themeName;std::istringstream(value)>>themeName;
                if(const auto theme=ParseUiTheme(themeName); theme && *theme!=CurrentUiTheme()) {SetUiTheme(*theme);styleDirty=true;}
            }
            if(key=="ui_color_output") {
                std::string outputMode;std::istringstream(value)>>outputMode;
                if(outputMode=="auto" || outputMode=="sdr" || outputMode=="hdr10" || outputMode=="scrgb") uiOutputMode=outputMode;
            }
            if(key=="ui_white_nits") {
                std::string whiteValue;std::istringstream(value)>>whiteValue;
                if(whiteValue=="auto") uiWhiteNits=0;
                else {
                    const float nits=std::stof(whiteValue);
                    if(std::isfinite(nits)) uiWhiteNits=std::clamp(nits,80.0f,400.0f);
                }
            }
        } catch(...) {}
    }
    if(shortcut==0x47 || shortcut==0x4C || shortcut==0x53) { shortcut=0x46; status=Tr(Tx::song_status_hotkey_conflict); }
}
// 参数 path/key/value 为配置文件、键和新值；替换唯一的同名行，没有时追加到末尾，保留其他行与换行风格。
// 只在后台执行器调用，写入临时文件后替换，避免写到一半的配置。
static bool WriteSettingValue(const fs::path& path,const std::string& key,const std::string& value) {
    std::string text;
    {std::ifstream in(path,std::ios::binary);text.assign(std::istreambuf_iterator<char>(in),{});}
    const std::string newline=text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    std::string out;bool replaced=false;size_t start=0;
    while(start<text.size()) {
        size_t end=text.find('\n',start);const bool last=end==std::string::npos;if(last) end=text.size();
        std::string line=text.substr(start,end-start);if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.starts_with(key+"=")) {if(replaced){start=end+1;continue;}line=key+"="+value;replaced=true;}
        out+=line;if(!last) out+=newline;
        start=end+1;
    }
    if(!replaced) {if(!out.empty() && !out.ends_with("\n")) out+=newline;out+=key+"="+value+newline;}
    const auto temporary=fs::path(path).concat(L".tmp");
    {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file<<out;if(!file) return false;}
    std::error_code error;fs::rename(temporary,path,error);return !error;
}
void View::ToggleTheme() {
    const auto next=CurrentUiTheme()==UiTheme::Pearl?UiTheme::Classic:UiTheme::Pearl;
    SetUiTheme(next);styleDirty=true;
    themeSave=searchExecutor->Submit([path=home/"settings.ini",value=std::string(UiThemeName(next))] {
        return WriteSettingValue(path,"ui_theme",value);
    });
}
// 按界面语言选择字体并套用当前界面主题；参数 size 为基准字号。
// 难度色块专用字体：微软雅黑 UI 的数字，按 19px 正文时为 20px，与参照图一致；当前难度改用同字号的粗体。未找到时回退正文字体。
static ImFont* difficultyFont=nullptr;
static ImFont* difficultyBoldFont=nullptr;
// 歌曲表难度与 PVID 列名专用字体：微软雅黑 UI，按 19px 正文时为 17px；按实际字号栅格化，不靠缩放，字形底部不会被吃掉。
static ImFont* headerFont=nullptr;
void SetupStyle(float size) {
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    // 下拉框弹层内用方向键选择；搜索框内方向键由列表统一处理。
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese()); builder.AddText("★☆·—↑↓←→…"); builder.BuildRanges(&ranges);
    wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows,MAX_PATH);
    const auto fonts=fs::path(windows)/"Fonts";
    const auto yahei=fonts/"msyh.ttc";
    // 简中沿用雅黑；其他语言的歌名多为日文，优先日文字体，避免日文汉字显示成中文字形。
    const bool chinese=CurrentLocale()=="zh-CN";
    const std::vector<fs::path> candidates=chinese?std::vector<fs::path>{yahei,fonts/"YuGothM.ttc",fonts/"meiryo.ttc"}:std::vector<fs::path>{fonts/"YuGothM.ttc",fonts/"meiryo.ttc",yahei};
    const auto primary=std::find_if(candidates.begin(),candidates.end(),[](const fs::path& path){return fs::exists(path);});
    if(primary==candidates.end()) io.Fonts->AddFontDefault();
    else {
        io.Fonts->AddFontFromFileTTF(Utf8(primary->wstring()).c_str(),size,nullptr,ranges.Data);
        // 日文字体缺少部分简体字，中文别名需要时由雅黑补齐。
        if(!chinese && *primary!=yahei && fs::exists(yahei)) {
            ImFontConfig merge;merge.MergeMode=true;
            io.Fonts->AddFontFromFileTTF(Utf8(yahei.wstring()).c_str(),size,&merge,ranges.Data);
        }
    }
    // msyh.ttc 第 2 个字体为微软雅黑 UI；只收录数字和占位破折号，图集开销很小。
    difficultyFont=difficultyBoldFont=headerFont=nullptr;
    if(fs::exists(yahei)) {
        static ImVector<ImWchar> digits;
        if(digits.empty()) {ImFontGlyphRangesBuilder digitBuilder;digitBuilder.AddText("0123456789.—");digitBuilder.BuildRanges(&digits);}
        // 单独栅格化时笔画偏细，略微加深以接近系统 ClearType 下的观感。
        ImFontConfig config;config.FontNo=1;config.RasterizerMultiply=1.15f;
        const float digitSize=std::round(size*20.0f/19.0f);
        difficultyFont=io.Fonts->AddFontFromFileTTF(Utf8(yahei.wstring()).c_str(),digitSize,&config,digits.Data);
        if(const auto bold=fonts/"msyhbd.ttc";fs::exists(bold))
            difficultyBoldFont=io.Fonts->AddFontFromFileTTF(Utf8(bold.wstring()).c_str(),digitSize,&config,digits.Data);
        // 列名只有固定英文，收录基本拉丁字符即可。
        ImFontConfig header;header.FontNo=1;
        headerFont=io.Fonts->AddFontFromFileTTF(Utf8(yahei.wstring()).c_str(),std::round(size*17.0f/19.0f),&header,io.Fonts->GetGlyphRangesDefault());
    }
    ApplyUiTheme(ImGui::GetStyle());
}
// 参数 label/selected/width 为分段名称、当前状态和宽度；保留原生按钮的键盘导航与焦点描边。
static bool SearchSegment(const char* label,bool selected,float width) {
    // 译文较长时加宽按钮；中文标签不会超过传入的基准宽度。
    return ThemedTabButton(label,selected,{std::max(width,ImGui::CalcTextSize(label,nullptr,true).x+28),36});
}
// 在固定宽度中省略长标题；参数 text/width 为文本与最大像素宽度。
// 参数 bold 为真时轻微叠绘加粗，用于选中行曲名。
static void ClippedText(const std::string& text,float width,bool bold=false) {
    std::string shown=text;
    if(ImGui::CalcTextSize(text.c_str()).x>width) {
        while(!shown.empty() && ImGui::CalcTextSize((shown+"…").c_str()).x>width) {
            size_t i=shown.size()-1; while(i>0 && ((unsigned char)shown[i]&0xC0)==0x80) --i; shown.resize(i);
        }
        shown+="…";
    }
    ImGui::TextUnformatted(shown.c_str());
    if(bold) {auto min=ImGui::GetItemRectMin();ImGui::GetWindowDrawList()->AddText({min.x+0.7f,min.y},ImGui::GetColorU32(ImGuiCol_Text),shown.c_str());}
}
// 绘制行首分组标签，以轻微叠绘加粗并突出操作类别；参数 text 为含冒号的中文标签。
static void ControlGroupLabel(const char* text) {
    DrawSearchGroupLabel(text);
}
// 参数 catalog/pack/style 为曲库、来源名和当前风格，返回带歌曲数的下拉框标签，与服装来源筛选格式一致。
static std::string PackFilterLabel(const Catalog& catalog,const std::string& pack,int style) {
    const auto& counts=catalog.packSongCounts[std::clamp(style,0,2)];
    const auto count=counts.find(pack);
    return Trf(Tx::filter_linked,{{"name",pack},{"count",std::to_string(count==counts.end()?0:count->second)}});
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
    std::string windowName;
    if(!ImGui::Begin(ThemedWindowName(Tr(Tx::app_title),windowName),&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    DrawThemedWindowFrame(Tr(Tx::app_title));
    // 外部左键关闭搜索；下拉弹层与输入法候选仍先处理自身输入，宿主保留本次鼠标释放隔离。
    if(ImGui::GetCurrentWindow()->WasActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !composing && !imeGuard
        && !ImGui::GetCurrentWindow()->Rect().Contains(ImGui::GetIO().MousePos)
        && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)) {
        open=false;ResetSongBodyClick();customization.ResetBodyClick();ClearActions();ImGui::End();return;
    }
    if(tabMove && !composing && !imeGuard) {const int step=tabMove;tabMove=0;CycleMode(step);}
    // 模型、发型、自定义道具是原生自定义父页下的三个同级入口，搜索面板分别保存条件；性能诊断不面向玩家开放。
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+ThemedTabOverhang(36));
    if(SearchSegment((std::string(Tr(Tx::tab_songs))+"##领域").c_str(),mode==SearchMode::Songs,100)) SetMode(SearchMode::Songs);
    ImGui::SameLine(0,4); if(SearchSegment((std::string(Tr(Tx::tab_modules))+"##领域").c_str(),mode==SearchMode::Modules,100)) SetMode(SearchMode::Modules);
    ImGui::SameLine(0,4); if(SearchSegment((std::string(Tr(Tx::tab_hair))+"##领域").c_str(),mode==SearchMode::Hair,100)) SetMode(SearchMode::Hair);
    ImGui::SameLine(0,4); if(SearchSegment((std::string(Tr(Tx::tab_accessories))+"##领域").c_str(),mode==SearchMode::Accessories,128)) SetMode(SearchMode::Accessories);
    // 主题切换靠右放在标签行，显示当前主题；空间不足时紧跟最后一个标签。
    {
        const std::string label=Trf(Tx::tab_theme,{{"name",Tr(CurrentUiTheme()==UiTheme::Pearl?Tx::tab_theme_pearl:Tx::tab_theme_classic)}})+"###theme-toggle";
        const float width=ImGui::CalcTextSize(label.c_str(),nullptr,true).x+40;
        ImGui::SameLine(0,12);ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowContentRegionMax().x-width));
        if(ThemedSecondaryButton(label.c_str(),{width,36})) ToggleTheme();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",Tr(Tx::tab_theme_tip));
    }
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    if(mode!=SearchMode::Songs) {
        customization.EnsureLoaded(home,game); customization.composing=composing;
        customization.Poll();
        if(focus) { customization.State().focus=true; focus=false; }
        customization.State().text.selectAll=customization.State().text.selectAll || textRequest.selectAll;
        customization.State().text.cursorMove+=textRequest.cursorMove;textRequest={};
        if(!composing && !imeGuard) {
            // 方向键改变了选择，第一下 Enter 的目标已失效。
            if(!itemMoves.empty()) {enterPressTick=0;enterDouble=false;}
            for(const auto& [action,repeats]:itemMoves) for(int i=0;i<std::min(repeats,1000);++i) customization.Navigate(action);
            if(requestEnter) customization.RequestDetails(enterDouble);
            if(requestPreview) customization.RequestSelectedAction(CustomizationAction::Preview);
            if(requestEquip) customization.RequestSelectedAction(CustomizationAction::Equip);
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
    if(ImGui::InputTextWithHint("##查询",Tr(Tx::song_search_hint),input,sizeof(input),ImGuiInputTextFlags_CallbackAlways,ApplySearchTextRequest,&textRequest)) QueueSearch(true);
    textRequest.cursorMove=0;
    bool searchActive=ImGui::IsItemActive();
    const char* styles[]={Tr(Tx::style_arcade),Tr(Tx::style_console),Tr(Tx::style_mixed)};
    const char* difficulties[]={"EASY","NORMAL","HARD","EXTREME","EXTRA EXTREME"};
    const char* sorts[]={Tr(Tx::sort_relevance),Tr(Tx::sort_pvid_asc),Tr(Tx::sort_name),Tr(Tx::sort_level_desc),Tr(Tx::sort_level_asc)};
    ControlGroupLabel(Tr(Tx::filter_label));
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Tr(Tx::filter_pack)); ImGui::SameLine(); ImGui::SetNextItemWidth(230);
    const std::string packPreview=query.pack.empty()?Tr(Tx::filter_all_sources):catalog?PackFilterLabel(*catalog,query.pack,query.style):query.pack;
    if(ImGui::BeginCombo("##曲包筛选",packPreview.c_str())) {
        if(ImGui::Selectable(Tr(Tx::filter_all_sources),query.pack.empty())) { query.pack.clear(); dirty=true; }
        if(catalog) for(const auto& p:catalog->packs) {
            // 当前风格下没有歌曲的来源不列出；已选中的保留，便于看到并改回。
            const auto& counts=catalog->packSongCounts[std::clamp(query.style,0,2)];
            if(p!=query.pack && !counts.contains(p)) continue;
            const auto label=PackFilterLabel(*catalog,p,query.style)+"##"+p;
            if(ImGui::Selectable(label.c_str(),p==query.pack)) { query.pack=p; dirty=true; }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Tr(Tx::filter_style)); ImGui::SameLine(); ImGui::SetNextItemWidth(170);
    if(ImGui::Combo("##风格筛选",&query.style,styles,3)) dirty=true;
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Tr(Tx::filter_difficulty)); ImGui::SameLine(); ImGui::SetNextItemWidth(165);
    if(ImGui::BeginCombo("##难度筛选",query.difficulty<0?Tr(Tx::filter_all_difficulties):difficulties[query.difficulty])) {
        if(ImGui::Selectable(Tr(Tx::filter_all_difficulties),query.difficulty<0)) { query.difficulty=-1; dirty=true; }
        for(int d=0;d<5;++d) if(ImGui::Selectable(difficulties[d],query.difficulty==d)) { query.difficulty=d; dirty=true; }
        ImGui::EndCombo();
    }
    ControlGroupLabel(Tr(Tx::sort_label));
    ImGui::SetNextItemWidth(190); if(ImGui::Combo("##结果排序",&query.sort,sorts,5)) dirty=true;
    ImGui::SameLine(); if(ImGui::Button(Tr(Tx::song_refresh))) Refresh();
    ImGui::SameLine(); if(ImGui::Button(Tr(Tx::song_reload_style))) ReloadStyle();
    // Enter 跳过防抖，但仍不等待后台；查询完成后才提交最新结果。
    if(requestEnter) queryDue=std::chrono::steady_clock::time_point{};
    Poll();
    ImGui::Separator(); ImGui::TextColored(SearchResultTextColor(),"%s",(Trf(Tx::song_results,{{"count",std::to_string(results.size())}})+(SearchPending()?Tr(Tx::song_searching):"")+"  ·  "+(composing?Tr(Tx::song_composing):Tr(Tx::song_key_hints))).c_str());
    Navigate();
    if(requestEscape && !composing) { open=false; requestEnter=false; }
    if(requestEnter && !composing && catalog && !SearchPending()) {
        for(const auto& r:results) if(catalog->songs[r.index].id==selectedId) {
            const auto& song=catalog->songs[r.index];
            // 第二下只有落在第一下的同一歌曲和查询上才跨页；第一下尚在等待查询时两下目标相同。
            const bool twice=enterDouble && (!enterPvid || (enterPvid==song.id && enterRevision==queryRevision));
            enterPvid=song.id;enterRevision=queryRevision;
            if(twice) {songClickPvid=song.id;songClickRevision=queryRevision;Locate(song,currentDifficulty,SongOrigin::BodyDoubleClick);}
            else Locate(song,currentDifficulty);
            break;
        }
        requestEnter=enterDouble=false;
    }
    requestEscape=false;
    const ImVec4 colors[]={{0.3f,0.7f,1,1},{0.4f,0.85f,0.52f,1},{1,0.74f,0.25f,1},{1,0.35f,0.43f,1},{0.76f,0.52f,1,1}};
    float width=ImGui::GetContentRegionAvail().x;
    if(songListWidth!=width) {ResetSongBodyClick();songListWidth=width;}
    bool songBodyHit=false;
    // 难度为紧凑扁色块，按 19px 字号时 68×33、块间空隙 12px，随字号等比缩放；行高与曲名排版不变。
    const float unit=ImGui::GetFontSize()/19.0f;
    const ImVec2 chip(std::round(68*unit),std::round(33*unit));
    const ImVec2 cellPadding(std::round(6*unit),std::round(6*unit));
    const float diffWidth=chip.x;
    // 按参照图：曲名列左侧内缩 10px；PVID 列与前一色块之间共 72px，数字在这段里居中，表格右侧不再另留边距。
    const float nameInset=std::round(10*unit);
    const float pvidWidth=std::round(72*unit)-cellPadding.x*2;
    const float headerHeight=std::round(40*unit);
    // 参数 width 为文字宽度；返回 PVID 列中文字左端，居中范围包含左侧列间空隙。
    const auto pvidTextX=[&](float width){return ImGui::GetCursorScreenPos().x-cellPadding.x*2+std::round((pvidWidth+cellPadding.x*2-width)*0.5f);};
    // 曲名两行之间留 4px，字形间距与参照图一致；行高不低于两行文字加单元格内边距，避免玩家配置过小时文字重叠。
    const float lineGap=std::round(4*unit);
    // 第二行（别名 · 来源）与曲名同字号，信息同等重要。
    const float nameBlock=ImGui::GetTextLineHeight()*2+lineGap;
    const float songRow=std::max(rowHeight,nameBlock+cellPadding.y*2);
    const float cellContent=songRow-cellPadding.y*2;
    // 结果由搜索框方向键操作，不进入键盘焦点。
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
    const bool playlistSongOnly=songLocateWithoutChart && songLocateWithoutChart();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,cellPadding);
    const bool tableOpen=ImGui::BeginTable("歌曲列表",7,ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH, {0,ImGui::GetContentRegionAvail().y-62});
    ImGui::PopStyleVar();
    if(tableOpen) {
        const float scroll=ImGui::GetScrollY();
        if(songListScroll!=scroll || ImGui::GetIO().MouseWheel!=0) ResetSongBodyClick();
        songListScroll=scroll;
        ImGui::TableSetupColumn(Tr(Tx::song_column_name),ImGuiTableColumnFlags_WidthStretch);
        const char* headers[]={"EASY","NORMAL","HARD","EXTREME","EX EXT"};
        for(int d=0;d<5;++d) ImGui::TableSetupColumn(headers[d],ImGuiTableColumnFlags_WidthFixed,diffWidth);
        ImGui::TableSetupColumn("PVID",ImGuiTableColumnFlags_WidthFixed,pvidWidth); ImGui::TableSetupScrollFreeze(0,1);
        // 自绘表头：加高并让难度与 PVID 列名居中，曲名列与正文同样内缩；各列名与正文同字号。
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers,headerHeight);
        for(int column=0;column<7;++column) {
            ImGui::TableSetColumnIndex(column);ImGui::PushID(column);
            const char* name=ImGui::TableGetColumnName(column);
            const ImVec2 at=ImGui::GetCursorScreenPos();const float available=ImGui::GetContentRegionAvail().x;
            ImGui::TableHeader("##表头");
            ImFont* font=column && headerFont?headerFont:ImGui::GetFont();
            const ImVec2 size=font->CalcTextSizeA(font->FontSize,FLT_MAX,0,name);
            const float x=column==0?at.x+nameInset:column==6?pvidTextX(size.x):at.x+(available-size.x)*0.5f;
            ImGui::GetWindowDrawList()->AddText(font,font->FontSize,{std::round(x),std::round(at.y+(headerHeight-cellPadding.y*2-size.y)*0.5f)},ImGui::GetColorU32(Palette().songMetaText),name);
            ImGui::PopID();
        }
        ImGuiListClipper clipper; clipper.Begin((int)results.size(),songRow);
        // 键盘选中在裁剪范围外时先滚动到目标行，下帧由裁剪器绘制。
        if(scrollSelection) for(size_t i=0;i<results.size();++i) if(catalog->songs[results[i].index].id==selectedId) {
            float top=(float)i*songRow,bottom=top+songRow;
            float scroll=ImGui::GetScrollY(),visible=ImGui::GetWindowHeight()-ImGui::GetFrameHeightWithSpacing();
            if(top<scroll) ImGui::SetScrollY(top);
            else if(bottom>scroll+visible) ImGui::SetScrollY(std::max(0.0f,bottom-visible));
        }
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const auto& song=catalog->songs[results[row].index]; ImGui::PushID(song.id);
            ImGui::TableNextRow(0,songRow); if(song.id==selectedId) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,Palette().songSelectedRow);
            ImGui::TableSetColumnIndex(0); auto pos=ImGui::GetCursorScreenPos();
            // 珍珠白主题在选中行首加初音青指示条；深色主题的指示色透明，不绘制。
            if(song.id==selectedId && (Palette().songSelectedIndicator>>IM_COL32_A_SHIFT)) {
                const auto padding=cellPadding;
                const ImVec2 barMin(pos.x-padding.x,pos.y-padding.y),barMax(barMin.x+4,barMin.y+songRow);
                // 表格单元格裁剪不含左内边距，单独放开到指示条范围。
                ImGui::PushClipRect(barMin,barMax,false);
                ImGui::GetWindowDrawList()->AddRectFilled(barMin,barMax,Palette().songSelectedIndicator);
                ImGui::PopClipRect();
            }
            ImGui::InvisibleButton("名称",{ImGui::GetContentRegionAvail().x,cellContent});
            const float nameWidth=ImGui::GetItemRectSize().x-nameInset;
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                songBodyHit=true;ClickSongBody(song);
            }
            // 曲名与别名两行作为一个整体在行内垂直居中；中日文字形在行框里偏下约 1px，上移后视觉居中。
            bool hovered=ImGui::IsItemHovered(); ImGui::SetCursorScreenPos({pos.x+nameInset,pos.y+std::round((cellContent-nameBlock)*0.5f-unit)});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,lineGap});
            ImGui::PushStyleColor(ImGuiCol_Text,Palette().songTitleText);
            ClippedText(SongDisplayTitle(song),nameWidth,song.id==selectedId);
            ImGui::PopStyleColor(); ImGui::PushStyleColor(ImGuiCol_Text,Palette().songAliasText);
            // 英文界面的主标题已是英文名，第二行改为原名。
            auto alias=PrefersEnglishTitles()?song.name:song.aliases.empty()?song.english:song.aliases;
            if(auto separator=alias.find('|');separator!=std::string::npos) alias.resize(separator);
            if(alias==SongDisplayTitle(song)) alias.clear();
            ImGui::SetCursorScreenPos({pos.x+nameInset,ImGui::GetCursorScreenPos().y});
            ClippedText((alias.empty()?"":alias+" · ")+song.source,nameWidth);
            ImGui::PopStyleColor(); ImGui::PopStyleVar();
            if(hovered) {
                ImGui::BeginTooltip(); ImGui::TextUnformatted(song.name.c_str()); ImGui::TextUnformatted(song.english.c_str()); ImGui::TextUnformatted(song.reading.c_str()); ImGui::TextUnformatted(song.aliases.c_str());
                for(const auto& p:song.sources) ImGui::TextUnformatted(Trf(Tx::song_tooltip_source,{{"source",p}}).c_str()); ImGui::EndTooltip();
            }
            for(int d=0;d<5;++d) {
                ImGui::TableSetColumnIndex(d+1); const auto& c=song.charts[query.style][d];
                ImGui::PushID(d);
                // 难度按钮使用独立不透明底色和彩色边框，避免被选中行的青绿色底色淹没。
                const auto tint=DifficultyButtonColors(d,colors[d],c.available);
                ImGui::PushStyleColor(ImGuiCol_Text,tint.text);
                ImGui::PushStyleColor(ImGuiCol_Button,tint.bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,tint.hovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,tint.active);
                ImGui::PushStyleColor(ImGuiCol_Border,tint.border);
                bool selected=song.id==selectedId && d==currentDifficulty;
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,selected?2.0f:1.0f);
                char label[40]; if(c.available) snprintf(label,sizeof(label),"%.1f",c.level); else snprintf(label,sizeof(label),"—");
                ImGui::BeginDisabled(!c.available && !playlistSongOnly);
                ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x+(ImGui::GetContentRegionAvail().x-chip.x)*0.5f,pos.y+(cellContent-chip.y)*0.5f});
                // 扁色块高度只比字高多一点，去掉上下内边距，数字才能居中。
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{0,0});
                if(ImGui::Button("##难度",chip)) Locate(song,d,SongOrigin::DifficultyButton);
                {
                    // 数字用难度专用字体自绘居中；当前难度换粗体。
                    ImFont* font=selected && difficultyBoldFont?difficultyBoldFont:difficultyFont?difficultyFont:ImGui::GetFont();
                    const ImVec2 min=ImGui::GetItemRectMin(),size=font->CalcTextSizeA(font->FontSize,FLT_MAX,0,label);
                    const ImVec2 at(std::round(min.x+(chip.x-size.x)*0.5f),std::round(min.y+(chip.y-size.y)*0.5f));
                    auto* draw=ImGui::GetWindowDrawList();const ImU32 color=ImGui::GetColorU32(ImGuiCol_Text);
                    draw->AddText(font,font->FontSize,at,color,label);
                }
                ImGui::EndDisabled(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(5);
                if(!playlistSongOnly && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip(); ImGui::TextUnformatted(c.available?c.source.c_str():Tr(Tx::song_no_chart_style)); if(c.available) ImGui::TextUnformatted(c.script.c_str()); ImGui::EndTooltip();
                } ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(6);
            {char pvid[16];snprintf(pvid,sizeof(pvid),"%04d",song.id);
             ImGui::SetCursorScreenPos({pvidTextX(ImGui::CalcTextSize(pvid).x),pos.y+(cellContent-ImGui::GetTextLineHeight())*0.5f});}
            ImGui::TextColored(Palette().songMetaText,"%04d",song.id); ImGui::PopID();
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
