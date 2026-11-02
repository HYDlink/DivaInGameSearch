#include "customization.h"
#include <Windows.h>
#include <iostream>
#include <fstream>
#include <map>
// 参数 argc/argv 为插件目录、游戏目录和审计文件；只读磁盘，不访问游戏进程。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=4) {std::cout<<"用法：CustomizationAudit 插件目录 游戏目录 输出文件\n";return 2;}
    auto catalog=diva::LoadCustomizationCatalog(argv[1],argv[2]);std::ofstream output(argv[3],std::ios::binary);
    output<<"领域\t编号\t名称\t原始角色\t部位令牌\t定义来源\n";
    // 按玩家可见领域统计：模型、发型（KAMI）、自定义道具（四部位）及未归类的未知部位。
    size_t modules=0,hair=0,accessories=0,unknown=0;
    std::map<std::string,size_t> parts;
    for(const auto& item:catalog.items) {
        const auto domain=diva::CustomizationItemDomain(item);
        if(!domain) ++unknown;
        else if(*domain==diva::CustomizationDomain::Models) ++modules;
        else if(*domain==diva::CustomizationDomain::Hair) ++hair;
        else {++accessories;++parts[item.partToken];}
        output<<(domain?diva::CustomizationDomainName(*domain):"未知部位")<<"\t"<<item.key.id<<"\t"<<item.name<<"\t"<<item.characterToken<<"\t"<<item.partToken<<"\t"<<item.definitionSource<<"\n";
    }
    for(const auto& reason:catalog.diagnostics) std::cout<<reason<<"\n";
    std::cout<<"磁盘元数据审计完成：模型 "<<modules<<" 件，发型 "<<hair<<" 件，自定义道具 "<<accessories<<" 件";
    for(const auto& [token,count]:parts) std::cout<<"（"<<token<<" "<<count<<"）";
    std::cout<<"，未知部位 "<<unknown<<" 件；不代表游戏内定位通过。\n";
    return output.good()?0:1;
}
