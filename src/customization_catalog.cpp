#include "customization.h"
#include <toml.hpp>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <regex>
#include <sstream>
#include <climits>

namespace diva {
namespace {
// 参数 path 为文件系统路径，转换为可保存的UTF-8，不使用本机ANSI代码页。
std::string PathText(const fs::path& path) {auto text=path.generic_u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());}
// 参数 text 为UTF-8路径字符串，保持中文目录可用。
fs::path NativePath(const std::string& text) {return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));}
// 参数 text 为DML配置快照；返回稳定本地诊断指纹，不作为安全哈希。
std::string Hash(const std::string& text) {uint64_t value=14695981039346656037ull;for(unsigned char byte:text) {value^=byte;value*=1099511628211ull;}std::ostringstream stream;stream<<std::hex<<value;return stream.str();}

// 参数 text 为UTF-8文本；清理空白和文件起始BOM。
std::string Trim(std::string text) {
    if(text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);
    auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    return first==std::string::npos?"":text.substr(first,last-first+1);
}
// 参数 text 为完整十进制整数，拒绝截断、溢出与尾随垃圾。
std::optional<int> Integer(const std::string& text) {
    int number=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),number);
    if(error!=std::errc() || end!=text.data()+text.size()) return {};return number;
}
// 参数 root/relative 为受限目录和相对路径，禁止绝对路径与父目录逃逸。
std::optional<fs::path> Inside(const fs::path& root,const std::string& relative) {
    fs::path path=NativePath(relative);if(path.empty() || path.is_absolute() || path.has_root_name()) return {};
    for(const auto& part:path) if(part=="..") return {};
    std::error_code error;auto base=fs::weakly_canonical(root,error);if(error) return {};
    auto target=fs::weakly_canonical(root/path,error);if(error) return {};
    auto b=base.begin(),t=target.begin();for(;b!=base.end();++b,++t) if(t==target.end() || *b!=*t) return {};
    return target;
}
// 参数 path/out 为输入文件和目录快照；指纹仅用于本地变动诊断。
void Fingerprint(const fs::path& path,CustomizationCatalog& out) {
    std::error_code error; if(!fs::is_regular_file(path,error)) return;
    auto size=fs::file_size(path,error);auto time=fs::last_write_time(path,error).time_since_epoch().count();
    out.configurationKey+=PathText(path)+":"+std::to_string(size)+":"+std::to_string(time)+"\n";
}
// 参数 path/out 为配置和诊断目录；解析失败保留中文诊断。
toml::table Config(const fs::path& path,CustomizationCatalog& out) {
    Fingerprint(path,out);std::error_code error;if(!fs::is_regular_file(path,error)) return {};
    try { std::ifstream stream(path,std::ios::binary);return toml::parse(stream,PathText(path)); }
    catch(const std::exception&) {out.diagnostics.push_back("TOML解析失败："+PathText(path.filename()));return {};}
}
// 参数 fields/key 为原始文本字段和键。
std::string Get(const std::map<std::string,std::string>& fields,const std::string& key) {auto it=fields.find(key);return it==fields.end()?"":it->second;}
// 参数 fields/kind/source 为完整实体字段、种类和来源；缺失ID或负数不创建实体。
std::optional<CustomizationItem> Item(const std::map<std::string,std::string>& fields,CustomizationKind kind,const std::string& source) {
    auto id=Integer(Get(fields,"id"));if(!id || *id<0) return {};
    CustomizationItem item;item.key={kind,*id};item.rawFields=fields;item.name=Get(fields,"name");item.characterToken=Get(fields,"chara");item.partToken=Get(fields,"parts");
    item.character=ParseCustomizationCharacter(item.characterToken);
    if(kind==CustomizationKind::Module && item.character==CustomizationCharacter::All) item.character=CustomizationCharacter::Unknown;
    item.part=ParseCustomizationPart(item.partToken);
    item.cos=Get(fields,"cos");item.sortIndex=Integer(Get(fields,"sort_index"));item.objectId=Integer(Get(fields,"obj_id"));item.bindModule=Integer(Get(fields,"bind_module"));
    if(!item.name.empty()) item.aliases.push_back(item.name);
    item.definitionSource=source;item.nameSource=source;item.sources.push_back({source,"定义",true});return item;
}
struct Root {fs::path path;std::string id,name,modDirectory,includeRoot;};
// 参数 game/out 为游戏目录和快照；只扫描DML明确启用的Mod和include。
std::vector<Root> Roots(const fs::path& game,CustomizationCatalog& out) {
    std::vector<Root> roots;auto dml=Config(game/"config.toml",out);if(!dml["enabled"].value_or(true)) return roots;
    auto mods=Inside(game,dml["mods"].value_or<std::string>("mods"));if(!mods) return roots;
    std::error_code error;if(!fs::is_directory(*mods,error)) return roots;
    std::vector<fs::path> paths;
    if(auto priorities=dml["priority"].as_array();priorities && !priorities->empty()) {
        for(auto& entry:*priorities) if(auto path=Inside(*mods,entry.value_or<std::string>(""));path) paths.push_back(*path);
    } else {
        for(fs::directory_iterator it(*mods,error),end;it!=end && !error;it.increment(error)) if(it->is_directory(error)) paths.push_back(it->path());
        std::sort(paths.begin(),paths.end());out.diagnostics.push_back("DML未列出priority，采用目录字母顺序；运行版本顺序待核对。");
    }
    std::set<std::string> seen;
    for(const auto& path:paths) {
        if(!fs::is_regular_file(path/"config.toml",error)) continue;
        auto cfg=Config(path/"config.toml",out);if(!cfg["enabled"].value_or(true)) continue;
        if(auto includes=cfg["include"].as_array()) for(auto& value:*includes) {
            auto relative=value.value_or<std::string>("");auto include=Inside(path,relative);if(!include || !fs::is_directory(*include,error)) continue;
            auto directory=PathText(path.filename());auto id="dml:"+directory+":"+PathText(NativePath(relative).lexically_normal());
            if(seen.insert(id).second) roots.push_back({*include,id,cfg["name"].value_or(directory),directory,PathText(NativePath(relative).lexically_normal())});
        }
    }
    return roots;
}
// 参数 dest/item 为合并目标和新整条定义；保留被覆盖贡献来源。
void Overlay(std::map<CustomizationKey,CustomizationItem>& dest,CustomizationItem item) {
    auto found=dest.find(item.key);
    if(found!=dest.end()) for(auto contribution:found->second.sources) {
        contribution.effective=false;
        if(std::none_of(item.sources.begin(),item.sources.end(),[&](const auto& candidate){return candidate.source==contribution.source && candidate.role==contribution.role;})) item.sources.push_back(std::move(contribution));
    }
    dest.insert_or_assign(item.key,std::move(item));
}
}
std::vector<CustomizationItem> ReadCustomizationTable(const fs::path& path,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics) {
    std::ifstream stream(path,std::ios::binary);std::string line;std::map<int,std::map<std::string,std::string>> records;
    const std::string prefix=kind==CustomizationKind::Module?"module.":"cstm_item.";std::optional<int> length;size_t lines=0;
    while(std::getline(stream,line)) {
        if(++lines>2000000 || line.size()>65536) {diagnostics.push_back("实体表超出安全读取上限");break;}
        line=Trim(line);if(line.empty() || line[0]=='#') continue;auto equals=line.find('=');if(equals==std::string::npos) continue;
        auto key=Trim(line.substr(0,equals)),value=Trim(line.substr(equals+1));
        if(key==prefix+"data_list.length") {length=Integer(value);if(!length || *length<0 || *length>100000) {diagnostics.push_back("实体表声明长度非法，拒绝导入");return {};}continue;}
        if(!key.starts_with(prefix)) continue;auto dot=key.find('.',prefix.size());if(dot==std::string::npos) continue;
        auto index=Integer(key.substr(prefix.size(),dot-prefix.size()));if(!index || *index<0 || *index>=100000) continue;
        auto field=key.substr(dot+1);if(records[*index].contains(field)) diagnostics.push_back("实体表字段重复，采用最后值："+key);
        records[*index][field]=value;
    }
    if(!stream.eof() && stream.fail()) diagnostics.push_back("实体表读取失败或中断："+PathText(path.filename()));
    if(length && records.size()!=size_t(*length)) diagnostics.push_back("实体表稀疏或声明长度不符");
    std::map<int,CustomizationItem> unique;
    for(const auto& [index,fields]:records) {
        if(length && index>=*length) {diagnostics.push_back("实体表序号超出声明长度，跳过");continue;}
        auto item=Item(fields,kind,source);if(!item) {diagnostics.push_back("实体表缺少合法非负ID，跳过记录");continue;}
        if(item->character==CustomizationCharacter::Unknown) diagnostics.push_back("实体表保留未知角色："+item->characterToken);
        if(kind==CustomizationKind::Accessory && item->part==CustomizationPart::Unknown) diagnostics.push_back("实体表保留未知部位："+item->partToken);
        if(unique.contains(item->key.id)) diagnostics.push_back("同文件ID重复，采用较大表内序号；不提供原生定位");
        unique.insert_or_assign(item->key.id,std::move(*item));
    }
    std::vector<CustomizationItem> out;for(auto& [id,item]:unique) { (void)id;out.push_back(std::move(item));}return out;
}
CustomizationCatalog LoadCustomizationCatalog(const fs::path& home,const fs::path& game) {
    CustomizationCatalog out;auto roots=Roots(game,out);out.dmlFingerprint=Hash(out.configurationKey);
    for(const auto& root:roots) out.sources.push_back({root.id,root.name,root.modDirectory,root.includeRoot,true});
    auto manifest=Config(home/"customization_catalog.toml",out);out.language=manifest["language"].value_or<std::string>("cn");
    bool valid=manifest["schema_version"].value_or<int64_t>(0)==1;
    auto expectedFingerprint=manifest["dml_fingerprint"].value_or<std::string>("");
    if(!expectedFingerprint.empty() && expectedFingerprint!=out.dmlFingerprint) {out.stale=true;out.diagnostics.push_back("导入清单DML指纹不匹配，来源配置已变化；目录标为过期。");}
    out.diagnostics.push_back("当前DML配置指纹："+out.dmlFingerprint);
    if(!manifest.empty() && !valid) out.diagnostics.push_back("不支持的服装导入清单版本；仅接受schema_version=1。");
    std::map<std::string,std::string> sourceIds;std::map<std::string,int> ranks;
    for(size_t index=0;index<roots.size();++index) ranks[roots[index].id]=static_cast<int>(index);
    std::set<std::string> inactiveModSources;
    if(valid) if(auto sources=manifest["sources"].as_array()) for(auto& node:*sources) if(auto source=node.as_table()) {
        auto id=(*source)["id"].value_or<std::string>("");if(id.empty() || sourceIds.contains(id)) {out.diagnostics.push_back("导入来源ID为空或重复，跳过");continue;}
        auto directory=(*source)["mod_directory"].value_or<std::string>(""),include=(*source)["include_root"].value_or<std::string>(".");
        auto active=std::find_if(roots.begin(),roots.end(),[&](const auto& root){return root.modDirectory==directory && root.includeRoot==PathText(NativePath(include).lexically_normal());});
        if(active!=roots.end()) sourceIds[id]=active->id;
        else {auto identity="import:"+id;sourceIds[id]=identity;ranks[identity]=INT_MAX;out.sources.push_back({identity,(*source)["name"].value_or(id),directory,include,false});
            if(!directory.empty()) {inactiveModSources.insert(id);out.diagnostics.push_back("导入来源当前未匹配启用的Mod/include，标为离线导入："+id);}
        }
    }
    std::map<CustomizationKey,CustomizationItem> merged;
    // 最终清单是用户导入目录，不能通过自填优先级宣称游戏生效集合。
    if(valid) if(auto items=manifest["items"].as_array()) for(auto& node:*items) if(auto table=node.as_table()) {
        auto source=(*table)["source"].value_or<std::string>("");if(!sourceIds.contains(source)) {out.diagnostics.push_back("项目引用未登记来源，跳过");continue;}
        auto kind=(*table)["kind"].value_or<std::string>("");if(kind!="module" && kind!="accessory") {out.diagnostics.push_back("未知项目kind，跳过");continue;}
        std::map<std::string,std::string> fields;
        for(const auto& [key,value]:*table) {if(auto text=value.value<std::string>()) fields[std::string(key.str())]=*text;else if(auto number=value.value<int64_t>()) fields[std::string(key.str())]=std::to_string(*number);}
        auto item=Item(fields,kind=="module"?CustomizationKind::Module:CustomizationKind::Accessory,sourceIds[source]);if(!item) {out.diagnostics.push_back("清单项目ID非法，跳过");continue;}
        if(auto aliases=(*table)["aliases"].as_array()) for(auto& alias:*aliases) if(auto text=alias.value<std::string>()) item->aliases.push_back(*text);
        if(auto names=(*table)["names"].as_table()) for(const auto& [language,value]:*names) if(auto name=value.value<std::string>()) item->namesByLanguage[std::string(language.str())]=*name;
        if(merged.contains(item->key)) out.diagnostics.push_back("最终导入清单出现重复ID，采用最后整条记录");
        Overlay(merged,std::move(*item));
    }
    struct Table {fs::path path;CustomizationKind kind;std::string source,mode,logical;int rank;size_t order;};
    std::vector<Table> tables;
    if(valid) if(auto entries=manifest["tables"].as_array()) for(auto& node:*entries) if(auto table=node.as_table()) {
        auto source=(*table)["source"].value_or<std::string>(""),kind=(*table)["kind"].value_or<std::string>("");
        auto mode=(*table)["mode"].value_or<std::string>(""),logical=(*table)["logical_path"].value_or<std::string>("");
        auto path=Inside(home,(*table)["path"].value_or<std::string>(""));
        if(!path || !sourceIds.contains(source) || (kind!="module" && kind!="accessory") || (mode!="append" && mode!="replace") || logical.empty()) {out.diagnostics.push_back("已解包表登记无效：须提供来源、kind、mode、logical_path与安全相对path");continue;}
        const auto expected=kind=="module"?"gm_module_id.bin":"gm_customize_item_id.bin";
        if(path->filename()!=expected) {out.diagnostics.push_back("已解包适配仅接受gm_module_id.bin或gm_customize_item_id.bin");continue;}
        if(inactiveModSources.contains(source)) {out.diagnostics.push_back("跳过当前未启用来源的已解包DML表："+source);continue;}
        auto identity=sourceIds[source];Fingerprint(*path,out);
        tables.push_back({*path,kind=="module"?CustomizationKind::Module:CustomizationKind::Accessory,identity,mode,logical,ranks[identity],tables.size()});
    }
    // 普通表同一逻辑路径仅选高优先级；追加表均参与低到高整条替换。
    std::stable_sort(tables.begin(),tables.end(),[](const auto& a,const auto& b){
        if(a.mode!=b.mode) return a.mode=="replace";
        return a.rank>b.rank;
    });
    for(const auto& table:tables) {
        if(table.mode=="replace") {
            bool winner=true;for(const auto& other:tables) if(other.mode=="replace" && other.kind==table.kind && other.logical==table.logical && (other.rank<table.rank || (other.rank==table.rank && other.order<table.order))) winner=false;
            if(!winner) continue;
        }
        auto items=ReadCustomizationTable(table.path,table.kind,table.source,out.diagnostics);for(auto& item:items) Overlay(merged,std::move(item));
    }
    std::set<CustomizationKey> named;
    for(const auto& root:roots) {
        for(const auto* rom:{"rom","rom_steam","rom_steam_en","rom_steam_cn"}) for(const auto* filename:{"gm_module_tbl.farc","mod_gm_module_tbl.farc","gm_customize_item_tbl.farc","mod_gm_customize_item_tbl.farc"}) {
            auto path=root.path/rom/filename;std::error_code error;if(fs::is_regular_file(path,error)) {++out.unimportedArchives;Fingerprint(path,out);}
        }
        auto strings=Config(root.path/"rom/lang2/mod_str_array.toml",out);
        // 每个root先本语言后通用，且customize先于cstm_item；高优先级首次写入生效。
        std::vector<std::string> languages={out.language,""};if(out.language.empty()) languages={""};
        for(const auto& language:languages) for(const auto* group:{"module","customize","cstm_item"}) {
            auto table=language.empty()?strings[group].as_table():strings[language][group].as_table();if(!table) continue;
            for(const auto& [number,value]:*table) {
                auto id=Integer(std::string(number.str()));auto name=value.value<std::string>();if(!id || *id<0 || !name) continue;
                CustomizationKey key{std::string(group)=="module"?CustomizationKind::Module:CustomizationKind::Accessory,*id};auto item=merged.find(key);
                if(item==merged.end()) {++out.orphanNames;continue;}
                item->second.aliases.push_back(*name);
                if(std::none_of(item->second.sources.begin(),item->second.sources.end(),[&](const auto& contribution){return contribution.source==root.id && contribution.role=="名称";})) item->second.sources.push_back({root.id,"名称",false});
                if(named.insert(key).second) {item->second.name=*name;item->second.nameSource=root.id;for(auto& contribution:item->second.sources) if(contribution.source==root.id && contribution.role=="名称") contribution.effective=true;}
            }
        }
        // 其他真实语言名称仅用于搜索，不能抢占显示语言优先级。
        for(const auto& [language,node]:strings) if(auto locale=node.as_table()) for(const auto* group:{"module","customize","cstm_item"}) if(auto table=(*locale)[group].as_table()) for(const auto& [number,value]:*table) {
            auto id=Integer(std::string(number.str()));auto name=value.value<std::string>();if(!id || !name) continue;
            auto item=merged.find({std::string(group)=="module"?CustomizationKind::Module:CustomizationKind::Accessory,*id});
            if(item!=merged.end()) {
                item->second.aliases.push_back(*name);item->second.namesByLanguage.try_emplace(std::string(language.str()),*name);
                if(std::none_of(item->second.sources.begin(),item->second.sources.end(),[&](const auto& contribution){return contribution.source==root.id && contribution.role=="名称";})) item->second.sources.push_back({root.id,"名称",false});
            }
        }
    }
    for(auto& [key,item]:merged) {
        if(out.stale) item.metadataState="来源配置已变化／导入目录过期";
        if(!named.contains(key)) {auto localized=item.namesByLanguage.find(out.language);if(localized!=item.namesByLanguage.end()) item.name=localized->second;}
        if(item.name.empty()) item.name=std::string(key.kind==CustomizationKind::Module?"服装 #":"饰品 #")+std::to_string(key.id);
        PrepareCustomizationItem(item,out);out.items.push_back(std::move(item));
    }
    if(out.items.empty()) out.diagnostics.push_back("未导入服装／饰品元数据。请在本插件目录创建customization_catalog.toml，参照docs/CUSTOMIZATION_IMPORT.md登记最终目录或已解包实体表。");
    if(out.unimportedArchives) out.diagnostics.push_back("发现 "+std::to_string(out.unimportedArchives)+" 个实体归档，尚未解包导入；不读取FARC/CPK或纹理。");
    if(out.orphanNames) out.diagnostics.push_back("忽略 "+std::to_string(out.orphanNames)+" 个孤立名称补丁；名称不会创建实体。");
    out.diagnostics.push_back("当前为导入目录，未验证为游戏有效目录；原版／DLC仅在用户导入后可搜索。显示语言使用本插件设置："+out.language+" → 通用 → 表名称。");
    return out;
}
}
