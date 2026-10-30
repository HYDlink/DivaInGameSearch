#include "customization.h"
#include <Windows.h>
#include <iostream>
#include <fstream>
// 参数 argc/argv 为插件目录、游戏目录和审计文件；只读磁盘，不访问游戏进程。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=4) {std::cout<<"用法：CustomizationAudit 插件目录 游戏目录 输出文件\n";return 2;}
    auto catalog=diva::LoadCustomizationCatalog(argv[1],argv[2]);std::ofstream output(argv[3],std::ios::binary);
    output<<"种类\t编号\t名称\t原始角色\t定义来源\n";
    size_t modules=0,accessories=0;
    for(const auto& item:catalog.items) {
        const bool module=item.key.kind==diva::CustomizationKind::Module;
        if(module) ++modules;else ++accessories;
        output<<(module?"服装":"其他定制")<<"\t"<<item.key.id<<"\t"<<item.name<<"\t"<<item.characterToken<<"\t"<<item.definitionSource<<"\n";
    }
    for(const auto& reason:catalog.diagnostics) std::cout<<reason<<"\n";
    std::cout<<"磁盘元数据审计完成：服装 "<<modules<<" 件，其他定制 "<<accessories<<" 件；不代表游戏内定位通过。\n";
    return output.good()?0:1;
}
