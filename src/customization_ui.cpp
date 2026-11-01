#include "customization_ui.h"
#include "search_ui_style.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace diva {
namespace {
constexpr float kPreviewHeight=280.0f;
constexpr float kCardHeight=340.0f;
constexpr float kGridGap=12.0f;
constexpr float kMinimumCardWidth=204.0f;
constexpr float kWideAspectThreshold=1.35f;
constexpr auto kHoverDelay=std::chrono::milliseconds(350);

// 参数 view 为当前服装搜索界面；列表或条件变化时清理旧悬浮身份。
void ClearHoveredItem(CustomizationView& view) {
    view.hoveredKey.reset();
    view.hoverSince={};
}

// 参数 catalog/id 为当前目录和来源稳定ID，返回包含来源身份的完整显示名。
std::string SourceLabel(const CustomizationCatalog& catalog,const std::string& id) {
    auto source=std::find_if(catalog.sources.begin(),catalog.sources.end(),[&](const auto& value){return value.id==id;});
    if(source==catalog.sources.end()) return id;
    const auto location=source->modDirectory.empty()?source->id:source->modDirectory+"/"+source->includeRoot;
    return source->name+" ["+location+"]"+((source->currentMod || source->id.starts_with("official:"))?"":"（导入）");
}

// 参数 catalog/id/counts 为目录、来源稳定ID和关联数量快照，返回下拉框短标签。
std::string SourceFilterLabel(const CustomizationCatalog& catalog,const std::string& id,const std::map<std::string,size_t>& counts) {
    auto source=std::find_if(catalog.sources.begin(),catalog.sources.end(),[&](const auto& value){return value.id==id;});
    if(source==catalog.sources.end()) return id;
    auto count=counts.find(id);
    const size_t associated=count==counts.end()?0:count->second;
    return source->name+"（关联 "+std::to_string(associated)+"）";
}

// 参数 text/width 为卡片名称和最大像素宽度；UTF-8 截断后保留省略号。
std::string EllipsizeText(const std::string& text,float width) {
    if(width<=0 || ImGui::CalcTextSize(text.c_str()).x<=width) return text;
    std::string clipped=text;
    const std::string suffix="…";
    while(!clipped.empty() && ImGui::CalcTextSize((clipped+suffix).c_str()).x>width) {
        size_t at=clipped.size()-1;
        while(at>0 && (static_cast<unsigned char>(clipped[at])&0xC0)==0x80) --at;
        clipped.resize(at);
    }
    return clipped.empty()?suffix:clipped+suffix;
}

// 参数 view/item/preview 为悬浮详情绘制对象与当前图片快照。
void DrawItemDetails(CustomizationView& view,const CustomizationItem& item,const CustomizationPreview& preview) {
    (void)view;(void)preview;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize()*24);
    ImGui::TextUnformatted(item.name.c_str());
    const auto reasons=view.State().recommendationReasons.find(item.key);
    if(reasons!=view.State().recommendationReasons.end()) for(const auto& reason:reasons->second) ImGui::TextWrapped("推荐歌曲：%s",reason.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// 参数 preview 为已知精灵尺寸；判断是否适合占用两列。
bool IsWidePreview(const CustomizationPreview& preview) {
    return preview.width>0 && preview.height>0 && float(preview.width)/float(preview.height)>=kWideAspectThreshold;
}

// 参数 view/state/previews/availableWidth 为当前视图、筛选结果、轻量图片元信息和网格宽度。
void BuildGridLayout(CustomizationView& view,CustomizationView::ModeState& state,const std::vector<CustomizationPreview>& previews,float availableWidth) {
    const float spacing=kGridGap;
    const float width=std::max(1.0f,availableWidth);
    int nextColumns=std::max(1,static_cast<int>((width+spacing)/(kMinimumCardWidth+spacing)));
    nextColumns=std::min(nextColumns,8);
    const float nextCardWidth=std::max(1.0f,(width-spacing*(nextColumns-1))/nextColumns);
    const float nextRowHeight=kCardHeight+ImGui::GetStyle().ItemSpacing.y;
    std::vector<int> spans(state.results.size(),1);
    if(nextColumns>=2) for(size_t i=0;i<state.results.size() && i<previews.size();++i) {
        if(IsWidePreview(previews[i])) spans[i]=2;
    }

    const bool topologyChanged=view.previousLayoutColumns!=nextColumns || view.previousLayoutSpans!=spans;
    if(topologyChanged) {
        view.ResetBodyClick();
        state.scroll=true;
        ClearHoveredItem(view);
    }
    view.previousLayoutColumns=nextColumns;
    view.previousLayoutSpans=std::move(spans);
    view.columns=nextColumns;
    view.cardWidth=nextCardWidth;
    view.cardHeight=kCardHeight;
    view.rowHeight=nextRowHeight;
    view.gridCells.clear();
    view.gridRows.clear();

    int row=0,column=0;
    view.gridRows.emplace_back();
    for(size_t index=0;index<state.results.size();++index) {
        const int span=view.previousLayoutSpans[index];
        if(column+span>view.columns) {
            ++row;column=0;view.gridRows.emplace_back();
        }
        const float cellWidth=span*view.cardWidth+(span-1)*spacing;
        CustomizationView::GridCell cell;
        cell.resultIndex=index;cell.row=row;cell.startColumn=column;cell.span=span;
        cell.center={column*(view.cardWidth+spacing)+cellWidth*0.5f,row*view.rowHeight+view.cardHeight*0.5f};
        const size_t cellIndex=view.gridCells.size();
        view.gridCells.push_back(cell);view.gridRows.back().push_back(cellIndex);
        column+=span;
        if(column>=view.columns && index+1<state.results.size()) {++row;column=0;view.gridRows.emplace_back();}
    }
    if(view.gridRows.size()==1 && view.gridRows.front().empty()) view.gridRows.clear();
}

// 参数 view/state/action 为当前卡片几何、游标和方向动作；按最近可见矩形移动选择。
bool NavigateGridGeometry(CustomizationView& view,CustomizationView::ModeState& state,CustomizationNavigation action) {
    if(view.gridCells.empty()) {state.cursor={};return false;}
    state.cursor.selected=std::min(state.cursor.selected,view.gridCells.size()-1);
    if(action==CustomizationNavigation::Home) state.cursor.selected=0;
    else if(action==CustomizationNavigation::End) state.cursor.selected=view.gridCells.size()-1;
    else {
        const auto current=view.gridCells[state.cursor.selected];
        const bool horizontal=action==CustomizationNavigation::Left || action==CustomizationNavigation::Right;
        const bool page=action==CustomizationNavigation::PageUp || action==CustomizationNavigation::PageDown;
        if(action!=CustomizationNavigation::Up && action!=CustomizationNavigation::Down && !horizontal && !page) return false;
        const float direction=(action==CustomizationNavigation::Left || action==CustomizationNavigation::Up || action==CustomizationNavigation::PageUp)?-1.0f:1.0f;
        const float pageOffset=page?std::max(1,view.pageRows)*view.rowHeight:0.0f;
        const ImVec2 target={current.center.x,current.center.y+direction*pageOffset};
        float best=std::numeric_limits<float>::max();
        size_t bestIndex=state.cursor.selected;
        for(size_t i=0;i<view.gridCells.size();++i) {
            if(i==state.cursor.selected) continue;
            const auto candidate=view.gridCells[i];
            const float dx=candidate.center.x-current.center.x;
            const float dy=candidate.center.y-current.center.y;
            const float primary=horizontal?dx:dy;
            if(primary*direction<=0.5f) continue;
            const float offsetX=candidate.center.x-target.x;
            const float offsetY=candidate.center.y-target.y;
            const float score=horizontal?(dx*dx+dy*dy*4.0f):(offsetY*offsetY+offsetX*offsetX*1.35f);
            if(score<best) {best=score;bestIndex=i;}
        }
        if(bestIndex==state.cursor.selected) return false;
        state.cursor.selected=bestIndex;
    }
    state.cursor.intendedColumn=view.gridCells[state.cursor.selected].startColumn;
    state.scroll=true;
    return true;
}

// 参数 view/state 为当前服装快照和等待中的 Enter 请求；只在最新搜索完成后提交稳定身份。
void SubmitPendingLocate(CustomizationView& view,CustomizationView::ModeState& state) {
    if(!state.locateRequested) return;
    if(view.locateRequestRevision!=state.revision) {state.locateRequested=false;state.detailsRequested=false;state.details=false;return;}
    if(view.composing || view.SearchPending()) return;
    state.locateRequested=false;state.detailsRequested=false;
    if(view.kind!=CustomizationKind::Module) {view.status="饰品浏览可用，饰品原生定位尚未接入。";return;}
    if(!view.catalog || state.results.empty()) {state.details=false;view.status="没有匹配的服装，未提交定位。";return;}
    if(view.catalogRequiresRestart) {view.status="服装目录快照已变化；请重启游戏后定位，当前仍可浏览筛选结果。";return;}
    state.cursor.selected=std::min(state.cursor.selected,state.results.size()-1);
    auto target=state.results[state.cursor.selected];
    if(view.pendingLocateKey) {
        const auto found=std::find_if(state.results.begin(),state.results.end(),[&](size_t index){return view.catalog->items[index].key==*view.pendingLocateKey;});
        view.pendingLocateKey.reset();
        if(found==state.results.end()) {view.status="目标已不在最新结果中，已取消定位。";return;}
        target=*found;
    }
    const auto& item=view.catalog->items[target];
    if(!view.submitModuleLocate) {view.status="原生服装定位接口尚未接入。";return;}
    CustomizationLocateRequest request;
    request.key=item.key;
    request.character=ModuleSearchCharacter(item.character);
    request.catalogFingerprint=view.catalog->catalogFingerprint;
    request.uiGeneration=view.generation;
    request.serial=++view.locateSerial;
    request.queryRevision=state.revision;request.origin=view.locateOrigin;
    if(request.origin==CustomizationOrigin::CardDoubleClick) {
        request.firstClickKey=view.bodyClickKey;request.firstClickRevision=view.bodyClickRevision;
    }
    view.status="定位请求已提交，正在等待游戏上下文确认。";
    try {view.submitModuleLocate(request);}
    catch(...) {view.status="定位请求提交失败，请检查当前游戏页面后重试。";}
}

// 参数 view 为当前来源快照；统计每个来源参与的不同实体数量，不重复计算贡献字段。
void BuildSourceAssociationCounts(CustomizationView& view) {
    for(auto& counts:view.sourceAssociationCounts) counts.clear();
    if(!view.catalog) return;
    for(const auto& item:view.catalog->items) {
        const size_t mode=item.key.kind==CustomizationKind::Module?0:1;
        std::set<std::string> uniqueSources;
        for(const auto& contribution:item.sources) uniqueSources.insert(contribution.source);
        for(const auto& source:uniqueSources) ++view.sourceAssociationCounts[mode][source];
    }
}
}

// 参数无；初始化服装和饰品各自的查询领域。
CustomizationView::CustomizationView() {
    states[0].query.kind=CustomizationKind::Module;
    states[1].query.kind=CustomizationKind::Accessory;
    states[0].query.character.reset();states[1].query.character.reset();
    status="尚未读取服装目录。";
}

// 参数 h/g 为插件和游戏目录；已有加载任务时不重复启动。
void CustomizationView::Refresh(const fs::path& h,const fs::path& g) {
    ResetBodyClick();
    if(invalidateModuleLocate) invalidateModuleLocate();
    home=h;game=g;
    if(pending.valid()) return;
    status="正在后台读取启用Mod与兼容导入元数据……";
    const auto language=languageProvider?languageProvider():std::string{};
    pending=std::async(std::launch::async,[h,g,language]{return LoadCustomizationCatalog(h,g,language);});
}

// 参数 h/g 为插件和游戏目录；仅在首次进入时异步启动加载。
void CustomizationView::EnsureLoaded(const fs::path& h,const fs::path& g) {
    if(!catalog && !pending.valid()) Refresh(h,g);
}

// 接收异步快照和查询结果，并调度最新修订；全过程不等待后台任务。
void CustomizationView::Poll() {
    const auto recommendations=recommendationProvider?recommendationProvider():nullptr;
    const auto recommendationGeneration=recommendations?recommendations->generation:0;
    std::string criteriaKey=songCriteria.pack+"|"+std::to_string(songCriteria.style)+"|"+std::to_string(songCriteria.difficulty)+"|"+std::to_string(songCriteria.sort)+"|"+std::to_string(songCriteria.modMode)+"|"+std::to_string(songCriteria.favoritesOnly);
    if(songCriteria.favoritesOnly) for(int id:songFavorites) criteriaKey+='|'+std::to_string(id);
    if(observedSongCatalog!=songCatalog || observedRecommendationGeneration!=recommendationGeneration || observedSongCriteria!=criteriaKey) {
        observedSongCatalog=songCatalog;observedRecommendationGeneration=recommendationGeneration;observedSongCriteria=std::move(criteriaKey);
        auto& state=states[0];++state.revision;state.due={};state.locateRequested=state.detailsRequested=false;
        ResetBodyClick();if(invalidateModuleLocate) invalidateModuleLocate();
    }
    if(pending.valid() && pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto next=std::make_shared<CustomizationCatalog>(pending.get());
            if(!hasInitialCatalogFingerprint) {
                initialCatalogFingerprint=next->catalogFingerprint;
                hasInitialCatalogFingerprint=true;
            } else if(next->catalogFingerprint!=initialCatalogFingerprint) {
                catalogRequiresRestart=true;
            }
            catalogRequiresRestart=catalogRequiresRestart || next->stale;
            catalog=next;
            ResetBodyClick();
            ++generation;
            gridCells.clear();gridRows.clear();
            BuildSourceAssociationCounts(*this);
            ClearHoveredItem(*this);
            for(auto& state:states) {
                state.results.clear();state.cursor={};state.details=false;state.detailsRequested=false;state.locateRequested=false;
                state.recommendationReasons.clear();state.recommendationWarnings.clear();state.matchedSongs=0;
                ++state.revision;state.applied=0;state.due={};state.scroll=true;
            }
            status.clear();
            if(catalogRequiresRestart) status="目录快照与游戏启动时资源不同；请重启游戏后定位，浏览筛选仍可用。";
        } catch(...) {status="定制物品读取失败，请刷新重试。";}
    }
    ConsumeSearchReply(searching,[&](SearchReply reply) {
            auto& state=states[reply.mode];
            if(reply.generation==generation && reply.revision==state.revision) {
                const auto publishStart=PerformanceNow();
                ResetBodyClick();
                std::optional<CustomizationKey> previous;
                if(catalog && state.cursor.selected<state.results.size()) previous=catalog->items[state.results[state.cursor.selected]].key;
                state.results=std::move(reply.results);state.applied=reply.revision;state.cursor={};
                state.recommendationReasons=std::move(reply.recommendations.reasons);
                state.recommendationWarnings=std::move(reply.recommendations.unresolved);state.matchedSongs=reply.recommendations.matchedSongs;
                if(previous) {
                    auto selected=std::find_if(state.results.begin(),state.results.end(),[&](size_t index){return catalog->items[index].key==*previous;});
                    if(selected!=state.results.end()) state.cursor.selected=static_cast<size_t>(selected-state.results.begin());
                }
                state.scroll=true;state.locateRequested=state.locateRequested && !state.results.empty();
                if(!state.locateRequested) {state.detailsRequested=false;state.details=false;}
                if(reply.mode==(kind==CustomizationKind::Module?0u:1u)) {gridCells.clear();gridRows.clear();}
                ClearHoveredItem(*this);
                performance->Query(reply.mode==0?"服装":"饰品",reply.revision,reply.timing,PerformanceNow()-publishStart);
            }
    },[&] {
            auto& state=states[runningMode];
            if(runningGeneration==generation && runningRevision==state.revision) {
                status="定制物品后台搜索失败，请修改条件重试。";
                state.recommendationReasons.clear();state.recommendationWarnings.clear();state.matchedSongs=0;
                state.applied=state.revision;state.results.clear();state.cursor={};state.locateRequested=false;state.detailsRequested=false;
                if(runningMode==(kind==CustomizationKind::Module?0u:1u)) {gridCells.clear();gridRows.clear();}
                ClearHoveredItem(*this);
            }
    });
    if(catalog && !searching.valid() && !composing) {
        const size_t active=kind==CustomizationKind::Module?0:1;
        for(size_t n=0;n<2;++n) {
            const size_t mode=(active+n)%2;
            const auto& state=states[mode];
            if(!SearchDue(state.revision,state.applied,state.due,composing)) continue;
            auto snapshot=catalog;auto criteria=state.query;auto revision=state.revision,snapshotGeneration=generation;
            auto songs=songCatalog;auto songQuery=songCriteria;auto favorites=songFavorites;
            runningMode=mode;runningRevision=revision;runningGeneration=snapshotGeneration;
            auto timing=state.searchTiming;timing.submitted=PerformanceNow();
            searching=searchExecutor->Submit([snapshot,criteria,revision,snapshotGeneration,mode,timing,songs,songQuery,favorites,recommendations]() mutable {
                timing.started=PerformanceNow();auto combined=SearchModulesWithSongs(*snapshot,criteria,songs.get(),songQuery,favorites,recommendations.get());timing.finished=PerformanceNow();
                auto results=std::move(combined.items);
                return SearchReply{mode,revision,snapshotGeneration,std::move(results),timing,std::move(combined)};
            });
            break;
        }
    }
}

