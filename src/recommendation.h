#pragma once
#include "i18n.h"
#include "customization.h"
#include <array>

namespace diva {
// 原始推荐仅保存值，不向后台线程泄漏游戏对象地址。
struct RecommendedPerformer {
    int type=-1,character=-1,pseudoSameId=-1;
    std::array<int,5> costumes{-1,-1,-1,-1,-1};
    // 值快照比较用于识别真实内容变化，不因周期性重读使查询反复失效。
    bool operator==(const RecommendedPerformer&) const=default;
};
struct RecommendationSnapshot {
    std::map<int,std::vector<RecommendedPerformer>> songs;
    uint64_t generation=0;
    bool ready=false;
    std::string reason=Tr(Tx::reco_waiting);
};
struct RecommendedModules {
    std::vector<size_t> items;
    std::map<CustomizationKey,std::vector<std::string>> reasons;
    std::vector<std::string> unresolved;
    size_t matchedSongs=0;
};
// 参数 snapshot/pvid/slot 为游戏 PV 演出角色快照、来源歌曲与实际歌者槽位；按伪人物继承规则返回原始人物，未知返回空。
// 槽位下标与 PV 演出角色下标一一对应是待本机核对的假设；结果只用于发型浏览默认值与动作守卫，不改写任何游戏状态。
std::optional<CustomizationCharacter> ResolvePerformerCharacter(const RecommendationSnapshot& snapshot,int pvid,int slot);
// 参数 snapshot/pvid/catalog 为游戏推荐值、歌曲编号及最终有效服装目录；按人物和零基COS唯一关联。
RecommendedModules ResolveRecommendedModules(const RecommendationSnapshot& snapshot,int pvid,const CustomizationCatalog& catalog);
// 参数为领域条件、共用曲库及歌曲条件、收藏和推荐快照；模型与发型共用歌曲匹配，发型通过 bindModule 唯一关联推荐模型。
RecommendedModules SearchModulesWithSongs(const CustomizationCatalog& modules,const CustomizationQuery& moduleQuery,const Catalog* songs,Query songQuery,const std::set<int>& favorites,const RecommendationSnapshot* recommendations);
// 参数 base 必须已通过定位器的完整SHA256核验；只初始化只读读取器。
void InitializeRecommendations(uintptr_t base);
// 参数 enabled 表示界面是否需要推荐快照；关闭时停止新的扫描。
void RequestRecommendations(bool enabled);
// 在原生外层帧末分批复制有界推荐值，不调用游戏函数、不等待后台线程。
void TickRecommendations();
// 返回插件持有的不可变值快照，后台查询不接触游戏指针。
std::shared_ptr<const RecommendationSnapshot> ReadRecommendations();
}
