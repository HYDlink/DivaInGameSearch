#include "customization_ui.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace diva {
CustomizationView::CustomizationView() {states[0].query.kind=CustomizationKind::Module;states[1].query.kind=CustomizationKind::Accessory;}
CustomizationView::ModeState& CustomizationView::State() {return states[kind==CustomizationKind::Module?0:1];}
const CustomizationView::ModeState& CustomizationView::State() const {return states[kind==CustomizationKind::Module?0:1];}
void CustomizationView::EnsureLoaded(const fs::path& h,const fs::path& g) {if(!catalog && !pending.valid()) Refresh(h,g);}
void CustomizationView::Refresh(const fs::path& h,const fs::path& g) {
    home=h;game=g;if(pending.valid()) return;status="正在后台读取定制物品导入目录……";
    pending=std::async(std::launch::async,[h,g]{return LoadCustomizationCatalog(h,g);});
}
void CustomizationView::SetKind(CustomizationKind value) {
    if(value==kind) return;State().details=false;State().detailsRequested=false;kind=value;State().details=false;State().detailsRequested=false;State().focus=true;
    textNavigation=gridNavigation=controlNavigation=false;
}
void CustomizationView::OnOpen(std::optional<CustomizationContext> context) {
    bool known=context && context->verified && context->character!=CustomizationCharacter::Unknown && context->character!=CustomizationCharacter::All;
    contextReason=known?"已读取并验证当前角色":"无法读取当前角色，默认初音未来";
    for(auto& state:states) {state.query.character=known?context->character:CustomizationCharacter::Miku;state.details=false;state.detailsRequested=false;state.focus=true;++state.revision;state.due={};}
    textNavigation=true;gridNavigation=controlNavigation=false;
}
void CustomizationView::QueueSearch(bool debounce) {
    auto& state=State();state.query.text=state.input;++state.revision;
    state.due=std::chrono::steady_clock::now()+(debounce?std::chrono::milliseconds(150):std::chrono::milliseconds(0));
}
bool CustomizationView::SearchPending() const {return !catalog || State().revision!=State().applied;}
void CustomizationView::Poll() {
    if(pending.valid() && pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {catalog=std::make_shared<CustomizationCatalog>(pending.get());++generation;for(auto& state:states) {state.results.clear();state.cursor={};++state.revision;state.applied=0;state.due={};state.details=false;state.detailsRequested=false;}
            status="已导入 "+std::to_string(catalog->items.size())+" 项；当前为导入目录，原生定位尚未接入。";
            if(catalog->stale) status="导入目录过期：DML配置指纹已变化。 "+status;
        } catch(...) {status="定制物品读取失败，请刷新重试。";}
    }
    if(searching.valid() && searching.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {auto reply=searching.get();auto& state=states[reply.mode];
            if(reply.generation==generation && reply.revision==state.revision) {
                std::optional<CustomizationKey> previous;if(catalog && state.cursor.selected<state.results.size()) previous=catalog->items[state.results[state.cursor.selected]].key;
                state.results=std::move(reply.results);state.applied=reply.revision;state.cursor={};
                if(previous) {auto selected=std::find_if(state.results.begin(),state.results.end(),[&](size_t index){return catalog->items[index].key==*previous;});if(selected!=state.results.end()) state.cursor.selected=selected-state.results.begin();}
                state.scroll=true;
            }
        } catch(...) {
            auto& state=states[runningMode];
            if(runningGeneration==generation && runningRevision==state.revision) {status="定制物品后台搜索失败，请修改条件重试。";state.applied=state.revision;state.results.clear();state.detailsRequested=false;}
        }
    }
    if(!catalog || searching.valid() || composing) return;
    // 优先当前模式，空闲时也完成另一模式的已排队查询。
    size_t active=kind==CustomizationKind::Module?0:1;
    for(size_t n=0;n<2;++n) {
        size_t mode=(active+n)%2;const auto& state=states[mode];if(state.revision==state.applied || std::chrono::steady_clock::now()<state.due) continue;
        auto snapshot=catalog;auto criteria=state.query;auto revision=state.revision,snapshotGeneration=generation;
        runningMode=mode;runningRevision=revision;runningGeneration=snapshotGeneration;
        searching=std::async(std::launch::async,[snapshot,criteria,revision,snapshotGeneration,mode]{return SearchReply{mode,revision,snapshotGeneration,SearchCustomization(*snapshot,criteria)};});break;
    }
}
bool CustomizationView::WantsListNavigation() const {return !composing && !State().details && (textNavigation || gridNavigation) && !controlNavigation;}
bool CustomizationView::WantsHorizontalNavigation() const {return WantsListNavigation() && gridNavigation;}
bool CustomizationView::Navigate(CustomizationNavigation action) {
    if(!WantsListNavigation() || SearchPending()) return false;
    if((action==CustomizationNavigation::Left || action==CustomizationNavigation::Right || action==CustomizationNavigation::Home || action==CustomizationNavigation::End || action==CustomizationNavigation::PageUp || action==CustomizationNavigation::PageDown) && !gridNavigation) return false;
    NavigateCustomizationGrid(State().cursor,State().results.size(),columns,pageRows,action);State().scroll=true;return true;
}
void CustomizationView::RequestDetails() {
    if(composing || !WantsListNavigation()) return;
    auto& state=State();state.detailsRequested=true;state.due={};
}
bool CustomizationView::HandleEscape() {
    auto& state=State();if(state.details || state.detailsRequested) {state.details=false;state.detailsRequested=false;return true;}
    return false;
}
// 参数 catalog/id 为快照与稳定来源身份；同名来源附目录和身份避免混淆。
static std::string SourceLabel(const CustomizationCatalog& catalog,const std::string& id) {
    auto source=std::find_if(catalog.sources.begin(),catalog.sources.end(),[&](const auto& value){return value.id==id;});
    if(source==catalog.sources.end()) return id;
    return source->name+" ["+(source->modDirectory.empty()?source->id:source->modDirectory+"/"+source->includeRoot)+"]"+(source->currentMod?"":"（导入）");
}
void CustomizationView::Draw() {
    auto& state=State();ImGui::PushID(kind==CustomizationKind::Module?"custom-module":"custom-accessory");
    if(state.focus) {ImGui::SetKeyboardFocusHere();state.focus=false;}
    ImGui::SetNextItemWidth(std::max(160.0f,ImGui::GetContentRegionAvail().x-135));
    if(ImGui::InputTextWithHint("##item-search","名称、别名或十进制ID",state.input,sizeof(state.input))) QueueSearch(true);
    textNavigation=ImGui::IsItemActive() || ImGui::IsItemFocused();
    ImGui::SameLine();if(ImGui::Checkbox("关联Mod搜索",&state.query.modMode)) QueueSearch(false);
    ImGui::TextDisabled("%s",contextReason.c_str());
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("角色");ImGui::SameLine();ImGui::SetNextItemWidth(125);
    const char* character=state.query.character?CustomizationCharacterName(*state.query.character):"全部角色";
    if(ImGui::BeginCombo("##character",character)) {
        if(ImGui::Selectable("全部角色",!state.query.character)) {state.query.character.reset();QueueSearch(false);}
        for(int n=0;n<12;++n) {auto value=static_cast<CustomizationCharacter>(n);if(kind==CustomizationKind::Module && value==CustomizationCharacter::All) continue;
            if(ImGui::Selectable(CustomizationCharacterName(value),state.query.character==value)) {state.query.character=value;QueueSearch(false);}}
        ImGui::EndCombo();
    }
    ImGui::SameLine();ImGui::TextUnformatted("关联来源");ImGui::SameLine();ImGui::SetNextItemWidth(200);
    auto source=state.query.source.empty()?std::string("全部来源"):catalog?SourceLabel(*catalog,state.query.source):state.query.source;
    if(ImGui::BeginCombo("##source",source.c_str())) {
        if(ImGui::Selectable("全部来源",state.query.source.empty())) {state.query.source.clear();QueueSearch(false);}
        if(catalog) for(const auto& value:catalog->sources) {auto label=SourceLabel(*catalog,value.id)+"##"+value.id;if(ImGui::Selectable(label.c_str(),state.query.source==value.id)) {state.query.source=value.id;QueueSearch(false);}}
        ImGui::EndCombo();
    }
    if(kind==CustomizationKind::Accessory) {
        ImGui::SameLine();ImGui::TextUnformatted("部位");ImGui::SameLine();ImGui::SetNextItemWidth(100);
        if(ImGui::BeginCombo("##part",state.query.part?CustomizationPartName(*state.query.part):"全部部位")) {
            if(ImGui::Selectable("全部部位",!state.query.part)) {state.query.part.reset();QueueSearch(false);}
            for(int n=0;n<5;++n) {auto part=static_cast<CustomizationPart>(n);if(ImGui::Selectable(CustomizationPartName(part),state.query.part==part)) {state.query.part=part;QueueSearch(false);}}
            ImGui::EndCombo();
        }
    }
    ImGui::TextUnformatted("排序");ImGui::SameLine();ImGui::SetNextItemWidth(130);
    const char* sorts[]={"默认顺序","名称","ID"};if(ImGui::Combo("##sort",&state.query.sort,sorts,3)) QueueSearch(false);
    ImGui::SameLine();if(ImGui::Button("刷新导入目录")) Refresh(home,game);
    ImGui::TextWrapped("%s",status.c_str());
    if(catalog && ImGui::CollapsingHeader("导入诊断与说明")) for(const auto& diagnostic:catalog->diagnostics) ImGui::TextWrapped("%s",diagnostic.c_str());
    if(!catalog || catalog->items.empty()) {
        ImGui::TextWrapped("未导入服装／饰品元数据。请在本插件目录创建 customization_catalog.toml：登记来源与最终项目，或登记已解包 gm_module_id.bin / gm_customize_item_id.bin。格式说明：docs/CUSTOMIZATION_IMPORT.md。FARC／CPK不会自动解包；原版／DLC需自行导入。");
    }
    if(SearchPending()) ImGui::TextDisabled("正在搜索，Enter 将在最新结果完成后打开详情……");
    else ImGui::Text("%zu 项；Enter / 空格打开详情，原生定位尚未接入",state.results.size());
    auto* parent=ImGui::GetCurrentWindow();auto gridId=parent->GetID("格子键盘入口");
    if(ImGui::Button("格子键盘入口") && !composing) {state.detailsRequested=true;state.due={};}
    gridNavigation=ImGui::IsItemFocused();
    ImGui::SameLine();ImGui::TextDisabled("Tab进入；方向键、Home/End、PageUp/PageDown 浏览");
    const float cardHeight=190.0f,spacing=ImGui::GetStyle().ItemSpacing.x;
    float available=std::max(100.0f,ImGui::GetContentRegionAvail().y-6);
    ImGui::BeginChild("##item-grid",ImVec2(0,available),false,ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoNavFocus);
    // 用子窗口的实际内部宽度排格子，避免右列被内边距或滚动条裁掉。
    float startX=ImGui::GetCursorPosX(),width=ImGui::GetContentRegionAvail().x;int nextColumns=std::max(1,int((width+spacing)/(185.0f+spacing)));
    if(nextColumns!=columns) {columns=nextColumns;state.cursor.intendedColumn=-1;state.scroll=true;}
    float cardWidth=std::max(1.0f,(width-spacing*(columns-1))/columns);
    float visibleHeight=ImGui::GetContentRegionAvail().y;pageRows=std::max(1,int(visibleHeight/cardHeight));
    bool clickedGrid=false;
    if(state.scroll && !state.results.empty()) {float top=float(state.cursor.selected/columns)*cardHeight;float bottom=top+cardHeight;float scroll=ImGui::GetScrollY();if(top<scroll) ImGui::SetScrollY(top);else if(bottom>scroll+visibleHeight) ImGui::SetScrollY(bottom-visibleHeight);state.scroll=false;}
    ImGuiListClipper clipper;clipper.Begin(int((state.results.size()+columns-1)/columns),cardHeight);
    while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
        float rowY=ImGui::GetCursorPosY();
        for(int column=0;column<columns;++column) {
            size_t index=size_t(row)*columns+column;if(index>=state.results.size()) break;
            const auto& item=catalog->items[state.results[index]];ImGui::PushID(int(index));
            ImGui::SetCursorPos(ImVec2(startX+column*(cardWidth+spacing),rowY));auto origin=ImGui::GetCursorScreenPos();
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav,true);bool clicked=ImGui::InvisibleButton("##card",ImVec2(cardWidth,cardHeight-8));ImGui::PopItemFlag();
            auto* draw=ImGui::GetWindowDrawList();bool selected=state.cursor.selected==index;
            draw->AddRectFilled(origin,ImVec2(origin.x+cardWidth,origin.y+cardHeight-8),selected?IM_COL32(35,82,91,255):IM_COL32(28,34,43,255),5);
            draw->AddRect(origin,ImVec2(origin.x+cardWidth,origin.y+cardHeight-8),selected?IM_COL32(64,215,204,255):IM_COL32(69,80,92,255),5,0,selected?2:1);
            draw->AddText(ImVec2(origin.x+10,origin.y+20),IM_COL32(153,164,174,255),"暂无预览");
            // 两行名称采用裁剪，完整名称可从详情读取。
            ImVec2 nameMin(origin.x+10,origin.y+66),nameMax(origin.x+cardWidth-8,origin.y+108);
            draw->PushClipRect(nameMin,nameMax,true);draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),nameMin,IM_COL32(238,241,243,255),item.name.c_str(),nullptr,cardWidth-20);draw->PopClipRect();
            std::string characterLabel=CustomizationCharacterName(item.character);if(kind==CustomizationKind::Accessory) characterLabel+=" / "+std::string(CustomizationPartName(item.part));
            draw->PushClipRect(origin,ImVec2(origin.x+cardWidth-6,origin.y+cardHeight-10),true);
            draw->AddText(ImVec2(origin.x+10,origin.y+112),IM_COL32(189,200,211,255),characterLabel.c_str());
            auto label=SourceLabel(*catalog,item.definitionSource);draw->AddText(ImVec2(origin.x+10,origin.y+136),IM_COL32(154,179,189,255),label.c_str());
            auto id="ID "+std::to_string(item.key.id);draw->AddText(ImVec2(origin.x+10,origin.y+159),IM_COL32(189,200,211,255),id.c_str());draw->PopClipRect();
            if(clicked) {state.cursor.selected=index;state.cursor.intendedColumn=column;clickedGrid=true;if(ImGui::IsMouseDoubleClicked(0)) state.detailsRequested=true;}
            ImGui::PopID();
        }
        ImGui::SetCursorPosY(rowY+cardHeight);
    }
    state.scrollY=ImGui::GetScrollY();ImGui::EndChild();
    if(clickedGrid) {ImGui::SetFocusID(gridId,parent);ImGui::FocusWindow(parent);gridNavigation=true;textNavigation=false;}
    if(state.detailsRequested && !SearchPending()) {state.detailsRequested=false;if(!state.results.empty()) {state.details=true;ImGui::OpenPopup("物品详情");}}
    if(state.details && !ImGui::IsPopupOpen("物品详情")) state.details=false;
    if(!state.details && ImGui::IsPopupOpen("物品详情")) {if(ImGui::BeginPopup("物品详情")) {ImGui::CloseCurrentPopup();ImGui::EndPopup();}}
    if(state.details && ImGui::BeginPopup("物品详情")) {
        if(state.cursor.selected<state.results.size()) {
            const auto& item=catalog->items[state.results[state.cursor.selected]];
            ImGui::TextWrapped("%s",item.name.c_str());ImGui::Text("ID %d · %s",item.key.id,CustomizationCharacterName(item.character));
            if(kind==CustomizationKind::Accessory) ImGui::Text("部位：%s",CustomizationPartName(item.part));
            ImGui::TextWrapped("定义来源：%s",SourceLabel(*catalog,item.definitionSource).c_str());ImGui::TextWrapped("名称来源：%s",SourceLabel(*catalog,item.nameSource).c_str());
            if(!item.cos.empty()) ImGui::Text("原始服装键：%s",item.cos.c_str());
            if(item.objectId) ImGui::Text("对象ID：%d（不是饰品ID）",*item.objectId);
            if(item.bindModule) ImGui::Text("绑定服装原值：%d（限制语义尚未验证）",*item.bindModule);
            ImGui::Separator();ImGui::TextUnformatted("关联来源（被覆盖定义仍可按来源检索）：");
            for(const auto& contribution:item.sources) ImGui::TextWrapped("%s · %s · %s",SourceLabel(*catalog,contribution.source).c_str(),contribution.role.c_str(),contribution.effective?"采用":"已由其他来源覆盖／未采用");
            ImGui::TextWrapped("元数据：%s",item.metadataState.c_str());
            ImGui::TextWrapped("暂无预览。此处仅展示已导入元数据；不代表已解锁或可装备。");
        }
        ImGui::BeginDisabled();ImGui::Button("原生服装／饰品定位尚未接入");ImGui::EndDisabled();
        if(ImGui::Button("关闭详情")) {state.details=false;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    controlNavigation=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || (!textNavigation && !gridNavigation);
    ImGui::PopID();
}
}
