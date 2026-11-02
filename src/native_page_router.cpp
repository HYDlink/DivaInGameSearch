#include "native_page_router.h"
#include "customization_item_locator.h"
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
// 跨页事务阶段：PageArrival 等待 expect 页稳定；SiblingBoundary 在中转模型页请求下一兄弟页；LocateReady 后按领域分发定位器。
enum class RoutePhase { Idle, Boundary, ParentArrival, ParentConfirm, PageArrival, SiblingBoundary, LocateReady, Locating, Done };
struct Route {
    RoutePhase phase=RoutePhase::Idle;
    CustomizationLocateRequest request;
    NativePageSnapshot source;
    // target 为网关按最新目录重算的最终目标页；expect 为当前等待到达的下一跳页。
    NativePage target=NativePage::Module,expect=NativePage::Unknown;
    uint64_t cancelled=0,deadline=0;
    unsigned stable=0;
    DWORD thread=0;
    ModuleLocateStatus status;
} route;
uint64_t currentUiGeneration=0,lastSerial=0;
std::string lockedFingerprint;
std::function<std::optional<CustomizationTargetFacts>(const CustomizationKey&,const std::string&)> targetResolver;
std::function<std::optional<CustomizationCharacter>(const NativePageSnapshot&)> singerResolver;
// 参数 s 为当前页面快照；只在自定义页内按来源歌曲与实际槽位解析歌者人物，其他页面返回空。
std::optional<CustomizationCharacter> SingerOf(const NativePageSnapshot& s) {
    if(!singerResolver || !(s.page==NativePage::Parent || s.page==NativePage::Module || s.page==NativePage::Hair || s.page==NativePage::Accessory)) return std::nullopt;
    if(s.slot<0 || s.slotCount<1 || s.slot>=s.slotCount) return std::nullopt;
    return singerResolver(s);
}
// 参数 itemCharacter/singer 为目标实体人物与当前歌者；共通 ALL 对任一歌者兼容，未知歌者不判定兼容。
bool CompatibleWithSinger(CustomizationCharacter itemCharacter,const std::optional<CustomizationCharacter>& singer) {
    return singer && (itemCharacter==CustomizationCharacter::All || itemCharacter==*singer);
}
// 原生导航调用表：生产绑定已核实构建的RVA；隔离测试替换为记录替身，避免跳入任何生产地址。
struct NativeRouteCalls {
    // 参数 mode 为外层请求模式：7 进入自定义父页、2 进入普通选曲（RVA 0x2C4BB0）。
    bool (*outerRequest)(int mode);
    // 参数 pvid/song 为有效来源歌曲与选曲对象：0x1DE750(pvid,null) 受理后 0x6EECD0(song,1)。
    bool (*songCustomize)(int pvid,uintptr_t song);
    // 参数 scene 为空闲父页场景：0x69B040(scene)，调用方随后写父页 +0x1C=2。
    bool (*parentConfirm)(uintptr_t scene);
    // 参数 manager/key 为场景管理器与目标页键：0x6968D0(manager,key)，返回是否受理。
    bool (*requestScene)(uintptr_t manager,int key);
    // 参数 panel 为来源页列表面板：0x682530(panel)。
    bool (*hidePanel)(uintptr_t panel);
};
// 参数 mode 为外层请求模式；在结构化异常保护下调用已核实的外层请求入口。
bool DefaultOuterRequest(int mode) {
    __try {reinterpret_cast<void(__fastcall*)(int)>(imageBase+0x2C4BB0)(mode);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 pvid/song 为来源歌曲编号与选曲对象；先保存有效PVID再进入自定义。
bool DefaultSongCustomize(int pvid,uintptr_t song) {
    __try {
        if(!reinterpret_cast<uint8_t(__fastcall*)(int,void*)>(imageBase+0x1DE750)(pvid,nullptr)) return false;
        reinterpret_cast<void(__fastcall*)(void*,uint8_t)>(imageBase+0x6EECD0)(reinterpret_cast<void*>(song),1);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 scene 为父页场景对象；只用于光标0的“模型”入口确认。
bool DefaultParentConfirm(uintptr_t scene) {
    __try {reinterpret_cast<void(__fastcall*)(void*)>(imageBase+0x69B040)(reinterpret_cast<void*>(scene));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 manager/key 为场景管理器与目标页键；返回原生是否受理。
bool DefaultRequestScene(uintptr_t manager,int key) {
    __try {return reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(imageBase+0x6968D0)(reinterpret_cast<void*>(manager),key)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 panel 为来源页面板对象；隐藏后由原生完成切换。
bool DefaultHidePanel(uintptr_t panel) {
    __try {reinterpret_cast<void(__fastcall*)(void*)>(imageBase+0x682530)(reinterpret_cast<void*>(panel));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
NativeRouteCalls nativeCalls{DefaultOuterRequest,DefaultSongCustomize,DefaultParentConfirm,DefaultRequestScene,DefaultHidePanel};
// 参数 page 为定制子页，返回场景树页键；非子页返回-1。
int PageKey(NativePage page) {
    return page==NativePage::Parent?1:page==NativePage::Module?2:page==NativePage::Hair?3:page==NativePage::Accessory?4:-1;
}
// 参数 page 为目标子页，返回中文页名，用于阶段反馈。
const char* PageName(NativePage page) {
    return page==NativePage::Module?"模型页":page==NativePage::Hair?"发型页":page==NativePage::Accessory?"自定义道具页":page==NativePage::Parent?"自定义父页":"目标页";
}
// 参数 from/target 为兄弟子页；直接同层边取实机链“模型→发型→道具→模型”与既有发型→模型。
// 模型→道具沿用交接报告“父页光标0→模型→目标兄弟页”方案及同一 0x6968D0 协议，但该单边未在实机链中单独记录，待本机确认。
bool DirectSibling(NativePage from,NativePage target) {
    return (from==NativePage::Module && (target==NativePage::Hair || target==NativePage::Accessory))
        || (from==NativePage::Hair && (target==NativePage::Accessory || target==NativePage::Module))
        || (from==NativePage::Accessory && target==NativePage::Module);
}
// 参数 from/target 为当前页与最终目标页；返回下一跳。道具→发型的直接同层边未实测，经模型页中转。
NativePage NextHop(NativePage from,NativePage target) {
    if(from==NativePage::Home || from==NativePage::Song) return NativePage::Parent;
    if(from==NativePage::Parent) return NativePage::Module;
    if(from==target || DirectSibling(from,target)) return target;
    return NativePage::Module;
}
// 参数 s 为当前帧页面快照；转换为发型／道具执行器的上下文，非发型／道具页标为无效。
CustomizationItemFrame ItemFrame(const NativePageSnapshot& s) {
    CustomizationItemFrame frame;
    frame.valid=s.page==NativePage::Hair || s.page==NativePage::Accessory;frame.idle=frame.valid && s.idle;
    frame.domain=s.page==NativePage::Accessory?CustomizationDomain::Accessories:CustomizationDomain::Hair;
    frame.scene=s.scene;frame.state=s.state;frame.controller=s.controller;frame.configuration=s.configuration;
    frame.slot=s.slot;frame.slotCount=s.slotCount;frame.nativeScope=s.nativeScope;frame.sourcePvid=s.sourcePvid;
    frame.singer=SingerOf(s);
    frame.now=GetTickCount64();
    return frame;
}
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
void Advance(RoutePhase phase,const std::string& text) {
    route.phase=phase;route.deadline=GetTickCount64()+12000;route.stable=0;route.status.message=text;
}
// 仅认证空闲来源；父页只开放已实测的模型光标0，不改写玩家的父页选择。参数 target 为最终目标页。
bool SupportedSource(const NativePageSnapshot& s,NativePage target) {
    if(!s.idle || s.page==target) return false;
    return s.page==NativePage::Home || s.page==NativePage::Song || s.page==NativePage::Module
        || s.page==NativePage::Hair || s.page==NativePage::Accessory
        || (s.page==NativePage::Parent && s.parentPhase==1 && s.parentCursor==0);
}
// 参数 s/targetKey 为空闲兄弟子页与目标页键；与F3／Esc自然分支一致：请求目标、隐藏来源面板、置来源完成字节。
bool InvokeSibling(const NativePageSnapshot& s,int targetKey) {
    if(s.key<2 || s.key>4 || targetKey<1 || targetKey>4 || targetKey==s.key || !s.idle) return false;
    const uintptr_t panelOffset=s.key==2?0x310:s.key==3?0x380:0x2E0;
    uintptr_t panel=0;if(!Read(s.scene+panelOffset,panel) || !panel) return false;
    if(!nativeCalls.requestScene(s.manager,targetKey)) return false;
    if(!nativeCalls.hidePanel(panel)) return false;
    return Write(s.scene+(s.key==4?0x29:0x25),uint8_t(1));
}
// 参数 s/next 为匹配真实帧边界后的新鲜页面与下一跳；只调用报告已验证的导航协议。
bool InvokeRoute(const NativePageSnapshot& s,NativePage next) {
    if(s.page==NativePage::Home) return next==NativePage::Parent && nativeCalls.outerRequest(7);
    if(s.page==NativePage::Song) return next==NativePage::Parent && nativeCalls.songCustomize(s.pvid,s.song);
    if(s.page==NativePage::Parent) {
        if(s.parentCursor!=0 || s.parentPhase!=1 || next!=NativePage::Module) return false;
        if(!nativeCalls.parentConfirm(s.scene)) return false;
        return Write(s.scene+0x1C,int32_t(2));
    }
    if(s.page==NativePage::Module || s.page==NativePage::Hair || s.page==NativePage::Accessory) return InvokeSibling(s,PageKey(next));
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
// 参数s/finish为当前已认证来源和退出层级；仅调用实测的自然退出协议（子页→父页1，父页→来源8）。
bool InvokeSongExit(const NativePageSnapshot& s,bool finish) {
    if(finish) {
        if(s.page!=NativePage::Parent || !s.idle || s.parentPhase!=1) return false;
        if(!nativeCalls.requestScene(s.manager,8)) return false;
        return Write(s.controller+0x28,int32_t(1)) && Write(s.scene+0x19,uint8_t(1));
    }
    return InvokeSibling(s,1);
}
// 仅在已经核实的外层主页控制帧请求进入普通选曲，不直接写全局菜单或演奏状态。
bool InvokeEnterSong() {return nativeCalls.outerRequest(2);}
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
// 参数 s 为到达目标页且稳定的快照；按领域分发到模型定位器或发型／道具执行器，返回是否已受理。
bool DispatchArrival(const NativePageSnapshot& s,std::string& message) {
    if(route.target==NativePage::Module) return SubmitModuleLocate(route.request,message);
    auto frame=ItemFrame(s);frame.thread=GetCurrentThreadId();
    // 到达发型页后按实际歌者重验：其他角色的发型只到达页面、不定位，筛选不授予切换歌者的权限。
    if(route.target==NativePage::Hair && frame.singer && !CompatibleWithSinger(route.request.character,frame.singer)) {
        message=std::string("已进入")+PageName(route.target)+"；该发型属于其他角色，仅供浏览。";return false;
    }
    if(SubmitCustomizationItemRequest(route.request,frame,message)) return true;
    // 页面到达不等于定位成功：未认证 Profile 时如实说明停留在原生列表当前位置。
    message=std::string("已进入")+PageName(route.target)+"；"+message;return false;
}
// 参数 boundary/object 是原生帧类型与真实this；每次只在匹配边界提交一步。
void OnBoundary(int boundary,uintptr_t object) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock() || !installed) return;
    OnSongBoundary(boundary,object);
    if(route.phase==RoutePhase::Idle || route.phase==RoutePhase::Done || route.phase==RoutePhase::Locating) return;
    if(route.cancelled!=cancellation.load() || route.request.uiGeneration!=currentUiGeneration) {Finish("查询、目录或面板已变化，旧跨页请求已取消。");return;}
    const auto now=GetTickCount64();
    if(now>route.deadline) {Finish(std::string("进入")+PageName(route.target)+"超时，已停止请求；可在当前页面重新搜索。");return;}
    const auto s=ReadNativePage(imageBase);
    const bool matches=boundary==0 || (boundary==1 && object==s.song) || (boundary==2 && object==s.state);
    if(!matches) return;
    const auto thread=GetCurrentThreadId();
    if(route.thread && route.thread!=thread) {Finish("原生路由线程发生变化，已取消请求。");return;}
    const std::string entering=std::string("正在进入")+PageName(route.target)+"……";
    if(route.phase==RoutePhase::Boundary) {
        const bool correct=(s.page==NativePage::Home && boundary==0) || (s.page==NativePage::Song && boundary==1)
            || ((s.page==NativePage::Parent || s.page==NativePage::Module || s.page==NativePage::Hair || s.page==NativePage::Accessory) && boundary==2);
        if(!correct) return;
        if(!SameSource(route.source,s) || !SupportedSource(s,route.target)) {Finish("提交后原生页面、曲目或配置已改变，旧双击已取消。");return;}
        route.thread=thread;
        const auto next=NextHop(s.page,route.target);
        if(!InvokeRoute(s,next)) {Finish("原生页面请求未受理，已停止后续操作。");return;}
        if(next==NativePage::Parent) {Advance(RoutePhase::ParentArrival,entering);return;}
        route.expect=next;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::ParentArrival) {
        if(s.page==NativePage::Parent && s.idle && s.parentPhase==1) {
            const bool expected=route.source.page==NativePage::Home?s.mode==7
                :s.mode==2 && s.sourcePvid==route.source.pvid;
            if(!expected) {Finish("到达的自定义页与原双击来源不符，已取消旧目标。");return;}
            // 父页直接进入发型／道具的主动协议未独立验收；只走已验证的光标0→模型→兄弟页。
            if(s.parentCursor!=0) {Finish("已进入自定义父页；原生光标不在“模型”，暂不支持从此光标继续进入，请将光标移到模型后再次双击。");return;}
            route.source=s;Advance(RoutePhase::ParentConfirm,entering);
        } else if(s.page==NativePage::Hair || s.page==NativePage::Accessory || s.page==NativePage::Module
            || (s.page==NativePage::Song && (route.source.page!=NativePage::Song || s.pvid!=route.source.pvid))
            || (s.page==NativePage::Home && route.source.page!=NativePage::Home)) Finish("进入自定义期间页面被改变，已停止自动操作。");
        return;
    }
    if(route.phase==RoutePhase::ParentConfirm) {
        if(boundary!=2) return;
        if(!SameSource(route.source,s) || !SupportedSource(s,route.target)) {Finish("自定义父页已改变，已停止进入模型。");return;}
        if(!InvokeRoute(s,NativePage::Module)) {Finish("原生模型确认未受理，已停止。");return;}
        route.expect=NativePage::Module;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::PageArrival) {
        if(s.page==route.expect && s.idle) {
            if(!SameConfiguration(route.source,s)) {Finish(std::string("进入")+PageName(route.target)+"期间曲目、范围或槽位已改变，旧目标已取消。");return;}
            route.source=s;
            if(route.expect==route.target) {Advance(RoutePhase::LocateReady,std::string(PageName(route.target))+"已到达，正在核对目标列表……");return;}
            Advance(RoutePhase::SiblingBoundary,std::string("已到达")+PageName(route.expect)+"，正在进入"+PageName(route.target)+"……");return;
        }
        if(s.page==NativePage::Song || s.page==NativePage::Home) Finish(std::string(PageName(route.target))+"路径被离开，已取消旧定位。");
        return;
    }
    if(route.phase==RoutePhase::SiblingBoundary) {
        if(s.page!=route.expect || !s.idle) {
            if(s.idle && s.page!=route.expect) Finish("中转页已被改变，已停止自动操作。");
            return;
        }
        if(boundary!=2) return;
        if(!SameSource(route.source,s)) {Finish("中转页配置已变化，旧目标已取消。");return;}
        // 中转页需连续两次空闲状态帧，避免在列表初始化帧内请求下一兄弟页。
        if(++route.stable<2) return;
        const auto next=NextHop(s.page,route.target);
        if(!InvokeRoute(s,next)) {Finish("原生同层页面请求未受理，已停止后续操作。");return;}
        route.expect=next;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::LocateReady) {
        if(s.page!=route.target || !s.idle) {
            if(s.page!=route.target) Finish(std::string(PageName(route.target))+"被离开，已取消旧定位。");
            return;
        }
        if(!SameSource(route.source,s)) {Finish(std::string(PageName(route.target))+"配置已变化，旧目标已取消。");return;}
        if(boundary!=0) return;
        if(route.target==NativePage::Module && !ReadModuleLocatorContext()) {route.stable=0;return;}
        if(++route.stable<2) return;
        std::string message;
        if(!DispatchArrival(s,message)) {Finish(message);return;}
        Advance(RoutePhase::Locating,std::string("已进入")+PageName(route.target)+"，正在定位"+(route.target==NativePage::Module?"服装":route.target==NativePage::Hair?"发型":"道具")+"……");
    }
}
// 参数 object 为原生内部状态机；仅当发型／道具执行器有事务时读取页面并转交本帧，不扫描驻留对象。
void ForwardItemFrame(uintptr_t object) {
    if(!installed || !CustomizationItemLocatorBusy()) return;
    const auto s=ReadNativePage(imageBase);
    if(s.state && object!=s.state) return;
    auto frame=ItemFrame(s);frame.thread=GetCurrentThreadId();
    OnCustomizationItemFrame(frame);
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
    ForwardItemFrame(reinterpret_cast<uintptr_t>(object));
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

namespace {
// 参数 domain 为检索领域，返回其原生目标子页。
NativePage TargetPage(CustomizationDomain domain) {
    return domain==CustomizationDomain::Models?NativePage::Module:domain==CustomizationDomain::Hair?NativePage::Hair:NativePage::Accessory;
}
// 参数 request/message 为待核对请求与拒绝原因；按最新目录重算领域、部位与人物，界面传入值不一致即拒绝。
bool ResolveTarget(CustomizationLocateRequest& request,std::string& message) {
    if(targetResolver) {
        const auto facts=targetResolver(request.key,request.catalogFingerprint);
        if(!facts) {message="目标已不在最新定制目录中，或目录已变化；未提交原生操作。";return false;}
        if(facts->domain!=request.domain || (facts->domain!=CustomizationDomain::Models && facts->part!=request.part)) {message="请求领域或部位与最新目录不一致，已拒绝。";return false;}
        request.part=facts->part;request.character=facts->character;
    }
    if(request.key.kind!=CustomizationDomainKind(request.domain)) {message="请求实体种类与领域不一致，已拒绝。";return false;}
    const bool part=request.part==CustomizationPart::Head || request.part==CustomizationPart::Face || request.part==CustomizationPart::Neck || request.part==CustomizationPart::Back;
    if((request.domain==CustomizationDomain::Hair && request.part!=CustomizationPart::Hair) || (request.domain==CustomizationDomain::Accessories && !part)) {message="发型／道具请求缺少有效部位，已拒绝。";return false;}
    return true;
}
std::mutex pageCacheMutex;
NativePageSnapshot cachedPage;
uint64_t cachedAt=0;
// 界面线程的页面缓存：每100毫秒至多读取一次，避免逐张卡片读取游戏内存。
NativePageSnapshot CachedNativePage() {
    std::lock_guard lock(pageCacheMutex);
    const auto now=GetTickCount64();
    if(!cachedAt || now<cachedAt || now-cachedAt>=100) {cachedPage=installed?ReadNativePage(imageBase):NativePageSnapshot{};cachedAt=now;}
    return cachedPage;
}
}

bool SubmitCustomizationRequest(const CustomizationLocateRequest& original,std::string& message) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock()) {message="原生操作正在处理中，请稍候。";return false;}
    if(SongBusy() && songRoute.cancelled==songCancellation.load()) {message="歌曲跨页或定位仍在处理中。";return false;}
    // 网关独立重算目标页，不信任界面传入的页面；领域、部位与人物以最新目录为准。
    auto request=original;
    if(!ResolveTarget(request,message)) return false;
    const bool models=request.domain==CustomizationDomain::Models;
    const auto target=TargetPage(request.domain);
    const auto page=ReadNativePage(imageBase);
    const bool atTarget=page.page==target || (models && !installed && ReadModuleLocatorContext());
    message=CheckCustomizationNavigation(request,!atTarget);
    if(!message.empty()) return false;
    if(previewContinuation.waiting) {message="正在退出旧预览并准备后续操作，请稍候。";return false;}
    // 模型预览中：模型动作或任一领域的双击定位先退出预览，续接后再次经过本网关。
    if((page.page==NativePage::Module || (models && atTarget)) && ModulePreviewCanExit() && (models || request.action==CustomizationAction::Locate)) {
        if(!request.serial || request.key.id<0 || !request.queryRevision
            || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256 || request.uiGeneration!=currentUiGeneration) {message="预览后续目标或目录身份无效。";return false;}
        const auto preview=PollModuleAction();
        if(!RequestModulePreviewExit(request,message)) return false;
        previewContinuation={};previewContinuation.waiting=true;previewContinuation.request=request;previewContinuation.source=page;
        previewContinuation.cancelled=cancellation.load();previewContinuation.previewSerial=preview.serial;previewContinuation.deadline=GetTickCount64()+20000;
        previewContinuation.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,"正在退出旧预览，随后执行所选操作。"};
        previewContinuation.status.action=request.action;route.phase=RoutePhase::Idle;message=previewContinuation.status.message;return true;
    }
    if(ModuleActionBusy()) {message="服装预览或换装仍在处理中，请先完成当前操作。";return false;}
    if(CustomizationItemLocatorBusy()) {message="发型／道具原生操作仍在处理中，请稍候。";return false;}
    if(route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load()) {message="上一条跨页定位仍在处理中。";return false;}
    // 发型：当前歌者已知时其他角色只读，任何入口（双击、Enter、底栏、按钮）都不跨页、不预览、不更换；
    // 歌者未知时仍可浏览与跨页，预览和更换拒绝，到达后再按实际歌者重验。
    if(request.domain==CustomizationDomain::Hair) {
        const auto singer=SingerOf(page);
        if(singer && !CompatibleWithSinger(request.character,singer)) {message="该发型属于其他角色，仅供浏览。";return false;}
        if(!singer && request.action!=CustomizationAction::Locate) {message="未读取到当前歌者，暂不支持此操作。";return false;}
    }
    // 发型／道具已在目标页：交给独立执行器；未知 Profile 明确 Unsupported，不照搬模型命令。
    if(!models && atTarget) {
        auto frame=ItemFrame(page);
        const bool accepted=SubmitCustomizationItemRequest(request,frame,message);
        if(accepted) route.phase=RoutePhase::Idle;
        return accepted;
    }
    if(request.action!=CustomizationAction::Locate) {
        const auto locate=PollModuleLocator();
        if(locate.phase==ModuleLocatePhase::WaitingForFrame || locate.phase==ModuleLocatePhase::SwitchingCategory || locate.phase==ModuleLocatePhase::Confirming) {message="原生定位仍在处理中，请稍候。";return false;}
        const bool accepted=SubmitModuleAction(request,message);
        if(accepted) route.phase=RoutePhase::Idle;
        return accepted;
    }
    if(atTarget) {
        const bool accepted=SubmitModuleLocate(request,message);
        if(accepted) route.phase=RoutePhase::Idle;
        return accepted;
    }
    if(!installed || !SupportedSource(page,target)) {
        message=page.page==NativePage::Parent && page.idle && page.parentCursor!=0
            ?"自定义父页光标不在“模型”；暂不支持从当前父页光标直接进入，请将光标移到模型或进入任一子页后再双击。"
            :"当前页面或过渡状态尚未认证；请进入空闲主页、选曲或自定义列表后重试。";
        return false;
    }
    if(request.key.id<0 || request.serial<=lastSerial || !request.serial
        || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedFingerprint.empty() && lockedFingerprint!=request.catalogFingerprint)) {message="跨页目标身份、序号或目录指纹无效。";return false;}
    lockedFingerprint=request.catalogFingerprint;lastSerial=request.serial;currentUiGeneration=request.uiGeneration;
    route={};route.request=request;route.source=page;route.target=target;route.cancelled=cancellation.load();
    route.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,""};
    route.status.action=request.action;
    // 目标定位未认证时在导航开始前说明缺口：到达不等于定位，更不等于装备。
    std::string text="正在等待原生页面帧……";
    if(!models) {
        const auto capability=CustomizationItemCapabilities(request.domain);
        if(!capability.locate) text=std::string("正在进入")+PageName(target)+"；"+capability.locateReason;
    }
    Advance(RoutePhase::Boundary,text);message=route.status.message;return true;
}

void TickNativePageRouter(uint64_t now,uint64_t generation) {
    TickCustomizationItemLocator(now,generation);
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
    if(now>route.deadline) {
        if(route.target==NativePage::Module) CancelModuleLocate(); else CancelCustomizationItemLocator();
        Finish("原生跨页定位超时，已停止后续动作。");return;
    }
    if(route.phase==RoutePhase::Locating) {
        const auto status=route.target==NativePage::Module?PollModuleLocator():PollCustomizationItemLocator();
        if(status.serial==route.request.serial && status.uiGeneration==generation) {
            route.status=status;if(status.completed) route.phase=RoutePhase::Done;
        }
    }
}

ModuleLocateStatus PollNativePageRouter() {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    const auto action=PollModuleAction(),locate=PollModuleLocator(),item=PollCustomizationItemLocator();
    auto result=action.serial>locate.serial?action:locate;
    if(item.serial>result.serial) result=item;
    if(lock.owns_lock()) {
        if(route.phase!=RoutePhase::Idle && route.status.serial>=result.serial) result=route.status;
        if((previewContinuation.waiting || previewContinuation.status.completed) && previewContinuation.status.serial>=result.serial) result=previewContinuation.status;
    }
    return result;
}

void CancelNativePageRouter() {cancellation.fetch_add(1);CancelModuleLocate();CancelModuleAction();CancelCustomizationItemLocator();CancelSongNavigation();}

CustomizationActionCapability ReadCustomizationActionCapability(const CustomizationKey& key,CustomizationCharacter targetCharacter) {
    auto capability=ReadModuleActionCapability(key,targetCharacter);
    if(!capability.targetPage && key.kind==CustomizationKind::Module && ReadModuleLocatorContext()) capability.targetPage=true;
    capability.locate=capability.targetPage;
    return capability;
}
CustomizationActionCapability ReadCustomizationItemActionCapability(CustomizationDomain domain) {
    auto capability=CustomizationItemCapabilities(domain);
    if(domain==CustomizationDomain::Models) return capability;
    const auto page=CachedNativePage();
    capability.targetPage=page.idle && page.page==TargetPage(domain);
    capability.busy=capability.busy || CustomizationItemLocatorBusy();
    // 实际歌者未知时预览与更换不开放；人物兼容性由界面只读规则与执行器再次核对。
    if(!SingerOf(page)) {capability.preview=capability.equip=false;capability.previewReason=capability.equipReason="未读取到当前歌者，暂不支持此操作。";}
    // 不在目标页时任何能力都不开放；按钮与底栏只提示双击主体进入。
    if(!capability.targetPage) capability.locate=capability.preview=capability.equip=false;
    return capability;
}
NativePage CurrentNativePage() {
    const auto page=CachedNativePage();
    return page.idle?page.page:NativePage::Unknown;
}
void ConfigureCustomizationSingerResolver(std::function<std::optional<CustomizationCharacter>(const NativePageSnapshot&)> resolver) {
    std::lock_guard lock(routeMutex);singerResolver=std::move(resolver);
}
std::optional<CustomizationContext> ReadCustomizationSingerContext() {
    const auto page=CachedNativePage();
    const auto singer=SingerOf(page);
    if(!singer) return std::nullopt;
    // 代数取自控制器、配置、槽位、来源歌曲、范围与人物；任一变化都视为歌者上下文改变。
    uint64_t generation=1469598103934665603ull;
    for(uint64_t value:{uint64_t(page.controller),uint64_t(page.configuration),uint64_t(uint32_t(page.slot)),uint64_t(uint32_t(page.sourcePvid)),uint64_t(uint32_t(page.nativeScope)),uint64_t(*singer)}) {generation^=value;generation*=1099511628211ull;}
    return CustomizationContext{*singer,page.slot,generation?generation:1,true};
}
void ConfigureCustomizationTargetResolver(std::function<std::optional<CustomizationTargetFacts>(const CustomizationKey&,const std::string&)> resolver) {
    std::lock_guard lock(routeMutex);targetResolver=std::move(resolver);
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
    if(ModuleActionBusy() || CustomizationItemLocatorBusy() || (route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load())
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
