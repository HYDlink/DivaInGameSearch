#pragma once
#include "customization.h"
#include <array>

namespace diva {
// 参数 character 为元数据或聚合角色；返回游戏菜单分类，不能直接使用元数据枚举值。
inline int ModuleMenuCategory(CustomizationCharacter character) {
    switch(character) {
    case CustomizationCharacter::Miku:return 0;
    case CustomizationCharacter::Rin:return 1;
    case CustomizationCharacter::Len:return 2;
    case CustomizationCharacter::Luka:return 3;
    case CustomizationCharacter::Kaito:return 4;
    case CustomizationCharacter::Meiko:return 5;
    case CustomizationCharacter::Neru:case CustomizationCharacter::Haku:
    case CustomizationCharacter::Sakine:case CustomizationCharacter::Teto:
    case CustomizationCharacter::Derived:return 6;
    default:return -1;
    }
}
// 参数 rawCharacter 为实体记录的原始人物编号，按独立映射核对当前菜单分类。
inline int ModuleRecordCategory(int rawCharacter) {
    switch(rawCharacter) {
    case 0:case 1:case 2:case 3:return rawCharacter;
    case 6:return 4;
    case 7:return 5;
    case 4:case 5:case 8:case 9:return 6;
    default:return -1;
    }
}
// 参数 current/target 为实际菜单分类；每帧仅取相邻一步，绝不经随机分类绕行。
inline int ModuleCategoryDirection(int current,int target) {
    if(current<0 || current>6 || target<0 || target>6) return 0;
    return target>current?1:target<current?-1:0;
}
// 参数 category 为原生菜单分类；随机和未知不返回可定位服装人物。
inline CustomizationCharacter ModuleMenuCharacter(int category) {
    constexpr std::array characters={CustomizationCharacter::Miku,CustomizationCharacter::Rin,CustomizationCharacter::Len,
        CustomizationCharacter::Luka,CustomizationCharacter::Kaito,CustomizationCharacter::Meiko,CustomizationCharacter::Derived};
    return category>=0 && category<int(characters.size())?characters[size_t(category)]:CustomizationCharacter::Unknown;
}
}
