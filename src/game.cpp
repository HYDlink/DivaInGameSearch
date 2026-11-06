#include "i18n.h"
#include "game.h"
#include "host.h"
#include "native_layout.h"
#include <detours.h>
#include <Psapi.h>
#include <vector>
#include <cstring>

namespace diva {
using namespace native;
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
    if(!ReadAt(object+pv_selector::kSelPvData,vector) || !vector || !ReadAt(vector,begin) || !ReadAt(vector+8,end) || end<begin || (end-begin)%8 || (end-begin)/8>100000) return -1;
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
    if(!ReadAt(base,dos) || !ReadAt(base+dos.e_lfanew,nt) || nt.FileHeader.TimeDateStamp!=build::kTimestamp || nt.OptionalHeader.SizeOfImage!=build::kImageSize) {
        diagnostic=Tr(Tx::game_build_unknown); return;
    }
    // 允许前一 Mod 已安装 detour：核对固定入口之后未被补丁覆盖的原函数尾部。
    const unsigned char signature[]={0x4C,0x8B,0x05,0,0,0,0,0x4D,0x85,0xC0,0x74,0x35,0x4C,0x63,0xC9,0x48,0xB8,0,0,0,0,0,0,0,0,0x49,0x8B,0x48,0x08,0x49,0x2B,0x08,0x48,0xF7,0xE9};
    // 本机既有歌词诊断确认入口为 0x1402AC970，参考源码的预设 0x1402AC960 不能直接照搬。
    auto entry=base+rva::kGetInputState;
    unsigned char tail[sizeof(signature)]{};
    SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)entry,tail,sizeof(tail),&read) || read!=sizeof(tail) || memcmp(tail+25,signature+25,10)!=0) {
        diagnostic=Tr(Tx::game_input_signature); return;
    }
    originalInput=(InputFn)entry;
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    auto attach=DetourAttach((PVOID*)&originalInput,FilterInput);
    auto commit=DetourTransactionCommit();
    if(attach!=NO_ERROR || commit!=NO_ERROR) { originalInput=nullptr; diagnostic=Tr(Tx::game_input_failed); return; }
    inputReady=true;
    int pvid=0;
    if(!ReadAt(base+rva::kLastSelectPvId,pvid)) { diagnostic=Tr(Tx::game_song_unreadable); return; }
    // Eden 的数据偏移仅在该构建原槽为零、偏移槽确实含有效 PVID 时启用。
    if(pvid==0) {
        int candidate=0;
        if(ReadAt(base+rva::kLastSelectPvId+kEdenDataOffset,candidate) && candidate>0 && candidate<10000000) dataOffset=kEdenDataOffset;
    }
    auto nc=GetModuleHandleW(L"NewClassics.dll");
    if(nc) {
        const unsigned char pattern[]={0x74,0x0B,0x8B,0x88,0,0,0,0,0x83,0xF9,0x03,0x75,0x02,0x33,0xC9,0x89,0x0D,0,0,0,0,0x40,0x0F,0xB6,0xC7,0x48,0x8B};
        auto hit=Find(nc,pattern,"xxxx????xxxxxxxxx????xxxxxx"); int displacement=0;
        if(!hit || !ReadAt(hit+17,displacement)) { diagnostic=Tr(Tx::game_nc_signature); return; }
        styleAddress=hit+21+displacement; int style=-1;
        if(!ReadAt(styleAddress,style) || style<0 || style>2) { diagnostic=Tr(Tx::game_nc_data); return; }
        unsigned char load[3]{};
        if(!ReadAt(hit-10,load) || memcmp(load,"\x48\x8B\x05",3) || !ReadAt(hit-7,displacement) || !ReadAt(hit+4,preferredStyleOffset) || preferredStyleOffset<8 || preferredStyleOffset>0x4000) {
            diagnostic=Tr(Tx::game_nc_pattern);return;
        }
        styleWindowAddress=hit-3+displacement;
    }
    // 复用歌词项目已在本机使用的 applySel 相对调用，避免沿用其他构建的字段偏移指令地址。
    auto apply=base+rva::kPvSelectorApplySel;unsigned char prefix[19]{};unsigned char opcode=0;int callOffset=0;
    SIZE_T prefixRead=0;
    if(!ReadProcessMemory(GetCurrentProcess(),(void*)apply,prefix,sizeof(prefix),&prefixRead) || prefixRead!=sizeof(prefix)
        || memcmp(prefix,"\x40\x57\x48\x81\xEC",5)!=0 || memcmp(prefix+16,"\x48\x8B\xF9",3)!=0
        || !ReadAt(apply+0x1D0,opcode) || opcode!=0xE8 || !ReadAt(apply+0x1D1,callOffset)) {
        diagnostic=Tr(Tx::game_getter_signature);return;
    }
    selectorGetter=apply+0x1D5+callOffset;
    MEMORY_BASIC_INFORMATION getterPage{};
    if(selectorGetter<base || selectorGetter>=base+nt.OptionalHeader.SizeOfImage || !VirtualQuery((void*)selectorGetter,&getterPage,sizeof(getterPage))
        || !(getterPage.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) { diagnostic=Tr(Tx::game_getter_page);return; }
    selectorObject=ReadSelectorObject(selectorGetter);
    supported=true; diagnostic="已核对本机 PC 构建、输入隔离和实际选曲对象，支持普通歌曲选择界面。";
}
// 参数image/output为通过构建校验的主程序基址和固定32字节输出，调用与DML相同的只读语言目录获取器。
static bool ReadLanguageDirectory(uintptr_t image,char* output) {
    __try {
        const unsigned char expected[]={0xE8,0xF6,0xF4,0x08,0x00};
        if(std::memcmp(reinterpret_cast<const void*>(image+rva::kGetLanguageDirectoryCall),expected,sizeof(expected))) return false;
        const char* directory=reinterpret_cast<const char*(__fastcall*)()>(image+rva::kGetLanguageDirectory)();
        if(!directory) return false;
        for(size_t n=0;n<32;++n) {output[n]=directory[n];if(!output[n]) return true;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return false;
}
// 原生目录形如lang2/cn；限定现有游戏语言集合，未知值返回空。
std::string GameMemory::CurrentLanguage() const {
    char directory[32]{};if(!supported || !base || !ReadLanguageDirectory(base,directory)) return {};
    const std::string path(directory);const auto slash=path.find_last_of('/');if(slash==std::string::npos) return {};
    const auto language=path.substr(slash+1);
    for(const auto* valid:{"jp","en","cn","tw","kr","fr","ge","it","sp"}) if(language==valid) return language;
    return {};
}
// 只读复用直接定位的页面身份守卫，避免把仍驻留的选曲对象误作当前选曲页。
bool GameMemory::IsSongSearchPage() {
    const auto snapshot=Read();uintptr_t object=0;int state=-1,pvid=-1;
    if(snapshot.playlistSong) return true;
    return snapshot.supported && snapshot.subCurrent==ModeSub::PvSel && snapshot.subNext==ModeSub::PlaylistSel && snapshot.subProc==ModeProc::Ctrl
        && (snapshot.mode==0 || snapshot.mode==1) && snapshot.visiblePvid>0
        && ReadAt(base+rva::kPvSelectorSwitch,object) && object && ReadAt(object+pv_selector::kState,state) && state==pv_selector::kStateIdle
        && ReadAt(object+pv_selector::kPvId,pvid) && pvid==snapshot.visiblePvid;
}
// 获取独立字段和真实选曲对象中的 PVID。
Snapshot GameMemory::Read() {
    Snapshot s; if(!supported) return s;
    s.supported=true;
    bool ok=ReadAt(base+mode_info::kSubCurrent,s.subCurrent) && ReadAt(base+mode_info::kSubNext,s.subNext) && ReadAt(base+mode_info::kSubProc,s.subProc)
        && ReadAt(base+rva::kLastSelectPvId+dataOffset,s.pvid) && ReadAt(base+rva::kLastSelectDifficulty+dataOffset,s.difficulty);
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
        actualDiff=s.visibleDifficulty;s.visibleDifficulty=actualExtra?kRequestExtraExtreme:actualDiff;s.mode=(int)mode;
        if(actualDiff<0 || actualDiff>PvDifficulty::Extreme || actualExtra<0 || actualExtra>PvEdition::Extra) s.visibleDifficulty=-1;
    } else { s.visiblePvid=-1;s.visibleDifficulty=-1;s.mode=-1; }
    // 01:26:58 高亮爱言叶读到 48，01:29:24 切困难后仍为 48；演奏对象 PVID 在选曲中是 -1，不能复用。
    uintptr_t menuObject=0;int member=0;
    if(ReadAt(base+rva::kPvSelectorSwitch,menuObject) && menuObject && ReadAt(base+rva::kVisiblePvidMember,member) && member>=pv_selector::kVisiblePvidBias && member<=pv_selector::kVisiblePvidMemberMax) {
        if(!ReadAt(menuObject+(intptr_t)member-pv_selector::kVisiblePvidBias,s.visiblePvid) || s.visiblePvid<=0 || s.visiblePvid>=10000000) s.visiblePvid=-1;
    }
    int menuState=-1,taskPvid=-1;
    s.playlistSong=s.subCurrent==ModeSub::PlaylistSel && s.subNext==ModeSub::Game && s.subProc==ModeProc::Ctrl && menuObject && s.visiblePvid>0
        && ReadAt(menuObject+pv_selector::kState,menuState) && menuState==pv_selector::kStateIdle && ReadAt(menuObject+pv_selector::kPvId,taskPvid)
        && taskPvid==s.visiblePvid && FindNativeIndex(menuObject,taskPvid)>=0;
    int nativeTarget=-1;
    s.playlistHighlightMatched=s.playlistSong && ReadAt(menuObject+pv_selector::kSelTargetIndex,nativeTarget)
        && nativeTarget==FindNativeIndex(menuObject,taskPvid);
    s.supported=ok && s.subCurrent>=0 && s.subCurrent<=64 && s.subNext>=0 && s.subNext<=64 && s.style>=0 && s.style<=2;
    auto now=GetTickCount64();
    bool unchanged=s.subCurrent==previous.subCurrent && s.subNext==previous.subNext && s.subProc==previous.subProc
        && s.visiblePvid==previous.visiblePvid && s.visibleDifficulty==previous.visibleDifficulty && s.mode==previous.mode && s.playlistSong==previous.playlistSong;
    if(!unchanged || !stableSince) stableSince=now;
    s.stable=s.supported && unchanged && now-stableSince>=150;
    previous=s;
    return s;
}
// 按已验证菜单协议请求离开当前子状态；参数 next 为 ModeSub::PvSel 或 ModeSub::PlaylistSel。
bool GameMemory::Transition(int next) {
    if(!supported || (next!=ModeSub::PvSel && next!=ModeSub::PlaylistSel)) return false;
    auto snapshot=Read(); if(!snapshot.supported || (snapshot.subCurrent!=ModeSub::PvSel && snapshot.subCurrent!=ModeSub::PlaylistSel)) return false;
    return WriteAt(base+mode_info::kSubNext,next) && WriteAt(base+mode_info::kSubProc,ModeProc::Dest);
}
// 按协议写入难度、PVID、风格和全曲筛选；参数 request 为已验证动作。
bool GameMemory::Select(const Request& request) {
    if(!supported || (!styleAddress && request.style!=0)) return false;
    auto s=Read(); if(!s.supported || (s.subCurrent!=ModeSub::PvSel && s.subCurrent!=ModeSub::PlaylistSel)) return false;
    return WriteAt(base+rva::kLastSelectDifficulty+dataOffset,request.difficulty) && WriteAt(base+rva::kLastSelectPvId+dataOffset,request.pvid)
        && (!styleAddress || WriteAt(styleAddress,request.style)) && WriteAt(base+rva::kLastSelectSortMode+dataOffset,1) && WriteAt(base+rva::kLastSelectSortIndex+dataOffset,19);
}
// 只要此构建通过启动核查，就坚持原界面路径；失败不会自动把用户送到播放列表。
bool GameMemory::SupportsDirect() const { return true; }
// 按 Switch QuickJump 的调用语义配合 PC New Classics 的真实入口，原位重建与高亮；参数 request 为目标。
bool GameMemory::DirectSelect(const Request& request) {
    if(!supported || request.pvid<=0 || request.difficulty<0 || request.difficulty>kRequestExtraExtreme || request.style<0 || request.style>2) return false;
    auto snapshot=Read();uintptr_t object=0;int internalState=-1,mode=-1,diff=-1,edition=-1,pvid=-1;
    // 播放列表选曲不改变难度、风格和播放列表条目，只在当前生效向量中定位。
    if(snapshot.playlistSong) {
        if(!snapshot.stable || !ReadAt(base+rva::kPvSelectorSwitch,object) || !object) return false;
        int index=FindNativeIndex(object,request.pvid);
        if(index<0) {
            // 仅对实机确认的曲名排序切回全曲，其他排序保持原筛选并拒绝未知目标。
            int sort=-1;if(!ReadAt(object+pv_selector::kSortMode,sort) || sort!=0) return false;
            const uintptr_t folder=object+pv_selector::kSortIndices;
            if(!WriteAt(folder,10) || !WriteValue(object+pv_selector::kCurSortIndex,folder) || !WriteAt(object+pv_selector::kSortChangeDir,1)
                || !RefreshNativeList(base+rva::kChangeSortFilter,object)) return false;
            index=FindNativeIndex(object,request.pvid);
        }
        if(index<0 || !WriteAt(object+pv_selector::kPvId,request.pvid)) return false;
        if(!RefreshNativeList(base+rva::kChangeSortFilter,object)) return false;
        index=FindNativeIndex(object,request.pvid);
        return index>=0 && HighlightNativeList(base+rva::kPvListSetSelectedIndex,object+pv_selector::kSelPvList,index);
    }
    if(!snapshot.supported || snapshot.subCurrent!=ModeSub::PvSel || snapshot.subNext!=ModeSub::PlaylistSel || snapshot.subProc!=ModeProc::Ctrl || !ReadAt(base+rva::kPvSelectorSwitch,object) || !object
        || !ReadAt(object+pv_selector::kState,internalState) || internalState!=pv_selector::kStateIdle || !ReadAt(object+pv_selector::kSortMode,mode) || mode<0 || mode>3
        || !ReadAt(object+pv_selector::kDifficulty,diff) || diff<0 || diff>PvDifficulty::Extreme || !ReadAt(object+pv_selector::kEdition,edition) || edition<0 || edition>PvEdition::Extra
        || !ReadAt(object+pv_selector::kPvId,pvid) || pvid!=snapshot.visiblePvid) { diagnostic="PC 原生选曲对象未通过一致性检查。";return false; }
    // PC 函数来自公开 New Classics 源码，既有模块的 Detour 链照常保留。
    const uintptr_t functions[]={base+rva::kCreateSortedPvList,base+rva::kChangeSortFilter,base+rva::kPvListSetSelectedIndex};
    for(auto function:functions) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery((void*)function,&info,sizeof(info)) || info.State!=MEM_COMMIT || !(info.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    }
    uintptr_t styleWindow=0;
    if(styleWindowAddress) {
        if(!ReadAt(styleWindowAddress,styleWindow) || !styleWindow || !WriteAt(styleWindow+preferredStyleOffset,request.style)) return false;
    } else if(request.style!=0) return false;
    int targetDiff=request.difficulty==kRequestExtraExtreme?PvDifficulty::Extreme:request.difficulty,targetEdition=request.difficulty==kRequestExtraExtreme?PvEdition::Extra:PvEdition::Original;
    const int allFolders[]={10,19,6,5};int all=allFolders[mode];
    uintptr_t currentVector=object+pv_selector::kSortedPvLists+(8*mode+2*targetDiff+targetEdition)*pv_selector::kSortedPvListStride;
    uintptr_t begin=0,end=0;
    if(!ReadAt(currentVector,begin) || !ReadAt(currentVector+8,end) || end<begin || (end-begin)%pv_selector::kPvDataSize || (end-begin)/pv_selector::kPvDataSize>100000) return false;
    if(!WriteAt(object+pv_selector::kDifficulty,targetDiff) || !WriteAt(object+pv_selector::kEdition,targetEdition)
        || !WriteValue(object+pv_selector::kCurPvList,currentVector) || !WriteAt(object+pv_selector::kSortIndices+mode*4,all)
        || !WriteValue(object+pv_selector::kCurSortIndex,object+pv_selector::kSortIndices+mode*4) || !WriteAt(object+pv_selector::kSortChangeDir,1)) return false;
    if(!CreateNativeList(functions[0],object)) { diagnostic="原生歌曲列表重建失败。";return false; }
    int index=FindNativeIndex(object,request.pvid);
    if(index<0) { diagnostic="目标歌曲不在该风格与难度的实际生效列表中。";return false; }
    if(!WriteAt(object+pv_selector::kPvId,request.pvid) || !WriteAt(base+rva::kLastSelectDifficulty+dataOffset,request.difficulty)
        || !WriteAt(base+rva::kLastSelectPvId+dataOffset,request.pvid) || !WriteAt(base+rva::kLastSelectSortMode+dataOffset,mode)
        || !WriteAt(base+rva::kLastSelectSortIndex+dataOffset,all) || (styleAddress && !WriteAt(styleAddress,request.style))) return false;
    if(!RefreshNativeList(functions[1],object)) return false;
    index=FindNativeIndex(object,request.pvid);
    if(index<0 || !HighlightNativeList(functions[2],object+pv_selector::kSelPvList,index)) return false;
    if(styleWindow) WriteValue<unsigned char>(styleWindow+preferredStyleOffset+6,1);
    diagnostic="已调用 PC 原生列表重建、筛选刷新与高亮入口，没有请求切换顶层菜单。";return true;
}
// 返回通过现有游戏构建核查的动态基址；不向未支持构建暴露可写内存入口。
uintptr_t GameMemory::ExecutableBase() const { return supported?base:0; }
}
