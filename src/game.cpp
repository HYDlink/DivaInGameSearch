#include "game.h"
#include "host.h"
#include <detours.h>
#include <Psapi.h>
#include <vector>
#include <cstring>

namespace diva {
using InputFn=void*(__fastcall*)(int);
static InputFn originalInput=nullptr;
// 在原有输入链之后返回空玩家状态；参数 player 为游戏请求玩家编号。
static void* __fastcall FilterInput(int player) {
    if(App().captures.load(std::memory_order_relaxed)) return originalInput(1);
    return originalInput(player);
}
// 只读访问已提交内存；参数 address/value 为地址与输出。
template<class T> static bool ReadAt(uintptr_t address,T& value) {
    SIZE_T done=0; return ReadProcessMemory(GetCurrentProcess(),(void*)address,&value,sizeof(T),&done) && done==sizeof(T);
}
// 写入已经确认可写的数据页；参数 address/value 为目标与数值。
template<class T> static bool WriteValue(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery((void*)address,&info,sizeof(info)) || info.State!=MEM_COMMIT || info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) return false;
    if(!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    SIZE_T done=0; return WriteProcessMemory(GetCurrentProcess(),(void*)address,&value,sizeof(value),&done) && done==sizeof(value);
}
// 写入经过页保护核查的整数字段；参数 address/value 为目标和值。
static bool WriteAt(uintptr_t address,int value) { return WriteValue(address,value); }
// 调用已按 PC 上游与构建核对的列表重建函数；参数 address/object 为入口和实际任务。
static bool CreateNativeList(uintptr_t address,uintptr_t object) {
    __try { return ((bool(__fastcall*)(void*))address)((void*)object); } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 调用原生筛选刷新入口；参数 address/object 为入口和实际任务。
static bool RefreshNativeList(uintptr_t address,uintptr_t object) {
    __try { ((void(__fastcall*)(void*,int))address)((void*)object,0);return true; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 让原生列表滚动到目标下标；参数 address/list/index 为入口、列表及已搜索出的下标。
static bool HighlightNativeList(uintptr_t address,uintptr_t list,int index) {
    __try { ((void(__fastcall*)(void*,int,int))address)((void*)list,index,0);return true; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// 从 PC 实际可见向量查找逻辑 PVID；参数 object/pvid 为真实任务与目标编号。
static int FindNativeIndex(uintptr_t object,int pvid) {
    uintptr_t vector=0,begin=0,end=0;
    if(!ReadAt(object+0x25A20,vector) || !vector || !ReadAt(vector,begin) || !ReadAt(vector+8,end) || end<begin || (end-begin)%8 || (end-begin)/8>100000) return -1;
    for(size_t i=0;i<(end-begin)/8;++i) {
        uintptr_t pv=0,data=0,entry=0;int id=-1;
        if(ReadAt(begin+i*8,pv) && pv && ReadAt(pv,data) && data && ReadAt(data,entry) && entry && ReadAt(entry,id) && id==pvid) return (int)i;
    }
    return -1;
}
// 调用已核对 PC 入口的只读选曲对象获取器；参数 address 为无参获取函数。
static uintptr_t ReadSelectorObject(uintptr_t address) {
    __try { return ((uintptr_t(__fastcall*)())address)(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// 扫描模块可执行节并要求唯一命中；参数 module/pattern/mask 为模块与模式。
static uintptr_t Find(HMODULE module,const unsigned char* pattern,const char* mask) {
    if(!module) return 0;
    auto base=(uintptr_t)module; IMAGE_DOS_HEADER dos{}; if(!ReadAt(base,dos)) return 0;
    IMAGE_NT_HEADERS64 nt{}; if(!ReadAt(base+dos.e_lfanew,nt)) return 0;
    auto section=base+dos.e_lfanew+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
    uintptr_t found=0; size_t n=strlen(mask);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER s{}; if(!ReadAt(section+i*sizeof(s),s) || !(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        auto begin=base+s.VirtualAddress,end=begin+s.Misc.VirtualSize;
        for(auto p=begin;p+n<=end;) {
            MEMORY_BASIC_INFORMATION info{}; if(!VirtualQuery((void*)p,&info,sizeof(info))) break;
            auto limit=std::min(end,(uintptr_t)info.BaseAddress+info.RegionSize);
            if(info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && info.Protect!=PAGE_EXECUTE) {
                for(;p+n<=limit;++p) {
                    bool match=true; for(size_t j=0;j<n;++j) if(mask[j]=='x' && *(unsigned char*)(p+j)!=pattern[j]) { match=false; break; }
                    if(match) { if(found) return 0; found=p; }
                }
            }
            p=limit;
        }
    } return found;
}
// 取得本进程适配器。
GameMemory& Game() { static GameMemory* memory=new GameMemory; return *memory; }
// 核查本机已记录构建，安装链式输入 Hook，并解析 New Classics 风格地址。
void GameMemory::Initialize() {
    supported=false;inputReady=false; base=(uintptr_t)GetModuleHandleW(nullptr);
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
    if(fs::path(path).filename()!=L"DivaMegaMix.exe") { inputReady=true;diagnostic="当前是验证宿主，已禁用游戏内存访问。"; return; }
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!ReadAt(base,dos) || !ReadAt(base+dos.e_lfanew,nt) || nt.FileHeader.TimeDateStamp!=1666920875 || nt.OptionalHeader.SizeOfImage!=602353664) {
        diagnostic="游戏构建不在已核查名单，已禁用写入。"; return;
    }
    // 允许前一 Mod 已安装 detour：核对固定入口之后未被补丁覆盖的原函数尾部。
    const unsigned char signature[]={0x4C,0x8B,0x05,0,0,0,0,0x4D,0x85,0xC0,0x74,0x35,0x4C,0x63,0xC9,0x48,0xB8,0,0,0,0,0,0,0,0,0x49,0x8B,0x48,0x08,0x49,0x2B,0x08,0x48,0xF7,0xE9};
    // 本机既有歌词诊断确认入口为 0x1402AC970，参考源码的预设 0x1402AC960 不能直接照搬。
    auto entry=base+0x2AC970;
    unsigned char tail[sizeof(signature)]{};
    SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)entry,tail,sizeof(tail),&read) || read!=sizeof(tail) || memcmp(tail+25,signature+25,10)!=0) {
        diagnostic="游戏输入签名不匹配，禁止安装与定位。"; return;
    }
    originalInput=(InputFn)entry;
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    auto attach=DetourAttach((PVOID*)&originalInput,FilterInput);
    auto commit=DetourTransactionCommit();
    if(attach!=NO_ERROR || commit!=NO_ERROR) { originalInput=nullptr; diagnostic="输入隔离安装失败，已禁止定位。"; return; }
    inputReady=true;
    int pvid=0;
    if(!ReadAt(base+0x12B6350,pvid)) { diagnostic="选曲数据不可读，已禁止定位。"; return; }
    // Eden 的数据偏移仅在该构建原槽为零、偏移槽确实含有效 PVID 时启用。
    if(pvid==0) {
        int candidate=0;
        if(ReadAt(base+0x12B6350+0x105F460,candidate) && candidate>0 && candidate<10000000) dataOffset=0x105F460;
    }
    auto nc=GetModuleHandleW(L"NewClassics.dll");
    if(nc) {
        const unsigned char pattern[]={0x74,0x0B,0x8B,0x88,0,0,0,0,0x83,0xF9,0x03,0x75,0x02,0x33,0xC9,0x89,0x0D,0,0,0,0,0x40,0x0F,0xB6,0xC7,0x48,0x8B};
        auto hit=Find(nc,pattern,"xxxx????xxxxxxxxx????xxxxxx"); int displacement=0;
        if(!hit || !ReadAt(hit+17,displacement)) { diagnostic="New Classics 风格签名不匹配，已禁止定位。"; return; }
        styleAddress=hit+21+displacement; int style=-1;
        if(!ReadAt(styleAddress,style) || style<0 || style>2) { diagnostic="New Classics 风格数据异常，已禁止定位。"; return; }
        unsigned char load[3]{};
        if(!ReadAt(hit-10,load) || memcmp(load,"\x48\x8B\x05",3) || !ReadAt(hit-7,displacement) || !ReadAt(hit+4,preferredStyleOffset) || preferredStyleOffset<8 || preferredStyleOffset>0x4000) {
            diagnostic="New Classics 原生风格对象模式不匹配，已禁止定位。";return;
        }
        styleWindowAddress=hit-3+displacement;
    }
    // 复用歌词项目已在本机使用的 applySel 相对调用，避免沿用其他构建的字段偏移指令地址。
    auto apply=base+0x6EFCF0;unsigned char prefix[19]{};unsigned char opcode=0;int callOffset=0;
    SIZE_T prefixRead=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)apply,prefix,sizeof(prefix),&prefixRead) || prefixRead!=sizeof(prefix)
        || memcmp(prefix,"\x40\x57\x48\x81\xEC",5)!=0 || memcmp(prefix+16,"\x48\x8B\xF9",3)!=0
        || !ReadAt(apply+0x1D0,opcode) || opcode!=0xE8 || !ReadAt(apply+0x1D1,callOffset)) {
        diagnostic="实际选曲对象获取签名不匹配，已禁止定位。";return;
    }
    selectorGetter=apply+0x1D5+callOffset;
    MEMORY_BASIC_INFORMATION getterPage{};
    if(selectorGetter<base || selectorGetter>=base+nt.OptionalHeader.SizeOfImage || !VirtualQuery((void*)selectorGetter,&getterPage,sizeof(getterPage))
        || !(getterPage.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) { diagnostic="选曲获取器不在有效代码页，已禁止定位。";return; }
    selectorObject=ReadSelectorObject(selectorGetter);
    supported=true; diagnostic="已核对本机 PC 构建、输入隔离和实际选曲对象，支持普通歌曲选择界面。";
}
// 获取独立字段和真实选曲对象中的 PVID。
Snapshot GameMemory::Read() {
    Snapshot s; if(!supported) return s;
    s.supported=true;
    bool ok=ReadAt(base+0xCC61094,s.current) && ReadAt(base+0xCC61098,s.next) && ReadAt(base+0xCC610A0,s.changing)
        && ReadAt(base+0x12B6350+dataOffset,s.pvid) && ReadAt(base+0x12B634C+dataOffset,s.difficulty);
    s.style=0; if(styleAddress) ok=ok && ReadAt(styleAddress,s.style);
    if(styleWindowAddress) {
        uintptr_t styleWindow=0;int count=0,index=-1;unsigned char option=0;
        if(ReadAt(styleWindowAddress,styleWindow) && styleWindow && ReadAt(styleWindow,count) && count>0 && count<=4
            && ReadAt(styleWindow+preferredStyleOffset-8,index) && index>=0 && index<count && ReadAt(styleWindow+4+index,option) && option<=2) s.style=option;
    }
    if(!selectorObject && selectorGetter) selectorObject=ReadSelectorObject(selectorGetter);
    int actualDiff=-1,actualExtra=-1;uint64_t mode=0;
    if(selectorObject && ReadAt(selectorObject,s.visibleDifficulty) && ReadAt(selectorObject+4,actualExtra)
        && ReadAt(selectorObject+0x18,mode)) {
        actualDiff=s.visibleDifficulty;s.visibleDifficulty=actualExtra?4:actualDiff;s.mode=(int)mode;
        if(actualDiff<0 || actualDiff>3 || actualExtra<0 || actualExtra>1) s.visibleDifficulty=-1;
    } else { s.visiblePvid=-1;s.visibleDifficulty=-1;s.mode=-1; }
    // 01:26:58 高亮爱言叶读到 48，01:29:24 切困难后仍为 48；演奏对象 PVID 在选曲中是 -1，不能复用。
    uintptr_t menuObject=0;int member=0;
    if(ReadAt(base+0xCC5EF18,menuObject) && menuObject && ReadAt(base+0x6EFE8C,member) && member>=0x9C8 && member<=0x100000) {
        if(!ReadAt(menuObject+(intptr_t)member-0x9C8,s.visiblePvid) || s.visiblePvid<=0 || s.visiblePvid>=10000000) s.visiblePvid=-1;
    }
    s.supported=ok && s.current>=0 && s.current<=64 && s.next>=0 && s.next<=64 && s.style>=0 && s.style<=2;
    auto now=GetTickCount64();
    bool unchanged=s.current==previous.current && s.next==previous.next && s.changing==previous.changing
        && s.visiblePvid==previous.visiblePvid && s.visibleDifficulty==previous.visibleDifficulty && s.mode==previous.mode;
    if(!unchanged || !stableSince) stableSince=now;
    s.stable=s.supported && unchanged && now-stableSince>=150;
    previous=s;
    return s;
}
// 按已验证菜单协议设置控制标记；参数 next 为 5 或 6。
bool GameMemory::Transition(int next) {
    if(!supported || (next!=5 && next!=6)) return false;
    auto snapshot=Read(); if(!snapshot.supported || (snapshot.current!=5 && snapshot.current!=6)) return false;
    return WriteAt(base+0xCC61098,next) && WriteAt(base+0xCC610A0,2);
}
// 按协议写入难度、PVID、风格和全曲筛选；参数 request 为已验证动作。
bool GameMemory::Select(const Request& request) {
    if(!supported || (!styleAddress && request.style!=0)) return false;
    auto s=Read(); if(!s.supported || (s.current!=5 && s.current!=6)) return false;
    return WriteAt(base+0x12B634C+dataOffset,request.difficulty) && WriteAt(base+0x12B6350+dataOffset,request.pvid)
        && (!styleAddress || WriteAt(styleAddress,request.style)) && WriteAt(base+0x12B6354+dataOffset,1) && WriteAt(base+0x12B635C+dataOffset,19);
}
// 只要此构建通过启动核查，就坚持原界面路径；失败不会自动把用户送到播放列表。
bool GameMemory::SupportsDirect() const { return true; }
// 按 Switch QuickJump 的调用语义配合 PC New Classics 的真实入口，原位重建与高亮；参数 request 为目标。
bool GameMemory::DirectSelect(const Request& request) {
    if(!supported || request.pvid<=0 || request.difficulty<0 || request.difficulty>4 || request.style<0 || request.style>2) return false;
    auto snapshot=Read();uintptr_t object=0;int internalState=-1,mode=-1,diff=-1,edition=-1,pvid=-1;
    if(!snapshot.supported || snapshot.current!=5 || snapshot.next!=6 || snapshot.changing!=1 || !ReadAt(base+0xCC5EF18,object) || !object
        || !ReadAt(object+0x6C,internalState) || internalState!=6 || !ReadAt(object+0x262C0,mode) || mode<0 || mode>3
        || !ReadAt(object+0x262C8,diff) || diff<0 || diff>3 || !ReadAt(object+0x262CC,edition) || edition<0 || edition>1
        || !ReadAt(object+0x262A4,pvid) || pvid!=snapshot.visiblePvid) { diagnostic="PC 原生选曲对象未通过一致性检查。";return false; }
    // PC 函数来自公开 New Classics 源码，既有模块的 Detour 链照常保留。
    const uintptr_t functions[]={base+0x6F3100,base+0x6F2F40,base+0x217500};
    for(auto function:functions) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery((void*)function,&info,sizeof(info)) || info.State!=MEM_COMMIT || !(info.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    }
    uintptr_t styleWindow=0;
    if(styleWindowAddress) {
        if(!ReadAt(styleWindowAddress,styleWindow) || !styleWindow || !WriteAt(styleWindow+preferredStyleOffset,request.style)) return false;
    } else if(request.style!=0) return false;
    int targetDiff=request.difficulty==4?3:request.difficulty,targetEdition=request.difficulty==4?1:0;
    const int allFolders[]={10,19,6,5};int all=allFolders[mode];
    uintptr_t currentVector=object+0x25EE8+(8*mode+2*targetDiff+targetEdition)*0x18;
    uintptr_t begin=0,end=0;
    if(!ReadAt(currentVector,begin) || !ReadAt(currentVector+8,end) || end<begin || (end-begin)%0x48 || (end-begin)/0x48>100000) return false;
    if(!WriteAt(object+0x262C8,targetDiff) || !WriteAt(object+0x262CC,targetEdition)
        || !WriteValue(object+0x261E8,currentVector) || !WriteAt(object+0x262AC+mode*4,all)
        || !WriteValue(object+0x26240,object+0x262AC+mode*4) || !WriteAt(object+0x262BC,1)) return false;
    if(!CreateNativeList(functions[0],object)) { diagnostic="原生歌曲列表重建失败。";return false; }
    int index=FindNativeIndex(object,request.pvid);
    if(index<0) { diagnostic="目标歌曲不在该风格与难度的实际生效列表中。";return false; }
    if(!WriteAt(object+0x262A4,request.pvid) || !WriteAt(base+0x12B634C+dataOffset,request.difficulty)
        || !WriteAt(base+0x12B6350+dataOffset,request.pvid) || !WriteAt(base+0x12B6354+dataOffset,mode)
        || !WriteAt(base+0x12B635C+dataOffset,all) || (styleAddress && !WriteAt(styleAddress,request.style))) return false;
    if(!RefreshNativeList(functions[1],object)) return false;
    index=FindNativeIndex(object,request.pvid);
    if(index<0 || !HighlightNativeList(functions[2],object+0x25930,index)) return false;
    if(styleWindow) WriteValue<unsigned char>(styleWindow+preferredStyleOffset+6,1);
    diagnostic="已调用 PC 原生列表重建、筛选刷新与高亮入口，没有请求切换顶层菜单。";return true;
}
}
