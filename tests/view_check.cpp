#include "ui.h"
#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 参数 value/text 为验收条件和失败说明。
static void Check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
// 参数 view 为待完成查询的视图；只用于测试，不进入游戏帧回调。
static void Complete(diva::View& view) {
    auto deadline=GetTickCount64()+5000;
    while(view.SearchPending() && GetTickCount64()<deadline) { view.Poll(); Sleep(1); }
    Check(!view.SearchPending(),"后台查询未在测试时限内完成");
}
// 参数 view 为定制视图；等待当前领域查询，用于验证外层入口与后台查询接线。
static void CompleteCustomization(diva::CustomizationView& view) {
    auto deadline=GetTickCount64()+5000;
    while(view.SearchPending() && GetTickCount64()<deadline) {view.Poll();Sleep(1);}
    Check(!view.SearchPending(),"定制后台查询未在测试时限内完成");
}
// 构造存在缺失难度的最小曲库，验证跳过不可用谱面。
static diva::Catalog Fixture() {
    diva::Catalog catalog;
    for(int id=1;id<=3;++id) {
        diva::Song song; song.id=id; song.name=id==1?"Alpha":id==2?"Beta":"Gamma";
        song.charts[0][1].available=id==1;
        song.charts[0][3].available=true;
        song.charts[0][4].available=id==2;
        diva::PrepareSong(song); catalog.songs.push_back(song);
    }
    return catalog;
}
// 参数 view 为视图；用真实 ImGui 帧验证 Enter 等待最新查询，不创建游戏或 DX11 对象。
static void Draw(diva::View& view) { ImGui::NewFrame(); view.Draw(); ImGui::EndFrame(); }
// 参数 argc/argv 为保留的命令行参数；验收不需要游戏安装目录。
int main(int argc,char** argv) {
    (void)argc; (void)argv; SetConsoleOutputCP(CP_UTF8);
    try {
        diva::View view; view.catalog=std::make_shared<diva::Catalog>(Fixture()); view.Poll(); Complete(view);
        Check(view.results.size()==3 && view.selectedId==1,"初始异步结果或选择错误");
        view.move=2; view.Navigate(); Check(view.selectedId==3,"同帧累计导航未生效");
        view.move=100; view.Navigate(); Check(view.selectedId==3,"下边界越界");
        view.move=-100; view.Navigate(); Check(view.selectedId==1,"上边界越界");
        view.currentDifficulty=3; view.difficultyMove=-1; view.Navigate(); Check(view.currentDifficulty==1,"左键未跳过缺失难度");
        view.difficultyMove=-10; view.Navigate(); Check(view.currentDifficulty==1,"难度左边界回绕");
        view.difficultyMove=1; view.Navigate(); Check(view.currentDifficulty==3,"右键未跳过缺失难度");
        view.move=1; view.Navigate(); Check(view.selectedId==2 && view.currentDifficulty==3,"换曲丢失难度偏好");
        view.difficultyMove=1; view.Navigate(); Check(view.currentDifficulty==4,"无法选择 EX EXT");
        view.move=1; view.Navigate(); Check(view.currentDifficulty==4,"换曲意外自动降级");
        std::strcpy(view.input,"Al"); view.QueueSearch(true); view.Poll();
        Check(!view.searching.valid() && view.SearchPending(),"防抖期间提前启动搜索");
        std::strcpy(view.input,"Beta"); view.QueueSearch(true); view.queryDue={}; view.Poll(); Complete(view);
        Check(view.results.size()==1 && view.selectedId==2,"防抖没有使用最终文本");
        // 模拟旧任务在新文本之后返回，包含故意越界的索引，确保不会发布。
        std::promise<diva::View::SearchReply> old;
        view.searching=old.get_future(); view.searchingRevision=view.queryRevision;
        auto revision=view.queryRevision;
        std::strcpy(view.input,"Gamma"); view.QueueSearch(false);
        old.set_value({revision,{{99999,1}}}); view.Poll(); Complete(view);
        Check(view.results.size()==1 && view.selectedId==3,"旧查询覆盖了新输入");
        view.composing=true; std::strcpy(view.input,"Alpha"); view.QueueSearch(false); view.Poll();
        Check(!view.searching.valid(),"输入法组词期间启动搜索");
        view.composing=false; view.Poll(); Complete(view);
        // 替换曲库时旧索引必须失效，即使旧查询也恰好已完成。
        std::promise<diva::Catalog> refresh;
        view.pending=refresh.get_future();
        std::promise<diva::View::SearchReply> outdated;
        view.searching=outdated.get_future(); view.searchingRevision=view.queryRevision;
        outdated.set_value({view.queryRevision,{{99999,1}}});
        auto next=Fixture(); next.songs.erase(next.songs.begin());
        refresh.set_value(std::move(next)); view.Poll(); Complete(view);
        Check(view.results.empty() && view.selectedId==0,"曲库替换保留失效结果");
        auto context=ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize={1440,960}; io.DeltaTime=1.0f/60.0f;
        unsigned char* pixels=nullptr; int width=0,height=0; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        view.open=true; view.currentDifficulty=3;
        std::promise<diva::View::SearchReply> blocked;
        view.searching=blocked.get_future(); view.searchingRevision=view.queryRevision;
        revision=view.queryRevision;
        std::strcpy(view.input,"Beta"); view.QueueSearch(true); view.requestEnter=true;
        int submitted=0;
        // 参数 request/available 为真实 Draw 提交的目标和有效性。
        view.submit=[&](diva::Request request,bool available) { Check(available && request.pvid==2 && request.difficulty==3,"Enter 定位了旧目标"); ++submitted; };
        Draw(view); Check(submitted==0 && view.requestEnter,"查询未完成时 Enter 被丢失或提前提交");
        blocked.set_value({revision,{}}); view.Poll(); Complete(view); Draw(view); Draw(view);
        Check(submitted==1 && !view.requestEnter,"延迟 Enter 未且仅提交一次");
        view.focus=true; Draw(view); Draw(view); Check(view.listNavigation,"搜索框未获得列表键盘焦点");
        std::strcpy(view.input,"Gamma"); view.QueueSearch(true); view.requestEnter=view.requestEscape=true;
        Draw(view); Check(!view.open && !view.requestEnter && submitted==1,"Esc 没有取消等待中的 Enter 定位");
        auto songInput=std::string(view.input);auto songDifficulty=view.currentDifficulty;
        auto items=std::make_shared<diva::CustomizationCatalog>();items->sources.push_back({"fixture","测试来源","","",false});
        for(int id=0;id<20;++id) {
            diva::CustomizationItem item;item.key={diva::CustomizationKind::Module,id};item.name="Module "+std::to_string(id);
            item.character=diva::CustomizationCharacter::Miku;item.definitionSource=item.nameSource="fixture";
            item.sources.push_back({"fixture",diva::CustomizationContributionRole::Definition,true});diva::PrepareCustomizationItem(item,*items);items->items.push_back(item);
        }
        int moduleSubmitted=0;int moduleSubmittedId=-1;
        diva::CustomizationLocateRequest lastModuleRequest;
        // 参数 request 为服装稳定定位请求；确认Enter只提交当前选择，不调用歌曲回调。
        view.customization.submitModuleLocate=[&](const diva::CustomizationLocateRequest& request) {++moduleSubmitted;moduleSubmittedId=request.key.id;lastModuleRequest=request;};
        view.customization.catalog=items;++view.customization.generation;
        view.requestEnter=true;view.move=2;view.difficultyMove=1;
        view.SetMode(diva::SearchMode::Modules);view.open=true;view.OnOpened();
        Check(!view.requestEnter && !view.move && !view.difficultyMove && view.itemMoves.empty(),"切换领域未清空歌曲动作");
        CompleteCustomization(view.customization);Draw(view);Draw(view);
        Check(view.listNavigation && view.horizontalNavigation,"服装输入焦点未支持左右结果导航");
        view.customization.requestGridFocus=true;Draw(view);Draw(view);
        Check(view.customization.gridNavigation,"无法从搜索框进入定制格子");
        view.itemMoves={{diva::CustomizationNavigation::Down,1},{diva::CustomizationNavigation::Right,1}};Draw(view);
        Check(view.customization.State().cursor.selected==size_t(view.customization.columns+1),"外层没有按顺序路由格子方向动作");
        view.requestEnter=true;Draw(view);
        Check(moduleSubmitted==1 && moduleSubmittedId==static_cast<int>(view.customization.State().cursor.selected)
            && submitted==1,"服装 Enter 未提交当前项或误触歌曲定位");
        io.AddKeyEvent(ImGuiKey_Escape,true);view.requestEscape=true;Draw(view);io.AddKeyEvent(ImGuiKey_Escape,false);Draw(view);
        Check(!view.open && moduleSubmitted==1,"Esc 未关闭浏览窗口或重复提交服装定位");
        auto capabilities=diva::GetCustomizationCapabilities();
        Check(!capabilities.canLocate && !capabilities.canPreviewNative && !diva::TryReadCustomizationContext(),"未验证的定制原生能力意外启用");
        std::strcpy(view.customization.State().input,"Module");view.customization.QueueSearch(false);
        view.SetMode(diva::SearchMode::Accessories);
        Check(view.customization.State().input[0]==0,"饰品未独立保存搜索条件");
        view.SetMode(diva::SearchMode::Hair);
        Check(view.customization.domain==diva::CustomizationDomain::Hair && view.customization.State().input[0]==0,"发型未作为独立领域保存搜索条件");
        std::strcpy(view.customization.State().input,"发型");view.customization.QueueSearch(false);
        view.SetMode(diva::SearchMode::Accessories);
        Check(view.customization.domain==diva::CustomizationDomain::Accessories && view.customization.State().input[0]==0,"自定义道具继承了发型搜索条件");
        view.SetMode(diva::SearchMode::Modules);
        Check(std::string(view.customization.State().input)=="Module","切回服装丢失搜索条件");
        view.SetMode(diva::SearchMode::Hair);Check(std::string(view.customization.State().input)=="发型","切回发型丢失搜索条件");
        view.SetMode(diva::SearchMode::Modules);
        view.customization.State().detailsRequested=true;
        view.SetMode(diva::SearchMode::Songs);Check(!view.customization.State().detailsRequested,"返回歌曲遗留待打开的定制详情");
        Check(std::string(view.input)==songInput && view.currentDifficulty==songDifficulty,"定制浏览改变歌曲条件");
        view.requestEnter=true;view.itemMoves={{diva::CustomizationNavigation::Right,1}};
        const auto preservedCharacter=view.customization.State().query.character;
        view.ApplyOpeningContext(diva::SearchMode::Modules,diva::CustomizationContext{diva::CustomizationCharacter::Kaito,-1,42,true});
        Check(view.mode==diva::SearchMode::Modules && view.customization.State().query.character==preservedCharacter,
            "模型页面打开覆盖了用户的角色筛选");
        Check(!view.requestEnter && view.itemMoves.empty(),"页面自动路由遗留旧领域动作");
        view.ApplyOpeningContext(diva::SearchMode::Songs);
        Check(view.mode==diva::SearchMode::Songs && std::string(view.input)==songInput && view.currentDifficulty==songDifficulty,
            "回到选曲页打开没有恢复歌曲搜索或丢失条件");
        view.open=true;view.SetMode(diva::SearchMode::Modules);
        auto& module=view.customization;module.State().query.character=diva::CustomizationCharacter::Miku;
        module.State().input[0]=0;module.QueueSearch(false);CompleteCustomization(module);Draw(view);Draw(view);
        ImGuiWindow* grid=nullptr;
        for(auto* window:GImGui->Windows) if(std::strstr(window->Name,"module-grid")) grid=window;
        Check(grid!=nullptr && !module.State().results.empty(),"鼠标定位回归缺少真实网格窗口或结果");
        const ImVec2 point(grid->InnerRect.Min.x+30,grid->InnerRect.Min.y+30);
        io.AddMousePosEvent(point.x,point.y);Draw(view);
        const auto beforeClick=moduleSubmitted;
        io.AddMouseButtonEvent(0,true);Draw(view);io.AddMouseButtonEvent(0,false);Draw(view);
        Check(moduleSubmitted==beforeClick+1,"单击服装卡片没有恰好提交一次定位");
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::CardSingleClick,"主体单击来源错误");
        io.AddMouseButtonEvent(0,true);Draw(view);io.AddMouseButtonEvent(0,false);Draw(view);
        if(lastModuleRequest.origin!=diva::CustomizationOrigin::CardDoubleClick)
            std::cerr<<"双击诊断：来源="<<int(lastModuleRequest.origin)<<"；首击存在="<<module.bodyClickKey.has_value()<<"；修订="<<module.bodyClickRevision<<"；当前="<<module.State().revision<<"；双击标志="<<module.bodyDoubleClick<<"\n";
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::CardDoubleClick
            && lastModuleRequest.firstClickKey==lastModuleRequest.key
            && lastModuleRequest.firstClickRevision==lastModuleRequest.queryRevision,"真实主体双击未绑定稳定ID与查询修订");
        Check(diva::CheckCustomizationNavigation(lastModuleRequest,true).empty(),"经过核对的双击被网关误拒绝");
        module.ResetBodyClick();
        io.AddMouseButtonEvent(0,true);Draw(view);io.AddMouseButtonEvent(0,false);Draw(view);
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::CardSingleClick,"失效首击仍授予跨页权限");
        // 参数key为按钮所在卡片；演示能力仅用于隔离输入测试，生产不导入该能力。
        module.actionCapability=[](const diva::CustomizationKey& key){(void)key;diva::CustomizationActionCapability c;c.targetPage=c.preview=c.equip=true;return c;};
        int actions=0;diva::CustomizationLocateRequest lastAction;
        // 参数request为真实ImGui按钮派发的稳定请求。
        module.submitModuleAction=[&](const diva::CustomizationLocateRequest& request){++actions;lastAction=request;};
        module.State().cursor.selected=1;Draw(view);
        const auto selectedBeforeAction=module.State().cursor.selected;
        // 当前第一张卡片操作条位于图片区底部；不点击名称，不先更改选择。
        const ImVec2 actionPoint(grid->DC.CursorStartPos.x+35,grid->DC.CursorStartPos.y+258);
        io.AddMousePosEvent(actionPoint.x,actionPoint.y);Draw(view);const int beforeActions=moduleSubmitted;
        for(int click=0;click<2;++click) {
            io.AddMouseButtonEvent(0,true);Draw(view);io.AddMouseButtonEvent(0,false);Draw(view);
            if(actions==0) std::cerr<<"动作释放诊断：次数="<<click<<"；状态="<<module.status<<"；活动="<<GImGui->ActiveId<<"；查询中="<<module.SearchPending()<<"；目录需重启="<<module.catalogRequiresRestart<<"；滚动="<<grid->Scroll.y<<"\n";
        }
        if(actions!=1 || moduleSubmitted!=beforeActions) std::cerr<<"按钮诊断：动作数="<<actions<<"；定位前="<<beforeActions<<"；定位后="<<moduleSubmitted<<"；屏蔽释放="<<module.suppressActionRelease<<"；坐标="<<actionPoint.x<<","<<actionPoint.y<<"\n";
        Check(actions==1 && moduleSubmitted==beforeActions,"按钮双击重复动作或冒泡成主体定位");
        Check(lastAction.key.id==0 && lastAction.action==diva::CustomizationAction::Equip
            && module.State().cursor.selected==selectedBeforeAction,"按钮借用了其他选择或修改搜索选择");
        io.AddMousePosEvent(-100,-100);Draw(view);
        auto invalidDouble=lastModuleRequest;invalidDouble.origin=diva::CustomizationOrigin::CardDoubleClick;
        invalidDouble.firstClickKey=diva::CustomizationKey{diva::CustomizationKind::Module,999};
        Check(!diva::CheckCustomizationNavigation(invalidDouble,true).empty(),"跨卡片双击通过网关");
        invalidDouble.firstClickKey=invalidDouble.key;invalidDouble.firstClickRevision=invalidDouble.queryRevision+1;
        Check(!diva::CheckCustomizationNavigation(invalidDouble,true).empty(),"旧查询双击通过网关");
        for(auto origin:{diva::CustomizationOrigin::CardSingleClick,diva::CustomizationOrigin::Keyboard,diva::CustomizationOrigin::Footer,diva::CustomizationOrigin::HoverButton}) {
            invalidDouble.origin=origin;
            Check(!diva::CheckCustomizationNavigation(invalidDouble,true).empty(),"非双击输入获得跨页权限");
        }
        lastAction.origin=diva::CustomizationOrigin::CardDoubleClick;lastAction.firstClickKey=lastAction.key;lastAction.firstClickRevision=lastAction.queryRevision;
        Check(!diva::CheckCustomizationNavigation(lastAction,true).empty(),"换装伪造双击来源获得导航权限");
        size_t metadataReads=0;
        // 参数item/visible区分布局尺寸读取和可见图片请求，不创建GPU纹理。
        module.previewProvider=[&](const diva::CustomizationItem& item,bool visible) {
            (void)item;if(!visible)++metadataReads;diva::CustomizationPreview p;p.width=408;p.height=494;return p;
        };
        ++module.previewMetadataGeneration;Draw(view);const auto firstReads=metadataReads;
        for(int frame=0;frame<20;++frame)Draw(view);
        Check(firstReads==module.State().results.size() && metadataReads==firstReads,"静止帧反复遍历全量服装预览元数据");
        ++module.previewMetadataGeneration;Draw(view);
        Check(metadataReads==firstReads*2,"资源发布后未刷新布局尺寸缓存");
        // 定制领域连按 Enter 的跨页身份要能通过导航网关。
        view.PressEnter(1000);Draw(view);Draw(view);
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::Keyboard,"定制第一下Enter获得跨页权限");
        view.PressEnter(1100);Draw(view);Draw(view);
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::CardDoubleClick && diva::CheckCustomizationNavigation(lastModuleRequest,true).empty(),"定制连按两次Enter未通过导航网关");
        view.PressEnter(1150);Draw(view);Draw(view);
        Check(lastModuleRequest.origin==diva::CustomizationOrigin::Keyboard,"定制第三下Enter累积出跨页权限");
        view.RequestSearchFocus();for(int frame=0;frame<3;++frame)Draw(view);
        Check(!module.State().text.selectAll && !view.textRequest.selectAll,"定制搜索框未消费全选请求");
        diva::View songs;songs.catalog=std::make_shared<diva::Catalog>(Fixture());songs.open=true;songs.currentDifficulty=3;
        songs.Poll();Complete(songs);std::vector<diva::Request> songRequests;
        // 参数request/available为真实歌曲名称和难度按钮提交，不跳过生产命中逻辑。
        songs.submit=[&](diva::Request request,bool available) {Check(available,"歌曲输入提交缺失谱面");songRequests.push_back(request);};
        Draw(songs);Draw(songs);
        ImGuiWindow* songList=nullptr;
        for(auto* window:GImGui->Windows) if(std::strstr(window->Name,"歌曲列表")) songList=window;
        Check(songList!=nullptr,"歌曲回归缺少实际表格子窗口");
        const ImVec2 songPoint(songList->InnerRect.Min.x+40,songList->DC.CursorStartPos.y+70);
        io.AddMousePosEvent(songPoint.x,songPoint.y);Draw(songs);
        io.AddMouseButtonEvent(0,true);Draw(songs);io.AddMouseButtonEvent(0,false);Draw(songs);
        Check(songRequests.size()==1 && songRequests.back().origin==diva::SongOrigin::BodySingleClick,"歌曲主体单击来源或提交次数错误");
        io.AddMouseButtonEvent(0,true);Draw(songs);io.AddMouseButtonEvent(0,false);Draw(songs);
        Check(songRequests.size()==2 && songRequests.back().origin==diva::SongOrigin::BodyDoubleClick
            && songRequests.back().firstClickPvid==songRequests.back().pvid
            && songRequests.back().firstClickRevision==songs.queryRevision,"歌曲主体双击未绑定真实PVID和修订");
        songs.ResetSongBodyClick();
        io.AddMouseButtonEvent(0,true);Draw(songs);io.AddMouseButtonEvent(0,false);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::BodySingleClick,"歌曲首击失效后仍携带双击权限");
        songs.requestEnter=true;Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::Keyboard && !songRequests.back().firstClickPvid,"歌曲Enter继承了主体跨页权限");
        // 连按 Enter：第一下不跨页，同项第二下才带跨页权限；中途换选择或超时都重新计起。
        songs.PressEnter(1000);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::Keyboard,"第一下Enter获得跨页权限");
        songs.PressEnter(1200);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::BodyDoubleClick && songRequests.back().firstClickPvid==songRequests.back().pvid
            && songRequests.back().firstClickRevision==songs.queryRevision,"连按两次Enter未绑定歌曲和修订");
        songs.PressEnter(1300);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::Keyboard,"第三下Enter累积出跨页权限");
        songs.PressEnter(5000);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::Keyboard,"超时后的Enter获得跨页权限");
        songs.move=1;songs.Navigate();songs.PressEnter(5100);Draw(songs);
        Check(songRequests.back().origin==diva::SongOrigin::Keyboard,"换选择后的Enter继承跨页权限");
        songs.onTargetPage=[](diva::SearchMode){return true;};
        songs.PressEnter(9000);Draw(songs);const auto onPage=songRequests.size();
        songs.PressEnter(9100);Check(!songs.requestEnter,"目标页第二下Enter被再次提交");Draw(songs);
        Check(songRequests.size()==onPage,"目标页第二下Enter产生了重复定位");
        songs.onTargetPage=nullptr;
        Check(songs.enterGuardUntil==9100+diva::kDoubleEnterMs,"第二下Enter后未延长游戏输入隔离");
        // 再次 Ctrl+F：焦点已在搜索框时也要全选当前文字。
        std::strcpy(songs.input,"Alpha");songs.QueueSearch(false);Complete(songs);
        songs.RequestSearchFocus();for(int frame=0;frame<3;++frame)Draw(songs);
        {
            auto* textState=ImGui::GetInputTextState(ImGui::GetActiveID());
            Check(textState && !songs.textRequest.selectAll && textState->Stb.select_start==0 && textState->Stb.select_end==5,"Ctrl+F 未全选搜索文字");
            songs.RequestSearchFocus();Draw(songs);Draw(songs);
            textState=ImGui::GetInputTextState(ImGui::GetActiveID());
            Check(textState && !songs.textRequest.selectAll && textState->Stb.select_end==5,"焦点在搜索框时再次 Ctrl+F 未全选");
        }
        // Ctrl+←→ 按字符移动光标并取消全选，不改变文字。
        songs.textRequest.cursorMove=-1;Draw(songs);Draw(songs);
        {
            auto* textState=ImGui::GetInputTextState(ImGui::GetActiveID());
            Check(textState && textState->Stb.cursor==4 && textState->Stb.select_start==textState->Stb.select_end && !songs.textRequest.cursorMove,"Ctrl+← 未把光标左移一个字符");
        }
        {
            // 光标步进必须落在 UTF-8 字符边界，中文不能被切成半个字。
            char text[]="千本";diva::SearchTextRequest request;request.cursorMove=-1;
            ImGuiInputTextCallbackData data{};data.Buf=text;data.BufTextLen=6;data.CursorPos=6;data.UserData=&request;
            diva::ApplySearchTextRequest(&data);Check(data.CursorPos==3 && data.SelectionStart==3 && data.SelectionEnd==3 && !request.cursorMove,"左移未跨过完整中文字符");
            request.cursorMove=2;diva::ApplySearchTextRequest(&data);Check(data.CursorPos==6,"右移越过文本末尾");
        }
        {
            // 性能报告要带上判断输入法卡顿所需的列和线程说明，不记录查询文字。
            diva::PerformanceFrame frame;frame.queueDelay=12;frame.imeDefault=34;frame.frameLock=56;
            const auto report=diva::PerformanceTrace::Report({frame},{},"窗口消息线程 1，帧回调线程 1（相同）");
            Check(report.find("消息排队\t输入法默认处理")!=std::string::npos && report.find("线程\t窗口消息线程 1")!=std::string::npos
                && report.find("12.000\t34.000\n")!=std::string::npos && report.find("56.000\t12.000\t34.000")!=std::string::npos,"性能报告缺少输入法测量列");
            diva::PerformanceTrace trace;trace.ImeDefault(1.5);trace.QueueDelay(7);trace.Begin(1);trace.End(2);
            Check(trace.last.imeDefault==1.5 && trace.last.queueDelay==7,"输入法测量未计入当前帧");
            trace.Begin(3);trace.End(4);Check(trace.last.imeDefault==0 && trace.last.queueDelay==0,"输入法测量跨帧残留");
        }
        {
            diva::View cycle;
            for(auto expected:{diva::SearchMode::Modules,diva::SearchMode::Hair,diva::SearchMode::Accessories,diva::SearchMode::Songs}) {cycle.CycleMode(1);Check(cycle.mode==expected,"Tab 未按顶部顺序循环主标签");}
            cycle.CycleMode(-1);Check(cycle.mode==diva::SearchMode::Accessories,"Ctrl+Tab 未回切到上一个主标签");
        }
        std::strcpy(songs.input,"");songs.QueueSearch(false);Complete(songs);
        songs.songClickPvid=1;songs.songClickRevision=songs.queryRevision;songs.QueueSearch(false);
        Check(!songs.songClickPvid,"歌曲查询更新没有清除首击身份");Complete(songs);
        songs.songClickPvid=1;songs.composing=true;Draw(songs);
        Check(!songs.songClickPvid,"歌曲IME组词没有清除首击身份");songs.composing=false;
        songs.open=true;Draw(songs);const auto beforeOutside=songRequests.size();
        io.AddMousePosEvent(1,1);io.AddMouseButtonEvent(0,true);Draw(songs);io.AddMouseButtonEvent(0,false);Draw(songs);
        Check(!songs.open && songRequests.size()==beforeOutside,"点击窗口外部未关闭搜索或误提交歌曲");
        ImGui::DestroyContext(context);
        std::cout<<"视图行为通过：歌曲防抖与导航、Tab 切换主标签、领域隔离、格子动作、定制详情与禁用原生定位。\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"视图验证失败："<<error.what()<<"\n"; return 1; }
}
