#include "native_page_router.h"
#include "module_action.h"
#include "performance_trace.h"
#include <Windows.h>
#include <detours.h>
#include <array>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>

namespace diva {
namespace {
constexpr uintptr_t kOuter=0xCC61078,kTask=0xCC6F100;
constexpr uintptr_t kOuterFrame=0x2C4A40,kSongFrame=0x6EDC40,kStateFrame=0x66B2D0;
constexpr std::array<uintptr_t,5> kSceneTables{0xCC4A80,0xCC5170,0xCC4BB0,0xCC4678,0xCC48C8};
using OuterFrame=uintptr_t(__fastcall*)();
using ObjectFrame=uintptr_t(__fastcall*)(void*);
OuterFrame originalOuter=nullptr;
ObjectFrame originalSong=nullptr,originalState=nullptr;
uintptr_t imageBase=0;
bool installed=false;
std::mutex routeMutex;
std::atomic<uint64_t> cancellation{0};
enum class RoutePhase { Idle, Boundary, ParentArrival, ParentConfirm, ModelArrival, LocateReady, Locating, Done };
struct Route {
    RoutePhase phase=RoutePhase::Idle;
    CustomizationLocateRequest request;
    NativePageSnapshot source;
    uint64_t cancelled=0,deadline=0;
    unsigned stable=0;
    DWORD thread=0;
    ModuleLocateStatus status;
} route;
uint64_t currentUiGeneration=0,lastSerial=0;
std::string lockedFingerprint;
struct PreviewContinuation {
    bool waiting=false;
    CustomizationLocateRequest request;
    NativePageSnapshot source;
    uint64_t cancelled=0,previewSerial=0,deadline=0,readyAfter=0;
    ModuleLocateStatus status;
} previewContinuation;
enum class SongPhase {Idle,PreviewExit,Boundary,ParentArrival,FinishBoundary,OuterArrival,HomeBoundary,SongArrival,Ready,Selecting,Done};
struct SongRoute {
    SongPhase phase=SongPhase::Idle;
    Request request;
    NativePageSnapshot source;
    int returnMode=-1,sourcePvid=-1;
    uint64_t cancelled=0,deadline=0,readyAfter=0,previewSerial=0;
    unsigned stable=0;
    DWORD thread=0;
    SongNavigationStatus status;
} songRoute;
std::atomic<uint64_t> songCancellation{0};
uint64_t songLastSerial=0,songGeneration=0,songRevision=0;
std::function<bool(Request,std::string&)> songSubmit;
std::function<void()> songCancel;
std::function<void(double)> nativePerformance;
// 返回歌曲事务是否仍持有导航或定位权限，供两类搜索共享互斥网关。
bool SongBusy() {return songRoute.phase!=SongPhase::Idle && songRoute.phase!=SongPhase::Done;}

// 参数 address/value 为当前进程内地址与输出，拒绝不可读对象。
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T count=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}
// 参数 address/value 为已核实原生状态字段；仅写当前进程可写提交页。
template<class T> bool Write(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};SIZE_T count=0;
    return address && VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}
