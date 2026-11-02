// 发型与自定义道具数据领域回归：官方定制表注入、Mod 覆盖、名称合并、三领域分流及罗马音检索。
// 只读取仓库内人工合成 UTF-8 夹具并在系统临时目录构造隔离游戏目录，不访问真实游戏或 CPK。
#include "customization.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
int checks=0;
// 参数 condition/message 为回归断言和中文失败原因。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
// 参数 data/value 为归档目录缓冲区及待写入的大端整数。
void Big(std::string& data,uint32_t value) {for(int shift:{24,16,8,0}) data.push_back(char(value>>shift));}
// 参数 name/payload 为归档内条目名与UTF-8实体表文本；生成仅含一个条目的普通FArc。
std::string Farc(const std::string& name,const std::string& payload) {
    const auto directorySize=static_cast<uint32_t>(13+name.size());
    const auto offset=directorySize+8;
    std::string data="FArc";Big(data,directorySize);Big(data,16);data+=name;data.push_back('\0');
    Big(data,offset);Big(data,static_cast<uint32_t>(payload.size()));data+=payload;return data;
}
// 参数 path/content 为隔离夹具文件和内容。
void Write(const diva::fs::path& path,const std::string& content) {
    diva::fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);
    stream.write(content.data(),static_cast<std::streamsize>(content.size()));
    if(!stream) throw std::runtime_error("测试夹具写入失败");
}
// 参数 path 为仓库内合成夹具；按字节读取 UTF-8 文本。
std::string Read(const diva::fs::path& path) {
    std::ifstream stream(path,std::ios::binary);if(!stream) throw std::runtime_error("缺少合成夹具："+path.filename().string());
    std::ostringstream text;text<<stream.rdbuf();return text.str();
}
// 参数 catalog/kind/id 为快照和实体键；断言实体唯一存在。
const diva::CustomizationItem& Find(const diva::CustomizationCatalog& catalog,diva::CustomizationKind kind,int id) {
    const diva::CustomizationItem* found=nullptr;
    for(const auto& item:catalog.items) if(item.key.kind==kind && item.key.id==id) {if(found) throw std::runtime_error("同一实体键出现两份身份");found=&item;}
    if(!found) throw std::runtime_error("预期实体缺失："+std::to_string(id));
    return *found;
}
// 参数 catalog/kind/id 为快照和实体键，返回是否存在。
bool Has(const diva::CustomizationCatalog& catalog,diva::CustomizationKind kind,int id) {
    return std::any_of(catalog.items.begin(),catalog.items.end(),[&](const auto& item){return item.key.kind==kind && item.key.id==id;});
}
// 参数 catalog/query 为快照与条件，返回结果稳定ID集合，便于与预期比较。
std::set<int> Ids(const diva::CustomizationCatalog& catalog,const diva::CustomizationQuery& query) {
    std::set<int> ids;for(auto index:diva::SearchCustomization(catalog,query)) ids.insert(catalog.items[index].key.id);return ids;
}
// 参数 domain/text 为检索领域与文本；人物默认全部，不受旧默认 MIKU 影响。
diva::CustomizationQuery Query(diva::CustomizationDomain domain,const std::string& text={}) {
    diva::CustomizationQuery query;query.domain=domain;query.character.reset();query.text=text;return query;
}
// 参数 catalog/needle 为快照与诊断片段，返回是否存在包含该片段的诊断。
bool Diagnosed(const diva::CustomizationCatalog& catalog,const std::string& needle) {
    return std::any_of(catalog.diagnostics.begin(),catalog.diagnostics.end(),[&](const auto& line){return line.find(needle)!=std::string::npos;});
}
// 参数 fixtures 为夹具目录；返回模拟随包 CPK 桥接读取到的官方 PC 地区表，路径与生产判定一致。
diva::OfficialTableSource Official(const diva::fs::path& fixtures) {
    diva::OfficialTableSource source;
    const auto modules=Read(fixtures/"official_pc_region_gm_module_id.txt");
    const auto base=Read(fixtures/"official_pc_region_gm_customize_item_id.txt");
    const auto dlc=Read(fixtures/"official_pc_region_dlc_gm_customize_item_id.txt");
    // 参数为生产签名的游戏目录、插件目录与诊断接收器；夹具不读取它们。
    source.modules=[modules](const diva::fs::path&,const diva::fs::path&,std::vector<std::string>&) {
        return std::vector<diva::OfficialEntityTable>{{"diva_main_region.cpk","rom_steam_region/rom/gm_module_tbl.farc!/gm_module_id.bin","rom_steam_region/rom/gm_module_tbl.farc!/gm_module_id.bin",modules,"合成模型表指纹"}};
    };
    // 参数同上；另附一份非 PC 地区路径的同名表，验证不会被任意合并。
    source.customizeItems=[base,dlc](const diva::fs::path&,const diva::fs::path&,std::vector<std::string>&) {
        return std::vector<diva::OfficialEntityTable>{
            {"diva_main_region.cpk","rom_steam_region/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin","rom_steam_region/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin",base,"合成定制表指纹"},
            {"diva_dlc00_region.cpk","rom_steam_region_dlc/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin","rom_steam_region_dlc/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin",dlc,"合成DLC定制表指纹"},
            {"diva_main.cpk","rom_ps4/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin","rom_ps4/rom/gm_customize_item_tbl.farc!/gm_customize_item_id.bin","cstm_item.0.id=999\ncstm_item.0.parts=KAMI\ncstm_item.0.chara=MIKU\ncstm_item.data_list.length=1\n","其他平台"}};
    };
    return source;
}
// 参数 root/fixtures 为隔离目录和夹具目录；构造一个启用 Mod 的游戏目录，返回 game 路径。
diva::fs::path Game(const diva::fs::path& root,const diva::fs::path& fixtures,bool replace) {
    const auto game=root/"game",mods=game/"mods";
    Write(game/"DivaMegaMix.exe","不匹配的测试构建指纹");
    Write(game/"config.toml",replace?"enabled=true\npriority=['ItemMod','ReplaceMod']\n":"enabled=true\npriority=['ItemMod']\n");
    Write(mods/"ItemMod/config.toml","name='合成道具Mod'\ninclude=['.']\n");
    Write(mods/"ItemMod/rom/mod_gm_customize_item_tbl.farc",Farc("gm_customize_item_id.bin",Read(fixtures/"mod_append_gm_customize_item_id.txt")));
    Write(mods/"ItemMod/rom/lang2/mod_str_array.toml",Read(fixtures/"mod_str_array.toml"));
    if(replace) {
        Write(mods/"ReplaceMod/config.toml","name='合成替换Mod'\ninclude=['.']\n");
        Write(mods/"ReplaceMod/rom/gm_customize_item_tbl.farc",Farc("gm_customize_item_id.bin",Read(fixtures/"mod_replace_gm_customize_item_id.txt")));
    }
    return game;
}
// 参数 root/fixtures 为隔离目录和夹具；验证官方定制表合并、三领域分流、字段保真与联合检索。
void Domains(const diva::fs::path& root,const diva::fs::path& fixtures) {
    using namespace diva;
    const auto home=root/"home";Write(home/"customization_catalog.toml",Read(fixtures/"customization_catalog.toml"));
    const auto game=Game(root,fixtures,false);
    const auto catalog=LoadCustomizationCatalog(home,game,"cn",Official(fixtures));

    Check(ParseCustomizationPart("KAMI")==CustomizationPart::Hair && ParseCustomizationPart("HANE")==CustomizationPart::Unknown,"KAMI未解析为发型或未知令牌被猜测");
    Check(CustomizationDomainKind(CustomizationDomain::Hair)==CustomizationKind::Accessory && CustomizationDomainKind(CustomizationDomain::Accessories)==CustomizationKind::Accessory
        && CustomizationDomainKind(CustomizationDomain::Models)==CustomizationKind::Module,"发型或道具没有共用cstm_item实体种类");
    Check(CustomizationDomainIndex(CustomizationDomain::Models)==0 && CustomizationDomainIndex(CustomizationDomain::Hair)==1 && CustomizationDomainIndex(CustomizationDomain::Accessories)==2,"领域状态下标不稳定");

    // 三个领域严格分流：KAMI只进发型，四部位只进道具，未知部位两边都不进。
    Check(Ids(catalog,Query(CustomizationDomain::Hair))==std::set<int>{500,501,502,505,506,650,700,900,901},"发型领域混入道具或遗漏KAMI");
    Check(Ids(catalog,Query(CustomizationDomain::Accessories))==std::set<int>{316,600,610,620,630,641},"道具领域混入发型或未知部位");
    Check(Ids(catalog,Query(CustomizationDomain::Models))==std::set<int>{316,500},"模型领域混入定制实体");
    const auto& unknownPart=Find(catalog,CustomizationKind::Accessory,640);
    Check(unknownPart.part==CustomizationPart::Unknown && unknownPart.partToken=="HANE" && !CustomizationItemDomain(unknownPart),"未知部位被自动当成发型或头部");
    Check(Diagnosed(catalog,"未知部位"),"未知部位没有诊断");
    Check(!Has(catalog,CustomizationKind::Accessory,999),"非PC地区路径的官方表被任意合并");

    // 不同种类相同数字ID各自独立；表内序号、稳定ID、obj_id与bind_module分别保留。
    const auto& moduleFive=Find(catalog,CustomizationKind::Module,500);
    const auto& hairFive=Find(catalog,CustomizationKind::Accessory,500);
    Check(moduleFive.name=="模型五百中文" && hairFive.name=="合成初音发型" && hairFive.part==CustomizationPart::Hair,"模型与发型同号身份或名称串用");
    Check(hairFive.objectId==500 && hairFive.bindModule.has_value() && *hairFive.bindModule==0 && hairFive.sortIndex==100,"bind_module原值零或obj_id丢失");
    const auto& rin=Find(catalog,CustomizationKind::Accessory,501);
    Check(rin.key.id==501 && rin.objectId==9501 && rin.bindModule==12,"稳定ID、对象ID与绑定值被混淆");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"11")).empty(),"表内序号被当作稳定ID检索");
    Check(Ids(catalog,Query(CustomizationDomain::Accessories,"316"))==std::set<int>{316} && Ids(catalog,Query(CustomizationDomain::Models,"316"))==std::set<int>{316}
        && Find(catalog,CustomizationKind::Module,316).name!=Find(catalog,CustomizationKind::Accessory,316).name,"同号模型与道具检索串领域");

    // 官方基础/DLC 按稳定ID整条覆盖并保留贡献；Mod 追加表覆盖官方条目。
    Check(rin.name=="合成リンヘアDLC" && rin.definitionSource=="official:pc-region-dlc-customize","DLC定制表未覆盖基础表");
    bool baseRetained=false,dlcEffective=false;
    for(const auto& contribution:rin.sources) {
        if(contribution.source=="official:pc-region-customize") baseRetained=!contribution.effective;
        if(contribution.source=="official:pc-region-dlc-customize") dlcEffective=contribution.effective;
    }
    Check(baseRetained && dlcEffective,"官方基础/DLC定制表来源贡献或有效状态错误");
    const auto& glasses=Find(catalog,CustomizationKind::Accessory,610);
    Check(glasses.character==CustomizationCharacter::Rin && glasses.name=="Mod眼镜" && glasses.definitionSource=="dml:ItemMod:.","Mod追加定制表未整条覆盖官方道具");
    Check(std::any_of(glasses.sources.begin(),glasses.sources.end(),[](const auto& c){return c.source=="official:pc-region-customize" && !c.effective;}),"被覆盖的官方道具贡献丢失");
    Check(Find(catalog,CustomizationKind::Accessory,650).name=="合成重复新" && Find(catalog,CustomizationKind::Accessory,650).rawFields.at("future_field")=="raw"
        && Diagnosed(catalog,"同文件ID重复"),"同文件重复ID未采用较大序号或未诊断");
    Check(Find(catalog,CustomizationKind::Accessory,505).name=="合成テトヘア" && Find(catalog,CustomizationKind::Accessory,505).metadataState=="官方PC地区定制表","官方定制表元数据状态错误");
    Check(std::any_of(catalog.sources.begin(),catalog.sources.end(),[](const auto& s){return s.id=="official:pc-region-customize";})
        && std::any_of(catalog.sources.begin(),catalog.sources.end(),[](const auto& s){return s.id=="official:pc-region-dlc-customize";}),"官方定制表来源未登记");

    // 名称：customize 优先、cstm_item 兼容、孤立名称不创建实体、其他语言仅作别名。
    Check(Find(catalog,CustomizationKind::Accessory,600).name=="通用护士帽" && Find(catalog,CustomizationKind::Accessory,620).name=="兼容名缎带","customize/cstm_item名称合并错误");
    Check(catalog.orphanNames==1 && !Has(catalog,CustomizationKind::Accessory,9999),"孤立定制名称创建了实体");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"Twin Tail"))==std::set<int>{502},"其他语言真实名称未参与发型检索");

    // 人物：发型按原始人物；道具共通ALL匹配任一具体人物；未知人物只在未知筛选出现。
    auto hair=Query(CustomizationDomain::Hair);hair.character=CustomizationCharacter::Miku;
    Check(Ids(catalog,hair)==std::set<int>{500,650,900,901},"发型人物筛选错误");
    hair.character=CustomizationCharacter::Teto;Check(Ids(catalog,hair)==std::set<int>{505,506},"发型衍生人物被模型分类合并或遗漏");
    hair.character=CustomizationCharacter::Derived;Check(Ids(catalog,hair).empty(),"发型错误套用模型专用衍生分类");
    auto items=Query(CustomizationDomain::Accessories);items.character=CustomizationCharacter::Rin;
    Check(Ids(catalog,items)==std::set<int>{600,610},"共通道具未匹配具体人物或Mod人物覆盖失效");
    items.character=CustomizationCharacter::Unknown;Check(Ids(catalog,items)==std::set<int>{641},"未知人物道具筛选错误");

    // 部位只属于道具领域；四个部位独立。
    items=Query(CustomizationDomain::Accessories);
    items.part=CustomizationPart::Head;Check(Ids(catalog,items)==std::set<int>{316,600,641},"头部筛选错误");
    items.part=CustomizationPart::Face;Check(Ids(catalog,items)==std::set<int>{610},"脸部筛选错误");
    items.part=CustomizationPart::Neck;Check(Ids(catalog,items)==std::set<int>{620},"胸口筛选错误");
    items.part=CustomizationPart::Back;Check(Ids(catalog,items)==std::set<int>{630},"背部筛选错误");
    hair.character.reset();hair.part=CustomizationPart::Head;Check(Ids(catalog,hair).size()==9,"发型领域被道具部位条件影响");

    // 罗马音：只转写真实假名或显式读音，汉字原样保留，不捏造读音。
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"tsuinteeru"))==std::set<int>{502} && Ids(catalog,Query(CustomizationDomain::Hair,"tuinteeru"))==std::set<int>{502},"片假名名称缺少罗马音或拼写变体");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"mikuhea"))==std::set<int>{500},"原表假名名称别名缺少罗马音");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"kanjinomi"))==std::set<int>{900},"显式假名读音没有罗马音检索键");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"hatsune")).empty() && Ids(catalog,Query(CustomizationDomain::Hair,"junkanji")).empty(),"为汉字名称捏造了读音");
    Check(Ids(catalog,Query(CustomizationDomain::Hair,"ﾂｲﾝﾃｰﾙ"))==std::set<int>{502} && Ids(catalog,Query(CustomizationDomain::Hair,"ついんてーる"))==std::set<int>{502},"半角或平假名未规范化");

    // 联合条件：领域、人物、来源与多词同时生效。
    auto combined=Query(CustomizationDomain::Hair,"合成 DLC");combined.character=CustomizationCharacter::Rin;
    Check(Ids(catalog,combined)==std::set<int>{501},"多词与人物联合检索错误");
    combined.character=CustomizationCharacter::Teto;Check(Ids(catalog,combined)==std::set<int>{506},"多词跨项目组合或人物条件失效");
    auto modSource=Query(CustomizationDomain::Hair);modSource.source="dml:ItemMod:.";
    const auto fromMod=Ids(catalog,modSource);
    Check(fromMod.contains(700) && fromMod.contains(500) && !fromMod.contains(501),"来源过滤未包含Mod定义与名称贡献");
    auto officialItems=Query(CustomizationDomain::Accessories);officialItems.source="official";
    Check(Ids(catalog,officialItems)==std::set<int>{316,600,610,620,630,641},"官方来源入口未匹配被Mod覆盖但保留官方贡献的道具");
    auto ordered=Query(CustomizationDomain::Accessories);
    const auto byDefault=SearchCustomization(catalog,ordered);
    Check(!byDefault.empty() && catalog.items[byDefault.front()].key.id==600,"道具默认排序未按sort_index");
}
// 参数 root/fixtures 为隔离目录和夹具；启用Mod提供普通定制表时官方定制表整表退出，模型表不受影响。
void Replacement(const diva::fs::path& root,const diva::fs::path& fixtures) {
    using namespace diva;
    const auto game=Game(root,fixtures,true);
    const auto catalog=LoadCustomizationCatalog(root/"home",game,"cn",Official(fixtures));
    Check(!Has(catalog,CustomizationKind::Accessory,500) && !Has(catalog,CustomizationKind::Accessory,600) && Has(catalog,CustomizationKind::Accessory,800),"普通定制表替换后官方定制实体残留");
    Check(Has(catalog,CustomizationKind::Accessory,700) && Has(catalog,CustomizationKind::Accessory,610) && Find(catalog,CustomizationKind::Accessory,610).name=="Mod眼镜","追加定制表未继续按稳定ID生效");
    Check(Has(catalog,CustomizationKind::Module,500) && Has(catalog,CustomizationKind::Module,316),"定制表替换误删官方模型表");
    Check(Diagnosed(catalog,"官方同逻辑定制表整表退出"),"官方定制表退出没有诊断");
}
}
// 参数 argc/argv 为测试入口；argv[1] 为仓库根目录，用于读取 tests/fixtures/customization。
int main(int argc,char** argv) {
    const diva::fs::path repository=argc>1?diva::fs::path(argv[1]):diva::fs::current_path();
    const auto fixtures=repository/"tests/fixtures/customization";
    const auto root=diva::fs::temp_directory_path()/("diva-customization-domain-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Domains(root/"domains",fixtures);Replacement(root/"replacement",fixtures);diva::fs::remove_all(root);
        std::cout<<"发型与自定义道具数据领域回归通过："<<checks<<" 项；只使用合成夹具，未读取真实游戏、CPK或实机列表。\n";return 0;
    } catch(const std::exception& error) {diva::fs::remove_all(root);std::cerr<<"发型／道具数据回归失败："<<error.what()<<"\n";return 1;}
}
