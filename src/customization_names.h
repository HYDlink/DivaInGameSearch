#pragma once
#include "customization.h"

namespace diva {
// 官方基准与最终有效实体分别保存，名称处理不改变有效实体集合。
using CustomizationItemMap=std::map<CustomizationKey,CustomizationItem>;

// 参数 items/baseline 为最终实体与未受Mod覆盖的官方基准；official 为可注入的语言表读取入口；
// home/game/out 为插件目录、游戏目录和目录快照。仅为可确认的同一物品继承官方译名。
void LocalizeOfficialCustomizationNames(CustomizationItemMap& items,const CustomizationItemMap& baseline,
    const OfficialTableSource& official,const fs::path& home,const fs::path& game,CustomizationCatalog& out);

// 参数 items/home/out 为已完成来源与语言合并的实体、插件目录和诊断快照；
// 生成本机服装参照表，并最后应用用户搜索显示名，不改变实体身份或游戏菜单。
void ApplyUserModuleNames(CustomizationItemMap& items,const fs::path& home,CustomizationCatalog& out);
}
