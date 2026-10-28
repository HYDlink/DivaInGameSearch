#include "core.h"
#include <Windows.h>
#include <fstream>
#include <iostream>

// 原生曲库审计入口；参数 argc/argv 为资源目录、游戏目录和审计输出文件。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=4) { std::cout<<"用法：CatalogCheck.exe 本项目目录 游戏目录 输出文件\n"; return 2; }
    try {
        auto catalog=diva::LoadCatalog(argv[1],argv[2]);
        std::ofstream out(argv[3]);
        out<<"PVID\t原名\t来源\t风格\t难度\t等级\t实际谱面提供方\t脚本\n";
        int charts=0;
        for(const auto& s:catalog.songs) for(int style=0;style<3;++style) for(int d=0;d<5;++d) {
            const auto& c=s.charts[style][d]; if(!c.available) continue;
            ++charts; out<<s.id<<"\t"<<s.name<<"\t"<<s.source<<"\t"<<style<<"\t"<<d<<"\t"<<c.level<<"\t"<<c.source<<"\t"<<c.script<<"\n";
        }
        std::cout<<"曲库审计完成："<<catalog.songs.size()<<" 首，"<<charts<<" 个生效风格难度组合，耗时 "<<(int)catalog.milliseconds<<" 毫秒。\n";
        for(const auto& d:catalog.diagnostics) std::cout<<d<<"\n";
        if(!out.good()) { std::cout<<"审计文件写入失败。\n"; return 1; }
        return catalog.songs.empty()?1:0;
    } catch(...) { std::cout<<"曲库审计异常，未生成可靠结果。\n"; return 1; }
}
