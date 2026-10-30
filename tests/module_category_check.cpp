#include "module_category.h"
#include <iostream>
#include <stdexcept>
namespace {
// 参数 result/message 为断言与中文失败原因。
void Check(bool result,const char* message) {if(!result) throw std::runtime_error(message);}
}
// 校验全部角色映射及逐帧路径规划；不调用游戏，不将离线通过当实机通过。
int main() {
    using namespace diva;
    try {
        const CustomizationCharacter major[]={CustomizationCharacter::Miku,CustomizationCharacter::Rin,CustomizationCharacter::Len,CustomizationCharacter::Luka,CustomizationCharacter::Kaito,CustomizationCharacter::Meiko,CustomizationCharacter::Derived};
        for(int category=0;category<7;++category) {Check(ModuleMenuCategory(major[category])==category,"游戏菜单分类映射错误");Check(ModuleMenuCharacter(category)==major[category],"菜单分类反向读取错误");}
        for(auto character:{CustomizationCharacter::Neru,CustomizationCharacter::Haku,CustomizationCharacter::Sakine,CustomizationCharacter::Teto}) Check(ModuleMenuCategory(character)==6,"衍生角色未归组");
        Check(ModuleRecordCategory(6)==4 && ModuleRecordCategory(7)==5,"KAITO/MEIKO原始编号误作菜单分类");
        for(int raw:{4,5,8,9}) Check(ModuleRecordCategory(raw)==6,"衍生角色记录编号匹配失败");
        Check(ModuleMenuCategory(CustomizationCharacter::Random)==-1 && ModuleRecordCategory(-1)==-1 && ModuleMenuCategory(CustomizationCharacter::Unknown)==-1,"随机或未知被开放成服装实体");
        for(int origin=0;origin<7;++origin) for(int target=0;target<7;++target) {
            int current=origin,steps=0;
            while(current!=target && steps<7) {const int direction=ModuleCategoryDirection(current,target);Check(direction==1 || direction==-1,"分类规划没有有效相邻步");current+=direction;++steps;Check(current>=0 && current<=6,"分类路径经过随机或越界");}
            Check(current==target && steps<=6,"跨角色路径未在有界帧数内到达");
        }
        std::cout<<"七类角色映射及49组跨类路径通过；未进行游戏内定位验收。\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
