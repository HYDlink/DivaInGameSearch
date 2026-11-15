#include "customization.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace diva;
int checks=0;
// 参数 condition/message 为回归条件与中文失败原因。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
// 参数 kind/id/language 为物品种类、稳定编号和语言；只生成虚构名称。
std::string Name(CustomizationKind kind,int id,const std::string& language="jp") {
    return std::string(language=="cn"?"中文":language=="en"?"English":"モジュール")+
        (kind==CustomizationKind::Module?"模型":"定制")+std::to_string(id);
}
// 参数 kind/count 为种类和记录数；生成带角色、服装或绑定对象身份的合成实体表。
std::string Table(CustomizationKind kind,int count=30) {
    std::string text;const std::string group=kind==CustomizationKind::Module?"module.":"cstm_item.";
    for(int id=0;id<count;++id) {
        const auto prefix=group+std::to_string(id)+".";
        text+=prefix+"id="+std::to_string(id)+"\n"+prefix+"name="+Name(kind,id)+"\n"+prefix+"chara=MIKU\n";
        if(kind==CustomizationKind::Module) text+=prefix+"cos=COS_"+std::to_string(id)+"\n";
        else text+=prefix+"obj_id="+std::to_string(1000+id)+"\n"+prefix+"parts="+(id%2?"FACE":"KAMI")+"\n"+prefix+"bind_module=0\n";
    }
    return text+group+"data_list.length="+std::to_string(count)+"\n";
}
// 参数 data/value 为普通FArc目录缓冲和大端整数。
void Big(std::string& data,uint32_t value) {for(int shift:{24,16,8,0}) data.push_back(char(value>>shift));}
// 参数 kind/text 为种类和实体文本；生成真实读取器使用的普通FArc。
std::string Farc(CustomizationKind kind,const std::string& text) {
    const std::string name=kind==CustomizationKind::Module?"gm_module_id.bin":"gm_customize_item_id.bin";
    const auto directory=static_cast<uint32_t>(13+name.size());
    std::string data="FArc";Big(data,directory);Big(data,16);data+=name;data.push_back('\0');
    Big(data,directory+8);Big(data,static_cast<uint32_t>(text.size()));return data+text;
}
// 参数 text/from/to 为表文本、必须存在的原字段和值；修改一处以构造重定义场景。
std::string Replace(std::string text,const std::string& from,const std::string& to) {
    const auto at=text.find(from);Check(at!=std::string::npos,"测试要求的原始字段不存在");text.replace(at,from.size(),to);return text;
}
// 参数 path/text 为隔离目录中的文件和UTF-8内容。
void Write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<text;
    Check(output.good(),"测试夹具写入失败");
}
// 参数 path 为隔离测试生成的UTF-8名称表；返回完整字节以验证用户文件未被覆盖。
std::string Read(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);Check(input.good(),"测试名称表不存在");
    std::ostringstream text;text<<input.rdbuf();return text.str();
}
// 参数 language/mismatches 为语言与日文模型故意不一致数；生成经典小端字符串表。
std::string Strings(const std::string& language,int mismatches=0) {
    constexpr int count=0x3a8+30;
    std::string data((count+1)*4,'\0');
    for(int index=0;index<count;++index) {
        const auto offset=static_cast<uint32_t>(data.size());
        for(int n=0;n<4;++n) data[index*4+n]=char(offset>>(n*8));
        if(index>=0x3a8) data+=Name(CustomizationKind::Accessory,index-0x3a8,language);
        else if(index>=0x113 && index<0x113+30) data+=index-0x113<mismatches?"不一致":Name(CustomizationKind::Module,index-0x113,language);
        else if(index==0x113+30) data+="不属于模型的其他字符串";
        else data+="BLANK";
        data.push_back('\0');
    }
    return data;
}
// 返回三种语言的规范PC路径候选；所有数据均由测试生成。
std::vector<OfficialCpkEntry> Entries() {
    std::vector<OfficialCpkEntry> entries;
    for(const auto* language:{"cn","jp","en"}) entries.push_back({"diva_main.cpk","rom_steam/rom/lang2/"+std::string(language)+"/str_array.bin","",Strings(language),language});
    return entries;
}
// 参数 entries 为可注入的语言候选；返回与生产相同签名的三类官方读取入口。
OfficialTableSource Source(const std::vector<OfficialCpkEntry>& entries=Entries()) {
    OfficialTableSource source;
    // 参数为游戏目录、插件目录与诊断；本回调只返回合成官方模型表。
    source.modules=[](const fs::path&,const fs::path&,std::vector<std::string>&) {
        return std::vector<OfficialEntityTable>{{"diva_main_region.cpk","rom_steam_region/rom/gm_module_tbl.farc!/gm_module_id.bin","",Table(CustomizationKind::Module),"模型基准"}};
    };
    // 参数同上；本回调只返回合成官方定制物品表。
    source.customizeItems=[](const fs::path&,const fs::path&,std::vector<std::string>&) {
        return std::vector<OfficialEntityTable>{{"diva_main_region.cpk","rom_steam_region/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin","",Table(CustomizationKind::Accessory),"定制基准"}};
    };
    // 参数 suffix 为目标后缀，其余参数保留生产接口；模拟已通过下层后缀匹配的候选。
    source.strings=[entries](const fs::path&,const fs::path&,const std::string& suffix,std::vector<std::string>&) {
        std::vector<OfficialCpkEntry> result;
        for(const auto& entry:entries) {
            std::string path=entry.internalPath;
            for(auto& ch:path) {if(ch=='\\') ch='/';else if(ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');}
            if(path.ends_with(suffix)) result.push_back(entry);
        }
        return result;
    };
    return source;
}
// 参数 root 为场景目录；启用唯一合成Mod，不访问游戏安装目录。
fs::path Game(const fs::path& root) {
    Write(root/"game/config.toml","enabled=true\npriority=['Fixture']\n");
    Write(root/"game/mods/Fixture/config.toml","enabled=true\ninclude=['.']\n");return root/"game";
}
// 参数 catalog/kind/id 为快照和稳定身份，返回唯一匹配条目。
const CustomizationItem& Find(const CustomizationCatalog& catalog,CustomizationKind kind,int id) {
    for(const auto& item:catalog.items) if(item.key==CustomizationKey{kind,id}) return item;
    throw std::runtime_error("预期物品缺失");
}
// 参数 catalog/expected/language 为快照、期望实体数和显示语言；检查全部物品名称。
void Names(const CustomizationCatalog& catalog,size_t expected,const std::string& language="cn") {
    Check(catalog.items.size()==expected,"名称修复改变了实体集合");
    for(const auto& item:catalog.items) Check(item.name==Name(item.key.kind,item.key.id,language),"物品名称未使用预期语言");
}
// 参数 root 为隔离目录；验证覆盖、删除与三种语言，定义来源和官方译名来源互不混淆。
void Inheritance(const fs::path& root) {
    for(const auto* language:{"cn","en","jp"}) Names(LoadCustomizationCatalog(root/"base/home",Game(root/"base"),language,Source()),60,language);
    const auto modules=CustomizationKind::Module,items=CustomizationKind::Accessory;
    const auto full=root/"full",game=Game(full);
    Write(game/"mods/Fixture/rom/gm_module_tbl.farc",Farc(modules,Table(modules,29)));
    Write(game/"mods/Fixture/rom/gm_customize_item_tbl.farc",Farc(items,Table(items)));
    const auto catalog=LoadCustomizationCatalog(full/"home",game,"cn",Source());Names(catalog,59);
    const auto& zero=Find(catalog,modules,0);
    Check(zero.definitionSource=="dml:Fixture:." && zero.nameSource=="official:pc-region","定义来源与继承译名来源混淆");
    // 参数 contribution 为单条来源贡献；有效名称必须唯一。
    Check(std::count_if(zero.sources.begin(),zero.sources.end(),[](const auto& contribution) {return contribution.role==CustomizationContributionRole::Name && contribution.effective;})==1,"继承名称贡献未唯一登记");
    const auto partial=root/"partial",partialGame=Game(partial);
    Write(partialGame/"mods/Fixture/rom/mod_gm_module_tbl.farc",Farc(modules,Table(modules,25)));
    Write(partialGame/"mods/Fixture/rom/mod_gm_customize_item_tbl.farc",Farc(items,Table(items,25)));
    Names(LoadCustomizationCatalog(partial/"home",partialGame,"cn",Source()),60);
}
// 参数 root 为隔离目录；保护显式改名、同ID换装、不同部位／绑定与名称补丁优先级。
void Overrides(const fs::path& root) {
    const auto modules=CustomizationKind::Module,items=CustomizationKind::Accessory;
    const auto game=Game(root);
    auto moduleText=Replace(Table(modules,31),"module.0.name="+Name(modules,0),"module.0.name=作者自定义服装");
    moduleText=Replace(moduleText,"module.1.cos=COS_1\n","module.1.cos=COS_CUSTOM\n");
    moduleText=Replace(moduleText,"module.2.chara=MIKU","module.2.chara=RIN");
    moduleText+="module.3.sort_index=999\n";
    auto itemText=Replace(Table(items),"cstm_item.0.obj_id=1000","cstm_item.0.obj_id=9999");
    itemText=Replace(itemText,"cstm_item.1.parts=FACE","cstm_item.1.parts=NECK");
    itemText=Replace(itemText,"cstm_item.2.bind_module=0","cstm_item.2.bind_module=99");
    Write(game/"mods/Fixture/rom/mod_gm_module_tbl.farc",Farc(modules,moduleText));
    Write(game/"mods/Fixture/rom/mod_gm_customize_item_tbl.farc",Farc(items,itemText));
    Write(game/"mods/Fixture/rom/lang2/mod_str_array.toml","module.4='作者通用名'\ncn.module.5='作者中文名'\n");
    const auto catalog=LoadCustomizationCatalog(root/"home",game,"cn",Source());
    Check(Find(catalog,modules,0).name=="作者自定义服装","覆盖了作者显式改名");
    for(int id:{1,2}) Check(Find(catalog,modules,id).name==Name(modules,id),"对同ID重定义服装误贴官方译名");
    for(int id:{0,1,2}) Check(Find(catalog,items,id).name==Name(items,id),"对重定义饰品误贴官方译名");
    Check(Find(catalog,modules,3).name==Name(modules,3,"cn"),"排序字段变化不应阻止继承译名");
    Check(Find(catalog,modules,30).name==Name(modules,30),"新增Mod编号被错误映射到无关字符串");
    Check(Find(catalog,modules,4).name=="作者通用名" && Find(catalog,modules,5).name=="作者中文名","破坏了显式名称补丁优先级");
    const auto& patched=Find(catalog,modules,4);
    // 参数 contribution 为覆盖后的来源贡献；被替换的官方名称不得继续标记有效。
    Check(std::count_if(patched.sources.begin(),patched.sources.end(),[](const auto& contribution) {return contribution.role==CustomizationContributionRole::Name && contribution.effective;})==1,"名称覆盖后保留了错误的有效贡献");
    const auto manifest=root/"manifest",manifestGame=Game(manifest);
    Write(manifest/"home/customization_catalog.toml","schema_version=1\n[[sources]]\nid='fixture'\nmod_directory='Fixture'\ninclude_root='.'\n[[items]]\nkind='module'\nid=0\nsource='fixture'\nname='"+Name(modules,0)+"'\nchara='MIKU'\ncos='COS_0'\n[items.names]\ncn='清单显式译名'\n");
    Check(Find(LoadCustomizationCatalog(manifest/"home",manifestGame,"cn",Source()),modules,0).name=="清单显式译名","继承名称覆盖了导入清单中的显式译名");
}
// 参数 root 为隔离目录；验证路径规范化、坏候选回退、同内容去重和冲突拒绝。
void Candidates(const fs::path& root) {
    auto entries=Entries();entries[0].internalPath="ROM_STEAM\\rom\\lang2\\cn\\str_array.bin";
    Names(LoadCustomizationCatalog(root/"normalized/home",Game(root/"normalized"),"cn",Source(entries)),60);
    entries=Entries();auto broken=entries[0];broken.entryData="损坏";entries.insert(entries.begin(),broken);
    entries.push_back(entries[1]);Names(LoadCustomizationCatalog(root/"fallback/home",Game(root/"fallback"),"cn",Source(entries)),60);
    entries=Entries();auto conflict=entries[0];conflict.entryData=Strings("en");entries.push_back(conflict);
    const auto rejected=LoadCustomizationCatalog(root/"conflict/home",Game(root/"conflict"),"cn",Source(entries));Names(rejected,60,"jp");
    // 参数 line 为目录诊断；内容冲突应有明确记录。
    Check(std::any_of(rejected.diagnostics.begin(),rejected.diagnostics.end(),[](const auto& line) {return line.find("内容冲突")!=std::string::npos;}),"冲突候选未留下可解释诊断");
    entries=Entries();entries.erase(entries.begin()+1);
    Names(LoadCustomizationCatalog(root/"missing/home",Game(root/"missing"),"cn",Source(entries)),60,"jp");
    entries=Entries();entries[0].internalPath="rom_ps4/rom/lang2/cn/str_array.bin";
    Names(LoadCustomizationCatalog(root/"platform/home",Game(root/"platform"),"cn",Source(entries)),60,"jp");
    entries=Entries();entries[1].entryData=Strings("jp",4);
    const auto partitioned=LoadCustomizationCatalog(root/"mismatch/home",Game(root/"mismatch"),"cn",Source(entries));
    for(const auto& item:partitioned.items) Check(item.name==Name(item.key.kind,item.key.id,item.key.kind==CustomizationKind::Module?"jp":"cn"),"核验失败影响了其他物品种类");
}
// 参数 root 为隔离目录；验证自动参照、个人最高优先级、原名检索与改名刷新不触发资源变更。
void UserNames(const fs::path& root) {
    const auto home=root/"home",game=Game(root),userFile=home/"module_names.toml",referenceFile=home/"module_names_reference.toml";
    const auto initial=LoadCustomizationCatalog(home,game,"cn",Source());Names(initial,60);
    const auto templateText=Read(userFile);
    Check(templateText.find("0 = \"\" # "+Name(CustomizationKind::Module,0,"cn"))!=std::string::npos,"首份可编辑名称表缺少ID与默认名参照");
    Check(templateText.find("| COS_0")!=std::string::npos,"名称参照缺少服装身份信息");
    Check(Read(referenceFile).find(Name(CustomizationKind::Accessory,0,"cn"))==std::string::npos,"服装参照混入其他编号空间");
    Write(game/"mods/Fixture/rom/lang2/mod_str_array.toml","cn.module.0='Mod原显示名'\n");
    const auto before=LoadCustomizationCatalog(home,game,"cn",Source());
    const std::string custom="# 保留我的注释和排版\n[module]\n0 = \"个人专用服装\"\n1 = \"\"\n99999 = \"不存在的服装\"\n";
    Write(userFile,custom);
    const auto renamed=LoadCustomizationCatalog(home,game,"cn",Source());
    const auto& item=Find(renamed,CustomizationKind::Module,0);
    Check(item.name=="个人专用服装" && item.nameSource=="user:module-names","个人名称未覆盖Mod名称");
    Check(item.definitionSource==Find(before,CustomizationKind::Module,0).definitionSource,"改名改变了实体定义来源");
    Check(renamed.items.size()==before.items.size(),"未知个人名称编号创建了实体");
    Check(Find(renamed,CustomizationKind::Accessory,0).name==Name(CustomizationKind::Accessory,0,"cn"),"模型改名影响了同编号定制物品");
    Check(Find(renamed,CustomizationKind::Module,1).name==Name(CustomizationKind::Module,1,"cn"),"留空名称未沿用原名");
    Check(renamed.catalogFingerprint==before.catalogFingerprint,"仅修改搜索名称被误判为游戏资源变更");
    Check(Read(userFile)==custom,"程序覆盖了用户名称表或注释");
    const auto reference=Read(referenceFile);
    Check(reference.find("Mod原显示名")!=std::string::npos && reference.find("个人专用服装")==std::string::npos,"自动参照表混入个人覆盖名");
    for(const auto& text:{std::string("个人专用服装"),std::string("Mod原显示名"),Name(CustomizationKind::Module,0),Name(CustomizationKind::Module,0,"cn")}) {
        CustomizationQuery query;query.character.reset();query.text=text;
        const auto results=SearchCustomization(renamed,query);
        // 参数 index 为候选索引；新名和各个旧名都应命中同一稳定模型ID。
        Check(std::any_of(results.begin(),results.end(),[&](size_t index) {return renamed.items[index].key==CustomizationKey{CustomizationKind::Module,0};}),"自定义名或旧名称无法继续检索");
    }
    // 参数 contribution 为重命名后的来源贡献；只有个人名称贡献可标为有效。
    Check(std::count_if(item.sources.begin(),item.sources.end(),[](const auto& contribution) {return contribution.role==CustomizationContributionRole::Name && contribution.effective;})==1,"个人名称贡献未唯一生效");
    Write(game/"mods/Fixture/rom/mod_gm_module_tbl.farc",Farc(CustomizationKind::Module,Table(CustomizationKind::Module,31)));
    const auto expanded=LoadCustomizationCatalog(home,game,"cn",Source());
    Check(Read(referenceFile).find("30 = \"\"")!=std::string::npos,"新增服装未更新自动参照");
    Check(Read(userFile)==custom && Find(expanded,CustomizationKind::Module,0).name=="个人专用服装","更新参照时丢失个人名称");
    Write(userFile,"[module]\n0 = \"\"\n");
    Check(Find(LoadCustomizationCatalog(home,game,"cn",Source()),CustomizationKind::Module,0).name=="Mod原显示名","清空名称后未恢复原有优先级");
}
// 参数 root 为隔离目录；无效、超限与损坏配置安全回退，原始文件始终保留。
void InvalidUserNames(const fs::path& root) {
    const auto home=root/"home",game=Game(root),path=home/"module_names.toml";
    const std::vector<std::string> broken={"[module]\n0 = \"未闭合\n",std::string("[module]\n0 = \"")+char(0xff)+"\"\n",std::string(8*1024*1024+1,'x')};
    for(const auto& text:broken) {
        Write(path,text);Names(LoadCustomizationCatalog(home,game,"cn",Source()),60);
        Check(Read(path)==text,"错误名称表被自动改写");
    }
    Write(path,"[module]\n0 = 42\n1 = \"含\\n换行\"\n2 = \"有效名称\"\n3 = \"   \"\n\"-1\" = \"负编号\"\n\"02\" = \"前导零重复编号\"\n");
    const auto partial=LoadCustomizationCatalog(home,game,"cn",Source());
    Check(Find(partial,CustomizationKind::Module,0).name==Name(CustomizationKind::Module,0,"cn"),"非法名称类型没有回退");
    Check(Find(partial,CustomizationKind::Module,1).name==Name(CustomizationKind::Module,1,"cn"),"名称接受了换行控制字符");
    Check(Find(partial,CustomizationKind::Module,2).name=="有效名称","合法行被其他错误行阻止或重复编号覆盖");
    Check(Find(partial,CustomizationKind::Module,3).name==Name(CustomizationKind::Module,3,"cn"),"纯空白名称没有回退");
    for(const auto* language:{"en","ja","zh-CN"}) {
        InitLocale(home,language);const auto translatedHome=root/language;
        const auto catalog=LoadCustomizationCatalog(translatedHome,game,"cn",Source());
        Check(Read(translatedHome/"module_names.toml").find("[module]")!=std::string::npos,"翻译后的模板缺少可解析分组");
        Names(catalog,60);
    }
}
}

// 运行全部隔离名称回归；所有夹具只存在于本次创建的临时目录。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    const auto root=diva::fs::temp_directory_path()/("diva-customization-names-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Inheritance(root/"inheritance");Overrides(root/"overrides");Candidates(root/"candidates");
        UserNames(root/"user");InvalidUserNames(root/"invalid-user");
        diva::fs::remove_all(root);std::cout<<"定制名称继承与候选回归通过："<<checks<<" 项；未访问真实游戏。\n";return 0;
    } catch(const std::exception& error) {diva::fs::remove_all(root);std::cerr<<"定制名称回归失败："<<error.what()<<"\n";return 1;}
}