// 参数 value 为服装或饰品模式；切换时保留各自查询并清理旧悬浮与待提交动作。
void CustomizationView::SetKind(CustomizationKind value) {
    if(value==kind) return;
    ResetBodyClick();
    if(invalidateModuleLocate) invalidateModuleLocate();
    State().locateRequested=false;State().detailsRequested=false;State().details=false;
    kind=value;
    State().focus=true;
    textNavigation=gridNavigation=controlNavigation=false;
    gridCells.clear();gridRows.clear();previousLayoutColumns=0;previousLayoutSpans.clear();
    ClearHoveredItem(*this);
}

// 参数context只用于说明当前游戏角色；打开窗口不覆盖用户的角色、来源、查询或排序。
void CustomizationView::OnOpen(std::optional<CustomizationContext> context) {
    ResetBodyClick();
    if(invalidateModuleLocate) invalidateModuleLocate();
    const bool known=context && context->verified && context->character!=CustomizationCharacter::Unknown && context->character!=CustomizationCharacter::All;
    contextReason=known?"已读取当前游戏角色；搜索筛选保持用户选择。":"未能读取当前游戏角色；搜索筛选保持不变。";
    for(auto& state:states) {
        state.locateRequested=false;state.detailsRequested=false;state.details=false;state.focus=true;
    }
    textNavigation=true;gridNavigation=controlNavigation=false;
    ClearHoveredItem(*this);
}

