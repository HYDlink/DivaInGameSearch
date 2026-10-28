#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <cstdint>
#include <cstring>
#include "selector.h"

static HANDLE processHandle=nullptr;static uintptr_t base=0,selection=0;
struct State { int current=-1,next=-1,command=-1,pvid=-1,difficulty=-1,cached=-1,cachedDifficulty=-1,mode=-1; };
// 只读已知地址；参数 address/value 为地址与输出值。
template<class T> static bool Read(uintptr_t address,T& value) { SIZE_T done=0;return ReadProcessMemory(processHandle,(void*)address,&value,sizeof(value),&done) && done==sizeof(value); }
// 仅写已知选曲控制与缓存字段；参数 rva/value 为经过构建核查的相对地址和值。
static bool Write(uintptr_t rva,int value) { SIZE_T done=0;return WriteProcessMemory(processHandle,(void*)(base+rva),&value,sizeof(value),&done) && done==sizeof(value); }
// 读取独立菜单 PVID、实际难度和请求缓存。
static State Snapshot() {
    State s;Read(base+0xCC61094,s.current);Read(base+0xCC61098,s.next);Read(base+0xCC610A0,s.command);
    Read(base+0x12B6350,s.cached);Read(base+0x12B634C,s.cachedDifficulty);
    uintptr_t object=0;int member=0,extra=0;uint64_t mode=0;
    if(Read(base+0xCC5EF18,object) && object && Read(base+0x6EFE8C,member) && member>=0x9C8 && member<=0x100000) Read(object+member-0x9C8,s.pvid);
    if(selection && Read(selection,s.difficulty) && Read(selection+4,extra) && Read(selection+0x18,mode)) { if(extra) s.difficulty=4;s.mode=(int)mode; }
    return s;
}
// 打印带毫秒轨迹；参数 label/state/start 为阶段、实测状态和计时起点。
static void Trace(const char* label,const State& s,uint64_t start) {
    std::cout<<"经过 "<<GetTickCount64()-start<<" 毫秒，"<<label<<"：菜单="<<s.current<<"/"<<s.next<<"，控制="<<s.command<<"，高亮="<<s.pvid<<"，实际难度="<<s.difficulty<<"，缓存="<<s.cached<<"/"<<s.cachedDifficulty<<"，模式="<<s.mode<<"\n"<<std::flush;
}
// 确认两次实测轨迹是否相同；参数 a/b 为相邻采样。
static bool Same(const State& a,const State& b) { return a.current==b.current && a.next==b.next && a.command==b.command && a.pvid==b.pvid && a.difficulty==b.difficulty && a.cached==b.cached && a.cachedDifficulty==b.cachedDifficulty; }
// 让真实进程复用 DLL 的同一套状态机，避免另写一份模拟协议。
class RemoteMemory:public diva::Memory {
    State previous;uint64_t changedAt=0;
public:
    // 读取经构建检查的真实字段，并按持续观测判定稳定性。
    diva::Snapshot Read() override {
        auto s=Snapshot();auto now=GetTickCount64();if(!changedAt || !Same(previous,s)) changedAt=now;previous=s;
        diva::Snapshot value{true,s.current,s.next,s.command,s.cached,s.cachedDifficulty,0,s.pvid,s.difficulty,s.mode};
        value.stable=now-changedAt>=150;return value;
    }
    // 仅向已确认状态发出菜单请求；参数 next 为 5 或 6。
    bool Transition(int next) override {
        auto s=Snapshot();if((s.current!=5 && s.current!=6) || (next!=5 && next!=6)) return false;
        return Write(0xCC61098,next) && Write(0xCC610A0,2);
    }
    // 只测试当前街机风格；参数 request 为已在曲库确认可用的目标。
    bool Select(const diva::Request& request) override {
        auto s=Snapshot();if(request.style!=0 || (s.current!=5 && s.current!=6)) return false;
        return Write(0x12B634C,request.difficulty) && Write(0x12B6350,request.pvid) && Write(0x12B6354,1) && Write(0x12B635C,19);
    }
};
// 受控单次协议验证，不发送确认键、不调用演奏；参数依次为进程号、目标 PVID、目标难度、预期初始高亮。
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if(argc!=5 && argc!=6) { std::cout<<"用法：ControlledSwitch.exe 进程编号 目标PVID 难度编号 预期初始PVID [--state-machine]\n";return 2; }
    DWORD pid=wcstoul(argv[1],nullptr,10);int target=_wtoi(argv[2]),difficulty=_wtoi(argv[3]),expected=_wtoi(argv[4]);
    if(target<=0 || difficulty<0 || difficulty>4) return 2;
    processHandle=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION,FALSE,pid);
    if(!processHandle) { std::cout<<"无法打开受控测试进程。\n";return 1; }
    auto modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);MODULEENTRY32W module{};module.dwSize=sizeof(module);
    if(!Module32FirstW(modules,&module) || _wcsicmp(module.szModule,L"DivaMegaMix.exe")) { std::cout<<"主模块不是目标游戏。\n";return 1; }
    base=(uintptr_t)module.modBaseAddr;CloseHandle(modules);
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!Read(base,dos) || !Read(base+dos.e_lfanew,nt) || nt.FileHeader.TimeDateStamp!=1666920875 || nt.OptionalHeader.SizeOfImage!=602353664) { std::cout<<"构建核查失败，没有写入。\n";return 1; }
    int displacement=0;Read(base+0x6EFCF0+0x1D1,displacement);auto getter=base+0x6EFCF0+0x1D5+displacement;unsigned char code[8]{};
    if(!Read(getter,code) || memcmp(code,"\x48\x8D\x05",3) || code[7]!=0xC3) { std::cout<<"只读获取器形式不匹配，没有写入。\n";return 1; }
    memcpy(&displacement,code+3,4);selection=getter+7+displacement;
    auto initial=Snapshot();auto start=GetTickCount64();Trace("初始",initial,start);
    if(initial.current!=5 || initial.next!=6 || initial.command!=1 || initial.pvid!=expected || initial.difficulty<0 || (initial.mode!=0 && initial.mode!=1)) { std::cout<<"当前状态与截图不一致，没有写入。\n";return 1; }
    int oldSort=0,oldFilter=0;Read(base+0x12B6354,oldSort);Read(base+0x12B635C,oldFilter);
    std::cout<<"单次目标："<<target<<"，难度 "<<difficulty<<"；不更改风格，不开始演奏。\n";
    if(argc==6) {
        if(wcscmp(argv[5],L"--state-machine")) return 2;
        RemoteMemory memory;memory.Read();Sleep(200);diva::Selector selector;
        if(!selector.Submit({target,difficulty,0,1},true,memory.Read(),GetTickCount64())) { std::cout<<selector.message<<"\n";return 1; }
        std::string message;State previous=initial;
        while(selector.Busy()) {
            selector.Tick(memory,GetTickCount64());auto s=Snapshot();
            if(selector.message!=message || !Same(s,previous)) { Trace(selector.message.c_str(),s,start);message=selector.message;previous=s; }
            Sleep(10);
        }
        std::cout<<"同一状态机验证结果："<<(selector.succeeded?"实际定位成功":"未确认成功")<<"\n";CloseHandle(processHandle);return selector.succeeded?0:1;
    }
    if(!Write(0xCC61098,6) || !Write(0xCC610A0,2)) return 1;
    State previous=initial;bool submitted=false;uint64_t matchedSince=0;
    while(GetTickCount64()-start<6000) {
        auto s=Snapshot();if(!Same(s,previous)) { Trace(submitted?"返回与读回":"准备与观察",s,start);previous=s; }
        if(!submitted && GetTickCount64()-start>=100 && s.current==6 && s.next==7 && s.command==1) {
            // 日志已证实 6/7 是协议中间的播放列表，必须完成返回步骤，不能在此错误取消。
            if(!Write(0x12B634C,difficulty) || !Write(0x12B6350,target) || !Write(0x12B6354,1) || !Write(0x12B635C,19) || !Write(0xCC61098,5) || !Write(0xCC610A0,2)) break;
            submitted=true;Trace("已写目标并请求返回普通选曲",Snapshot(),start);
        }
        if(submitted && s.current==5 && s.next==6 && s.command==1 && s.pvid==target && s.difficulty==difficulty && s.cached==target && s.cachedDifficulty==difficulty) {
            if(!matchedSince) matchedSince=GetTickCount64();
            if(GetTickCount64()-matchedSince>=200) { Trace("单次协议读回成功",s,start);CloseHandle(processHandle);return 0; }
        } else matchedSince=0;
        if(s.current!=5 && s.current!=6) { std::cout<<"出现未允许状态，停止后续目标写入。\n";break; }
        Sleep(10);
    }
    auto s=Snapshot();Trace("测试未确认成功",s,start);
    // 仅在已知播放列表中恢复原缓存并返回，避免再把用户留在错误界面。
    if(s.current==6 && s.command==1) {
        Write(0x12B634C,initial.cachedDifficulty);Write(0x12B6350,initial.cached);Write(0x12B6354,oldSort);Write(0x12B635C,oldFilter);
        Write(0xCC61098,5);Write(0xCC610A0,2);std::cout<<"已恢复原缓存并请求返回普通选曲，未启动演奏。\n";
    }
    CloseHandle(processHandle);return 1;
}
