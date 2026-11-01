#include "customization_ui.h"
#include <imgui.h>
#include <imgui_internal.h>
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
    query={};query.kind=CustomizationKind::Accessory;Check(SearchCustomization(catalog,query).size()==1,"共通饰品未匹配具体角色或未知误入MIKU");
    query.part=CustomizationPart::Face;Check(SearchCustomization(catalog,query).size()==1,"饰品部位交集错误");
    query.character=CustomizationCharacter::Unknown;query.part=CustomizationPart::Unknown;Check(SearchCustomization(catalog,query).size()==1,"未知角色/部位过滤失败");
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
    std::strcpy(view.State().input,"English Alias");view.QueueSearch(true);view.Poll();Check(!view.searching.valid() || view.runningMode==1,"防抖期间提前启动当前模式任务");
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
    view.SetKind(CustomizationKind::Accessory);Check(!view.states[0].detailsRequested,"模式切换遗留待确认");
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
}
// 参数 argc/argv 保留命令行兼容；测试只在系统临时隔离目录创建夹具。
int main(int argc,char** argv) {
    (void)argc;(void)argv;auto root=diva::fs::temp_directory_path()/("diva-customization-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {Parser(root);auto catalog=Catalog(root);
        DefaultSearchLanguages();Navigation();Async(catalog);LocateRevision(catalog);diva::fs::remove_all(root);std::cout<<"服装饰品基础回归通过："<<checks<<" 项；未验证Windows原生DX11与实际游戏。\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<"\n";diva::fs::remove_all(root);return 1;}
}