// 参数 address 为已核实构建中的候选函数入口，保留其他Mod的已有跳转链。
bool Executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
// 参数 head/node 为树头和目标节点；有界遍历且拒绝循环，不接受堆扫描候选。
bool Contains(uintptr_t head,uintptr_t node) {
    uintptr_t root=0;if(!head || !node || head==node || !Read(head+8,root)) return false;
    std::vector<uintptr_t> pending{root};std::set<uintptr_t> visited;
    while(!pending.empty() && visited.size()<32) {
        const auto current=pending.back();pending.pop_back();
        if(!current || current==head || !visited.insert(current).second) continue;
        if(current==node) return true;
        uintptr_t left=0,right=0;if(!Read(current,left) || !Read(current+0x10,right)) return false;
        pending.push_back(left);pending.push_back(right);
    }
    return false;
}
// 参数 s 为前后帧身份，确保请求前用户没有切页、换槽或换曲。
bool SameSource(const NativePageSnapshot& a,const NativePageSnapshot& b) {
    return a.page==b.page && a.mode==b.mode && a.currentSub==b.currentSub && a.controller==b.controller
        && a.configuration==b.configuration && a.scene==b.scene && a.state==b.state && a.song==b.song && a.pvid==b.pvid
        && a.slot==b.slot && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid;
}
// 参数a/b为同次自定义事务的前后页，允许场景切换但不允许偷偷换曲、范围或歌者槽位。
bool SameConfiguration(const NativePageSnapshot& a,const NativePageSnapshot& b) {
    return a.controller==b.controller && a.configuration==b.configuration && a.mode==b.mode
        && a.slot==b.slot && a.slotCount==b.slotCount && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid;
}
// 参数 text/success 为最终反馈与结果；不在失败后自动恢复或再发导航。
void Finish(const std::string& text,bool success=false) {
    route.phase=RoutePhase::Done;route.status.message=text;route.status.completed=true;route.status.succeeded=success;
    route.status.phase=success?ModuleLocatePhase::Succeeded:ModuleLocatePhase::Rejected;
}
// 参数 phase/text 为预期下一阶段与中文进度，每阶段重新计算超时。
void Advance(RoutePhase phase,const char* text) {
    route.phase=phase;route.deadline=GetTickCount64()+12000;route.stable=0;route.status.message=text;
}
// 仅认证空闲来源；父页只开放已实测的模型光标0，不改写玩家的父页选择。
bool SupportedSource(const NativePageSnapshot& s) {
    if(!s.idle) return false;
    return s.page==NativePage::Home || s.page==NativePage::Song || s.page==NativePage::Module
        || s.page==NativePage::Hair || s.page==NativePage::Accessory
        || (s.page==NativePage::Parent && s.parentPhase==1 && s.parentCursor==0);
}
// 参数 s 为匹配真实帧边界后的新鲜页面；只调用报告已验证的导航协议。
bool InvokeRoute(const NativePageSnapshot& s) {
    __try {
        if(s.page==NativePage::Home) {
            reinterpret_cast<void(__fastcall*)(int)>(imageBase+0x2C4BB0)(7);return true;
        }
        if(s.page==NativePage::Song) {
            if(!reinterpret_cast<uint8_t(__fastcall*)(int,void*)>(imageBase+0x1DE750)(s.pvid,nullptr)) return false;
            reinterpret_cast<void(__fastcall*)(void*,uint8_t)>(imageBase+0x6EECD0)(reinterpret_cast<void*>(s.song),1);return true;
        }
        if(s.page==NativePage::Parent) {
            if(s.parentCursor!=0 || s.parentPhase!=1) return false;
            reinterpret_cast<void(__fastcall*)(void*)>(imageBase+0x69B040)(reinterpret_cast<void*>(s.scene));
            return Write(s.scene+0x1C,int32_t(2));
        }
        if(s.page==NativePage::Hair || s.page==NativePage::Accessory) {
            uintptr_t panel=0;const auto panelOffset=s.key==3?0x380:0x2E0;
            if(!Read(s.scene+panelOffset,panel) || !panel) return false;
            if(!reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(imageBase+0x6968D0)(reinterpret_cast<void*>(s.manager),2)) return false;
            reinterpret_cast<void(__fastcall*)(void*)>(imageBase+0x682530)(reinterpret_cast<void*>(panel));
            return Write(s.scene+(s.key==4?0x29:0x25),uint8_t(1));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return false;
}
// 参数text/success为歌曲事务结束反馈；不把到达选曲页直接记作定位成功。
void FinishSong(const std::string& text,bool success=false) {
    songRoute.phase=SongPhase::Done;songRoute.status.busy=false;songRoute.status.completed=true;
    songRoute.status.succeeded=success;songRoute.status.message=text;
}
// 参数phase/text为下一真实阶段；每阶段有独立超时且不阻塞游戏帧。
void AdvanceSong(SongPhase phase,const char* text) {
    songRoute.phase=phase;songRoute.deadline=GetTickCount64()+12000;songRoute.stable=0;songRoute.status.message=text;
}
// 参数s/finish为当前已认证来源和退出层级；仅调用实测的自然退出协议。
bool InvokeSongExit(const NativePageSnapshot& s,bool finish) {
    __try {
        if(finish) {
            if(s.page!=NativePage::Parent || !s.idle || s.parentPhase!=1) return false;
            if(!reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(imageBase+0x6968D0)(reinterpret_cast<void*>(s.manager),8)) return false;
            return Write(s.controller+0x28,int32_t(1)) && Write(s.scene+0x19,uint8_t(1));
        }
        if(s.key<2 || s.key>4 || !s.idle) return false;
        const auto panelOffset=s.key==2?0x310:s.key==3?0x380:0x2E0;
        uintptr_t panel=0;if(!Read(s.scene+panelOffset,panel) || !panel) return false;
        if(!reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(imageBase+0x6968D0)(reinterpret_cast<void*>(s.manager),1)) return false;
        reinterpret_cast<void(__fastcall*)(void*)>(imageBase+0x682530)(reinterpret_cast<void*>(panel));
        return Write(s.scene+(s.key==4?0x29:0x25),uint8_t(1));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 仅在已经核实的外层主页控制帧请求进入普通选曲，不直接写全局菜单或演奏状态。
bool InvokeEnterSong() {
    __try {reinterpret_cast<void(__fastcall*)(int)>(imageBase+0x2C4BB0)(2);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数boundary/object为真实原生帧边界；歌曲和服装共享Hook、页面读取器与事务互斥。
void OnSongBoundary(int boundary,uintptr_t object) {
    if(!SongBusy() || songRoute.phase==SongPhase::PreviewExit || songRoute.phase==SongPhase::Ready || songRoute.phase==SongPhase::Selecting) return;
    if(songRoute.cancelled!=songCancellation.load()) {FinishSong("旧歌曲跨页请求已取消。");return;}
    if(GetTickCount64()>songRoute.deadline) {FinishSong("进入选曲页超时，已停止旧歌曲请求。");return;}
    const auto s=ReadNativePage(imageBase);
    if(boundary!=0 && !((boundary==1 && object==s.song) || (boundary==2 && object==s.state))) return;
    const auto thread=GetCurrentThreadId();
    if(songRoute.thread && songRoute.thread!=thread) {FinishSong("歌曲原生导航线程已改变，已停止请求。");return;}
    if(songRoute.phase==SongPhase::Boundary || songRoute.phase==SongPhase::HomeBoundary) {
        const bool home=s.page==NativePage::Home;
        if((home && boundary!=0) || (!home && boundary!=2)) return;
        if(!SameSource(songRoute.source,s) || !s.idle) {FinishSong("歌曲请求来源页面、范围或槽位已变化。");return;}
        songRoute.thread=thread;
        if(home) {
            if(!InvokeEnterSong()) {FinishSong("原生选曲入口未受理。");return;}
            AdvanceSong(SongPhase::SongArrival,"正在进入选曲页……");return;
        }
        const bool parent=s.page==NativePage::Parent;
        if(!InvokeSongExit(s,parent)) {FinishSong("原生自定义退出请求未受理，已停止导航。");return;}
        AdvanceSong(parent?SongPhase::OuterArrival:SongPhase::ParentArrival,"正在退出自定义页面……");return;
    }
    if(songRoute.phase==SongPhase::ParentArrival) {
        if(s.page==NativePage::Parent && s.idle && s.parentPhase==1) {
            if(!SameConfiguration(songRoute.source,s)) {FinishSong("返回父页期间曲目、范围或歌者槽位已改变。");return;}
            songRoute.source=s;AdvanceSong(SongPhase::FinishBoundary,"正在返回自定义来源页……");
        } else if(s.idle && (s.page==NativePage::Home || s.page==NativePage::Song)) FinishSong("用户已离开预期父页，旧歌曲请求已停止。");
        return;
    }
    if(songRoute.phase==SongPhase::FinishBoundary) {
        if(boundary!=2) return;
        if(!SameSource(songRoute.source,s) || !InvokeSongExit(s,true)) {FinishSong("父页已变化或原生退出未受理。");return;}
        AdvanceSong(SongPhase::OuterArrival,"正在等待原生自定义退出……");return;
    }
    if(songRoute.phase==SongPhase::OuterArrival) {
        if(s.page==NativePage::Song && s.idle) {
            if(songRoute.returnMode!=2 || s.pvid!=songRoute.sourcePvid) {FinishSong("退出后的选曲来源与原事务不符。");return;}
            songRoute.source=s;AdvanceSong(SongPhase::SongArrival,"已返回选曲，正在等待列表就绪……");
        } else if(s.page==NativePage::Home && s.idle) {
            if(songRoute.returnMode!=7) {FinishSong("退出后的主页来源与原事务不符。");return;}
            songRoute.source=s;AdvanceSong(SongPhase::HomeBoundary,"已返回主页，正在进入选曲……");
        }
        return;
    }
    if(songRoute.phase==SongPhase::SongArrival) {
        if(s.page!=NativePage::Song || !s.idle) return;
        if(!songRoute.stable) songRoute.source=s;
        else if(!SameSource(songRoute.source,s)) {FinishSong("选曲列表就绪期间对象或当前歌曲已改变。");return;}
        if(boundary!=0 || ++songRoute.stable<2) return;
        AdvanceSong(SongPhase::Ready,"选曲页已到达，正在核对歌曲定位上下文……");
        songRoute.readyAfter=GetTickCount64()+250;
    }
}
// 参数 boundary/object 是原生帧类型与真实this；每次只在匹配边界提交一步。
void OnBoundary(int boundary,uintptr_t object) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock() || !installed) return;
    OnSongBoundary(boundary,object);
    if(route.phase==RoutePhase::Idle || route.phase==RoutePhase::Done || route.phase==RoutePhase::Locating) return;
    if(route.cancelled!=cancellation.load() || route.request.uiGeneration!=currentUiGeneration) {Finish("查询、目录或面板已变化，旧跨页请求已取消。");return;}
    const auto now=GetTickCount64();
    if(now>route.deadline) {Finish("进入模型页超时，已停止请求；可在当前页面重新搜索。");return;}
    const auto s=ReadNativePage(imageBase);
    const bool matches=boundary==0 || (boundary==1 && object==s.song) || (boundary==2 && object==s.state);
    if(!matches) return;
    const auto thread=GetCurrentThreadId();
    if(route.thread && route.thread!=thread) {Finish("原生路由线程发生变化，已取消请求。");return;}
    if(route.phase==RoutePhase::Boundary) {
        const bool correct=(s.page==NativePage::Home && boundary==0) || (s.page==NativePage::Song && boundary==1)
            || ((s.page==NativePage::Parent || s.page==NativePage::Hair || s.page==NativePage::Accessory) && boundary==2);
        if(!correct) return;
        if(!SameSource(route.source,s) || !SupportedSource(s)) {Finish("提交后原生页面、曲目或配置已改变，旧双击已取消。");return;}
        route.thread=thread;
        if(!InvokeRoute(s)) {Finish("原生页面请求未受理，已停止后续操作。");return;}
        Advance(s.page==NativePage::Home || s.page==NativePage::Song?RoutePhase::ParentArrival:RoutePhase::ModelArrival,"正在进入模型页……");return;
    }
    if(route.phase==RoutePhase::ParentArrival) {
        if(s.page==NativePage::Parent && s.idle && s.parentPhase==1) {
            const bool expected=route.source.page==NativePage::Home?s.mode==7
                :s.mode==2 && s.sourcePvid==route.source.pvid;
            if(!expected) {Finish("到达的自定义页与原双击来源不符，已取消旧目标。");return;}
            if(s.parentCursor!=0) {Finish("已进入自定义页；请将原生光标移到模型后再次双击。");return;}
            route.source=s;Advance(RoutePhase::ParentConfirm,"正在进入模型页……");
        } else if(s.page==NativePage::Hair || s.page==NativePage::Accessory || s.page==NativePage::Module
            || (s.page==NativePage::Song && (route.source.page!=NativePage::Song || s.pvid!=route.source.pvid))
            || (s.page==NativePage::Home && route.source.page!=NativePage::Home)) Finish("进入自定义期间页面被改变，已停止自动操作。");
        return;
    }
    if(route.phase==RoutePhase::ParentConfirm) {
        if(boundary!=2) return;
        if(!SameSource(route.source,s) || !SupportedSource(s)) {Finish("自定义父页已改变，已停止进入模型。");return;}
        if(!InvokeRoute(s)) {Finish("原生模型确认未受理，已停止。");return;}
        Advance(RoutePhase::ModelArrival,"正在等待原生模型列表……");return;
    }
    if(route.phase==RoutePhase::ModelArrival || route.phase==RoutePhase::LocateReady) {
        if(s.page==NativePage::Module && s.idle) {
            if(!SameConfiguration(route.source,s)) {Finish("进入模型期间曲目、范围或槽位已改变，旧目标已取消。");return;}
            if(route.phase==RoutePhase::ModelArrival) {route.source=s;Advance(RoutePhase::LocateReady,"模型页已到达，正在核对目标列表……");return;}
            if(!SameSource(route.source,s)) {Finish("模型页配置已变化，旧目标已取消。");return;}
            if(boundary!=0) return;
            if(!ReadModuleLocatorContext()) {route.stable=0;return;}
            if(++route.stable<2) return;
            std::string message;
            if(!SubmitModuleLocate(route.request,message)) {Finish(message);return;}
            Advance(RoutePhase::Locating,"已进入模型页，正在定位服装……");
        } else if(route.phase==RoutePhase::LocateReady || s.page==NativePage::Song || s.page==NativePage::Home) Finish("模型页被离开，已取消旧定位。");
    }
}
// 保留原外层控制帧返回值，帧末才消费双击路由。
uintptr_t __fastcall OuterHook() {const auto result=originalOuter();const auto begin=PerformanceNow();TickRecommendations();OnBoundary(0,0);if(nativePerformance)nativePerformance(PerformanceNow()-begin);return result;}
// 参数 object 为真实选曲对象，保留既有Mod调用链。
uintptr_t __fastcall SongHook(void* object) {const auto result=originalSong(object);const auto begin=PerformanceNow();OnBoundary(1,reinterpret_cast<uintptr_t>(object));if(nativePerformance)nativePerformance(PerformanceNow()-begin);return result;}
// 参数 object 为真实内部状态机，先完成原生更新再处理下一步。
uintptr_t __fastcall StateHook(void* object) {
    const auto result=originalState(object);
    const auto begin=PerformanceNow();
    OnModuleActionStateFrame(reinterpret_cast<uintptr_t>(object));
    OnBoundary(2,reinterpret_cast<uintptr_t>(object));if(nativePerformance)nativePerformance(PerformanceNow()-begin);return result;
}
}

NativePageSnapshot ReadNativePage(uintptr_t base) {
    NativePageSnapshot s;if(!base) return s;
    if(!Read(base+kOuter,s.mode) || !Read(base+kOuter+0xC,s.modeProcess) || !Read(base+kOuter+0x10,s.modeSwitch)
        || !Read(base+kOuter+0x1C,s.currentSub) || !Read(base+kOuter+0x20,s.nextSub) || !Read(base+kOuter+0x28,s.subProcess)
        || !Read(base+kOuter+0x2C,s.subSwitch) || !Read(base+kTask+0x78,s.controller) || !Read(base+0xCC5EF18,s.song)) return s;
    const bool outerIdle=s.modeProcess==1 && s.subProcess==1 && !s.modeSwitch && !s.subSwitch;
    if(!s.controller) {
        if(!s.song && s.mode==9 && s.currentSub==42 && s.nextSub==43 && outerIdle) {s.page=NativePage::Home;s.idle=true;}
        int32_t member=0;
        if(s.song && s.mode==2 && s.currentSub==5 && s.nextSub==6 && outerIdle
            && Read(s.song+0x6C,s.songState) && s.songState==6 && Read(base+0x6EFE8C,member)
            && member>=0x9C8 && member<=0x100000 && Read(s.song+member-0x9C8,s.pvid) && s.pvid>0 && s.pvid<10000000) {s.page=NativePage::Song;s.idle=true;}
        return s;
    }
    uintptr_t table=0;
    if(!outerIdle || !((s.mode==2 && s.currentSub==5 && s.nextSub==6) || (s.mode==7 && s.currentSub==36 && s.nextSub==37))
        || !Read(s.controller+0x18,s.configuration) || !s.configuration
        || !Read(s.controller+0x998,s.manager) || !Read(s.manager,table) || table!=base+0xCC4E38
        || !Read(s.manager+8,s.head) || !Read(s.manager+0x18,s.node) || !Contains(s.head,s.node)
        || !Read(s.manager+0x20,s.target) || !Read(s.manager+0x28,s.transition)
        || !Read(s.node+0x20,s.key) || s.key<1 || s.key>4 || !Read(s.node+0x28,s.scene)
        || !Read(s.scene,table) || table!=base+kSceneTables[s.key]) return s;
    uintptr_t sceneController=0;
    if(!Read(s.scene+8,sceneController) || sceneController!=s.controller) return s;
    Read(s.controller+0x38,s.slotCount);Read(s.controller+0x3C,s.slot);
    Read(s.controller+8,s.sourcePvid);Read(base+0x12B5A74,s.nativeScope);
    const uintptr_t stateOffset=s.key==1?0x178:s.key==2?0x330:s.key==3?0x3A0:0x300;
    if(!Read(s.scene+stateOffset,s.state) || !s.state || !Read(s.state+0x18,s.internal)) return s;
    s.page=s.key==1?NativePage::Parent:s.key==2?NativePage::Module:s.key==3?NativePage::Hair:NativePage::Accessory;
    s.idle=s.target==s.head && s.transition==1 && s.internal==0;
    if(s.key==1) {if(!Read(s.scene+0x28,s.parentCursor) || !Read(s.scene+0x1C,s.parentPhase)) s.idle=false;}
    if(s.key==2) {if(!Read(s.scene+0x60,s.popup) || s.popup!=-1) s.idle=false;}
    return s;
}

bool InitializeNativePageRouter(uintptr_t base) {
    std::lock_guard lock(routeMutex);if(installed) return true;
    // 调用者只有在定位器完整核验SHA、PE及签名后才提供基址；拒绝直接绕过初始化。
    if(!base || PollModuleLocator().phase==ModuleLocatePhase::Disabled) return false;
    for(auto rva:{kOuterFrame,kSongFrame,kStateFrame,uintptr_t(0x2C4BB0),uintptr_t(0x1DE750),uintptr_t(0x6EECD0),uintptr_t(0x69B040),uintptr_t(0x6968D0),uintptr_t(0x682530)})
        if(!Executable(base+rva)) return false;
    imageBase=base;originalOuter=reinterpret_cast<OuterFrame>(base+kOuterFrame);
    originalSong=reinterpret_cast<ObjectFrame>(base+kSongFrame);originalState=reinterpret_cast<ObjectFrame>(base+kStateFrame);
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    LONG result=DetourAttach(reinterpret_cast<PVOID*>(&originalOuter),reinterpret_cast<void*>(&OuterHook));
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<PVOID*>(&originalSong),reinterpret_cast<void*>(&SongHook));
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<PVOID*>(&originalState),reinterpret_cast<void*>(&StateHook));
    if(result!=NO_ERROR) {DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR) return false;
    installed=true;InitializeModuleActions(base);return true;
}

bool SubmitCustomizationRequest(const CustomizationLocateRequest& request,std::string& message) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock()) {message="原生操作正在处理中，请稍候。";return false;}
    if(SongBusy() && songRoute.cancelled==songCancellation.load()) {message="歌曲跨页或定位仍在处理中。";return false;}
    const auto page=ReadNativePage(imageBase);
    const bool atModel=page.page==NativePage::Module || (!installed && ReadModuleLocatorContext());
    message=CheckCustomizationNavigation(request,!atModel);
    if(!message.empty()) return false;
    if(previewContinuation.waiting) {message="正在退出旧预览并准备后续操作，请稍候。";return false;}
    if(atModel && ModulePreviewCanExit()) {
        if(!request.serial || request.key.kind!=CustomizationKind::Module || request.key.id<0 || !request.queryRevision
            || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256 || request.uiGeneration!=currentUiGeneration) {message="预览后续目标或目录身份无效。";return false;}
        const auto preview=PollModuleAction();
        if(!RequestModulePreviewExit(request,message)) return false;
        previewContinuation={};previewContinuation.waiting=true;previewContinuation.request=request;previewContinuation.source=page;
        previewContinuation.cancelled=cancellation.load();previewContinuation.previewSerial=preview.serial;previewContinuation.deadline=GetTickCount64()+20000;
        previewContinuation.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,"正在退出旧预览，随后执行所选操作。"};
        previewContinuation.status.action=request.action;route.phase=RoutePhase::Idle;message=previewContinuation.status.message;return true;
    }
    if(ModuleActionBusy()) {message="服装预览或换装仍在处理中，请先完成当前操作。";return false;}
    if(route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load()) {message="上一条跨页定位仍在处理中。";return false;}
    if(request.action!=CustomizationAction::Locate) {
        const auto locate=PollModuleLocator();
        if(locate.phase==ModuleLocatePhase::WaitingForFrame || locate.phase==ModuleLocatePhase::SwitchingCategory || locate.phase==ModuleLocatePhase::Confirming) {message="原生定位仍在处理中，请稍候。";return false;}
        const bool accepted=SubmitModuleAction(request,message);
        if(accepted) route.phase=RoutePhase::Idle;
        return accepted;
    }
    if(atModel) {
        const bool accepted=SubmitModuleLocate(request,message);
        if(accepted) route.phase=RoutePhase::Idle;
        return accepted;
    }
    if(!installed || !SupportedSource(page)) {message="当前页面或过渡状态尚未认证；请进入空闲主页、选曲或自定义列表后重试。";return false;}
    if(request.key.kind!=CustomizationKind::Module || request.key.id<0 || request.serial<=lastSerial || !request.serial
        || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedFingerprint.empty() && lockedFingerprint!=request.catalogFingerprint)) {message="跨页目标身份、序号或目录指纹无效。";return false;}
    lockedFingerprint=request.catalogFingerprint;lastSerial=request.serial;currentUiGeneration=request.uiGeneration;
    route={};route.request=request;route.source=page;route.cancelled=cancellation.load();
    route.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,""};
    Advance(RoutePhase::Boundary,"正在等待原生页面帧……");message=route.status.message;return true;
}

