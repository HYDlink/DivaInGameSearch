#include "customization_names.h"
#include <algorithm>
#include <array>
#include <optional>

namespace diva {
namespace {
// 偏移必须由未合并的官方实体与日文表共同核验，不使用Mod覆盖后剩余的样本。
constexpr size_t kMinimumSamples=20,kMinimumMatchPercent=90;

struct NameTable {
    std::map<int,std::string> names;
    std::string fingerprint;
};

// 参数 kind 为模型或定制物品；返回已核查PC字符串编号偏移。
int NameOffset(CustomizationKind kind) {return kind==CustomizationKind::Module?0x113:0x3a8;}

// 参数 path 为归档内部路径；只统一斜线和ASCII大小写，不放宽平台路径范围。
std::string NormalizePath(std::string path) {
    for(auto& ch:path) {
        if(ch=='\\') ch='/';
        else if(ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
    }
    return path;
}

// 参数 official/home/game/language/out 为读取入口、插件目录、游戏目录、语言与快照；
// 尝试全部规范PC路径候选，忽略损坏副本，相同内容去重，不任意选择相互冲突的有效表。
std::optional<NameTable> ReadNameTable(const OfficialTableSource& official,const fs::path& home,const fs::path& game,
                                     const std::string& language,CustomizationCatalog& out) {
    if(!official.strings) return {};
    const auto suffix="rom/lang2/"+language+"/str_array.bin";
    const auto expected="rom_steam/"+suffix;
    std::optional<NameTable> selected;
    for(const auto& entry:official.strings(game,home,suffix,out.diagnostics)) {
        if(NormalizePath(entry.internalPath)!=expected) continue;
        auto names=ReadCustomizationStrings(entry.entryData);
        if(names.empty()) {
            out.diagnostics.push_back("跳过无法解析的官方定制语言表候选："+language+" / "+entry.internalPath);
            continue;
        }
        if(selected && selected->names!=names) {
            out.diagnostics.push_back("官方定制语言表存在内容冲突，拒绝任意选择："+language);
            return {};
        }
        if(!selected) {
            const auto path=entry.cpkPath.filename().generic_u8string();
            out.diagnostics.push_back("选中官方定制语言表："+language+" / "+
                std::string(reinterpret_cast<const char*>(path.data()),path.size())+" / "+entry.internalPath);
            selected=NameTable{std::move(names),entry.fingerprint};
        }
    }
    return selected;
}

// 参数 baseline/table/kind/out 为原始官方基准、日文表、物品种类和诊断快照；返回偏移是否可信。
bool VerifyNameOffset(const CustomizationItemMap& baseline,const NameTable& table,CustomizationKind kind,CustomizationCatalog& out) {
    size_t total=0,matched=0;
    for(const auto& [key,item]:baseline) {
        if(key.kind!=kind || key.id>100000-NameOffset(kind) || item.name.empty()) continue;
        ++total;
        const auto name=table.names.find(key.id+NameOffset(kind));
        if(name!=table.names.end() && name->second==item.name) ++matched;
    }
    const bool trusted=total>=kMinimumSamples && matched*100>=total*kMinimumMatchPercent;
    out.diagnostics.push_back(std::string("官方")+(kind==CustomizationKind::Module?"模型":"定制道具")+
        "语言表偏移核对：原始官方日文一致 "+std::to_string(matched)+"/"+std::to_string(total)+
        (trusted?"，采用。":"，不足，保留实体表原名。"));
    return trusted;
}

// 参数 item/original 为最终定义和同编号官方定义；保留Mod显式改名及角色、服装或绑定对象重定义。
bool CanInheritOfficialName(const CustomizationItem& item,const CustomizationItem& original) {
    if(item.definitionSource.starts_with("official:")) return true;
    if(item.name!=original.name || item.characterToken.empty() || item.characterToken!=original.characterToken) return false;
    if(item.key.kind==CustomizationKind::Module) return !original.cos.empty() && item.cos==original.cos;
    return original.objectId.has_value() && item.objectId==original.objectId &&
        item.partToken==original.partToken && item.bindModule==original.bindModule;
}
}

// 参数 items/baseline/official/home/game/out 为有效实体、原始官方基准、读取入口、插件目录、游戏目录和诊断快照。
void LocalizeOfficialCustomizationNames(CustomizationItemMap& items,const CustomizationItemMap& baseline,
    const OfficialTableSource& official,const fs::path& home,const fs::path& game,CustomizationCatalog& out) {
    if(out.language.size()!=2 || out.language.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos) return;
    const auto localized=ReadNameTable(official,home,game,out.language,out);
    if(!localized) {
        out.diagnostics.push_back("未读取到可采用的官方定制语言表："+out.language+"；保留实体名称。");
        return;
    }
    out.configurationKey+=localized->fingerprint+"\n";
    const auto japanese=out.language=="jp"?localized:ReadNameTable(official,home,game,"jp",out);
    if(!japanese) {
        out.diagnostics.push_back("未读取到日文定制语言表，无法核对偏移；保留实体名称。");
        return;
    }
    out.configurationKey+=japanese->fingerprint+"\n";
    const std::array<bool,2> trusted{VerifyNameOffset(baseline,*japanese,CustomizationKind::Module,out),
                                  VerifyNameOffset(baseline,*japanese,CustomizationKind::Accessory,out)};
    size_t direct=0,inherited=0,preserved=0;
    for(auto& [key,item]:items) {
        const auto original=baseline.find(key);
        const auto slot=key.kind==CustomizationKind::Module?0u:1u;
        if(original==baseline.end() || !trusted[slot] || key.id>100000-NameOffset(key.kind)) continue;
        if(!CanInheritOfficialName(item,original->second)) {++preserved;continue;}
        const auto name=localized->names.find(key.id+NameOffset(key.kind));
        if(name==localized->names.end() || item.namesByLanguage.contains(out.language)) continue;
        item.namesByLanguage[out.language]=name->second;
        item.name=name->second;item.nameSource=original->second.definitionSource;
        if(item.definitionSource.starts_with("official:")) ++direct;
        else {
            ++inherited;
            item.sources.push_back({item.nameSource,CustomizationContributionRole::Name,true});
        }
    }
    out.diagnostics.push_back("官方译名应用：语言 "+out.language+"，官方定义 "+std::to_string(direct)+
        " 件，Mod同一物品继承 "+std::to_string(inherited)+" 件，Mod改名或重定义保留 "+std::to_string(preserved)+" 件；后续名称补丁按DML优先级处理。");
}
}
