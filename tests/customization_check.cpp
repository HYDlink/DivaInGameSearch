#include "customization_ui.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>

namespace {
int checks=0;
// 参数 value/message 为回归条件和中文失败原因。
void Check(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
// 参数 path/text 为隔离测试目录的文件和UTF-8内容；不写真实游戏资产。
void Write(const diva::fs::path& path,const std::string& text) {diva::fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<text;}
// 参数 view 为测试视图；等待仅用于测试，不进入绘制或宿主帧回调。
void Complete(diva::CustomizationView& view) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(view.SearchPending() && std::chrono::steady_clock::now()<deadline) {view.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Check(!view.SearchPending(),"定制查询未及时完成");
}
// 参数 view 为定制界面；在测试ImGui窗口中绘制一帧，不创建游戏或DX11设备。
void DrawCustomization(diva::CustomizationView& view) {
    ImGui::NewFrame();ImGui::SetNextWindowSize({1100,780});ImGui::Begin("定制界面回归");view.Draw();ImGui::End();ImGui::EndFrame();
}
// 参数 catalog/kind/id 为快照和实体键，测试断言需要明确存在。
const diva::CustomizationItem& Find(const diva::CustomizationCatalog& catalog,diva::CustomizationKind kind,int id) {
    for(const auto& item:catalog.items) if(item.key.kind==kind && item.key.id==id) return item;
    throw std::runtime_error("测试实体缺失");
}
// 参数 root 为临时目录；验证真实key=value格式、稀疏和ID边界。
void Parser(const diva::fs::path& root) {
    using namespace diva;std::vector<std::string> diagnostics;auto path=root/"parser/gm_module_id.bin";
    // 官方经典语言表以小端偏移索引UTF-8文本；非法偏移及损坏编码不得部分发布。
    std::string strings(12,'\0');strings[0]=12;strings[4]=19;
    strings.append("初音",6);strings.push_back('\0');strings.append("MIKU");strings.push_back('\0');
    auto localized=ReadCustomizationStrings(strings);
    Check(localized.size()==2 && localized.at(0)=="初音" && localized.at(1)=="MIKU","官方语言表没有保持真实偏移或UTF-8名称");
    auto brokenStrings=strings;brokenStrings[4]=127;Check(ReadCustomizationStrings(brokenStrings).empty(),"越界语言表未拒绝整表");
    brokenStrings=strings;brokenStrings.pop_back();Check(ReadCustomizationStrings(brokenStrings).empty(),"语言表未终止文本未拒绝");
    brokenStrings=strings;brokenStrings[12]=static_cast<char>(0xFF);Check(ReadCustomizationStrings(brokenStrings).empty(),"损坏UTF-8语言表未拒绝");
    Write(path,"\xEF\xBB\xBFmodule.2.name=First=Equals\r\nmodule.0.id=0\r\nmodule.0.chara=MIKU\nmodule.0.cos=COS_001\nmodule.2.id=7\nmodule.2.chara=ALIEN\nmodule.2.future_field=raw\nmodule.3.id=7\nmodule.3.name=Last\nmodule.3.chara=RIN\nmodule.4.name=Missing\nmodule.5.id=-1\nmodule.6.id=12oops\nmodule.data_list.length=7\n");
    auto items=ReadCustomizationTable(path,CustomizationKind::Module,"fixture",diagnostics);
    Check(items.size()==2 && items[0].key.id==0,"零ID非法或无效ID被创建");
    Check(items[0].cos=="COS_001" && items[1].name=="Last" && !items[1].cos.size(),"整条记录或重复ID顺序错误");
    Check(!diagnostics.empty(),"缺失ID/重复ID没有诊断");
    Write(path,"module.0.id=0\nmodule.0.name=First=Equals\nmodule.0.chara=ALIEN\nmodule.0.future_field=raw\nmodule.data_list.length=1\n");
    items=ReadCustomizationTable(path,CustomizationKind::Module,"fixture",diagnostics);
    Check(items[0].name=="First=Equals" && items[0].character==CustomizationCharacter::Unknown && items[0].rawFields.at("future_field")=="raw","首等号/未知角色/原始字段丢失");
    Write(path,"module.data_list.length=999999999\nmodule.0.id=0\n");Check(ReadCustomizationTable(path,CustomizationKind::Module,"fixture",diagnostics).empty(),"声明长度没有上限");
}
// 参数 root 为隔离目录；验证DML优先级、整条替换、语言、普通表与追加表。
diva::CustomizationCatalog Catalog(const diva::fs::path& root) {
    using namespace diva;auto h=root/"home",g=root/"game";
    Write(g/"config.toml","enabled=true\npriority=['High','Disabled','Low']\n");
    Write(g/"mods/High/config.toml","name='重复显示名'\ninclude=['content']\n");
    Write(g/"mods/Low/config.toml","name='重复显示名'\ninclude=['.']\n");
    Write(g/"mods/Disabled/config.toml","enabled=false\ninclude=['.']\n");
    Write(g/"mods/Omitted/config.toml","include=['.']\n");
    Write(g/"mods/High/content/rom/lang2/mod_str_array.toml","module.0='High General'\ncn.module.0='高 中文'\nmodule.1='General Wins'\nmodule.999='Orphan'\ncustomize.0='Accessory Primary'\ncstm_item.0='Alias Loses'\n");
    Write(g/"mods/Low/rom/lang2/mod_str_array.toml","cn.module.1='低中文不能覆盖高通用'\nen.module.0='English Alias'\n");
    Write(g/"mods/Disabled/rom/lang2/mod_str_array.toml","cn.module.0='Disabled'\n");
    Write(g/"mods/Omitted/rom/lang2/mod_str_array.toml","cn.module.0='Omitted'\n");
    Write(g/"mods/High/content/rom/mod_gm_module_tbl.farc","test archive marker; never decoded");
    Write(h/"dump/high/gm_module_id.bin","module.0.id=0\nmodule.0.name=High Table\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n");
    Write(h/"dump/low/gm_module_id.bin","module.0.id=0\nmodule.0.name=Low Table\nmodule.0.chara=RIN\nmodule.0.cos=COS_999\nmodule.0.sort_index=1\nmodule.1.id=1\nmodule.1.chara=MIKU\nmodule.2.id=2\nmodule.2.name=Derived Costume\nmodule.2.chara=SAKINE\nmodule.data_list.length=3\n");
    Write(h/"dump/repl-low/gm_module_id.bin","module.0.id=31\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n");
    Write(h/"dump/repl-high/gm_module_id.bin","module.0.id=30\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n");
    Write(h/"dump/disabled/gm_module_id.bin","module.0.id=88\nmodule.data_list.length=1\n");
    Write(h/"customization_catalog.toml",R"(schema_version=1
language='cn'
[[sources]]
id='hi'
name='High'
mod_directory='High'
include_root='content'
[[sources]]
id='lo'
mod_directory='Low'
include_root='.'
[[sources]]
id='disabled'
mod_directory='Disabled'
include_root='.'
[[sources]]
id='base'
name='离线基础'
[[items]]
kind='accessory'
id=0
source='base'
chara='ALL'
parts='FACE'
name='Base Accessory'
bind_module=0
[[items]]
kind='accessory'
id=2
source='base'
chara='ALIEN'
parts='KAMI'
name='Unknown'
[[items]]
kind='module'
id=9
source='base'
chara='MIKU'
name='Base Name'
sort_index=0
names.cn='手工中文'
[[tables]]
source='hi'
kind='module'
mode='append'
logical_path='rom/mod_gm_module_tbl.farc'
path='dump/high/gm_module_id.bin'
[[tables]]
source='lo'
kind='module'
mode='append'
logical_path='rom/mod_gm_module_tbl.farc'
path='dump/low/gm_module_id.bin'
[[tables]]
source='lo'
kind='module'
mode='replace'
logical_path='rom/gm_module_tbl.farc'
path='dump/repl-low/gm_module_id.bin'
[[tables]]
source='hi'
kind='module'
mode='replace'
logical_path='rom/gm_module_tbl.farc'
path='dump/repl-high/gm_module_id.bin'
[[tables]]
source='disabled'
kind='module'
mode='append'
logical_path='rom/mod_gm_module_tbl.farc'
path='dump/disabled/gm_module_id.bin'
)");
    auto catalog=LoadCustomizationCatalog(h,g);Check(catalog.items.size()==7,"普通替换表累加、禁用来源导入或孤立名字创建实体");
    auto english=LoadCustomizationCatalog(h,g,"en");
    Check(english.language=="en" && Find(english,CustomizationKind::Module,0).name=="High General","当前游戏语言未覆盖导入默认语言或破坏Mod名称优先级");
    Check(catalog.sources[0].name==catalog.sources[1].name && catalog.sources[0].id!=catalog.sources[1].id,"重复显示名被合并为同一来源");
    const auto& zero=Find(catalog,CustomizationKind::Module,0);
    Check(zero.character==CustomizationCharacter::Miku && zero.cos.empty() && !zero.sortIndex,"高优先级没有整条替换，泄漏低优先级字段");
    Check(zero.name=="高 中文" && Find(catalog,CustomizationKind::Module,1).name=="General Wins","root/本语言/通用优先级错误");
    Check(zero.definitionSource=="dml:High:content" && zero.nameSource=="dml:High:content","稳定来源身份错误");
    Check(Find(catalog,CustomizationKind::Accessory,0).name=="Accessory Primary","customize未优先于cstm_item兼容别名");
    Check(Find(catalog,CustomizationKind::Module,9).name=="手工中文","最终清单本语言名称未用于显示");
    Check(catalog.orphanNames==1 && catalog.unimportedArchives==1,"孤立名称或未导入归档诊断计数错误");
    CustomizationQuery query;query.text="手工 English";Check(SearchCustomization(catalog,query).empty(),"多词不应跨项目组合");
    query.text="English Alias";Check(SearchCustomization(catalog,query).size()==1,"其他语言真实别名未保留");
    query.text="手工 中文";Check(SearchCustomization(catalog,query).size()==1,"原文分词规范化错误");
    query.text.clear();query.source="dml:Low:.";Check(SearchCustomization(catalog,query).size()==2,"被覆盖定义/名字未保留关联来源");
    const auto& derived=Find(catalog,CustomizationKind::Module,2);
    Check(derived.characterToken=="SAKINE" && derived.character==CustomizationCharacter::Sakine && ModuleSearchCharacter(derived.character)==CustomizationCharacter::Derived,"原始角色与搜索分类没有独立保存");
    query={};query.character=CustomizationCharacter::Derived;auto derivedResults=SearchCustomization(catalog,query);
    Check(derivedResults.size()==1 && catalog.items[derivedResults[0]].key.id==2,"衍生角色筛选没有映射元数据角色");
    query.character=CustomizationCharacter::Sakine;Check(SearchCustomization(catalog,query).empty(),"元数据角色泄漏为单独游戏分类");
    query.character=CustomizationCharacter::Random;Check(SearchCustomization(catalog,query).empty(),"随机分类被错误解释为静态实体或全部服装");
    query={};query.domain=CustomizationDomain::Accessories;Check(SearchCustomization(catalog,query).size()==1,"共通饰品未匹配具体角色或未知误入MIKU");
    query.part=CustomizationPart::Face;Check(SearchCustomization(catalog,query).size()==1,"饰品部位交集错误");
    // 清单中的KAMI属于发型领域，不能再以未知部位混入道具。
    query={};query.domain=CustomizationDomain::Hair;query.character=CustomizationCharacter::Unknown;
    auto hairResults=SearchCustomization(catalog,query);Check(hairResults.size()==1 && catalog.items[hairResults[0]].key.id==2,"KAMI未归入发型或未知角色过滤失败");
    query.domain=CustomizationDomain::Accessories;query.part=CustomizationPart::Unknown;Check(SearchCustomization(catalog,query).empty(),"发型KAMI误入道具未知部位");
    Check(Find(catalog,CustomizationKind::Accessory,0).bindModule==0,"绑定原值零被丢失");
    query={};auto byDefault=SearchCustomization(catalog,query);Check(catalog.items[byDefault.front()].key.id==9,"缺sort_index没有排最后");
    query.sort=2;auto results=SearchCustomization(catalog,query);Check(catalog.items[results.front()].key.id==0,"数值ID排序错误");
    Write(g/"config.toml","enabled=false\npriority=['High','Low']\n");auto disabled=LoadCustomizationCatalog(h,g);
    Check(disabled.items.size()==3 && disabled.orphanNames==0,"DML总禁用仍扫描Mod实体/名字");
    auto updated=std::ifstream(h/"customization_catalog.toml");std::string contents((std::istreambuf_iterator<char>(updated)),{});updated.close();
    Write(h/"customization_catalog.toml","dml_fingerprint='obsolete'\n"+contents);Check(LoadCustomizationCatalog(h,g).stale,"不匹配DML指纹没有标记过期");
    Write(g/"config.toml","priority=['中文目录']\n");Write(g/L"mods/中文目录/config.toml","include=['.']\nname='重复显示名'\n");
    auto unicode=LoadCustomizationCatalog(h,g);Check(!unicode.sources.empty() && unicode.sources[0].id=="dml:中文目录:.","中文Mod目录身份使用了本机ANSI编码");
    return catalog;
}
// 验证显示语言、英文/日文检索与合并后的官方来源入口。
void DefaultSearchLanguages() {
    using namespace diva;CustomizationCatalog catalog;catalog.language="cn";
    CustomizationItem item;item.key={CustomizationKind::Module,1};item.name="中文服装";item.character=CustomizationCharacter::Miku;
    item.namesByLanguage={{"en","English Module"},{"jp","カタカナ"},{"fr","RobeFrancaise"}};
    item.aliases={"AutreNomFrancais"};item.sources={{"official:pc-region","定义",false},{"official:pc-region-dlc","定义",true}};
    PrepareCustomizationItem(item,catalog);catalog.items.push_back(std::move(item));
    CustomizationQuery query;query.source="official";
    Check(SearchCustomization(catalog,query).size()==1,"合并官方入口没有唯一匹配官方条目");
    for(const auto* name:{"中文服装","English Module","english module","カタカナ","かたかな","ｶﾀｶﾅ"}) {query.text=name;Check(SearchCustomization(catalog,query).size()==1,"默认显示名称或英文/日文检索失败");}
    for(const auto* name:{"RobeFrancaise","AutreNomFrancais"}) {query.text=name;Check(SearchCustomization(catalog,query).size()==1,"真实语言或别名未恢复检索");}
}
// 验证列意图、末行、边界、不同行数及有序重复动作。
void Navigation() {
    using namespace diva;CustomizationGridCursor cursor{2,-1};
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::Down);Check(cursor.selected==5,"向下没有按行");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::Down);Check(cursor.selected==7,"末行不足未夹到最后项");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::Up);Check(cursor.selected==5,"返回完整行丢失原列意图");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::Right);Check(cursor.selected==5,"行尾右键回绕");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::Home);Check(cursor.selected==0,"Home错误");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::PageDown);Check(cursor.selected==6,"PageDown未按完整可见行数");
    NavigateCustomizationGrid(cursor,8,3,2,CustomizationNavigation::End);Check(cursor.selected==7,"End错误");
    for(int i=0;i<20;++i) NavigateCustomizationGrid(cursor,8,2,1,CustomizationNavigation::Up);Check(cursor.selected==1,"长按边界或改列数失败");
    NavigateCustomizationGrid(cursor,8,1,2,CustomizationNavigation::Down);Check(cursor.selected==2,"一列导航错误");
    NavigateCustomizationGrid(cursor,0,1,1,CustomizationNavigation::End);Check(cursor.selected==0,"空格子导航越界");
}
// 参数 catalog 为隔离快照；验证防抖、IME、跨版本任务与安全能力。
void Async(const diva::CustomizationCatalog& catalog) {
    using namespace diva;CustomizationView view;view.catalog=std::make_shared<CustomizationCatalog>(catalog);view.OnOpen(std::nullopt);Complete(view);
    Check(!view.State().query.character && view.contextReason.find("保持")!=std::string::npos,"首次打开没有使用全部角色或误填当前人物");
    std::strcpy(view.State().input,"English Alias");view.QueueSearch(true);view.Poll();Check(!view.searching.valid() || view.runningMode!=CustomizationDomainIndex(view.domain),"防抖期间提前启动当前模式任务");
    view.State().due={};Complete(view);Check(view.State().results.size()==1,"最终防抖查询未生效");
    // 人工Promise仅控制异步任务时序，返回越界索引证明旧结果未被发布。
    if(view.searching.valid()) {view.searching.wait();view.Poll();}
    std::promise<CustomizationView::SearchReply> old;view.searching=old.get_future();auto revision=view.State().revision;
    std::strcpy(view.State().input,"General Wins");view.QueueSearch(false);old.set_value({0,revision,view.generation,{99999}});view.Poll();Complete(view);
    Check(view.State().results.size()==1 && catalog.items[view.State().results[0]].key.id==1,"旧查询覆盖新输入");
    std::promise<CustomizationView::SearchReply> failure;view.searching=failure.get_future();view.runningMode=0;view.runningRevision=view.State().revision;view.runningGeneration=view.generation;
    std::strcpy(view.State().input,"手工中文");view.QueueSearch(false);failure.set_exception(std::make_exception_ptr(std::runtime_error("旧任务失败")));view.Poll();Complete(view);
    Check(view.State().results.size()==1 && catalog.items[view.State().results[0]].key.id==9,"旧异常吞掉新查询");
    view.composing=true;std::strcpy(view.State().input,"English");view.QueueSearch(false);view.Poll();Check(!view.searching.valid(),"IME组词期间启动查询");view.composing=false;Complete(view);
    std::promise<CustomizationCatalog> replacement;std::promise<CustomizationView::SearchReply> staleSnapshot;
    view.pending=replacement.get_future();view.searching=staleSnapshot.get_future();
    staleSnapshot.set_value({0,view.State().revision,view.generation,{99999}});
    auto next=catalog;
    for(auto it=next.items.begin();it!=next.items.end();) {if(it->key==CustomizationKey{CustomizationKind::Module,0}) it=next.items.erase(it);else ++it;}
    replacement.set_value(std::move(next));view.Poll();Complete(view);
    Check(view.State().results.empty(),"旧快照结果覆盖新目录或保留失效索引");
    view.textNavigation=true;view.controlNavigation=false;view.RequestDetails();view.drawFrame=view.locateRequestFrame;
    std::strcpy(view.State().input,"High General");view.QueueSearch(true);
    Check(view.State().locateRequested && view.State().detailsRequested && view.locateRequestRevision==view.State().revision,"同帧输入未绑定到已排队Enter");
    view.SetDomain(CustomizationDomain::Accessories);Check(!view.states[0].detailsRequested,"模式切换遗留待确认");
    Check(!view.WantsHorizontalNavigation(),"无文本或卡片焦点时意外接管左右");
    view.OnOpen(std::nullopt);Check(!TryReadCustomizationContext() && !GetCustomizationCapabilities().canLocate && !GetCustomizationCapabilities().canPreviewNative,"安全stub意外启用原生能力");
    view.states[0].query.character=CustomizationCharacter::Kaito;view.states[0].query.source="保留来源";view.states[0].query.sort=2;
    view.states[1].query.character.reset();const auto preservedRevision=view.states[0].revision;
    view.OnOpen(CustomizationContext{CustomizationCharacter::Miku,0,1,true});
    view.OnOpen(std::nullopt);
    Check(view.states[0].query.character==CustomizationCharacter::Kaito && view.states[0].query.source=="保留来源" && view.states[0].query.sort==2
        && !view.states[1].query.character && view.states[0].revision==preservedRevision,"重新打开／角色变化覆盖了独立角色来源排序条件或触发无效重查");
}
// 参数 catalog 为隔离资源快照；验证Enter请求A后跨帧改搜B不会提交B。
void LocateRevision(const diva::CustomizationCatalog& catalog) {
    using namespace diva;
    auto* context=ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize={1280.0f,900.0f};io.DeltaTime=1.0f/60.0f;
    unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);

    CustomizationView view;view.catalog=std::make_shared<CustomizationCatalog>(catalog);view.OnOpen(std::nullopt);Complete(view);
    int submitted=0;int submittedId=-1;int invalidated=0;
    // 记录向已提交原生请求发布的失效通知，不能只清理界面自身的布尔状态。
    view.invalidateModuleLocate=[&]{++invalidated;};
    // 参数 request 为稳定服装定位请求；记录ID以发现错误提交到新查询结果。
    view.submitModuleLocate=[&](const CustomizationLocateRequest& request){++submitted;submittedId=request.key.id;};
    view.textNavigation=true;view.controlNavigation=false;
    std::strcpy(view.State().input,"High Table");view.QueueSearch(false);
    const auto requestRevision=view.State().revision;
    const auto firstResults=SearchCustomization(*view.catalog,view.State().query);
    Check(firstResults.size()==1 && view.catalog->items[firstResults.front()].key.id==0,"A查询夹具未命中服装ID 0");
    std::promise<CustomizationView::SearchReply> delayedA;
    view.searching=delayedA.get_future();view.runningMode=0;view.runningRevision=requestRevision;view.runningGeneration=view.generation;
    view.RequestDetails();
    DrawCustomization(view);DrawCustomization(view);
    Check(view.State().locateRequested && submitted==0,"A结果尚未完成时Enter提前提交");

    const int beforeChange=invalidated;
    std::strcpy(view.State().input,"General Wins");view.QueueSearch(false);
    Check(invalidated==beforeChange+1,"查询变化未使已提交原生请求失效");
    Check(!view.State().locateRequested && !view.State().detailsRequested,"跨帧改搜B未取消A的Enter请求");
    delayedA.set_value({0,requestRevision,view.generation,firstResults});
    view.Poll();Complete(view);DrawCustomization(view);
    Check(view.State().results.size()==1 && view.catalog->items[view.State().results.front()].key.id==1,"B查询夹具未命中服装ID 1");
    Check(submitted==0 && submittedId==-1,"A的Enter请求错误地自动提交了B");
    // 外部夹具替换文本前结束旧InputText会话，避免其内部缓冲把B写回。
    ImGui::ClearActiveID();view.State().focus=false;
    std::strcpy(view.State().input,"");view.QueueSearch(false);Complete(view);DrawCustomization(view);Complete(view);DrawCustomization(view);
    Check(!view.SearchPending(),"方向键回归尚未得到最新搜索结果");
    Check(view.State().results.size()>1,"方向键回归需要多件真实夹具服装");
    view.textNavigation=true;view.gridNavigation=false;view.controlNavigation=false;
    view.Navigate(CustomizationNavigation::Down);DrawCustomization(view);
    Check(view.WantsHorizontalNavigation(),"上下浏览后焦点没有交给卡片，左右键仍被文本框占用");
    const auto beforeRight=view.State().cursor.selected;
    view.Navigate(CustomizationNavigation::Right);
    Check(view.State().cursor.selected!=beforeRight,"卡片焦点的右方向键没有移动选择");
    ImGui::DestroyContext(context);
}
// 人工合成三领域目录：30 个发型、8 个道具（四部位循环）与 4 个模型，名称和 ID 均为虚构。
diva::CustomizationCatalog ItemDomainCatalog() {
    using namespace diva;CustomizationCatalog catalog;catalog.catalogFingerprint="合成三领域";
    catalog.sources={{"synthetic:a","合成来源A","","",false},{"synthetic:b","合成来源B","","",false}};
    auto add=[&](CustomizationKind kind,int id,CustomizationPart part,CustomizationCharacter character,const std::string& name) {
        CustomizationItem item;item.key={kind,id};item.part=part;item.character=character;item.name=name;item.sortIndex=id;
        item.definitionSource=item.nameSource=id%2?"synthetic:a":"synthetic:b";item.sources.push_back({item.definitionSource,"定义",true});
        if(id%5==0) item.sources.push_back({"synthetic:a","名称",false});
        PrepareCustomizationItem(item,catalog);catalog.items.push_back(std::move(item));
    };
    for(int n=0;n<30;++n) add(CustomizationKind::Accessory,100+n,CustomizationPart::Hair,n%3?CustomizationCharacter::Miku:CustomizationCharacter::Rin,"合成发型"+std::to_string(n));
    const CustomizationPart parts[]={CustomizationPart::Head,CustomizationPart::Face,CustomizationPart::Neck,CustomizationPart::Back};
    for(int n=0;n<8;++n) add(CustomizationKind::Accessory,200+n,parts[n%4],n%2?CustomizationCharacter::All:CustomizationCharacter::Kaito,"合成道具"+std::to_string(n));
    for(int n=0;n<4;++n) add(CustomizationKind::Module,100+n,CustomizationPart::Unknown,CustomizationCharacter::Miku,"合成模型"+std::to_string(n));
    return catalog;
}
// 记录型 Fake 后端：按模拟原生页独立计算跨页需求并复用生产权限网关，统计导航、原位定位和动作次数。
struct FakeItemBackend {
    std::optional<diva::CustomizationDomain> page;
    bool allowActions=false;
    std::vector<diva::CustomizationLocateRequest> requests;
    int navigations=0,locates=0,actions=0,rejections=0;
    // 参数 request 为界面提交的发型／道具请求；目标页由 Fake 自行计算，不信任界面。
    void Submit(const diva::CustomizationLocateRequest& request) {
        requests.push_back(request);
        const bool atTarget=page && *page==request.domain;
        if(!diva::CheckCustomizationNavigation(request,!atTarget).empty()) {++rejections;return;}
        if(!atTarget) ++navigations;
        else if(request.action==diva::CustomizationAction::Locate) ++locates;
        else ++actions;
    }
    // 参数 domain 为卡片领域；返回只依赖模拟页面的能力，按钮能力仍需在目标页。
    diva::CustomizationActionCapability Capability(diva::CustomizationDomain domain) const {
        diva::CustomizationActionCapability c;c.targetPage=page && *page==domain;c.preview=c.equip=allowActions;
        c.locateReason="合成后端未认证定位。";c.previewReason=c.equipReason="合成后端未认证动作。";return c;
    }
};
// 参数 view 为测试视图；返回网格子窗口，测试据此计算卡片坐标。
ImGuiWindow* GridWindow() {
    // 每个领域／子标签有各自的子窗口；取最近一帧实际绘制的那个，避免用到已隐藏领域的旧坐标。
    ImGuiWindow* latest=nullptr;
    for(auto* window:GImGui->Windows) if(std::strstr(window->Name,"module-grid") && (!latest || window->LastFrameActive>latest->LastFrameActive)) latest=window;
    if(!latest) throw std::runtime_error("缺少定制网格窗口");
    return latest;
}
// 参数 view/point/frames 为视图、屏幕坐标与左键按下前的悬浮帧数；执行一次真实 ImGui 左键单击。
void ClickAt(diva::CustomizationView& view,ImVec2 point,int frames=1) {
    auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);for(int n=0;n<frames;++n) DrawCustomization(view);
    io.AddMouseButtonEvent(0,true);DrawCustomization(view);io.AddMouseButtonEvent(0,false);DrawCustomization(view);
}
// 参数 view/frames 为视图与空闲帧数；推进 ImGui 时钟使旧首击超出双击时限。
void Idle(diva::CustomizationView& view,int frames) {for(int n=0;n<frames;++n) DrawCustomization(view);}
// 验证发型／道具 Tab 独立状态、Fake 后端隔离、主体双击唯一跨页及按钮、Enter、底栏、悬浮、排序和滚动守卫。
void ItemDomainInteractions() {
    using namespace diva;
    auto* context=ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize={1280.0f,900.0f};io.DeltaTime=1.0f/60.0f;
    unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);

    CustomizationView view;view.catalog=std::make_shared<CustomizationCatalog>(ItemDomainCatalog());++view.generation;view.OnOpen(std::nullopt);
    FakeItemBackend backend;int moduleCalls=0,moduleActions=0;
    view.submitItemRequest=[&](const CustomizationLocateRequest& request) {backend.Submit(request);};
    // 参数 request 为模型请求；发型与道具请求不得进入模型定位器。
    view.submitModuleLocate=[&](const CustomizationLocateRequest& request) {(void)request;++moduleCalls;};
    view.submitModuleAction=[&](const CustomizationLocateRequest& request) {(void)request;++moduleActions;};
    // 参数 key 为按钮所在卡片；按卡片自身领域计算能力。
    view.actionCapability=[&](const CustomizationKey& key) {
        for(const auto& item:view.catalog->items) if(item.key==key) {auto domain=CustomizationItemDomain(item);if(domain) return backend.Capability(*domain);}
        return CustomizationActionCapability{};
    };

    // 三个领域首次人物均为全部，并且切换领域不提交任何请求。
    for(auto& state:view.states) Check(!state.query.character,"定制领域首次人物不是全部");
    view.SetDomain(CustomizationDomain::Hair);Complete(view);DrawCustomization(view);DrawCustomization(view);
    Check(view.State().results.size()==30 && backend.requests.empty() && moduleCalls==0,"发型领域结果错误或切Tab提交了请求");
    std::strcpy(view.State().input,"合成发型1");view.QueueSearch(false);view.State().query.character=CustomizationCharacter::Rin;view.QueueSearch(false);Complete(view);
    const auto hairRevision=view.State().revision;const auto hairResults=view.State().results;
    view.SetDomain(CustomizationDomain::Accessories);Complete(view);
    // 道具首个子标签为头；每次只展示一个部位，不提供混合四部位结果，也没有人物筛选。
    auto allPart=[&](CustomizationPart part) {
        return std::all_of(view.State().results.begin(),view.State().results.end(),[&](size_t index){return view.catalog->items[index].part==part;});
    };
    Check(view.State().input[0]==0 && !view.State().query.character && view.accessoryPart==CustomizationPart::Head
        && view.State().results.size()==2 && allPart(CustomizationPart::Head),"道具领域继承了发型查询、人物筛选或混合了部位");
    std::strcpy(view.State().input,"合成道具");view.QueueSearch(false);Complete(view);view.State().cursor.selected=1;
    const auto headRevision=view.State().revision;
    view.SetAccessoryPart(CustomizationPart::Face);Complete(view);
    Check(view.State().input[0]==0 && view.State().results.size()==2 && allPart(CustomizationPart::Face) && view.State().query.part==CustomizationPart::Face,"脸部子标签继承了头部查询或部位错误");
    for(auto part:{CustomizationPart::Neck,CustomizationPart::Back}) {
        view.SetAccessoryPart(part);Complete(view);
        Check(view.State().results.size()==2 && allPart(part) && !view.State().query.character,"胸口／背部子标签结果混入其他部位");
    }
    view.SetAccessoryPart(CustomizationPart::Head);
    Check(std::string(view.State().input)=="合成道具" && view.State().cursor.selected==1 && view.State().revision==headRevision,"头部子标签未独立保留查询、选择或修订");
    view.SetAccessoryPart(CustomizationPart::Face);
    Check(backend.requests.empty(),"切换道具子标签提交了原生请求");
    view.SetDomain(CustomizationDomain::Models);Complete(view);
    Check(view.State().results.size()==4 && !view.State().query.part,"模型领域混入定制结果或继承部位");
    view.SetDomain(CustomizationDomain::Hair);
    Check(std::string(view.State().input)=="合成发型1" && view.State().query.character==CustomizationCharacter::Rin && view.State().revision==hairRevision
        && view.State().results==hairResults,"切回发型丢失独立查询、筛选或修订");
    view.SetDomain(CustomizationDomain::Accessories);Check(view.accessoryPart==CustomizationPart::Face && view.State().query.part==CustomizationPart::Face,"切回道具丢失子标签");
    Check(backend.requests.empty() && moduleCalls==0,"切换搜索Tab触发了原生请求");

    // 恢复发型全部结果，模拟当前原生页为模型页（发型目标不在当前页）。
    view.SetDomain(CustomizationDomain::Hair);view.State().input[0]=0;view.State().query.character.reset();view.QueueSearch(false);Complete(view);
    // 恢复时保留了旧选择并滚动到它；测试显式回到首项并滚到顶部，使坐标与稳定ID对应。
    ImGui::ClearActiveID();view.State().focus=false;view.State().cursor={};view.State().scroll=true;Idle(view,3);
    Check(GridWindow()->Scroll.y<1.0f,"网格未回到顶部");
    backend.page=CustomizationDomain::Models;
    auto* grid=GridWindow();
    const ImVec2 first(grid->InnerRect.Min.x+30,grid->InnerRect.Min.y+30);
    const ImVec2 second(first.x+view.cardWidth+12.0f,first.y);
    // 悬浮卡片只显示操作条，不提交请求。
    io.AddMousePosEvent(first.x,first.y);Idle(view,30);
    Check(backend.requests.empty(),"悬浮卡片提交了请求");
    ClickAt(view,first);
    Check(backend.requests.size()==1 && backend.requests.back().origin==CustomizationOrigin::CardSingleClick && backend.navigations==0
        && backend.requests.back().domain==CustomizationDomain::Hair && backend.requests.back().part==CustomizationPart::Hair && backend.requests.back().key.id==100,"发型单击来源、领域或部位错误，或单击跨页");
    ClickAt(view,first,0);
    const auto doubleClick=backend.requests.back();
    Check(backend.requests.size()==2 && doubleClick.origin==CustomizationOrigin::CardDoubleClick && doubleClick.firstClickKey==doubleClick.key
        && doubleClick.firstClickDomain==CustomizationDomain::Hair && doubleClick.firstClickRevision==doubleClick.queryRevision && backend.navigations==1,"同一发型主体双击未恰好产生一次跨页");
    Check(moduleCalls==0 && moduleActions==0,"发型请求进入了模型定位器");
    // 跨两张卡片、过期首击都不能构成双击。
    Idle(view,40);ClickAt(view,first,0);ClickAt(view,second,0);
    Check(backend.requests.back().origin==CustomizationOrigin::CardSingleClick && backend.requests.back().key.id==101 && backend.navigations==1,"跨卡片点击被当作双击");
    // 首击后排序变化（同位置仍是同一稳定ID）使旧首击失效。
    Idle(view,40);ClickAt(view,first,0);
    view.State().query.sort=2;view.QueueSearch(false);Complete(view);Idle(view,1);
    Check(view.catalog->items[view.State().results.front()].key.id==100,"排序夹具未保持首项");
    ClickAt(view,first,0);
    Check(backend.requests.back().origin==CustomizationOrigin::CardSingleClick && backend.navigations==1,"排序后旧首击仍授予跨页");
    // 首击后滚动再回到原位使旧首击失效。
    Idle(view,40);ClickAt(view,first,0);
    io.AddMouseWheelEvent(0,-1);Idle(view,2);io.AddMouseWheelEvent(0,1);Idle(view,2);
    Check(grid->Scroll.y<1.0f,"滚动夹具未回到顶部");
    ClickAt(view,first,0);
    Check(backend.requests.back().origin==CustomizationOrigin::CardSingleClick && backend.navigations==1,"滚动后旧首击仍授予跨页");
    // Enter 与底栏定位不跨页；输入法组词期间 Enter 不提交。
    Idle(view,40);const auto beforeKeyboard=backend.requests.size();
    view.composing=true;view.RequestDetails();Idle(view,2);view.composing=false;
    Check(backend.requests.size()==beforeKeyboard,"输入法组词期间Enter提交了定位");
    view.RequestDetails();Idle(view,2);
    Check(backend.requests.size()==beforeKeyboard+1 && backend.requests.back().origin==CustomizationOrigin::Keyboard && !backend.requests.back().firstClickKey && backend.navigations==1,"Enter获得跨页权限");
    view.RequestLocate(view.catalog->items[view.State().results.front()].key);Idle(view,2);
    Check(backend.requests.back().origin==CustomizationOrigin::Footer && backend.navigations==1,"底栏定位获得跨页权限");
    // 不在目标页时按钮禁用；即使能力误报可执行，也只提示双击而不提交、不冒泡成主体点击。
    backend.allowActions=true;const auto beforeButtons=backend.requests.size();
    // 发型卡片使用紧凑图片区；操作条位于图片区底部。
    Check(view.imageHeight>=view.cardWidth*0.70f-0.5f && view.imageHeight<=view.cardWidth*0.85f+0.5f && view.cardHeight<340.0f*0.76f && view.cardHeight>340.0f*0.55f,"发型卡片尺寸不在设计区间");
    const ImVec2 equip(grid->DC.CursorStartPos.x+35,grid->DC.CursorStartPos.y+view.imageHeight-22.0f);
    Idle(view,40);ClickAt(view,equip);ClickAt(view,equip,0);
    Check(backend.requests.size()==beforeButtons && backend.navigations==1 && moduleActions==0,"不在发型页时更换按钮提交请求、跨页或冒泡");
    view.RequestAction(view.catalog->items[view.State().results.front()].key,CustomizationAction::Preview);
    Check(backend.requests.size()==beforeButtons && view.status.find("发型页")!=std::string::npos,"非目标页预览按钮未提示双击进入发型页");
    // 在目标页时按钮只执行一次动作，双击按钮不冒泡成主体定位或导航。
    backend.page=CustomizationDomain::Hair;Idle(view,40);
    ClickAt(view,equip);ClickAt(view,equip,0);
    Check(backend.actions==1 && backend.locates==0 && backend.navigations==1 && backend.requests.back().origin==CustomizationOrigin::HoverButton
        && backend.requests.back().action==CustomizationAction::Equip && backend.requests.back().key.id==100,"目标页按钮双击重复动作、冒泡或借用了其他目标");
    // 目标页内单击／Enter 是原位定位，不是跨页。
    Idle(view,40);ClickAt(view,second,0);
    Check(backend.locates==1 && backend.navigations==1,"目标页内单击未原位定位或误跨页");
    // 伪造按钮请求携带双击首击也不能跨页；首击领域不符的双击被网关拒绝。
    auto forged=doubleClick;forged.action=CustomizationAction::Preview;forged.origin=CustomizationOrigin::CardDoubleClick;
    Check(!CheckCustomizationNavigation(forged,true).empty(),"伪造双击的预览按钮获得跨页权限");
    forged=doubleClick;forged.firstClickDomain=CustomizationDomain::Accessories;
    Check(!CheckCustomizationNavigation(forged,true).empty() && CheckCustomizationNavigation(doubleClick,true).empty(),"首击领域不符的双击通过网关");
    Check(CheckCustomizationNavigation(doubleClick,true).find("发型页")==std::string::npos && CheckCustomizationNavigation(forged,true).find("发型页")!=std::string::npos,"跨页提示未使用发型页文案");

    // 道具领域双击携带真实部位，并与发型状态各自独立。
    backend.page=CustomizationDomain::Hair;const auto navigationsBefore=backend.navigations;
    view.SetDomain(CustomizationDomain::Accessories);view.SetAccessoryPart(CustomizationPart::Head);view.State().input[0]=0;view.QueueSearch(false);Complete(view);
    view.State().cursor={};view.State().scroll=true;Idle(view,40);
    grid=GridWindow();
    // 道具页多一行子标签，网格位置下移；按当前网格重新计算首张卡片坐标。
    const ImVec2 accessoryFirst(grid->InnerRect.Min.x+30,grid->InnerRect.Min.y+30);
    Check(view.imageHeight>=view.cardWidth*0.50f-0.5f && view.imageHeight<=view.cardWidth*0.65f+0.5f && view.cardHeight<340.0f*0.66f,"道具卡片尺寸不在设计区间");
    // 首击后切换子标签再切回，旧首击失效，不构成双击。
    ClickAt(view,accessoryFirst);view.SetAccessoryPart(CustomizationPart::Face);view.SetAccessoryPart(CustomizationPart::Head);Idle(view,1);ClickAt(view,accessoryFirst,0);
    Check(backend.requests.back().domain==CustomizationDomain::Accessories && backend.requests.back().origin==CustomizationOrigin::CardSingleClick
        && backend.navigations==navigationsBefore,"切换子标签后旧首击仍授予跨页");
    Idle(view,40);ClickAt(view,accessoryFirst);ClickAt(view,accessoryFirst,0);
    const auto item=backend.requests.back();
    Check(backend.navigations==navigationsBefore+1 && item.domain==CustomizationDomain::Accessories && item.part==CustomizationPart::Head && item.key.id==200
        && item.firstClickDomain==CustomizationDomain::Accessories,"道具双击未携带领域与部位或未跨页");
    Check(moduleCalls==0,"道具请求进入了模型定位器");
    // 模型领域仍走原模型定位入口，不经发型／道具后端。
    const auto itemRequests=backend.requests.size();
    view.SetDomain(CustomizationDomain::Models);Complete(view);Idle(view,40);grid=GridWindow();
    ClickAt(view,ImVec2(grid->InnerRect.Min.x+30,grid->InnerRect.Min.y+30));
    Check(moduleCalls==1 && backend.requests.size()==itemRequests,"模型请求进入了发型／道具后端");
    ImGui::DestroyContext(context);
}
}
// 参数 argc/argv 保留命令行兼容；测试只在系统临时隔离目录创建夹具。
int main(int argc,char** argv) {
    (void)argc;(void)argv;auto root=diva::fs::temp_directory_path()/("diva-customization-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {Parser(root);auto catalog=Catalog(root);
        DefaultSearchLanguages();Navigation();Async(catalog);LocateRevision(catalog);ItemDomainInteractions();diva::fs::remove_all(root);std::cout<<"服装饰品基础回归通过："<<checks<<" 项；未验证Windows原生DX11与实际游戏。\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<"\n";diva::fs::remove_all(root);return 1;}
}