void TickNativePageRouter(uint64_t now,uint64_t generation) {
    std::unique_lock lock(routeMutex,std::try_to_lock);if(!lock.owns_lock()) return;
    currentUiGeneration=generation;
    if(previewContinuation.waiting) {
        auto& pending=previewContinuation;
        // 失败只撤销尚未执行的新意图，已经开始的原生退出继续安全收尾。
        const auto reject=[&](const std::string& reason) {pending.waiting=false;pending.status.completed=true;pending.status.succeeded=false;pending.status.phase=ModuleLocatePhase::Rejected;pending.status.message=reason;};
        if(pending.cancelled!=cancellation.load() || pending.request.uiGeneration!=generation) {reject("后续查询或目录已变化，预览退出后的旧请求已取消。");return;}
        if(now>pending.deadline) {reject("退出旧预览超时，未执行后续操作。");return;}
        const auto previous=PollModuleAction();
        if(previous.serial!=pending.previewSerial) {reject("预览事务身份已变化，未执行后续操作。");return;}
        if(!previous.completed || ModuleActionBusy()) return;
        if(!previous.succeeded) {reject("旧预览未能安全恢复原装备："+previous.message);return;}
        // 原分类与高亮按用户要求保留；等后续原生帧刷新定位器后再派发，帧内不等待。
        if(!pending.readyAfter) {pending.readyAfter=now+20;return;}
        if(now<pending.readyAfter) return;
        const auto page=ReadNativePage(imageBase);
        if(page.page!=NativePage::Module || !page.idle || page.scene!=pending.source.scene || page.state!=pending.source.state || !SameConfiguration(page,pending.source)) {reject("退出预览期间歌曲、槽位、范围或页面已改变。");return;}
        auto request=pending.request;
        if(request.action!=CustomizationAction::Locate) {
            const auto capability=ReadModuleActionCapability(request.key,request.character);
            if(capability.slot!=request.slot || capability.nativeScope!=request.nativeScope || capability.scope!=request.scope) {reject("后续动作槽位或范围不再匹配。");return;}
            // 只更新本次已认证退出产生的上下文代数，保留原请求目标、目录、查询和槽位意图。
            request.contextGeneration=capability.contextGeneration;
        }
        pending.waiting=false;pending.status={};lock.unlock();
        std::string message;const bool accepted=SubmitCustomizationRequest(request,message);
        if(!accepted && lock.try_lock()) {previewContinuation.status={ModuleLocatePhase::Rejected,request.serial,request.uiGeneration,0,true,false,message};previewContinuation.status.action=request.action;}
        return;
    }
    if(route.phase==RoutePhase::Idle || route.phase==RoutePhase::Done) return;
    if(route.cancelled!=cancellation.load() || route.request.uiGeneration!=generation) {Finish("面板、查询或目录已变化，旧跨页定位已取消。");return;}
    if(now>route.deadline) {CancelModuleLocate();Finish("原生跨页定位超时，已停止后续动作。");return;}
    if(route.phase==RoutePhase::Locating) {
        const auto status=PollModuleLocator();
        if(status.serial==route.request.serial && status.uiGeneration==generation) {
            route.status=status;if(status.completed) route.phase=RoutePhase::Done;
        }
    }
}

