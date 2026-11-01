#include "customization.h"
#include "farc.h"
#include "module_assets.h"
#include <toml.hpp>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <regex>
#include <sstream>
#include <climits>
#include <Windows.h>
#include <wincrypt.h>

namespace diva {
namespace {
// 参数 game 为游戏根目录；仅支持已核实构建的旧式平面名称映射。
bool LegacyNameBuild(const fs::path& game) {
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;bool valid=false;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return false;
    if(CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) {
        std::ifstream file(game/"DivaMegaMix.exe",std::ios::binary);char buffer[65536];bool okay=file.good();
        while(file.read(buffer,sizeof(buffer)) || file.gcount()) if(!CryptHashData(hash,reinterpret_cast<const BYTE*>(buffer),static_cast<DWORD>(file.gcount()),0)) {okay=false;break;}
        BYTE digest[32];DWORD size=32;
        if(okay && file.eof() && CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0)) {
            const char* digits="0123456789ABCDEF";std::string text;for(auto byte:digest) {text+=digits[byte>>4];text+=digits[byte&15];}
            valid=text=="6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D";
        }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(provider,0);return valid;
}
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
// 参数 stream/kind/source/diagnostics 为受限文本流、种类、来源与诊断。
std::vector<CustomizationItem> ReadCustomizationStream(std::istream& stream,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics) {
    std::string line;std::map<int,std::map<std::string,std::string>> records;
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
    if(!stream.eof() && stream.fail()) diagnostics.push_back("实体表读取失败或中断");
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
// 参数 path/kind/source/diagnostics 为本地文本、种类、来源与诊断。
std::vector<CustomizationItem> ReadCustomizationTable(const fs::path& path,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics) {
    std::ifstream stream(path,std::ios::binary);return ReadCustomizationStream(stream,kind,source,diagnostics);
}
// 参数 text/kind/source/diagnostics 为内存实体文本、种类、来源及诊断。
std::vector<CustomizationItem> ReadCustomizationText(const std::string& text,CustomizationKind kind,const std::string& source,std::vector<std::string>& diagnostics) {
    if(text.size()>16*1024*1024) {diagnostics.push_back("内存实体表超出安全大小上限");return {};}
    std::istringstream stream(text);return ReadCustomizationStream(stream,kind,source,diagnostics);
}
// 参数data为PC小端偏移表；遵循随包MikuMikuLibrary的经典StringArray格式，不接受现代节格式。
std::map<int,std::string> ReadCustomizationStrings(const std::string& data) {
    std::map<int,std::string> result;
    if(data.size()<8 || data.size()>16*1024*1024) return {};
    const auto offset=[&](size_t at) {uint32_t value=0;for(int n=0;n<4;++n) value|=static_cast<uint32_t>(static_cast<unsigned char>(data[at+n]))<<(n*8);return value;};
    const auto first=offset(0);
    if(first<8 || first%4 || first>data.size() || first/4>100000) return {};
    for(size_t at=0;at<first;at+=4) {
        const auto begin=offset(at);if(!begin) break;
        if(begin<first || begin>=data.size()) return {};
        const auto end=data.find('\0',begin);if(end==std::string::npos || end-begin>65536) return {};
        auto text=data.substr(begin,end-begin);
        if(!text.empty() && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)) return {};
        if(!text.empty() && text!="BLANK") result.emplace(static_cast<int>(at/4),std::move(text));
    }
    return result;
}
CustomizationCatalog LoadCustomizationCatalog(const fs::path& home,const fs::path& game,const std::string& language) {
    CustomizationCatalog out;auto roots=Roots(game,out);out.dmlFingerprint=Hash(out.configurationKey);
    for(const auto& root:roots) out.sources.push_back({root.id,root.name,root.modDirectory,root.includeRoot,true});
    auto manifest=Config(home/"customization_catalog.toml",out);out.language=language.empty()?manifest["language"].value_or<std::string>("cn"):language;
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
    // 官方目录使用PC地区表；DLC地区表是独立明确来源，不从PS4/Switch同名表任取版本。
    auto officialTables=ReadOfficialModuleTables(game,home,out.diagnostics);
    bool officialModuleTableReplaced=false;
    for(const auto& root:roots) {
        std::string reason;
        if(ReadFarcEntry(root.path/"rom/gm_module_tbl.farc","gm_module_id.bin",reason)) {officialModuleTableReplaced=true;break;}
    }
    const OfficialModuleTable* pcBase=nullptr;const OfficialModuleTable* pcDlc=nullptr;
    for(const auto& table:officialTables) {
        if(table.internalPath=="rom_steam_region/rom/gm_module_tbl.farc!/gm_module_id.bin" && table.cpkPath.filename()=="diva_main_region.cpk") pcBase=&table;
        if(table.internalPath=="rom_steam_region_dlc/rom/gm_module_tbl.farc!/gm_module_id.bin" && table.cpkPath.filename()=="diva_dlc00_region.cpk") pcDlc=&table;
    }
    for(const auto* table:{pcBase,pcDlc}) if(table) {
        const std::string identity=table==pcBase?"official:pc-region":"official:pc-region-dlc";
        out.sources.push_back({identity,table==pcBase?"官方PC基础地区表":"官方PC DLC地区表（含基础条目）","","",false});
        out.configurationKey+=table->fingerprint+"\n";
        if(officialModuleTableReplaced) continue;
        auto items=ReadCustomizationText(table->tableText,CustomizationKind::Module,identity,out.diagnostics);
        for(auto& item:items) {item.metadataState="官方PC地区实体表";Overlay(merged,std::move(item));}
    }
    if(officialModuleTableReplaced) out.diagnostics.push_back("启用Mod提供普通rom/gm_module_tbl.farc，官方同逻辑实体表整表退出，不保留其被删除的ID。");
    if(!pcBase && !pcDlc && !officialTables.empty()) out.diagnostics.push_back("发现官方表，但未匹配已支持的PC地区路径；未任意合并其他平台表。");
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
        auto identity=sourceIds[source];
        if(mode=="replace") {
            bool automaticWinner=false;
            for(const auto& candidate:roots) {
                if(ranks[candidate.id]>ranks[identity]) continue;
                auto candidatePath=Inside(candidate.path,logical);std::string reason;
                if(candidatePath && ReadFarcEntry(*candidatePath,expected,reason)) {automaticWinner=true;break;}
            }
            if(automaticWinner) {out.diagnostics.push_back("普通兼容表已由更高优先自动归档替换："+identity+" / "+logical);continue;}
        }
        // 同来源同逻辑表存在自动归档时，兼容副本整表退出，避免旧副本独有ID残留。
        auto activeRoot=std::find_if(roots.begin(),roots.end(),[&](const auto& root){return root.id==identity;});
        if(activeRoot!=roots.end()) {
            auto automaticPath=Inside(activeRoot->path,logical);std::error_code error;
            if(automaticPath && fs::is_regular_file(*automaticPath,error)) {
                std::string reason;
                auto automaticText=ReadFarcEntry(*automaticPath,expected,reason);
                if(automaticText) {out.diagnostics.push_back("跳过已由自动归档接管的兼容表："+identity+" / "+logical);continue;}
                out.diagnostics.push_back("自动归档无法读取，保留显式兼容表："+identity+" / "+logical);
            }
        }
        Fingerprint(*path,out);
        tables.push_back({*path,kind=="module"?CustomizationKind::Module:CustomizationKind::Accessory,identity,mode,logical,ranks[identity],tables.size()});
    }
    // 自动实体优先于兼容导入；普通表按逻辑路径只选最高优先级，追加表从低到高整条覆盖。
    std::map<fs::path,std::string> automaticTexts;
    struct AutomaticTable {fs::path path;std::string source,logical;CustomizationKind kind;bool append;};
    std::vector<AutomaticTable> automatic;
    for(const auto& root:roots) for(const auto* rom:{"rom","rom_steam","rom_steam_en","rom_steam_cn"}) for(const auto* filename:{"gm_module_tbl.farc","mod_gm_module_tbl.farc","gm_customize_item_tbl.farc","mod_gm_customize_item_tbl.farc"}) {
        auto path=root.path/rom/filename;std::error_code error;if(!fs::is_regular_file(path,error)) continue;
        Fingerprint(path,out);std::string logical=std::string(rom)+"/"+filename;bool append=std::string(filename).starts_with("mod_");
        const auto kind=std::string(filename).find("customize")!=std::string::npos?CustomizationKind::Accessory:CustomizationKind::Module;
        if(!append && std::any_of(tables.begin(),tables.end(),[&](const auto& table){return table.mode=="replace" && table.kind==kind && table.logical==logical && table.rank<ranks[root.id];})) continue;

        automatic.push_back({path,root.id,logical,std::string(filename).find("customize")!=std::string::npos?CustomizationKind::Accessory:CustomizationKind::Module,append});
    }
    for(const auto& archive:automatic) {
        std::string reason;
        auto text=ReadFarcEntry(archive.path,archive.kind==CustomizationKind::Module?"gm_module_id.bin":"gm_customize_item_id.bin",reason);
        if(!text) {++out.unimportedArchives;out.diagnostics.push_back("实体归档读取失败："+archive.source+" / "+archive.logical+"："+reason);continue;}
        automaticTexts[archive.path]=std::move(*text);
        tables.push_back({archive.path,archive.kind,archive.source,archive.append?"append":"replace",archive.logical,ranks[archive.source],tables.size()});
    }
    // 自动与兼容表统一竞争：普通表只取一个整表，追加按真实DML优先级低到高覆盖。
    std::stable_sort(tables.begin(),tables.end(),[](const auto& left,const auto& right){
        if(left.mode!=right.mode) return left.mode=="replace";
        return left.rank>right.rank;
    });
    for(const auto& table:tables) {
        if(table.mode=="replace") {
            bool winner=true;
            for(const auto& other:tables) if(other.mode=="replace" && other.kind==table.kind && other.logical==table.logical && (other.rank<table.rank || (other.rank==table.rank && other.order<table.order))) winner=false;
            if(!winner) continue;
        }
        std::vector<CustomizationItem> items;
        auto text=automaticTexts.find(table.path);
        if(text!=automaticTexts.end()) {
            std::istringstream stream(text->second);items=ReadCustomizationStream(stream,table.kind,table.source,out.diagnostics);
            out.diagnostics.push_back("自动实体定义："+table.source+" / "+table.logical+"："+std::to_string(items.size())+" 件");
        } else items=ReadCustomizationTable(table.path,table.kind,table.source,out.diagnostics);
        for(auto& item:items) Overlay(merged,std::move(item));
    }
    for(const auto& root:roots) if(std::none_of(automatic.begin(),automatic.end(),[&](const auto& table){return table.source==root.id;})) {
        out.diagnostics.push_back("未检测到标准定制实体定义："+root.id+"；资源替换、PV引用或DLL动态注册需另行核查。");
    }
    const bool legacyBuild=LegacyNameBuild(game);
    // PC已核查构建的原生名称调用以ModuleID+0x113查语言表；只应用于官方实体，Mod名称随后按DML优先级覆盖。
    if(legacyBuild && out.language.size()==2 && out.language.find_first_not_of("abcdefghijklmnopqrstuvwxyz")==std::string::npos) {
        const auto suffix="rom/lang2/"+out.language+"/str_array.bin";
        auto strings=ReadOfficialCpkEntries(game,home,suffix,out.diagnostics);
        for(const auto& entry:strings) if(entry.internalPath=="rom_steam/"+suffix) {
            const auto names=ReadCustomizationStrings(entry.entryData);
            out.configurationKey+=entry.fingerprint+"\n";
            for(auto& [key,item]:merged) if(key.kind==CustomizationKind::Module && item.definitionSource.starts_with("official:") && key.id<=100000-0x113) {
                const auto name=names.find(key.id+0x113);if(name==names.end()) continue;
                item.namesByLanguage[out.language]=name->second;item.name=name->second;
            }
            out.diagnostics.push_back("官方服装语言表："+out.language+"，有效字符串 "+std::to_string(names.size())+" 条。");
            break;
        }
    }
    std::set<CustomizationKey> named;
    for(const auto& root:roots) {
        for(const auto* rom:{"rom","rom_steam","rom_steam_en","rom_steam_cn"}) for(const auto* filename:{"gm_module_tbl.farc","mod_gm_module_tbl.farc","gm_customize_item_tbl.farc","mod_gm_customize_item_tbl.farc"}) {
            auto path=root.path/rom/filename;std::error_code error;if(fs::is_regular_file(path,error)) {Fingerprint(path,out);}
        }
        auto strings=Config(root.path/"rom/lang2/mod_str_array.toml",out);
        // 已核实的旧式名称仅在精确来源、实体及COS对应时映射；绝不统一减275。
        auto legacy=merged.find({CustomizationKind::Module,997});
        if(legacyBuild && root.modDirectory=="X Song Pack" && legacy!=merged.end() && legacy->second.definitionSource==root.id && legacy->second.cos=="COS_482") {
            for(const auto* locale:{"cn","tw","fr","ge","it","kr","sp",""}) {
                auto name=std::string(locale).empty()?strings["1272"].value<std::string>():strings[locale]["1272"].value<std::string>();
                if(!name) continue;
                legacy->second.aliases.push_back(*name);
                if(*locale) legacy->second.namesByLanguage[locale]=*name;
                if(out.language==locale || (legacy->second.name.empty() && !*locale)) {legacy->second.name=*name;legacy->second.nameSource=root.id;}
            }
        }
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
    // 图片替换也属于快照：只读索引元数据，刷新后失效旧定位，不能当成游戏已重载。
    for(const auto& root:roots) {
        auto directory=Inside(root.path,"rom/2d");if(!directory) continue;
        std::vector<fs::path> imageFiles;std::error_code error;
        for(fs::directory_iterator entry(*directory,error),end;entry!=end && !error && imageFiles.size()<32768;entry.increment(error)) {
            auto name=PathText(entry->path().filename());
            if(name=="spr_db.bin" || name=="mod_spr_db.bin" || (name.starts_with("spr_sel_md") && name.ends_with(".farc"))) imageFiles.push_back(entry->path());
        }
        std::sort(imageFiles.begin(),imageFiles.end());
        for(const auto& path:imageFiles) Fingerprint(path,out);
    }
    out.catalogFingerprint=Hash(out.configurationKey);
    if(out.items.empty()) out.diagnostics.push_back("未检测到可读取的服装／饰品实体。请查看来源诊断。");
    if(out.unimportedArchives) out.diagnostics.push_back("发现 "+std::to_string(out.unimportedArchives)+" 个实体归档读取失败；详见来源诊断。");
    if(out.orphanNames) out.diagnostics.push_back("忽略 "+std::to_string(out.orphanNames)+" 个孤立名称补丁；名称不会创建实体。");
    out.diagnostics.push_back("当前为磁盘元数据快照；磁盘刷新不等于游戏重载。官方PC地区表自动读取，未读取已购买或解锁状态。显示语言："+out.language+" → Mod通用名称 → 实体表原名；游戏内优先跟随当前语言。");
    return out;
}
}
