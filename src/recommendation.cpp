#include "recommendation.h"
#include <algorithm>
#include <charconv>

namespace diva {
// 参数 snapshot/pvid/catalog 为当前运行时值、目标歌曲及有效目录；未知关联保留具体原因。
RecommendedModules ResolveRecommendedModules(const RecommendationSnapshot& snapshot,int pvid,const CustomizationCatalog& catalog) {
    RecommendedModules result;
    if(!snapshot.ready) {result.unresolved.push_back(snapshot.reason);return result;}
    const auto song=snapshot.songs.find(pvid);
    if(song==snapshot.songs.end()) {result.unresolved.push_back("这首歌曲尚无已加载的推荐记录。");return result;}
    if(song->second.empty()) {result.unresolved.push_back("这首歌曲未提供演出角色推荐。");return result;}
    // PV人物编号与菜单分类不同，显式转换，禁止将未知人物回退为初音。
    constexpr CustomizationCharacter characters[]={CustomizationCharacter::Miku,CustomizationCharacter::Rin,CustomizationCharacter::Len,CustomizationCharacter::Luka,CustomizationCharacter::Neru,CustomizationCharacter::Haku,CustomizationCharacter::Kaito,CustomizationCharacter::Meiko,CustomizationCharacter::Sakine,CustomizationCharacter::Teto};
    const char* difficulties[]={"EASY","NORMAL","HARD","EXTREME","ENCORE"};
    for(size_t slot=0;slot<song->second.size();++slot) {
        const auto& performer=song->second[slot];
        const auto prefix="演出角色 "+std::to_string(slot+1)+"：";
        int character=performer.character;
        // 商业PC 4BCAB0/4BD200已核对：伪人物只继承引用项原始人物，不复制其推荐COS。
        if(performer.type>=1 && performer.type<=5) {
            int reference=-1;
            if(performer.type==2 && performer.pseudoSameId!=-1) {
                if(performer.pseudoSameId>=0 && static_cast<size_t>(performer.pseudoSameId)<slot) reference=performer.pseudoSameId;
            } else for(int previous=static_cast<int>(slot)-1;previous>=0;--previous) if(song->second[previous].type==0) {reference=previous;break;}
            if(reference>=0) character=song->second[reference].character;
        }
        if(performer.type<0 || performer.type>6 || character<0 || character>=10) {
            result.unresolved.push_back(prefix+"人物或继承引用无效，未回退为默认人物。");continue;
        }
        if(performer.type==3 || performer.type==4) {
            result.unresolved.push_back(prefix+"特殊泳装推荐转换尚未认证，保留为未解析推荐。");continue;
        }
        std::map<int,std::vector<int>> variants;
        for(int d=0;d<5;++d) variants[performer.costumes[d]].push_back(d);
        for(const auto& [costume,difficultiesForCostume]:variants) {
            if(costume<0) {result.unresolved.push_back(prefix+"未指定推荐服装。");continue;}
            std::vector<size_t> matches;
            for(size_t i=0;i<catalog.items.size();++i) {
                const auto& item=catalog.items[i];
                if(item.key.kind!=CustomizationKind::Module || item.character!=characters[character] || !item.cos.starts_with("COS_")) continue;
                int oneBased=0;const auto* begin=item.cos.data()+4;const auto* end=item.cos.data()+item.cos.size();
                const auto parsed=std::from_chars(begin,end,oneBased);
                if(parsed.ec==std::errc{} && parsed.ptr==end && oneBased>0 && oneBased-1==costume) matches.push_back(i);
            }
            if(matches.size()!=1) {
                result.unresolved.push_back(prefix+CustomizationCharacterName(characters[character])+" / COS_"+std::to_string(static_cast<int64_t>(costume)+1)+(matches.empty()?" 未匹配有效服装目录。":" 对应多个服装，未猜测目标。"));continue;
            }
            const auto index=matches.front();
            if(std::find(result.items.begin(),result.items.end(),index)==result.items.end()) result.items.push_back(index);
            std::string label=prefix+CustomizationCharacterName(characters[character]);
            if(variants.size()>1) {label+=" · 原生推荐分支 ";for(int d:difficultiesForCostume) {label+=difficulties[d];label+=' ';}}
            result.reasons[catalog.items[index].key].push_back(std::move(label));
        }
    }
    return result;
}
// 保留原服装名称结果；歌曲匹配严格使用歌曲页Search，推荐卡片按稳定身份去重。
RecommendedModules SearchModulesWithSongs(const CustomizationCatalog& modules,const CustomizationQuery& moduleQuery,const Catalog* songs,Query songQuery,const std::set<int>& favorites,const RecommendationSnapshot* recommendations) {
    RecommendedModules result;result.items=SearchCustomization(modules,moduleQuery);
    if(moduleQuery.kind!=CustomizationKind::Module || moduleQuery.modMode || Normalize(moduleQuery.text).empty()) return result;
    if(!songs) {result.unresolved.push_back("歌曲目录正在加载，歌曲推荐暂不可用。");return result;}
    if(songs->requiresRestart || modules.stale) {result.unresolved.push_back("目录配置已改变，重启游戏后读取歌曲推荐。");return result;}
    songQuery.text=moduleQuery.text;
    const auto matches=Search(*songs,songQuery,favorites);result.matchedSongs=matches.size();
    if(matches.empty()) return result;
    if(!recommendations || !recommendations->ready) {result.unresolved.push_back(recommendations?recommendations->reason:"等待游戏推荐数据。");return result;}
    auto filter=moduleQuery;filter.text.clear();filter.character.reset();
    const auto allowed=SearchCustomization(modules,filter);const std::set<size_t> allowedSet(allowed.begin(),allowed.end());
    for(const auto& match:matches) {
        const auto& song=songs->songs[match.index];const auto mapped=ResolveRecommendedModules(*recommendations,song.id,modules);
        const auto title=song.name+" [PVID "+std::to_string(song.id)+"]";
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