// 参数 debounce 为是否等待连续输入停止300毫秒；新条件会取消旧Enter和旧悬浮详情。
void CustomizationView::QueueSearch(bool debounce) {
    ResetBodyClick();
    if(invalidateModuleLocate) invalidateModuleLocate();
    auto& state=State();
    const bool sameFrameRequest=state.locateRequested && locateRequestFrame==drawFrame;
    state.query.modMode=false;
    state.query.text=state.input;++state.revision;
    state.due=SearchDeadline(debounce);
    state.searchTiming.changed=PerformanceNow();state.searchTiming.due=state.searchTiming.changed+(debounce?kSearchDebounce.count():0);
    if(sameFrameRequest) locateRequestRevision=state.revision;
    else {state.locateRequested=false;state.detailsRequested=false;}
    state.details=false;
    state.scroll=true;status.clear();
    ClearHoveredItem(*this);
}

// 判断当前模式是否仍在等待目录或最新查询的结果。
bool CustomizationView::SearchPending() const {
    return !catalog || State().revision!=State().applied;
}

// 参数 action 为方向动作；宽图卡片按当前绘制的实际中心点寻找同向最近目标。
bool CustomizationView::Navigate(CustomizationNavigation action) {
    if(!WantsListNavigation() || SearchPending()) return false;
    const auto previous=State().cursor.selected;
    const bool moved=NavigateGridGeometry(*this,State(),action);
    if(!State().results.empty()) {requestGridFocus=true;gridNavigation=true;textNavigation=false;}
    if(previous!=State().cursor.selected && invalidateModuleLocate) invalidateModuleLocate();
    return moved;
}

