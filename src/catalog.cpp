#include "core.h"
#include <toml.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>

namespace diva {
// 清除行首尾空白和 UTF-8 BOM；参数 value 为原始文本。
static std::string Trim(std::string value) {
    if(value.starts_with("\xEF\xBB\xBF")) value.erase(0,3);
    auto a=value.find_first_not_of(" \t\r\n"),b=value.find_last_not_of(" \t\r\n");
    return a==std::string::npos?"":value.substr(a,b-a+1);
}
// 读取 PV 属性且保留字段来源；参数 path/source 为文件与来源名称。
Fields ReadPvdb(const fs::path& path,const std::string& source) {
    std::ifstream in(path,std::ios::binary); std::string line; Fields out;
    static const std::regex pattern(R"(^pv_([0-9]+)\.(.+?)=(.*)$)");
    while(std::getline(in,line)) {
        line=Trim(line); if(line.empty() || line[0]=='#') continue;
        std::smatch match; if(!std::regex_match(line,match,pattern)) continue;
        try { int id=std::stoi(match[1]); if(id>0 && id<10000000) out[id][match[2]]={Trim(match[3]),source}; } catch(...) {}
    } return out;
}
// 解析游戏星级常量；参数 text 为等级表示。
float ParseLevel(const std::string& text) {
    try { if(text.starts_with("PV_LV_")) { auto s=text.substr(6); std::replace(s.begin(),s.end(),'_','.'); return std::stof(s); } return std::stof(text); } catch(...) { return 0; }
}
struct Root { fs::path path; std::string name; };
// 记录影响已加载曲库的文件版本；参数 path/key 为路径与累计指纹。
static void Fingerprint(const fs::path& path,std::string& key) {
    std::error_code error;
    if(fs::is_regular_file(path,error)) key+=Utf8(path.wstring())+":"+std::to_string(fs::file_size(path,error))+":"+std::to_string(fs::last_write_time(path,error).time_since_epoch().count())+"\n";
}
// 按照低到高优先级覆盖单个属性；参数 dest/source 为累计与新属性。
static void Overlay(Fields& dest,const Fields& source) {
    for(const auto& [id,fields]:source) for(const auto& [key,value]:fields) dest[id][key]=value;
}
// 读取有默认值的配置；参数 path 为配置文件，诊断写入 diagnostics。
static toml::table Config(const fs::path& path,std::vector<std::string>& diagnostics) {
    if(!fs::exists(path)) return {};
    try { std::ifstream in(path,std::ios::binary); return toml::parse(in,Utf8(path.wstring())); }
    catch(...) { diagnostics.push_back("配置解析失败："+Utf8(path.wstring())); return {}; }
}
// 查找实际脚本覆盖；参数 script 为逻辑路径、roots 为高优先级在前的目录、fallback 为原来源。
static std::pair<bool,std::string> Resolve(const std::string& script,const std::vector<Root>& roots,const std::string& fallback) {
    if(script.empty()) return {false,fallback};
    auto p=fs::path(Wide(script));
    if(p.is_absolute() || script.find("..")!=std::string::npos) return {false,fallback};
    for(const auto& root:roots) if(fs::is_regular_file(root.path/p)) return {true,root.name};
    // 随发行包的官方元数据对应 CPK 内脚本，不依赖开发机解包路径。
    return {fallback=="原版" || fallback=="官方 DLC",fallback};
}
// 生成配置对应的曲库快照；参数 home/game 为本 Mod 和游戏目录。
Catalog LoadCatalog(const fs::path& home,const fs::path& game) {
    auto start=std::chrono::steady_clock::now(); Catalog out; Fields fields;
    Overlay(fields,ReadPvdb(home/"data/base_pv_db.txt","原版"));
    if(fs::exists(game/"diva_dlc00.cpk")) Overlay(fields,ReadPvdb(home/"data/dlc_pv_db.txt","官方 DLC"));
    std::map<int,std::set<std::string>> associations;
    std::vector<Root> roots;
    bool ncEnabled=false;
    Fingerprint(game/"config.toml",out.configurationKey);
    auto dml=Config(game/"config.toml",out.diagnostics);
    auto mods=game/fs::path(Wide(dml["mods"].value_or<std::string>("mods")));
    std::vector<fs::path> paths;
    if(dml["enabled"].value_or(true) && fs::is_directory(mods)) {
        if(auto a=dml["priority"].as_array(); a && !a->empty()) {
            for(auto& value:*a) { auto name=value.value_or<std::string>(""); if(!name.empty()) paths.push_back(mods/fs::path(Wide(name))); }
        } else {
            for(auto& entry:fs::directory_iterator(mods)) if(entry.is_directory()) paths.push_back(entry.path());
            std::sort(paths.begin(),paths.end());
        }
    }
    for(const auto& path:paths) {
        if(!fs::is_regular_file(path/"config.toml")) continue;
        Fingerprint(path/"config.toml",out.configurationKey);
        auto cfg=Config(path/"config.toml",out.diagnostics);
        if(!cfg["enabled"].value_or(true)) continue;
        if(auto dlls=cfg["dll"].as_array()) for(auto& dll:*dlls) if(Normalize(dll.value_or<std::string>(""))=="newclassics.dll") ncEnabled=true;
        auto name=cfg["name"].value_or(Utf8(path.filename().wstring()));
        if(auto includes=cfg["include"].as_array()) for(auto& item:*includes) {
            auto relative=item.value_or<std::string>(""); if(relative.empty()) continue;
            auto root=path/fs::path(Wide(relative)); if(fs::is_directory(root)) roots.push_back({root,name});
        }
    }
    // DML 第一个目录优先；先加载低优先级，再逐字段覆盖。
    for(auto root=roots.rbegin();root!=roots.rend();++root) {
        for(const auto& rom:{"rom","rom_steam","rom_steam_en","rom_steam_cn"}) {
            for(const auto& file:{"mdata_pv_db.txt","mod_pv_db.txt"}) {
                auto path=root->path/rom/file;Fingerprint(path,out.configurationKey);
                auto parsed=ReadPvdb(path,root->name);
                for(const auto& [id,f]:parsed) { (void)f; associations[id].insert(root->name); }
                Overlay(fields,parsed);
            }
        }
    }
    std::map<int,Song> songs;
    const std::array<std::string,4> names={"easy","normal","hard","extreme"};
    for(const auto& [id,attrs]:fields) {
        auto& song=songs[id]; song.id=id;
        // 按属性名称取值；参数 key 为属性路径。
        auto get=[&](const std::string& key)->std::string { auto it=attrs.find(key); return it==attrs.end()?"":it->second.first; };
        song.name=get("song_name"); song.english=get("song_name_en"); song.reading=get("song_name_reading");
        song.hidden=get("hidden")=="1";
        if(song.name.empty()) song.name=song.english.empty()?"PV "+std::to_string(id):song.english;
        if(auto it=attrs.find("song_name");it!=attrs.end()) song.source=it->second.second;
        if(!song.source.empty()) associations[id].insert(song.source);
        for(int d=0;d<4;++d) {
            auto prefix="difficulty."+names[d]+".";
            int length=0;
            try { auto raw=get(prefix+"length"); if(!raw.empty()) length=std::clamp(std::stoi(raw),0,16); } catch(...) {}
            for(int index=0;index<length;++index) {
                auto p=prefix+std::to_string(index)+".";
                auto scriptKey=p+"script_file_name"; auto it=attrs.find(scriptKey);
                if(it==attrs.end()) continue;
                int actual=(d==3 && (index>0 || get(p+"attribute.extra")=="1"))?4:d;
                auto resolved=Resolve(it->second.first,roots,it->second.second);
                song.charts[0][actual]={ParseLevel(get(p+"level")),it->second.first,resolved.second,resolved.first};
            }
        }
    }
    // NC 对同一歌曲和难度的列表以高优先级为准；无 script 条目继承官方谱面。
    struct NcEntry { toml::table table; std::string source; };
    std::map<std::pair<int,int>,NcEntry> nc;
    const std::array<std::string,5> ncNames={"easy","normal","hard","extreme","ex_extreme"};
    for(auto root=roots.rbegin();root!=roots.rend();++root) {
        auto path=root->path/"rom/nc_db.toml";
        if(!ncEnabled || !fs::exists(path)) continue;
        Fingerprint(path,out.configurationKey);
        auto db=Config(path,out.diagnostics);
        if(auto list=db["songs"].as_array()) for(auto& item:*list) {
            auto table=item.as_table(); if(!table) continue;
            int id=(int)(*table)["id"].value_or<int64_t>(0); if(!songs.contains(id)) continue;
            for(int d=0;d<5;++d) if((*table)[ncNames[d]].is_array()) nc.insert_or_assign({id,d},NcEntry{*table,root->name});
            associations[id].insert(root->name);
        }
    }
    for(auto& [key,entry]:nc) {
        auto [id,d]=key; auto& song=songs[id]; auto inherited=song.charts[0][d];
        for(int s=0;s<3;++s) song.charts[s][d]={};
        auto list=entry.table[ncNames[d]].as_array();
        for(auto& node:*list) {
            auto t=node.as_table(); if(!t) continue;
            auto style=(*t)["style"].value_or<std::string>("ARCADE");
            int s=style=="CONSOLE"?1:style=="MIXED"?2:style=="ARCADE"?0:-1;
            if(s<0) continue;
            auto script=(*t)["script_file_name"].value_or<std::string>("");
            if(script.empty()) song.charts[s][d]=inherited;
            else {
                auto resolved=Resolve(script,roots,entry.source);
                auto level=(*t)["level"].value_or<std::string>("");
                song.charts[s][d]={ParseLevel(level),script,resolved.second,resolved.first};
            }
        }
    }
    // 用户别名独立于来源覆盖；每个 PVID 对应竖线分隔的多个别名。
    auto aliases=Config(home/"aliases.toml",out.diagnostics);
    std::set<std::string> packs;
    for(auto& [id,song]:songs) {
        song.aliases=aliases["aliases"][std::to_string(id)].value_or<std::string>("");
        song.sources.assign(associations[id].begin(),associations[id].end());
        for(auto& style:song.charts) for(auto& c:style) if(c.available) {
            if(std::find(song.sources.begin(),song.sources.end(),c.source)==song.sources.end()) song.sources.push_back(c.source);
        }
        for(const auto& p:song.sources) packs.insert(p);
        PrepareSong(song); out.songs.push_back(std::move(song));
    }
    out.packs.assign(packs.begin(),packs.end());
    if(out.songs.empty()) out.diagnostics.push_back("未读取到歌曲，请检查发行包 data 目录。");
    out.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return out;
}
}
