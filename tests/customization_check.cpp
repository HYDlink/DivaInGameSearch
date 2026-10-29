#include "customization_ui.h"
#include <imgui.h>
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
// 参数 catalog/kind/id 为快照和实体键，测试断言需要明确存在。
const diva::CustomizationItem& Find(const diva::CustomizationCatalog& catalog,diva::CustomizationKind kind,int id) {
    for(const auto& item:catalog.items) if(item.key.kind==kind && item.key.id==id) return item;
    throw std::runtime_error("测试实体缺失");
}
// 参数 root 为临时目录；验证真实key=value格式、稀疏和ID边界。
void Parser(const diva::fs::path& root) {
    using namespace diva;std::vector<std::string> diagnostics;auto path=root/"parser/gm_module_id.bin";
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
    Write(h/"dump/low/gm_module_id.bin","module.0.id=0\nmodule.0.name=Low Table\nmodule.0.chara=RIN\nmodule.0.cos=COS_999\nmodule.0.sort_index=1\nmodule.1.id=1\nmodule.1.chara=MIKU\nmodule.data_list.length=2\n");
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
    auto catalog=LoadCustomizationCatalog(h,g);Check(catalog.items.size()==6,"普通替换表累加、禁用来源导入或孤立名字创建实体");
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
    Check(view.contextReason.find("默认初音未来")!=std::string::npos,"未知角色没有说明默认原因");
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
    view.textNavigation=true;view.controlNavigation=false;view.State().detailsRequested=true;view.QueueSearch(true);Check(view.State().detailsRequested,"同帧输入清掉已排队Enter");view.SetKind(CustomizationKind::Accessory);Check(!view.states[0].detailsRequested,"模式切换遗留待确认");
    Check(!view.WantsHorizontalNavigation(),"文本框左右被格子接管");
    view.OnOpen(std::nullopt);Check(!TryReadCustomizationContext() && !GetCustomizationCapabilities().canLocate && !GetCustomizationCapabilities().canPreviewNative,"安全stub意外启用原生能力");
}
}
// 参数 argc/argv 保留命令行兼容；测试只在系统临时隔离目录创建夹具。
int main(int argc,char** argv) {
    (void)argc;(void)argv;auto root=diva::fs::temp_directory_path()/("diva-customization-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {Parser(root);auto catalog=Catalog(root);Navigation();Async(catalog);diva::fs::remove_all(root);std::cout<<"服装饰品基础回归通过："<<checks<<" 项；未验证Windows原生DX11与实际游戏。\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<"\n";diva::fs::remove_all(root);return 1;}
}