// 文本框和单一卡片焦点可接收上下；下拉框或操作按钮保留控件行为。
bool CustomizationView::WantsListNavigation() const {
    return !composing && !controlNavigation && (textNavigation || gridNavigation);
}

// 与歌曲搜索一致：文本或卡片焦点均可用左右浏览结果，其他控件保留自身导航。
bool CustomizationView::WantsHorizontalNavigation() const {
    return WantsListNavigation();
}

// Enter 在文本或单一卡片焦点下，等待最新搜索修订后提交所选服装。
void CustomizationView::RequestDetails() {
    if(composing) return;
    auto& state=State();
    if(kind!=CustomizationKind::Module) {state.detailsRequested=false;status="饰品浏览可用，饰品原生定位尚未接入。";return;}
    pendingLocateKey.reset();locateOrigin=CustomizationOrigin::Keyboard;ResetBodyClick();
    state.detailsRequested=true;state.locateRequested=true;locateRequestRevision=state.revision;locateRequestFrame=drawFrame+1;
    state.due=std::chrono::steady_clock::now();
}

// 参数 key 为点击卡片或定位按钮时的稳定身份，所有点击入口共用同一请求路径。
void CustomizationView::RequestLocate(const CustomizationKey& key,CustomizationOrigin origin) {
    auto& state=State();
    if(composing || kind!=CustomizationKind::Module || !catalog || SearchPending()) return;
    auto selected=std::find_if(state.results.begin(),state.results.end(),[&](size_t index){return catalog->items[index].key==key;});
    if(selected==state.results.end()) {status="所选服装已不在最新结果中；未提交定位。";return;}
    state.cursor.selected=static_cast<size_t>(selected-state.results.begin());
    pendingLocateKey=key;locateOrigin=origin;
    state.locateRequested=true;
    state.detailsRequested=true;
    locateRequestRevision=state.revision;locateRequestFrame=drawFrame;
    state.due=std::chrono::steady_clock::now();
}

// 清除主体双击链，不改变已经提交的原生事务。
void CustomizationView::ResetBodyClick() {
    bodyClickKey.reset();bodyClickRevision=bodyClickGeneration=0;bodyClickTime=-1;bodyDoubleClick=false;
}

// 参数 key/action 直接来自当前卡片按钮；拒绝过期结果及未认证能力。
void CustomizationView::RequestAction(const CustomizationKey& key,CustomizationAction action) {
    ResetBodyClick();
    if(composing || SearchPending() || catalogRequiresRestart || !catalog || action==CustomizationAction::Locate) return;
    const auto found=std::find_if(State().results.begin(),State().results.end(),[&](size_t index){return catalog->items[index].key==key;});
    if(found==State().results.end()) {status="动作目标已不在当前结果中。";return;}
    const auto capability=actionCapability?actionCapability(key):CustomizationActionCapability{};
    if(!capability.modelPage) {status="双击卡片进入模型页后使用。";return;}
    if(capability.busy || (action==CustomizationAction::Preview?!capability.preview:!capability.equip)) {
        status=action==CustomizationAction::Preview?capability.previewReason:capability.equipReason;return;
    }
    if(!submitModuleAction) {status="原生服装动作尚未接入。";return;}
    CustomizationLocateRequest request;
    request.key=key;request.character=ModuleSearchCharacter(catalog->items[*found].character);
    request.catalogFingerprint=catalog->catalogFingerprint;request.uiGeneration=generation;
    request.queryRevision=State().revision;request.serial=++locateSerial;
    request.action=action;request.origin=CustomizationOrigin::HoverButton;
    request.contextGeneration=capability.contextGeneration;request.slot=capability.slot;
    request.scope=capability.scope;request.nativeScope=capability.nativeScope;
    submitModuleAction(request);
}

// Esc 优先取消尚未提交的定位请求；否则交由共享搜索窗口关闭。
bool CustomizationView::HandleEscape() {
    auto& state=State();
    if(state.locateRequested || state.detailsRequested || state.details) {
        const bool pendingRequest=state.locateRequested || state.detailsRequested;
        state.locateRequested=false;state.detailsRequested=false;state.details=false;
        status=pendingRequest?"已取消等待中的服装定位。":"已关闭服装详情。";
        return true;
    }
    return false;
}

// 返回当前模式的可变状态，供桥接和绘制逻辑使用。
CustomizationView::ModeState& CustomizationView::State() {return states[kind==CustomizationKind::Module?0:1];}

// 返回当前模式的只读状态。
const CustomizationView::ModeState& CustomizationView::State() const {return states[kind==CustomizationKind::Module?0:1];}

