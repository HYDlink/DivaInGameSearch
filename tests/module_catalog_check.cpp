#include "customization.h"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
// 参数 condition/message 为回归断言和中文失败原因。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 参数 data/value 为归档目录缓冲区及待写入的大端整数。
void Big(std::string& data,uint32_t value) {for(int shift:{24,16,8,0}) data.push_back(char(value>>shift));}
// 参数 payload 为UTF-8实体表文本；生成包含指定实体表的普通FArc文件。
std::string Farc(const std::string& payload) {
    const std::string name="gm_module_id.bin";
    const auto directorySize=static_cast<uint32_t>(13+name.size());
    const auto offset=directorySize+8;
    std::string data="FArc";Big(data,directorySize);Big(data,16);data+=name;data.push_back('\0');
    Big(data,offset);Big(data,static_cast<uint32_t>(payload.size()));data+=payload;return data;
}
// 参数 path/content 为隔离夹具文件和UTF-8或二进制内容。
void Write(const diva::fs::path& path,const std::string& content) {
    diva::fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);
    stream.write(content.data(),static_cast<std::streamsize>(content.size()));
    if(!stream) throw std::runtime_error("测试夹具写入失败");
}
// 参数 catalog/kind/id 为结果快照和实体键，检查该键是否存在。
bool Has(const diva::CustomizationCatalog& catalog,diva::CustomizationKind kind,int id) {
    for(const auto& item:catalog.items) if(item.key.kind==kind && item.key.id==id) return true;
    return false;
}
// 参数 catalog/id 为自动目录结果和服装ID，要求测试实体唯一存在。
const diva::CustomizationItem& Find(const diva::CustomizationCatalog& catalog,int id) {
    for(const auto& item:catalog.items) if(item.key.kind==diva::CustomizationKind::Module && item.key.id==id) return item;
    throw std::runtime_error("预期的自动服装实体缺失");
}
// 参数 root 为临时隔离目录；端到端验证DML目录、归档读取、覆盖和搜索分类。
void AutomaticCatalog(const diva::fs::path& root) {
    using namespace diva;
    const auto home=root/"home",game=root/"game",mods=game/"mods";
    Write(game/"DivaMegaMix.exe","不匹配的测试构建指纹");
    Write(game/"config.toml","enabled=true\nmods='mods'\npriority=['High','Disabled','LowBase','Low','X Song Pack']\n");
    Write(mods/"High/config.toml","name='重复显示名'\ninclude=['content']\n");
    Write(mods/"LowBase/config.toml","name='重复显示名'\ninclude=['.']\n");
    Write(mods/"Low/config.toml","name='重复显示名'\ninclude=['.']\n");
    Write(mods/"Disabled/config.toml","enabled=false\ninclude=['.']\n");
    Write(mods/"Unlisted/config.toml","include=['.']\n");
    Write(mods/"X Song Pack/config.toml","include=['.']\n");

    Write(mods/"High/content/rom/gm_module_tbl.farc",Farc(
        "module.0.id=0\nmodule.0.name=普通表零号\nmodule.0.chara=MIKU\n"
        "module.1.id=4\nmodule.1.name=高优先普通表\nmodule.1.chara=MIKU\nmodule.data_list.length=2\n"));
    Write(mods/"High/content/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=1\nmodule.0.name=高优先追加定义\nmodule.0.chara=TETO\nmodule.0.cos=COS_HIGH\n"
        "module.1.id=3\nmodule.1.name=高优先新增\nmodule.1.chara=MIKU\nmodule.data_list.length=2\n"));
    Write(mods/"LowBase/rom/gm_module_tbl.farc",Farc(
        "module.0.id=0\nmodule.0.name=低优先普通表\nmodule.0.chara=LEN\n"
        "module.1.id=5\nmodule.1.name=不应累加\nmodule.1.chara=MIKU\nmodule.data_list.length=2\n"));
    Write(mods/"Low/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=0\nmodule.0.name=低优先追加覆盖\nmodule.0.chara=RIN\n"
        "module.1.id=1\nmodule.1.name=低优先追加定义\nmodule.1.chara=SAKINE\nmodule.1.cos=COS_LOW\n"
        "module.2.id=2\nmodule.2.name=咲音服装\nmodule.2.chara=SAKINE\nmodule.data_list.length=3\n"));
    Write(mods/"Disabled/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=88\nmodule.0.name=已禁用\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n"));
    Write(mods/"Unlisted/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=89\nmodule.0.name=未列入优先级\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n"));
    Write(mods/"X Song Pack/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=997\nmodule.0.name=\nmodule.0.chara=MIKU\nmodule.0.cos=COS_482\nmodule.data_list.length=1\n"));
    Write(mods/"High/content/rom/lang2/mod_str_array.toml",
        "cn.module.0='高优先级中文名'\ncn.module.1='高追加定义中文名'\nmodule.999='孤立补丁'\n");
    Write(mods/"Low/rom/lang2/mod_str_array.toml",
        "cn.module.0='低优先级中文名'\nen.module.0='English Alias'\n");
    Write(mods/"X Song Pack/rom/lang2/mod_str_array.toml","1272='未授权旧式名称'\n");

    // 同一高优先级追加归档也存在旧式解包登记；自动归档应取代这份重复表。
    Write(home/"dump/high/gm_module_id.bin",
        "module.0.id=1\nmodule.0.name=旧解包内容\nmodule.0.chara=MEIKO\n"
        "module.1.id=50\nmodule.1.name=重复导入幽灵\nmodule.1.chara=MIKU\nmodule.data_list.length=2\n");
    Write(home/"customization_catalog.toml",
        "schema_version=1\nlanguage='cn'\n"
        "[[sources]]\nid='high'\nname='High'\nmod_directory='High'\ninclude_root='content'\n"
        "[[tables]]\nsource='high'\nkind='module'\nmode='append'\n"
        "logical_path='rom/mod_gm_module_tbl.farc'\npath='dump/high/gm_module_id.bin'\n");

    const auto catalog=LoadCustomizationCatalog(home,game);
    Check(catalog.items.size()==6,"自动扫描或去重后实体数量不正确");
    Check(catalog.unimportedArchives==0,"有效FArc归档被报告读取失败");
    Check(catalog.orphanNames==1 && !Has(catalog,CustomizationKind::Module,999),"孤立名称补丁创建了实体或计数不正确");
    Check(!Has(catalog,CustomizationKind::Module,5) && !Has(catalog,CustomizationKind::Module,50),"普通低优先表或重复手工登记被误当作新实体");
    Check(!Has(catalog,CustomizationKind::Module,88) && !Has(catalog,CustomizationKind::Module,89),"禁用或未列入DML优先级的Mod被扫描");

    const auto& zero=Find(catalog,0);
    Check(zero.character==CustomizationCharacter::Rin && zero.characterToken=="RIN" && zero.cos.empty(),"追加表没有覆盖普通表或未保存原始角色");
    Check(zero.name=="高优先级中文名" && zero.nameSource=="dml:High:content","DML优先级名称补丁选择错误");
    const auto& one=Find(catalog,1);
    Check(one.definitionSource=="dml:High:content" && one.character==CustomizationCharacter::Teto && one.characterToken=="TETO" && one.cos=="COS_HIGH","高优先追加表未整条覆盖低优先定义");
    Check(one.name=="高追加定义中文名","自动实体名称补丁未映射到实体ID");
    size_t highDefinitions=0,lowDefinitions=0;
    for(const auto& contribution:one.sources) if(contribution.role=="定义") {
        if(contribution.source=="dml:High:content") ++highDefinitions;
        if(contribution.source=="dml:Low:.") ++lowDefinitions;
    }
    Check(highDefinitions==1 && lowDefinitions==1,"跨Mod追加覆盖未保留唯一的胜出及被覆盖来源贡献");

    Check(catalog.sources.size()==4 && catalog.sources[0].id!=catalog.sources[1].id && catalog.sources[0].name==catalog.sources[1].name,
        "同名Mod来源没有按目录身份独立保存");
    CustomizationQuery query;query.character.reset();query.source="dml:Low:.";
    auto lowResults=SearchCustomization(catalog,query);Check(lowResults.size()==3,"被覆盖追加定义未保留来源过滤关联");
    query.source="dml:LowBase:.";Check(SearchCustomization(catalog,query).empty(),"低优先普通替换表错误保留为有效来源贡献");
    query={};query.character.reset();query.text="English Alias";
    auto aliasResults=SearchCustomization(catalog,query);
    Check(aliasResults.size()==1 && catalog.items[aliasResults[0]].key.id==0,"其他语言名称别名未与既有ID关联");

    query={};query.character=CustomizationCharacter::Derived;auto derived=SearchCustomization(catalog,query);
    Check(derived.size()==2,"衍生角色筛选未将TETO和SAKINE归入游戏分类");
    Check(Find(catalog,2).characterToken=="SAKINE" && Find(catalog,2).character==CustomizationCharacter::Sakine,
        "搜索分类覆盖了底层原始角色信息");
    query.character=CustomizationCharacter::Teto;Check(SearchCustomization(catalog,query).empty(),"衍生角色仍作为独立游戏分类出现");
    query.character=CustomizationCharacter::Random;Check(SearchCustomization(catalog,query).empty(),"随机角色被展开为静态实体或全部服装");

    query={};query.character.reset();query.text="未授权旧式名称";
    Check(SearchCustomization(catalog,query).empty(),"未匹配已核实SHA的构建错误应用1272到997名称映射");
    query.text="997";auto legacyId=SearchCustomization(catalog,query);
    Check(legacyId.size()==1 && catalog.items[legacyId[0]].key.id==997,"X高ID实体自身的编号检索失败");
}
// 参数 root 为独立隔离目录；验证自动与兼容表混合时追加覆盖及普通表整表优先级。
void MixedPriorityCatalog(const diva::fs::path& root) {
    using namespace diva;
    const auto home=root/"home",game=root/"game",mods=game/"mods";
    Write(game/"config.toml","enabled=true\npriority=['High','Low']\n");
    Write(mods/"High/config.toml","name='High'\ninclude=['.']\n");
    Write(mods/"Low/config.toml","name='Low'\ninclude=['.']\n");

    Write(mods/"High/rom/gm_module_tbl.farc",Farc(
        "module.0.id=0\nmodule.0.name=高优先自动普通表\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n"));
    Write(mods/"Low/rom/mod_gm_module_tbl.farc",Farc(
        "module.0.id=1\nmodule.0.name=低优先自动追加\nmodule.0.chara=RIN\nmodule.data_list.length=1\n"));
    Write(mods/"Low/rom_steam/gm_module_tbl.farc",Farc(
        "module.0.id=5\nmodule.0.name=低优先自动Steam普通表\nmodule.0.chara=LUKA\nmodule.data_list.length=1\n"));

    Write(home/"dump/high_append/gm_module_id.bin",
        "module.0.id=1\nmodule.0.name=高优先兼容追加\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n");
    Write(home/"dump/low_replace/gm_module_id.bin",
        "module.0.id=5\nmodule.0.name=低优先兼容普通替换\nmodule.0.chara=RIN\nmodule.data_list.length=1\n");
    Write(home/"dump/high_replace_steam/gm_module_id.bin",
        "module.0.id=6\nmodule.0.name=高优先兼容Steam普通表\nmodule.0.chara=MIKU\nmodule.data_list.length=1\n");
    Write(home/"customization_catalog.toml",R"(schema_version=1
language='cn'
[[sources]]
id='high'
mod_directory='High'
include_root='.'
[[sources]]
id='low'
mod_directory='Low'
include_root='.'
[[tables]]
source='high'
kind='module'
mode='append'
logical_path='rom/mod_gm_module_tbl.farc'
path='dump/high_append/gm_module_id.bin'
[[tables]]
source='low'
kind='module'
mode='replace'
logical_path='rom/gm_module_tbl.farc'
path='dump/low_replace/gm_module_id.bin'
[[tables]]
source='high'
kind='module'
mode='replace'
logical_path='rom_steam/gm_module_tbl.farc'
path='dump/high_replace_steam/gm_module_id.bin'
)");

    const auto catalog=LoadCustomizationCatalog(home,game);
    Check(catalog.items.size()==3,"混合自动／兼容表优先级留下了被替换实体");
    const auto& appended=Find(catalog,1);
    Check(appended.character==CustomizationCharacter::Miku && appended.characterToken=="MIKU"
        && appended.definitionSource=="dml:High:." && appended.name=="高优先兼容追加",
        "高优先兼容追加没有覆盖低优先自动追加定义");
    size_t highDefinitions=0,lowDefinitions=0;bool highEffective=false,lowEffective=true;
    for(const auto& contribution:appended.sources) if(contribution.role=="定义") {
        if(contribution.source=="dml:High:.") {++highDefinitions;highEffective=contribution.effective;}
        if(contribution.source=="dml:Low:.") {++lowDefinitions;lowEffective=contribution.effective;}
    }
    Check(highDefinitions==1 && lowDefinitions==1 && highEffective && !lowEffective,
        "混合追加覆盖未保留高低来源贡献或有效状态错误");

    const auto& automaticReplace=Find(catalog,0);
    Check(automaticReplace.name=="高优先自动普通表" && automaticReplace.definitionSource=="dml:High:.",
        "高优先自动普通替换未胜过低优先兼容普通替换");
    Check(!Has(catalog,CustomizationKind::Module,5),
        "低优先兼容普通替换实体泄漏到高优先自动普通替换结果");

    const auto& compatibilityReplace=Find(catalog,6);
    Check(compatibilityReplace.name=="高优先兼容Steam普通表" && compatibilityReplace.definitionSource=="dml:High:.",
        "高优先兼容普通替换未胜过低优先自动普通替换");
    Check(!Has(catalog,CustomizationKind::Module,5),
        "低优先自动普通替换实体泄漏到高优先兼容普通替换结果");
}
}

// 参数 argc/argv 保留测试运行入口兼容性；测试目录只在系统临时路径创建。
int main(int argc,char** argv) {
    (void)argc;(void)argv;
    const auto root=diva::fs::temp_directory_path()/
        ("diva-module-catalog-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {AutomaticCatalog(root/"automatic");MixedPriorityCatalog(root/"mixed");diva::fs::remove_all(root);std::cout<<"服装自动目录及混合优先级回归通过；未访问或修改真实游戏目录。\n";return 0;}
    catch(const std::exception& error) {diva::fs::remove_all(root);std::cerr<<error.what()<<"\n";return 1;}
}
