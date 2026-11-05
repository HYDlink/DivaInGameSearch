#include "i18n.h"
#include "recommendation.h"
#include <algorithm>
#include <charconv>

namespace diva {
namespace {
constexpr CustomizationCharacter kPerformerCharacters[]={CustomizationCharacter::Miku,CustomizationCharacter::Rin,CustomizationCharacter::Len,CustomizationCharacter::Luka,CustomizationCharacter::Neru,CustomizationCharacter::Haku,CustomizationCharacter::Kaito,CustomizationCharacter::Meiko,CustomizationCharacter::Sakine,CustomizationCharacter::Teto};
// 参数 performers/slot 为一首歌的演出角色及下标；按已核对的伪人物继承规则返回原始人物编号，无效返回-1。
int PerformerCharacterIndex(const std::vector<RecommendedPerformer>& performers,size_t slot) {
    const auto& performer=performers[slot];
    int character=performer.character;
    // 商业PC 4BCAB0/4BD200已核对：伪人物只继承引用项原始人物，不复制其推荐COS。
    if(performer.type>=1 && performer.type<=5) {
        int reference=-1;
        if(performer.type==2 && performer.pseudoSameId!=-1) {
            if(performer.pseudoSameId>=0 && static_cast<size_t>(performer.pseudoSameId)<slot) reference=performer.pseudoSameId;
        } else for(int previous=static_cast<int>(slot)-1;previous>=0;--previous) if(performers[static_cast<size_t>(previous)].type==0) {reference=previous;break;}
        if(reference>=0) character=performers[static_cast<size_t>(reference)].character;
    }
    if(performer.type<0 || performer.type>6 || character<0 || character>=10) return -1;
    return character;
}
}
std::optional<CustomizationCharacter> ResolvePerformerCharacter(const RecommendationSnapshot& snapshot,int pvid,int slot) {
    if(!snapshot.ready || slot<0) return std::nullopt;
    const auto song=snapshot.songs.find(pvid);
    if(song==snapshot.songs.end() || static_cast<size_t>(slot)>=song->second.size()) return std::nullopt;
    const int index=PerformerCharacterIndex(song->second,static_cast<size_t>(slot));
    if(index<0) return std::nullopt;
    return kPerformerCharacters[index];
}
// 参数 snapshot/pvid/catalog 为当前运行时值、目标歌曲及有效目录；未知关联保留具体原因。
RecommendedModules ResolveRecommendedModules(const RecommendationSnapshot& snapshot,int pvid,const CustomizationCatalog& catalog) {
    RecommendedModules result;
    if(!snapshot.ready) {result.unresolved.push_back(snapshot.reason);return result;}
    const auto song=snapshot.songs.find(pvid);
    if(song==snapshot.songs.end()) {result.unresolved.push_back(Tr(Tx::reco_no_record));return result;}
    if(song->second.empty()) {result.unresolved.push_back(Tr(Tx::reco_no_performer));return result;}
    // PV人物编号与菜单分类不同，显式转换，禁止将未知人物回退为初音。
    const auto& characters=kPerformerCharacters;
    const char* difficulties[]={"EASY","NORMAL","HARD","EXTREME","ENCORE"};
    for(size_t slot=0;slot<song->second.size();++slot) {
        const auto& performer=song->second[slot];
        const auto prefix=Trf(Tx::reco_performer,{{"n",std::to_string(slot+1)}});
        const int character=PerformerCharacterIndex(song->second,slot);
        if(character<0) {
            result.unresolved.push_back(prefix+Tr(Tx::reco_ref_invalid));continue;
        }
        if(performer.type==3 || performer.type==4) {
            result.unresolved.push_back(prefix+Tr(Tx::reco_swimsuit));continue;
        }
        std::map<int,std::vector<int>> variants;
        for(int d=0;d<5;++d) variants[performer.costumes[d]].push_back(d);
        for(const auto& [costume,difficultiesForCostume]:variants) {
            if(costume<0) {result.unresolved.push_back(prefix+Tr(Tx::reco_no_costume));continue;}
            std::vector<size_t> matches;
            for(size_t i=0;i<catalog.items.size();++i) {
                const auto& item=catalog.items[i];
                if(item.key.kind!=CustomizationKind::Module || item.character!=characters[character] || !item.cos.starts_with("COS_")) continue;
                int oneBased=0;const auto* begin=item.cos.data()+4;const auto* end=item.cos.data()+item.cos.size();
                const auto parsed=std::from_chars(begin,end,oneBased);
                if(parsed.ec==std::errc{} && parsed.ptr==end && oneBased>0 && oneBased-1==costume) matches.push_back(i);
            }
            if(matches.size()!=1) {
                result.unresolved.push_back(prefix+CustomizationCharacterName(characters[character])+" / COS_"+std::to_string(static_cast<int64_t>(costume)+1)+(matches.empty()?Tr(Tx::reco_no_match):Tr(Tx::reco_multi_match)));continue;
            }
            const auto index=matches.front();
            if(std::find(result.items.begin(),result.items.end(),index)==result.items.end()) result.items.push_back(index);
            std::string label=prefix+CustomizationCharacterName(characters[character]);
            if(variants.size()>1) {label+=Tr(Tx::reco_branch);for(int d:difficultiesForCostume) {label+=difficulties[d];label+=' ';}}
            result.reasons[catalog.items[index].key].push_back(std::move(label));
        }
    }
    return result;
}
// 保留原服装名称结果；歌曲匹配严格使用歌曲页Search，推荐卡片按稳定身份去重。
RecommendedModules SearchModulesWithSongs(const CustomizationCatalog& modules,const CustomizationQuery& moduleQuery,const Catalog* songs,Query songQuery,const std::set<int>& favorites,const RecommendationSnapshot* recommendations) {
    RecommendedModules result;result.items=SearchCustomization(modules,moduleQuery);
    if(moduleQuery.domain==CustomizationDomain::Accessories || moduleQuery.modMode || Normalize(moduleQuery.text).empty()) return result;
    if(!songs) {result.unresolved.push_back(Tr(Tx::reco_catalog_loading));return result;}
    if(songs->requiresRestart || modules.stale) {result.unresolved.push_back(Tr(Tx::reco_restart));return result;}
    songQuery.text=moduleQuery.text;
    const auto matches=Search(*songs,songQuery,favorites);result.matchedSongs=matches.size();
    if(matches.empty()) return result;
    if(!recommendations || !recommendations->ready) {result.unresolved.push_back(recommendations?recommendations->reason:Tr(Tx::reco_waiting));return result;}
    auto filter=moduleQuery;filter.text.clear();filter.character.reset();
    const auto allowed=SearchCustomization(modules,filter);const std::set<size_t> allowedSet(allowed.begin(),allowed.end());
    for(const auto& match:matches) {
        const auto& song=songs->songs[match.index];auto mapped=ResolveRecommendedModules(*recommendations,song.id,modules);
        const auto title=SongDisplayTitle(song)+" [PVID "+std::to_string(song.id)+"]";
        if(moduleQuery.domain==CustomizationDomain::Hair) {
            RecommendedModules hair;
            hair.unresolved=std::move(mapped.unresolved);
            for(size_t moduleIndex:mapped.items) {
                const auto& module=modules.items[moduleIndex];std::vector<size_t> candidates;
                // bind_module 才是模型到默认发型的关联；两个编号空间不同，绝不按相同 ID 或名称猜测。
                for(size_t index=0;index<modules.items.size();++index) {
                    const auto& item=modules.items[index];
                    if(item.key.kind==CustomizationKind::Accessory && item.part==CustomizationPart::Hair
                        && item.character==module.character && item.bindModule==module.key.id) candidates.push_back(index);
                }
                if(candidates.size()!=1) {
                    hair.unresolved.push_back(std::string(CustomizationCharacterName(module.character))+Tr(Tx::reco_module_id)+std::to_string(module.key.id)
                        +(candidates.empty()?Tr(Tx::reco_no_hair):Tr(Tx::reco_multi_hair)));continue;
                }
                const size_t index=candidates.front();
                if(std::find(hair.items.begin(),hair.items.end(),index)==hair.items.end()) hair.items.push_back(index);
                auto& reasons=hair.reasons[modules.items[index].key];
                const auto& original=mapped.reasons.at(module.key);reasons.insert(reasons.end(),original.begin(),original.end());
            }
            mapped=std::move(hair);
        }
        for(size_t index:mapped.items) {
            if(!allowedSet.contains(index)) continue;
            if(std::find(result.items.begin(),result.items.end(),index)==result.items.end()) result.items.push_back(index);
            for(const auto& reason:mapped.reasons.at(modules.items[index].key)) result.reasons[modules.items[index].key].push_back(title+" · "+reason);
        }
        for(const auto& reason:mapped.unresolved) result.unresolved.push_back(title+" · "+reason);
    }
    return result;
}
}