// 在共享搜索窗口内绘制紧凑条件栏、可换行预览网格、悬浮详情与底部定位区。
void CustomizationView::Draw() {
    auto& state=State();
    ++drawFrame;
    Poll();
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) suppressActionRelease=false;
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) ResetBodyClick();
    if(composing) ResetBodyClick();
    ImGui::PushID(kind==CustomizationKind::Module?"custom-module":"custom-accessory");
    const float availableWidth=ImGui::GetContentRegionAvail().x;
    const bool splitFilters=availableWidth<680.0f;
    ImGui::SetNextItemWidth(-1);
    if(state.focus) {ImGui::SetKeyboardFocusHere();state.focus=false;}
    if(ImGui::InputTextWithHint("##item-search",kind==CustomizationKind::Module?"服装名称、歌曲名称或 ID":"名称、英文、假名或 ID",state.input,sizeof(state.input))) QueueSearch(true);
    textNavigation=ImGui::IsItemActive() || ImGui::IsItemFocused();
    DrawSearchGroupLabel("角色：");
    const float characterWidth=std::min(142.0f,std::max(90.0f,availableWidth-110.0f));
    ImGui::SetNextItemWidth(characterWidth);
    const char* character=state.query.character?CustomizationCharacterName(*state.query.character):"全部角色";
    if(ImGui::BeginCombo("##character",character)) {
        if(ImGui::Selectable("全部角色",!state.query.character)) {state.query.character.reset();QueueSearch(false);}
        const std::vector<CustomizationCharacter> moduleCharacters={CustomizationCharacter::Miku,CustomizationCharacter::Rin,CustomizationCharacter::Len,CustomizationCharacter::Luka,CustomizationCharacter::Kaito,CustomizationCharacter::Meiko,CustomizationCharacter::Derived,CustomizationCharacter::Random};
        std::vector<CustomizationCharacter> characters=moduleCharacters;
        if(kind==CustomizationKind::Accessory) {characters.clear();for(int n=0;n<12;++n) characters.push_back(static_cast<CustomizationCharacter>(n));}
        for(auto value:characters) if(ImGui::Selectable(CustomizationCharacterName(value),state.query.character==value)) {state.query.character=value;QueueSearch(false);}
        ImGui::EndCombo();
    }
    if(splitFilters) ImGui::NewLine(); else ImGui::SameLine();
    DrawSearchGroupLabel("来源：");
    const float sourceAvailable=ImGui::GetContentRegionAvail().x;
    const float sourceWidth=splitFilters?std::max(130.0f,sourceAvailable-75.0f):std::clamp(sourceAvailable-280.0f,120.0f,390.0f);
    ImGui::SetNextItemWidth(sourceWidth);
    const auto& activeSourceCounts=sourceAssociationCounts[kind==CustomizationKind::Module?0:1];
    std::string selectedSource=state.query.source.empty()?"全部来源":state.query.source=="official"?"官方":catalog?SourceFilterLabel(*catalog,state.query.source,activeSourceCounts):state.query.source;
    if(ImGui::BeginCombo("##source",selectedSource.c_str())) {
        if(ImGui::Selectable("全部来源",state.query.source.empty())) {state.query.source.clear();QueueSearch(false);}
        const bool hasOfficial=std::any_of(activeSourceCounts.begin(),activeSourceCounts.end(),[](const auto& entry){return entry.first.starts_with("official:") && entry.second>0;});
        if(hasOfficial && ImGui::Selectable("官方",state.query.source=="official")) {state.query.source="official";QueueSearch(false);}
        if(catalog) for(const auto& source:catalog->sources) {
            if(source.id.starts_with("official:")) continue;
            auto count=activeSourceCounts.find(source.id);if(count==activeSourceCounts.end() || count->second==0) continue;
            const auto label=SourceFilterLabel(*catalog,source.id,activeSourceCounts)+"##"+source.id;
            if(ImGui::Selectable(label.c_str(),state.query.source==source.id)) {state.query.source=source.id;QueueSearch(false);}
        }
        ImGui::EndCombo();
    }
    if(splitFilters) ImGui::NewLine(); else ImGui::SameLine();
    DrawSearchGroupLabel("排序：");ImGui::SetNextItemWidth(std::min(130.0f,std::max(90.0f,availableWidth*0.34f)));
    const char* sorts[]={"默认顺序","名称","ID"};
    if(ImGui::Combo("##sort",&state.query.sort,sorts,3)) QueueSearch(false);
    if(kind==CustomizationKind::Accessory) {
        ImGui::SameLine();ImGui::TextUnformatted("部位");ImGui::SameLine();ImGui::SetNextItemWidth(100.0f);
        if(ImGui::BeginCombo("##part",state.query.part?CustomizationPartName(*state.query.part):"全部部位")) {
            if(ImGui::Selectable("全部部位",!state.query.part)) {state.query.part.reset();QueueSearch(false);}
            for(int n=0;n<5;++n) {auto part=static_cast<CustomizationPart>(n);if(ImGui::Selectable(CustomizationPartName(part),state.query.part==part)) {state.query.part=part;QueueSearch(false);}}
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("选项")) ImGui::OpenPopup("##catalog-options");
    ImGui::SetNextWindowSizeConstraints({320,0},{600,420});
    if(ImGui::BeginPopup("##catalog-options")) {
        ImGui::TextUnformatted("预览样式");ImGui::SameLine();ImGui::SetNextItemWidth(150);
        int previewChoice=previewStyle==1?0:1;
        const char* previewStyles[]={"MEGA39","Future Tone"};
        if(ImGui::Combo("##preview-style",&previewChoice,previewStyles,2)) {previewStyle=previewChoice==0?1:2;hoveredKey.reset();}
        if(ImGui::Button("刷新目录")) Refresh(home,game);
        ImGui::TextDisabled("%s",contextReason.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("单击选择 · Enter 定位 · Esc 关闭");
        ImGui::TextUnformatted("方向键浏览 · Tab 切换焦点");
        ImGui::TextUnformatted("Home / End 首尾 · PageUp / PageDown 翻页");
        ImGui::Separator();
        ImGui::TextWrapped("目录记录不代表已购买或已解锁。");
        if(catalog && ImGui::CollapsingHeader("来源诊断")) for(const auto& diagnostic:catalog->diagnostics) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*28);ImGui::TextUnformatted(diagnostic.c_str());ImGui::PopTextWrapPos();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();
    auto* parent=ImGui::GetCurrentWindow();
    const std::string gridFocusLabel=std::to_string(state.results.size())+" 项结果###结果卡片键盘焦点";
    const ImGuiID gridFocusId=parent->GetID(gridFocusLabel.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text,SearchResultTextColor);
    const bool clickedFocus=ImGui::Selectable(gridFocusLabel.c_str(),false,0,ImVec2(120.0f,22.0f));
    ImGui::PopStyleColor();
    gridNavigation=ImGui::IsItemFocused();
    if(requestGridFocus) {ImGui::ClearActiveID();ImGui::SetFocusID(gridFocusId,parent);ImGui::FocusWindow(parent);gridNavigation=true;textNavigation=false;requestGridFocus=false;}
    if(clickedFocus) {ImGui::SetFocusID(gridFocusId,parent);ImGui::FocusWindow(parent);gridNavigation=true;textNavigation=false;}
    if(SearchPending() || composing) {ImGui::SameLine();ImGui::TextDisabled("%s",composing?"组词中…":"搜索中…");}
    if(kind==CustomizationKind::Module && state.matchedSongs && !SearchPending()) {
        ImGui::SameLine();ImGui::TextDisabled("匹配 %zu 首歌曲 · 推荐显示全部角色",state.matchedSongs);
    }
    if(!state.recommendationWarnings.empty() && !SearchPending()) {
        ImGui::SameLine();if(ImGui::SmallButton("推荐说明")) ImGui::OpenPopup("##recommendation-warnings");
        if(ImGui::BeginPopup("##recommendation-warnings")) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*34);
            for(const auto& warning:state.recommendationWarnings) ImGui::TextWrapped("%s",warning.c_str());
            ImGui::PopTextWrapPos();ImGui::EndPopup();
        }
    }

    const float remaining=ImGui::GetContentRegionAvail().y;
    const float gridHeight=std::max(80.0f,remaining-60.0f);
    ImGui::BeginChild("##module-grid",ImVec2(0,gridHeight),false);
    float startX=ImGui::GetCursorPosX();
    float gridWidth=ImGui::GetContentRegionAvail().x;
    if(std::abs(ImGui::GetScrollY()-gridScrollY)>1.0f) {ResetBodyClick();ClearHoveredItem(*this);}
    if(previousGridWidth>0.0f && std::abs(gridWidth-previousGridWidth)>2.0f) {ResetBodyClick();ClearHoveredItem(*this);}
    previousGridWidth=gridWidth;

    const bool metadataChanged=layoutRevision!=state.applied || layoutCatalogGeneration!=generation
        || layoutMetadataGeneration!=previewMetadataGeneration || layoutKind!=kind || layoutStyle!=previewStyle;
    if(metadataChanged) {
        ResetBodyClick();
        layoutPreviews.assign(state.results.size(),{});
        if(kind==CustomizationKind::Module && previewProvider && catalog) for(size_t i=0;i<state.results.size();++i) {
            const auto facts=previewProvider(catalog->items[state.results[i]],false);
            layoutPreviews[i].width=facts.width;layoutPreviews[i].height=facts.height;
        }
        layoutRevision=state.applied;layoutCatalogGeneration=generation;layoutMetadataGeneration=previewMetadataGeneration;
        layoutKind=kind;layoutStyle=previewStyle;
    }
    const auto& previewFacts=layoutPreviews;
    if(metadataChanged || layoutWidth!=gridWidth || gridCells.size()!=state.results.size()) {
        BuildGridLayout(*this,state,previewFacts,gridWidth);layoutWidth=gridWidth;
    }
    const float visibleHeight=ImGui::GetContentRegionAvail().y;
    pageRows=std::max(1,static_cast<int>(visibleHeight/rowHeight));
    if(state.scroll && !gridCells.empty()) {
        const int selectedRow=gridCells[std::min(state.cursor.selected,gridCells.size()-1)].row;
        const float top=selectedRow*rowHeight,bottom=top+cardHeight;
        const float scroll=ImGui::GetScrollY();
        if(top<scroll || cardHeight>=visibleHeight) ImGui::SetScrollY(top);
        else if(bottom>scroll+visibleHeight) ImGui::SetScrollY(std::max(0.0f,bottom-visibleHeight));
        state.scroll=false;
    }

    const CustomizationItem* detailItem=nullptr;
    CustomizationPreview detailPreview;
    bool cardClicked=false,hoveredThisFrame=false,bodyPressedThisFrame=false;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(gridRows.size()),rowHeight);
    while(clipper.Step()) {
        for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
            const float rowY=ImGui::GetCursorPosY();
            for(const size_t cellIndex:gridRows[static_cast<size_t>(row)]) {
                const auto& cell=gridCells[cellIndex];
                const size_t index=cell.resultIndex;
                const auto& item=catalog->items[state.results[index]];
                const float x=startX+cell.startColumn*(cardWidth+kGridGap);
                const float cellWidth=cell.span*cardWidth+(cell.span-1)*kGridGap;
                ImGui::PushID(static_cast<int>(item.key.kind));ImGui::PushID(item.key.id);
                ImGui::SetCursorPos(ImVec2(x,rowY));
                const ImVec2 origin=ImGui::GetCursorScreenPos();
                const bool selected=state.cursor.selected==index;
                const bool hovered=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                    && ImGui::IsMouseHoveringRect(origin,ImVec2(origin.x+cellWidth,origin.y+cardHeight));
                const auto& navigation=*ImGui::GetCurrentContext();
                const auto previewId=ImGui::GetID("预览"),equipId=ImGui::GetID("换装"),equippedId=ImGui::GetID("已装备");
                const bool actionFocused=navigation.NavWindow==ImGui::GetCurrentWindow()
                    && (navigation.NavId==previewId || navigation.NavId==equipId || navigation.NavId==equippedId);
                const bool showActions=kind==CustomizationKind::Module && (hovered || selected || actionFocused);
                const float actionY=kPreviewHeight-38.0f;
                const float actionWidth=(cellWidth-22.0f)*0.5f;
                const auto& io=ImGui::GetIO();
                const bool inActionBand=showActions && io.MousePos.y>=origin.y+actionY && io.MousePos.y<origin.y+actionY+32.0f;
                const bool inButton=inActionBand && ((io.MousePos.x>=origin.x+8 && io.MousePos.x<origin.x+8+actionWidth)
                    || (io.MousePos.x>=origin.x+14+actionWidth && io.MousePos.x<origin.x+cellWidth-8));
                const bool bodyHovered=hovered && !inButton;
                if(composing || io.AppFocusLost) ResetBodyClick();
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && bodyHovered && !composing && !SearchPending()) {
                    bodyPressedThisFrame=true;
                    const float dx=io.MousePos.x-bodyClickPosition.x,dy=io.MousePos.y-bodyClickPosition.y;
                    const bool same=bodyClickKey && *bodyClickKey==item.key && bodyClickRevision==state.revision
                        && bodyClickGeneration==generation && ImGui::GetTime()-bodyClickTime<io.MouseDoubleClickTime
                        && dx*dx+dy*dy<io.MouseDoubleClickMaxDist*io.MouseDoubleClickMaxDist;
                    bodyDoubleClick=same && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    bodyClickKey=item.key;bodyClickRevision=state.revision;bodyClickGeneration=generation;
                    bodyClickTime=ImGui::GetTime();bodyClickPosition=io.MousePos;
                }
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);
                bool clicked=false;
                if(!showActions) clicked=ImGui::InvisibleButton("##card",ImVec2(cellWidth,cardHeight));
                else {
                    clicked=ImGui::InvisibleButton("##card",ImVec2(cellWidth,actionY));
                    ImGui::SetCursorPos(ImVec2(x,rowY+actionY+32));
                    clicked=ImGui::InvisibleButton("##card-name",ImVec2(cellWidth,cardHeight-actionY-32)) || clicked;
                    ImGui::SetCursorPos(ImVec2(x,rowY+actionY));
                    clicked=ImGui::InvisibleButton("##card-left",ImVec2(8,32)) || clicked;
                    ImGui::SetCursorPos(ImVec2(x+8+actionWidth,rowY+actionY));
                    clicked=ImGui::InvisibleButton("##card-gap",ImVec2(6,32)) || clicked;
                    ImGui::SetCursorPos(ImVec2(x+cellWidth-8,rowY+actionY));
                    clicked=ImGui::InvisibleButton("##card-right",ImVec2(8,32)) || clicked;
                }
                ImGui::PopItemFlag();
                if(hovered) {
                    hoveredThisFrame=true;
                    if(!hoveredKey || *hoveredKey!=item.key) {hoveredKey=item.key;hoverSince=std::chrono::steady_clock::now();}
                }

                auto* draw=ImGui::GetWindowDrawList();
                const ImVec2 cardMax(origin.x+cellWidth,origin.y+cardHeight);
                draw->AddRectFilled(origin,cardMax,selected?IM_COL32(38,52,53,255):IM_COL32(29,33,39,255),8.0f);
                draw->AddRect(origin,cardMax,selected?IM_COL32(107,224,194,255):hovered?IM_COL32(99,115,122,255):IM_COL32(53,61,69,255),8.0f,0,selected?2.0f:1.0f);

                const ImVec2 imageMin(origin.x+1.0f,origin.y+1.0f);
                const ImVec2 imageMax(origin.x+cellWidth-1.0f,origin.y+kPreviewHeight+1.0f);
                draw->AddRectFilled(imageMin,imageMax,IM_COL32(35,40,47,255),7.0f);
                CustomizationPreview preview;
                if(kind==CustomizationKind::Module && previewProvider) preview=previewProvider(item,true);
                else if(index<previewFacts.size()) preview=previewFacts[index];
                // 首次解码或轻量查询碰到try-lock时，按可见项补齐尺寸，下一帧仅重建几何。
                if(preview.width>0 && preview.height>0 && index<layoutPreviews.size()
                    && (layoutPreviews[index].width!=preview.width || layoutPreviews[index].height!=preview.height)) {
                    layoutPreviews[index].width=preview.width;layoutPreviews[index].height=preview.height;layoutWidth=-1;
                }
                ImTextureID texture=preview.texture;
                if(!texture && preview.rgbaOwner && !preview.rgbaOwner->empty() && uploadPreviewPixels) {
                    const unsigned char* pixels=preview.rgbaPixels?preview.rgbaPixels:preview.rgbaOwner->data();
                    texture=uploadPreviewPixels(item.key,pixels,preview.width,preview.height);
                }
                if(texture && preview.width>0 && preview.height>0) {
                    const float scale=std::min((imageMax.x-imageMin.x)/preview.width,(imageMax.y-imageMin.y)/preview.height);
                    const ImVec2 size(preview.width*scale,preview.height*scale);
                    const ImVec2 pos(imageMin.x+((imageMax.x-imageMin.x)-size.x)*0.5f,imageMin.y+((imageMax.y-imageMin.y)-size.y)*0.5f);
                    draw->PushClipRect(imageMin,imageMax,true);
                    draw->AddImage(texture,pos,ImVec2(pos.x+size.x,pos.y+size.y));
                    draw->PopClipRect();
                } else {
                    const char* missing="暂无预览";
                    const ImVec2 labelSize=ImGui::CalcTextSize(missing);
                    draw->AddText(ImVec2(imageMin.x+(cellWidth-labelSize.x)*0.5f,imageMin.y+kPreviewHeight*0.5f-labelSize.y*0.5f),IM_COL32(141,163,174,255),missing);
                    if(preview.width>0 && preview.height>0) {
                        const std::string dimensions=std::to_string(preview.width)+" × "+std::to_string(preview.height);
                        const ImVec2 size=ImGui::CalcTextSize(dimensions.c_str());
                        draw->AddText(ImVec2(imageMax.x-size.x-9.0f,imageMax.y-size.y-7.0f),IM_COL32(120,143,154,255),dimensions.c_str());
                    }
                }

                const auto recommendation=state.recommendationReasons.find(item.key);
                if(recommendation!=state.recommendationReasons.end() && !recommendation->second.empty()) {
                    auto title=recommendation->second.front();const auto separator=title.find(" [PVID");if(separator!=std::string::npos) title.resize(separator);
                    const auto badge=EllipsizeText("推荐 · "+title,cellWidth-24);
                    draw->AddRectFilled(ImVec2(origin.x+5,origin.y+5),ImVec2(origin.x+cellWidth-5,origin.y+34),IM_COL32(24,43,43,240),4);
                    draw->AddText(ImVec2(origin.x+10,origin.y+9),IM_COL32(137,229,207,255),badge.c_str());
                }
                const float textWidth=cellWidth-20.0f;
                const std::string name=EllipsizeText(item.name,textWidth);
                draw->PushClipRect(ImVec2(origin.x+8.0f,origin.y+288.0f),ImVec2(origin.x+cellWidth-6.0f,origin.y+313.0f),true);
                draw->AddText(ImVec2(origin.x+10.0f,origin.y+290.0f),ImGui::GetColorU32(ImGuiCol_Text),name.c_str());
                draw->PopClipRect();
                std::string secondLine=CustomizationCharacterName(kind==CustomizationKind::Module?ModuleSearchCharacter(item.character):item.character);
                if(kind==CustomizationKind::Accessory) secondLine+=" / "+std::string(CustomizationPartName(item.part));
                secondLine+=" · ID "+std::to_string(item.key.id);
                secondLine=EllipsizeText(secondLine,textWidth);
                draw->PushClipRect(ImVec2(origin.x+8.0f,origin.y+313.0f),ImVec2(origin.x+cellWidth-6.0f,origin.y+337.0f),true);
                draw->AddText(ImVec2(origin.x+10.0f,origin.y+315.0f),ImGui::GetColorU32(SearchSecondaryTextColor),secondLine.c_str());
                draw->PopClipRect();

                if(clicked) {
                    if(state.cursor.selected!=index && invalidateModuleLocate) invalidateModuleLocate();
                    state.cursor.selected=index;
                    state.cursor.intendedColumn=cell.startColumn;
                    state.scroll=false;
                    cardClicked=true;
                    RequestLocate(item.key,bodyDoubleClick?CustomizationOrigin::CardDoubleClick:CustomizationOrigin::CardSingleClick);
                }
                if(showActions) {
                    const auto capability=actionCapability?actionCapability(item.key):CustomizationActionCapability{};
                    const bool unavailable=SearchPending() || composing || catalogRequiresRestart || capability.busy;
                    ImGui::SetCursorPos(ImVec2(x+8,rowY+actionY));
                    ImGui::BeginDisabled(unavailable || !capability.equip);
                    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.15f,0.28f,0.25f,1));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.21f,0.39f,0.34f,1));
                    const bool equip=ImGui::Button(capability.equipped?"已装备":"换装",ImVec2(actionWidth,32));
                    ImGui::PopStyleColor(2);
                    ImGui::EndDisabled();
                    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",!capability.modelPage?"双击卡片进入模型页后使用":capability.equipReason.c_str());
                    ImGui::SetCursorPos(ImVec2(x+14+actionWidth,rowY+actionY));
                    ImGui::BeginDisabled(unavailable || !capability.preview);
                    const bool previewClicked=ImGui::Button("预览",ImVec2(actionWidth,32));
                    ImGui::EndDisabled();
                    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",!capability.modelPage?"双击卡片进入模型页后使用":capability.previewReason.c_str());
                    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inButton) {
                        suppressActionRelease=ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && actionPressKey && *actionPressKey==item.key;
                        actionPressKey=item.key;ResetBodyClick();
                    }
                    const bool duplicate=suppressActionRelease && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                    if(equip && !duplicate) RequestAction(item.key,CustomizationAction::Equip);
                    if(previewClicked && !duplicate) RequestAction(item.key,CustomizationAction::Preview);
                    if(capability.equipped) draw->AddText(ImVec2(origin.x+cellWidth-70,origin.y+8),IM_COL32(107,224,194,255),"已装备");
                }
                const bool keyboardFocused=gridNavigation && selected;
                const bool hoverMature=hovered && hoveredKey && *hoveredKey==item.key && std::chrono::steady_clock::now()-hoverSince>=kHoverDelay;
                if((keyboardFocused || hoverMature) && (!detailItem || keyboardFocused)) {detailItem=&item;detailPreview=preview;}
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::SetCursorPosY(rowY+rowHeight);
        }
    }
    const int firstRendered=clipper.DisplayStart,lastRendered=clipper.DisplayEnd;
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bodyPressedThisFrame) ResetBodyClick();
    gridScrollY=ImGui::GetScrollY();
    state.scrollY=gridScrollY;
    ImGui::EndChild();
    if(cardClicked) {
        ImGui::SetFocusID(gridFocusId,parent);ImGui::FocusWindow(parent);
        gridNavigation=true;textNavigation=false;
    }
    if(hoveredKey && !hoveredThisFrame) ClearHoveredItem(*this);
    if(hoveredKey && std::chrono::steady_clock::now()-hoverSince>=kHoverDelay && !detailItem) ClearHoveredItem(*this);
    if(detailItem) DrawItemDetails(*this,*detailItem,detailPreview);

    if(previewProvider && kind==CustomizationKind::Module && !gridRows.empty()) {
        for(int row:{firstRendered-1,lastRendered}) {
            if(row<0 || row>=static_cast<int>(gridRows.size()) || (row>=firstRendered && row<lastRendered)) continue;
            for(size_t cellIndex:gridRows[static_cast<size_t>(row)]) {
                const auto index=gridCells[cellIndex].resultIndex;
                previewProvider(catalog->items[state.results[index]],true);
            }
        }
    }

    SubmitPendingLocate(*this,state);
    ImGui::Separator();
    if(catalog && !state.results.empty()) {
        state.cursor.selected=std::min(state.cursor.selected,state.results.size()-1);
        const auto& selected=catalog->items[state.results[state.cursor.selected]];
        const std::string selectedText=selected.name+" · ID "+std::to_string(selected.key.id);
        const float footerWidth=ImGui::GetContentRegionAvail().x;
        const bool compactFooter=footerWidth>=400.0f;
        const float buttonWidth=kind==CustomizationKind::Module?150.0f:190.0f;
        const std::string footerText=status.empty()?selectedText:status;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(EllipsizeText(footerText,compactFooter?footerWidth-buttonWidth-16.0f:footerWidth).c_str());
        if(ImGui::IsItemHovered() && ImGui::CalcTextSize(footerText.c_str()).x>ImGui::GetItemRectSize().x) {
            ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);ImGui::TextUnformatted(footerText.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
        }
        if(compactFooter) ImGui::SameLine(footerWidth-buttonWidth+ImGui::GetWindowContentRegionMin().x);
        const bool targetPage=!actionCapability || actionCapability(selected.key).modelPage;
        const bool canSubmit=kind==CustomizationKind::Module && bool(submitModuleLocate) && !catalogRequiresRestart && !SearchPending() && targetPage;
        ImGui::BeginDisabled(!canSubmit);
        if(ImGui::Button(kind==CustomizationKind::Module?(targetPage?"定位 · Enter":"双击进入模型页"):"饰品定位尚未接入",ImVec2(buttonWidth,30.0f))) RequestLocate(selected.key);
        ImGui::EndDisabled();
    } else if(!catalog) {
        ImGui::TextWrapped("%s",status.empty()?"正在读取服装目录……":status.c_str());
    } else if(SearchPending()) {
        ImGui::TextUnformatted("正在搜索当前条件……");
    } else if(catalog->items.empty()) {
        ImGui::TextWrapped("没有检测到服装，请在选项中检查来源诊断。");
    } else {
        ImGui::TextWrapped("%s",status.empty()?"没有匹配的服装，请调整筛选条件。":status.c_str());
    }
    controlNavigation=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || (!textNavigation && !gridNavigation);
    ImGui::PopID();
}
}