ModuleLocateStatus PollNativePageRouter() {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    const auto action=PollModuleAction(),locate=PollModuleLocator();
    auto result=action.serial>locate.serial?action:locate;
    if(lock.owns_lock()) {
        if(route.phase!=RoutePhase::Idle && route.status.serial>=result.serial) result=route.status;
        if((previewContinuation.waiting || previewContinuation.status.completed) && previewContinuation.status.serial>=result.serial) result=previewContinuation.status;
    }
    return result;
}

void CancelNativePageRouter() {cancellation.fetch_add(1);CancelModuleLocate();CancelModuleAction();CancelSongNavigation();}

CustomizationActionCapability ReadCustomizationActionCapability(const CustomizationKey& key,CustomizationCharacter targetCharacter) {
    auto capability=ReadModuleActionCapability(key,targetCharacter);
    if(!capability.modelPage && key.kind==CustomizationKind::Module && ReadModuleLocatorContext()) capability.modelPage=true;
    return capability;
}
// 参数submit/cancel只连接既有Selector，原生页面调用继续由共享帧Hook负责。
void ConfigureSongNavigation(std::function<bool(Request,std::string&)> submit,std::function<void()> cancel) {
    std::lock_guard lock(routeMutex);songSubmit=std::move(submit);songCancel=std::move(cancel);
}
// 参数report在安装Hook之前配置，运行中只累计原子计数，不取得UI锁或写日志。
void ConfigureNativePerformance(std::function<void(double)> report) {nativePerformance=std::move(report);}
// 参数request/available/message为稳定歌曲动作；即使外部伪造来源，也须匹配首击身份及当前UI代数。
bool SubmitSongNavigation(const Request& request,bool available,std::string& message) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock()) {message="原生导航正在处理中。";return false;}
    if(!available || request.pvid<=0 || request.difficulty<0 || request.difficulty>4 || request.style<0 || request.style>2) {message="目标歌曲、难度或风格无效。";return false;}
    if(!installed || !songSubmit) {message="歌曲原生跨页尚未初始化。";return false;}
    const auto s=ReadNativePage(imageBase);
    const bool doubleBody=request.origin==SongOrigin::BodyDoubleClick && request.queryRevision
        && request.firstClickPvid==request.pvid && request.firstClickRevision==request.queryRevision;
    const bool difficultyButton=request.origin==SongOrigin::DifficultyButton;
    if(s.page!=NativePage::Song && !doubleBody && !difficultyButton) {
        message="双击同一歌曲名称或点击难度进入选曲页；名称单击和Enter仅原位定位。";return false;
    }
    if(SongBusy() && songRoute.cancelled==songCancellation.load()) {message="上一条歌曲请求仍在处理中。";return false;}
    if(previewContinuation.waiting) {message="服装预览后续操作正在退出准备中。";return false;}
    if(s.page==NativePage::Module && ModulePreviewCanExit()) {
        if(!request.serial || request.serial<=songLastSerial || !request.catalogGeneration || request.catalogGeneration!=songGeneration || request.queryRevision!=songRevision) {message="歌曲目标、查询或目录身份已过期。";return false;}
        const auto preview=PollModuleAction();if(!RequestModulePreviewExitForSong(message)) return false;
        songLastSerial=request.serial;songRoute={};songRoute.request=request;songRoute.source=s;songRoute.previewSerial=preview.serial;
        songRoute.returnMode=s.mode;songRoute.sourcePvid=s.sourcePvid;songRoute.cancelled=songCancellation.load();songRoute.status.serial=request.serial;songRoute.status.busy=true;
        AdvanceSong(SongPhase::PreviewExit,"正在退出服装预览，随后进入所选歌曲……");songRoute.deadline=GetTickCount64()+20000;message=songRoute.status.message;return true;
    }
    const auto locate=PollModuleLocator();
    if(ModuleActionBusy() || (route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load())
        || locate.phase==ModuleLocatePhase::WaitingForFrame || locate.phase==ModuleLocatePhase::SwitchingCategory || locate.phase==ModuleLocatePhase::Confirming) {
        message="服装原生操作仍在处理中，请先完成当前操作。";return false;
    }
    if(!s.idle || !(s.page==NativePage::Home || s.page==NativePage::Song || s.page==NativePage::Module
        || s.page==NativePage::Hair || s.page==NativePage::Accessory || (s.page==NativePage::Parent && s.parentPhase==1))) {
        message="当前原生页面或过渡状态尚未认证，未切换选曲。";return false;
    }
    if(!request.serial || request.serial<=songLastSerial || !request.catalogGeneration
        || request.catalogGeneration!=songGeneration || request.queryRevision!=songRevision) {message="歌曲目标、查询或目录身份已过期。";return false;}
    songLastSerial=request.serial;songRoute={};songRoute.request=request;songRoute.source=s;
    songRoute.returnMode=s.mode;songRoute.sourcePvid=s.sourcePvid;songRoute.cancelled=songCancellation.load();
    songRoute.status.serial=request.serial;songRoute.status.busy=true;
    AdvanceSong(s.page==NativePage::Song?SongPhase::Ready:SongPhase::Boundary,
        s.page==NativePage::Song?"正在提交原位歌曲定位……":"正在等待原生帧进入选曲……");
    message=songRoute.status.message;return true;
}
// 参数now/generation/revision由UI宿主逐帧提供；旧查询不在到达后重新绑定当前选中行。
void TickSongNavigation(uint64_t now,uint64_t generation,uint64_t revision) {
    std::unique_lock lock(routeMutex,std::try_to_lock);if(!lock.owns_lock()) return;
    songGeneration=generation;songRevision=revision;
    if(!SongBusy()) return;
    if(songRoute.cancelled!=songCancellation.load() || songRoute.request.catalogGeneration!=generation || songRoute.request.queryRevision!=revision) {
        if(songRoute.phase==SongPhase::Selecting && songCancel) songCancel();
        FinishSong("歌曲查询、目录或面板已变化，旧请求已取消。");return;
    }
    if(now>songRoute.deadline) {
        if(songRoute.phase==SongPhase::Selecting && songCancel) songCancel();
        FinishSong("歌曲跨页或定位超时，已停止后续操作。");return;
    }
    if(songRoute.phase==SongPhase::PreviewExit) {
        const auto previous=PollModuleAction();
        if(previous.serial!=songRoute.previewSerial) {FinishSong("原预览事务已改变，取消旧歌曲请求。");return;}
        if(!previous.completed || ModuleActionBusy()) return;
        if(!previous.succeeded) {FinishSong("退出旧预览未能核对原装备，未切换歌曲。");return;}
        if(!songRoute.readyAfter) {songRoute.readyAfter=now+20;return;}
        if(now<songRoute.readyAfter) return;
        const auto page=ReadNativePage(imageBase);
        if(page.page!=NativePage::Module || !page.idle || page.scene!=songRoute.source.scene || !SameConfiguration(page,songRoute.source)) {FinishSong("退出预览期间曲目、槽位或范围改变，未切换歌曲。");return;}
        songRoute.source=page;AdvanceSong(SongPhase::Boundary,"旧预览已安全退出，正在进入所选歌曲……");return;
    }
    if(songRoute.phase!=SongPhase::Ready || now<songRoute.readyAfter) return;
    const auto current=ReadNativePage(imageBase);
    if(current.page!=NativePage::Song || !current.idle || !SameSource(songRoute.source,current)) {FinishSong("提交歌曲定位前原生页面或当前歌曲已变化。");return;}
    std::string message;
    if(!songSubmit || !songSubmit(songRoute.request,message)) {FinishSong(message.empty()?"原位歌曲定位未受理。":message);return;}
    AdvanceSong(SongPhase::Selecting,"已进入选曲，正在独立核对PVID、难度和风格……");
}
// 参数success/message必须来自既有Selector的真实读回，导航层不伪造定位成功。
void CompleteSongNavigation(bool success,const std::string& message) {
    std::lock_guard lock(routeMutex);if(songRoute.phase==SongPhase::Selecting) FinishSong(message,success);
}
// 只发布取消代数，避免输入消息线程调用原生导航或反向改歌。
void CancelSongNavigation() {songCancellation.fetch_add(1);}
// 返回上一条歌曲事务反馈，渲染线程不阻塞等待导航线程。
SongNavigationStatus PollSongNavigation() {
    std::unique_lock lock(routeMutex,std::try_to_lock);return lock.owns_lock()?songRoute.status:SongNavigationStatus{};
}
}
