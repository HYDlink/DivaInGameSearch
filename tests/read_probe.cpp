#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>

static HANDLE processHandle=nullptr;
// 只读目标进程；参数 address/value 为已知地址和输出，不申请写入或执行权限。
template<class T> static bool Read(uintptr_t address,T& value) { SIZE_T done=0;return ReadProcessMemory(processHandle,(void*)address,&value,sizeof(value),&done) && done==sizeof(value); }
// 输出已知对象中的少量整数；参数 label/address/bytes 为中文名称、地址与范围。
static void Dump(const char* label,uintptr_t address,size_t bytes) {
    std::cout<<label<<"：0x"<<std::hex<<address<<std::dec<<"\n";
    for(size_t offset=0;offset<bytes;offset+=16) {
        std::cout<<"  偏移 0x"<<std::hex<<offset<<std::dec<<"：";
        for(size_t j=0;j<16;j+=4) { int32_t value=0;if(Read(address+offset+j,value)) std::cout<<value<<" ";else std::cout<<"不可读 "; }std::cout<<"\n";
    }
}
// 核对当前游戏原始字段和只读获取器代码；参数 argc/argv 为目标进程编号。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=2) { std::cout<<"请提供游戏进程编号。\n";return 2; }
    auto pid=(DWORD)wcstoul(argv[1],nullptr,10);
    processHandle=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,pid);
    if(!processHandle) { std::cout<<"只读打开游戏失败，错误码："<<GetLastError()<<"\n";return 1; }
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);MODULEENTRY32W module{};module.dwSize=sizeof(module);
    if(snapshot==INVALID_HANDLE_VALUE || !Module32FirstW(snapshot,&module)) { std::cout<<"读取主模块失败。\n";return 1; }
    auto base=(uintptr_t)module.modBaseAddr;CloseHandle(snapshot);
    SYSTEMTIME time{};GetLocalTime(&time);std::cout<<"采样时间："<<time.wYear<<"-"<<time.wMonth<<"-"<<time.wDay<<" "<<time.wHour<<":"<<time.wMinute<<":"<<time.wSecond<<"."<<time.wMilliseconds<<"\n";
    std::cout<<"游戏主模块：0x"<<std::hex<<base<<std::dec<<"；只读权限，没有写入游戏。\n";
    Dump("菜单原始字段",base+0xCC61090,32);Dump("请求字段",base+0x12B6340,48);Dump("另一数据布局候选",base+0x12B6340+0x105F460,48);
    uintptr_t menuObject=0;int member=0;Read(base+0xCC5EF18,menuObject);Read(base+0x6EFE8C,member);
    std::cout<<"QuantumSwitch 对象指针：0x"<<std::hex<<menuObject<<"；成员原值：0x"<<member<<std::dec<<"\n";
    if(menuObject) {
        Dump("菜单对象头",menuObject,96);Dump("参考公式的菜单歌曲字段",menuObject+(intptr_t)member-0x9C8,16);
        auto task=menuObject-0x9C8;Dump("PC 原生选曲任务状态",task+0x60,32);Dump("PC 原生选曲尾部",task+0x261E8,0xF0);
        uintptr_t list=0;Read(task+0x25A20,list);Dump("当前可见歌曲向量",list,24);
        Dump("PC 直接菜单对象状态",menuObject+0x60,32);Dump("PC 直接菜单对象尾部",menuObject+0x261E8,0xF0);
        list=0;Read(menuObject+0x25A20,list);Dump("PC 直接对象歌曲向量",list,24);
    }
    int displacement=0;Read(base+0x6EFCF0+0x1D1,displacement);auto getter=base+0x6EFCF0+0x1D5+displacement;
    std::array<unsigned char,96> code{};Read(getter,code);
    std::cout<<"选曲获取器地址：0x"<<std::hex<<getter<<"\n获取器机器码：";
    for(auto byte:code) std::cout<<std::setw(2)<<std::setfill('0')<<(int)byte<<" ";std::cout<<std::dec<<"\n";
    // 只解析常见的 RIP 相对 LEA/MOV，不执行游戏函数。
    for(size_t i=0;i+7<code.size() && i<32;++i) if(code[i]==0x48 && (code[i+1]==0x8D || code[i+1]==0x8B) && code[i+2]==0x05) {
        int relative=0;memcpy(&relative,code.data()+i+3,4);auto object=getter+i+7+relative;
        if(code[i+1]==0x8B) Read(object,object);
        Dump("获取器返回对象候选",object,160);break;
    }
    CloseHandle(processHandle);return 0;
}
