#include "i18n.h"
#include "native_page_router.h"
#include "customization_item_locator.h"
#include "customization_item_profiles.h"
#include "module_action.h"
#include "native_memory.h"
#include "performance_trace.h"
#include <Windows.h>
#include <detours.h>
#include <array>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>

namespace diva {
using namespace native;
namespace {
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
// 模块 ID 到人物的只读快照：由界面线程在目录更新时发布，游戏帧线程只读取，避免跨线程访问界面对象。
std::mutex modelCharacterMutex;
std::shared_ptr<const std::unordered_map<int,CustomizationCharacter>> modelCharacters;
// 参数 s 为当前页面快照；只在自定义页内按实际装备模型解析歌者人物，模型未知时才退回 PV 演出角色。
std::optional<CustomizationCharacter> SingerOf(const NativePageSnapshot& s) {
    if(!(s.page==NativePage::Parent || s.page==NativePage::Module || s.page==NativePage::Hair || s.page==NativePage::Accessory)) return std::nullopt;
    if(s.slot<0 || s.slotCount<1 || s.slot>=s.slotCount) return std::nullopt;
    // 用户换过模型、或播放列表槽位与 PV 演出角色不同时，发型与道具列表跟随当前槽位实际装备的模型。
    if(s.model>=0) {
        std::shared_ptr<const std::unordered_map<int,CustomizationCharacter>> characters;
        {std::lock_guard lock(modelCharacterMutex);characters=modelCharacters;}
        if(characters) {const auto found=characters->find(s.model);if(found!=characters->end()) return found->second;}
    }
    if(!singerResolver) return std::nullopt;
    return singerResolver(s);
}
// 参数 itemCharacter/singer 为目标实体人物与当前歌者；共通 ALL 对任一歌者兼容，未知歌者不判定兼容。
bool CompatibleWithSinger(CustomizationCharacter itemCharacter,const std::optional<CustomizationCharacter>& singer) {
    return singer && (itemCharacter==CustomizationCharacter::All || itemCharacter==*singer);
}
// 原生导航调用表：生产绑定已核实构建的RVA；隔离测试替换为记录替身，避免跳入任何生产地址。
struct NativeRouteCalls {
    // 参数 mode 为外层请求模式：Mode::Customize 进入自定义父页、Mode::Game 进入普通选曲（rva::kRequestMode）。
    bool (*outerRequest)(int mode);
    // 参数 pvid/song 为有效来源歌曲与选曲对象：kActivateCustomize(pvid,null) 受理后 kHideSelector(song,1)。
    bool (*songCustomize)(int pvid,uintptr_t song);
    // 参数 scene 为空闲父页场景：kTopMenuConfirm(scene)，调用方随后写父页 +0x1C=2。
    bool (*parentConfirm)(uintptr_t scene);
    // 参数 manager/key 为场景管理器与目标页键：kSceneManagerRequest(manager,key)，返回是否受理。
    bool (*requestScene)(uintptr_t manager,int key);
    // 参数 panel 为来源页列表面板：kScenePanelHide(panel)。
    bool (*hidePanel)(uintptr_t panel);
};
// 参数 mode 为外层请求模式；在结构化异常保护下调用已核实的外层请求入口。
bool DefaultOuterRequest(int mode) {
    __try {reinterpret_cast<void(__fastcall*)(int)>(imageBase+rva::kRequestMode)(mode);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 pvid/song 为来源歌曲编号与选曲对象；先保存有效PVID再进入自定义。
bool DefaultSongCustomize(int pvid,uintptr_t song) {
    __try {
        if(!reinterpret_cast<uint8_t(__fastcall*)(int,void*)>(imageBase+rva::kActivateCustomize)(pvid,nullptr)) return false;
        reinterpret_cast<void(__fastcall*)(void*,uint8_t)>(imageBase+rva::kHideSelector)(reinterpret_cast<void*>(song),1);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 scene 为父页场景对象；只用于光标0的“模型”入口确认。
bool DefaultParentConfirm(uintptr_t scene) {
    __try {reinterpret_cast<void(__fastcall*)(void*)>(imageBase+rva::kTopMenuConfirm)(reinterpret_cast<void*>(scene));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 manager/key 为场景管理器与目标页键；返回原生是否受理。
bool DefaultRequestScene(uintptr_t manager,int key) {
    __try {return reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(imageBase+rva::kSceneManagerRequest)(reinterpret_cast<void*>(manager),key)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 panel 为来源页面板对象；隐藏后由原生完成切换。
bool DefaultHidePanel(uintptr_t panel) {
    __try {reinterpret_cast<void(__fastcall*)(void*)>(imageBase+rva::kScenePanelHide)(reinterpret_cast<void*>(panel));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
NativeRouteCalls nativeCalls{DefaultOuterRequest,DefaultSongCustomize,DefaultParentConfirm,DefaultRequestScene,DefaultHidePanel};
// 参数 page 为定制子页，返回场景树页键；非子页返回-1。
int PageKey(NativePage page) {
    return page==NativePage::Parent?SceneKey::TopMenu:page==NativePage::Module?SceneKey::Module:page==NativePage::Hair?SceneKey::Hairstyle:page==NativePage::Accessory?SceneKey::Item:-1;
}
// 参数 page 为目标子页，返回中文页名，用于阶段反馈。
const char* PageName(NativePage page) {
    return Tr(page==NativePage::Module?Tx::page_module:page==NativePage::Hair?Tx::page_hair:page==NativePage::Accessory?Tx::page_accessory:page==NativePage::Parent?Tx::route_page_parent:Tx::route_page_target);
}
// 参数 from/target 为兄弟子页；直接同层边取实机链“模型→发型→道具→模型”与既有发型→模型。
// 模型→道具沿用交接报告“父页光标0→模型→目标兄弟页”方案及同一 kSceneManagerRequest 协议，但该单边未在实机链中单独记录，待本机确认。
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
    // 播放列表来源把独立记录放在 controller+0x10；事务据此绑定同一外层子状态，离开列表即取消。
    frame.playlist=IsPlaylistCustomization(s);frame.mode=s.mode;frame.subCurrent=s.subCurrent;frame.subNext=s.subNext;
    frame.singer=SingerOf(s);
    frame.now=GetTickCount64();
    return frame;
}
struct PreviewContinuation {
    bool waiting=false,itemPreview=false;
    CustomizationLocateRequest request;
    NativePageSnapshot source;
    uint64_t cancelled=0,previewSerial=0,deadline=0,readyAfter=0;
    ModuleLocateStatus status;
} previewContinuation;
enum class SongPhase {Idle,PreviewExit,Boundary,ParentArrival,FinishBoundary,OuterArrival,HomeBoundary,SongArrival,Ready,Selecting,Done};
struct SongRoute {
    SongPhase phase=SongPhase::Idle;
    bool itemPreview=false;
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

// 参数 head/node 为树头和目标节点；有界遍历且拒绝循环，不接受堆扫描候选。
bool Contains(uintptr_t head,uintptr_t node) {
    uintptr_t root=0;if(!head || !node || head==node || !ReadAt(head+scene_manager::kNodeParent,root)) return false;
    std::vector<uintptr_t> pending{root};std::set<uintptr_t> visited;
    while(!pending.empty() && visited.size()<32) {
        const auto current=pending.back();pending.pop_back();
        if(!current || current==head || !visited.insert(current).second) continue;
        if(current==node) return true;
        uintptr_t left=0,right=0;if(!ReadAt(current+scene_manager::kNodeLeft,left) || !ReadAt(current+scene_manager::kNodeRight,right)) return false;
        pending.push_back(left);pending.push_back(right);
    }
    return false;
}
// 参数 s 为前后帧身份，确保请求前用户没有切页、换槽或换曲。
bool SameSource(const NativePageSnapshot& a,const NativePageSnapshot& b) {
    return a.page==b.page && a.mode==b.mode && a.subCurrent==b.subCurrent && a.controller==b.controller
        && a.configuration==b.configuration && a.scene==b.scene && a.state==b.state && a.song==b.song && a.pvid==b.pvid
        && a.slot==b.slot && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid;
}
// 参数a/b为同次自定义事务的前后页，允许场景切换但不允许偷偷换曲、范围或歌者槽位。
bool SameConfiguration(const NativePageSnapshot& a,const NativePageSnapshot& b) {
    return a.controller==b.controller && a.configuration==b.configuration && a.mode==b.mode
        && a.subCurrent==b.subCurrent && a.subNext==b.subNext
        && a.slot==b.slot && a.slotCount==b.slotCount && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid;
}
// 参数 s 为已核查页面；识别播放列表来源的自定义上下文。
bool PlaylistCustomization(const NativePageSnapshot& s) {
    return s.controller && s.mode==Mode::Game && s.subCurrent==ModeSub::PlaylistSel && s.subNext==ModeSub::Game;
}// 参数 text/success 为最终反馈与结果；不在失败后自动恢复或再发导航。
void Finish(const std::string& text,bool success=false,CustomizationStage stage=CustomizationStage::None,const std::string& detail={}) {
    route.phase=RoutePhase::Done;route.status.message=text;route.status.completed=true;route.status.succeeded=success;
    route.status.phase=success?ModuleLocatePhase::Succeeded:ModuleLocatePhase::Rejected;
    route.status.stage=stage;route.status.detail=detail;
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
    if(s.key<SceneKey::Module || s.key>SceneKey::Item || targetKey<SceneKey::TopMenu || targetKey>SceneKey::Item || targetKey==s.key || !s.idle) return false;
    const uintptr_t panelOffset=s.key==SceneKey::Module?0x310:s.key==SceneKey::Hairstyle?0x380:0x2E0;
    uintptr_t panel=0;if(!ReadAt(s.scene+panelOffset,panel) || !panel) return false;
    if(!nativeCalls.requestScene(s.manager,targetKey)) return false;
    if(!nativeCalls.hidePanel(panel)) return false;
    return WriteAt(s.scene+(s.key==SceneKey::Item?0x29:0x25),uint8_t(1));
}
// 参数 s/next 为匹配真实帧边界后的新鲜页面与下一跳；只调用报告已验证的导航协议。
bool InvokeRoute(const NativePageSnapshot& s,NativePage next) {
    if(s.page==NativePage::Home) return next==NativePage::Parent && nativeCalls.outerRequest(Mode::Customize);
    if(s.page==NativePage::Song) return next==NativePage::Parent && nativeCalls.songCustomize(s.pvid,s.song);
    if(s.page==NativePage::Parent) {
        if(s.parentCursor!=0 || s.parentPhase!=1 || next!=NativePage::Module) return false;
        if(!nativeCalls.parentConfirm(s.scene)) return false;
        return WriteAt(s.scene+0x1C,int32_t(2));
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
        if(!nativeCalls.requestScene(s.manager,SceneKey::Finish)) return false;
        return WriteAt(s.controller+0x28,int32_t(1)) && WriteAt(s.scene+0x19,uint8_t(1));
    }
    return InvokeSibling(s,SceneKey::TopMenu);
}
// 仅在已经核实的外层主页控制帧请求进入普通选曲，不直接写全局菜单或演奏状态。
bool InvokeEnterSong() {return nativeCalls.outerRequest(Mode::Game);}
// 参数boundary/object为真实原生帧边界；歌曲和服装共享Hook、页面读取器与事务互斥。
void OnSongBoundary(int boundary,uintptr_t object) {
    if(!SongBusy() || songRoute.phase==SongPhase::PreviewExit || songRoute.phase==SongPhase::Ready || songRoute.phase==SongPhase::Selecting) return;
    if(songRoute.cancelled!=songCancellation.load()) {FinishSong(Tr(Tx::songroute_cancelled));return;}
    if(GetTickCount64()>songRoute.deadline) {FinishSong(Tr(Tx::songroute_enter_timeout));return;}
    const auto s=ReadNativePage(imageBase);
    if(boundary!=0 && !((boundary==1 && object==s.song) || (boundary==2 && object==s.state))) return;
    const auto thread=GetCurrentThreadId();
    if(songRoute.thread && songRoute.thread!=thread) {FinishSong(Tr(Tx::songroute_thread_changed));return;}
    if(songRoute.phase==SongPhase::Boundary || songRoute.phase==SongPhase::HomeBoundary) {
        const bool home=s.page==NativePage::Home;
        if((home && boundary!=0) || (!home && boundary!=2)) return;
        if(!SameSource(songRoute.source,s) || !s.idle) {FinishSong(Tr(Tx::songroute_source_changed));return;}
        songRoute.thread=thread;
        if(home) {
            if(!InvokeEnterSong()) {FinishSong(Tr(Tx::songroute_enter_rejected));return;}
            AdvanceSong(SongPhase::SongArrival,Tr(Tx::songroute_entering));return;
        }
        const bool parent=s.page==NativePage::Parent;
        if(!InvokeSongExit(s,parent)) {FinishSong(Tr(Tx::songroute_exit_rejected));return;}
        AdvanceSong(parent?SongPhase::OuterArrival:SongPhase::ParentArrival,Tr(Tx::songroute_exiting));return;
    }
    if(songRoute.phase==SongPhase::ParentArrival) {
        if(s.page==NativePage::Parent && s.idle && s.parentPhase==1) {
            if(!SameConfiguration(songRoute.source,s)) {FinishSong(Tr(Tx::songroute_parent_changed));return;}
            songRoute.source=s;AdvanceSong(SongPhase::FinishBoundary,Tr(Tx::songroute_returning));
        } else if(s.idle && (s.page==NativePage::Home || s.page==NativePage::Song)) FinishSong(Tr(Tx::songroute_user_left));
        return;
    }
    if(songRoute.phase==SongPhase::FinishBoundary) {
        if(boundary!=2) return;
        if(!SameSource(songRoute.source,s) || !InvokeSongExit(s,true)) {FinishSong(Tr(Tx::songroute_parent_exit_failed));return;}
        AdvanceSong(SongPhase::OuterArrival,Tr(Tx::songroute_waiting_exit));return;
    }
    if(songRoute.phase==SongPhase::OuterArrival) {
        if(s.page==NativePage::Song && s.idle) {
            if(songRoute.returnMode!=Mode::Game || s.pvid!=songRoute.sourcePvid) {FinishSong(Tr(Tx::songroute_song_mismatch));return;}
            songRoute.source=s;AdvanceSong(SongPhase::SongArrival,Tr(Tx::songroute_back_song));
        } else if(s.page==NativePage::Home && s.idle) {
            if(songRoute.returnMode!=Mode::Customize) {FinishSong(Tr(Tx::songroute_home_mismatch));return;}
            songRoute.source=s;AdvanceSong(SongPhase::HomeBoundary,Tr(Tx::songroute_back_home));
        }
        return;
    }
    if(songRoute.phase==SongPhase::SongArrival) {
        if(s.page!=NativePage::Song || !s.idle) return;
        if(!songRoute.stable) songRoute.source=s;
        else if(!SameSource(songRoute.source,s)) {FinishSong(Tr(Tx::songroute_list_changed));return;}
        if(boundary!=0 || ++songRoute.stable<2) return;
        AdvanceSong(SongPhase::Ready,Tr(Tx::songroute_arrived));
        songRoute.readyAfter=GetTickCount64()+250;
    }
}
// 参数 s 为到达目标页且稳定的快照；按领域分发到模型定位器或发型／道具执行器，返回是否已受理。
bool DispatchArrival(const NativePageSnapshot& s,std::string& message) {
    if(route.target==NativePage::Module) return SubmitModuleLocate(route.request,message);
    auto frame=ItemFrame(s);frame.thread=GetCurrentThreadId();
    // 到达发型页后按实际歌者重验：其他角色的发型只到达页面、不定位，筛选不授予切换歌者的权限。
    if(route.target==NativePage::Hair && frame.singer && !CompatibleWithSinger(route.request.character,frame.singer)) {
        message=Trf(Tx::route_entered_other_character,{{"page",PageName(route.target)}});return false;
    }
    if(SubmitCustomizationItemRequest(route.request,frame,message)) return true;
    // 页面到达不等于定位成功：未认证 Profile 时如实说明停留在原生列表当前位置。
    message=Trf(Tx::route_entered_with_note,{{"page",PageName(route.target)},{"note",message}});return false;
}
// 参数 boundary/object 是原生帧类型与真实this；每次只在匹配边界提交一步。
void OnBoundary(int boundary,uintptr_t object) {
    std::unique_lock lock(routeMutex,std::try_to_lock);
    if(!lock.owns_lock() || !installed) return;
    OnSongBoundary(boundary,object);
    if(route.phase==RoutePhase::Idle || route.phase==RoutePhase::Done || route.phase==RoutePhase::Locating) return;
    if(route.cancelled!=cancellation.load() || route.request.uiGeneration!=currentUiGeneration) {Finish(Tr(Tx::route_cancelled_changed));return;}
    const auto now=GetTickCount64();
    if(now>route.deadline) {Finish(Trf(Tx::route_enter_timeout,{{"page",PageName(route.target)}}));return;}
    const auto s=ReadNativePage(imageBase);
    const bool matches=boundary==0 || (boundary==1 && object==s.song) || (boundary==2 && object==s.state);
    if(!matches) return;
    const auto thread=GetCurrentThreadId();
    if(route.thread && route.thread!=thread) {Finish(Tr(Tx::route_thread_changed));return;}
    const std::string entering=Trf(Tx::route_entering,{{"page",PageName(route.target)}});
    if(route.phase==RoutePhase::Boundary) {
        const bool correct=(s.page==NativePage::Home && boundary==0) || (s.page==NativePage::Song && boundary==1)
            || ((s.page==NativePage::Parent || s.page==NativePage::Module || s.page==NativePage::Hair || s.page==NativePage::Accessory) && boundary==2);
        if(!correct) return;
        if(!SameSource(route.source,s) || !SupportedSource(s,route.target)) {Finish(Tr(Tx::route_source_changed));return;}
        route.thread=thread;
        const auto next=NextHop(s.page,route.target);
        if(!InvokeRoute(s,next)) {Finish(Tr(Tx::route_page_rejected));return;}
        if(next==NativePage::Parent) {Advance(RoutePhase::ParentArrival,entering);return;}
        route.expect=next;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::ParentArrival) {
        if(s.page==NativePage::Parent && s.idle && s.parentPhase==1) {
            const bool expected=route.source.page==NativePage::Home?s.mode==Mode::Customize
                :s.mode==Mode::Game && s.sourcePvid==route.source.pvid;
            if(!expected) {Finish(Tr(Tx::route_unexpected_page));return;}
            // 父页直接进入发型／道具的主动协议未独立验收；只走已验证的光标0→模型→兄弟页。
            if(s.parentCursor!=0) {Finish(Tr(Tx::route_parent_cursor));return;}
            route.source=s;Advance(RoutePhase::ParentConfirm,entering);
        } else if(s.page==NativePage::Hair || s.page==NativePage::Accessory || s.page==NativePage::Module
            || (s.page==NativePage::Song && (route.source.page!=NativePage::Song || s.pvid!=route.source.pvid))
            || (s.page==NativePage::Home && route.source.page!=NativePage::Home)) Finish(Tr(Tx::route_changed_entering));
        return;
    }
    if(route.phase==RoutePhase::ParentConfirm) {
        if(boundary!=2) return;
        if(!SameSource(route.source,s) || !SupportedSource(s,route.target)) {Finish(Tr(Tx::route_parent_changed));return;}
        if(!InvokeRoute(s,NativePage::Module)) {Finish(Tr(Tx::route_module_rejected));return;}
        route.expect=NativePage::Module;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::PageArrival) {
        if(s.page==route.expect && s.idle) {
            if(!SameConfiguration(route.source,s)) {Finish(Trf(Tx::route_changed_during,{{"page",PageName(route.target)}}));return;}
            route.source=s;
            if(route.expect==route.target) {Advance(RoutePhase::LocateReady,Trf(Tx::route_arrived_checking,{{"page",PageName(route.target)}}));return;}
            Advance(RoutePhase::SiblingBoundary,Trf(Tx::route_arrived_next,{{"from",PageName(route.expect)},{"page",PageName(route.target)}}));return;
        }
        if(s.page==NativePage::Song || s.page==NativePage::Home) Finish(Trf(Tx::route_path_left,{{"page",PageName(route.target)}}));
        return;
    }
    if(route.phase==RoutePhase::SiblingBoundary) {
        if(s.page!=route.expect || !s.idle) {
            if(s.idle && s.page!=route.expect) Finish(Tr(Tx::route_transit_changed));
            return;
        }
        if(boundary!=2) return;
        if(!SameSource(route.source,s)) {Finish(Tr(Tx::route_transit_config));return;}
        // 中转页需连续两次空闲状态帧，避免在列表初始化帧内请求下一兄弟页。
        if(++route.stable<2) return;
        const auto next=NextHop(s.page,route.target);
        if(!InvokeRoute(s,next)) {Finish(Tr(Tx::route_sibling_rejected));return;}
        route.expect=next;Advance(RoutePhase::PageArrival,entering);return;
    }
    if(route.phase==RoutePhase::LocateReady) {
        if(s.page!=route.target || !s.idle) {
            if(s.page!=route.target) Finish(Trf(Tx::route_page_left,{{"page",PageName(route.target)}}));
            return;
        }
        if(!SameSource(route.source,s)) {Finish(Trf(Tx::route_page_changed,{{"page",PageName(route.target)}}));return;}
        if(boundary!=0) return;
        if(route.target==NativePage::Module && !ReadModuleLocatorContext()) {route.stable=0;return;}
        if(++route.stable<2) return;
        std::string message;
        // 未能交给定位器时只确认“已进入页面”，不显示定位成功；开发原因只写日志细节。
        if(!DispatchArrival(s,message)) {Finish(message,false,CustomizationStage::PageEntered,route.target==NativePage::Module?std::string{}:PollCustomizationItemLocator().detail);return;}
        Advance(RoutePhase::Locating,Trf(Tx::route_entered_locating,{{"page",PageName(route.target)},{"item",Tr(route.target==NativePage::Module?Tx::item_module:route.target==NativePage::Hair?Tx::item_hair:Tx::item_accessory)}}));
    }
}
// 参数 object 为原生内部状态机；仅当发型／道具执行器有事务时读取页面并转交本帧，不扫描驻留对象。
void ForwardItemFrame(uintptr_t object) {
    if(!installed || !CustomizationItemFrameNeeded()) return;
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

// 参数 page 为只读页面快照；对外暴露播放列表来源判定，使动作执行器把事务绑定到同一外层子状态。
bool IsPlaylistCustomization(const NativePageSnapshot& page) {return PlaylistCustomization(page);}

NativePageSnapshot ReadNativePage(uintptr_t base) {
    NativePageSnapshot s;if(!base) return s;
    if(!ReadAt(base+mode_info::kCurrent,s.mode) || !ReadAt(base+mode_info::kProc,s.modeProc) || !ReadAt(base+mode_info::kShift,s.modeShift)
        || !ReadAt(base+mode_info::kSubCurrent,s.subCurrent) || !ReadAt(base+mode_info::kSubNext,s.subNext) || !ReadAt(base+mode_info::kSubProc,s.subProc)
        || !ReadAt(base+mode_info::kSubShift,s.subShift) || !ReadAt(base+rva::kCustomizeTask+customize_task::kController,s.controller) || !ReadAt(base+rva::kPvSelectorSwitch,s.song)) return s;
    const bool outerIdle=s.modeProc==ModeProc::Ctrl && s.subProc==ModeProc::Ctrl && !s.modeShift && !s.subShift;
    if(!s.controller) {
        if(!s.song && s.mode==Mode::MenuSwitch && s.subCurrent==ModeSub::MenuSwitchHome && s.subNext==ModeSub::MenuSwitchUnk43 && outerIdle) {s.page=NativePage::Home;s.idle=true;}
        int32_t member=0;
        if(s.song && s.mode==Mode::Game && s.subCurrent==ModeSub::PvSel && s.subNext==ModeSub::PlaylistSel && outerIdle
            && ReadAt(s.song+pv_selector::kState,s.songState) && s.songState==pv_selector::kStateIdle && ReadAt(base+rva::kVisiblePvidMember,member)
            && member>=pv_selector::kVisiblePvidBias && member<=pv_selector::kVisiblePvidMemberMax && ReadAt(s.song+member-pv_selector::kVisiblePvidBias,s.pvid) && s.pvid>0 && s.pvid<10000000) {s.page=NativePage::Song;s.idle=true;}
        return s;
    }
    uintptr_t table=0;
    if(!outerIdle || !((s.mode==Mode::Game && ((s.subCurrent==ModeSub::PvSel && s.subNext==ModeSub::PlaylistSel) || (s.subCurrent==ModeSub::PlaylistSel && s.subNext==ModeSub::Game))) || (s.mode==Mode::Customize && s.subCurrent==ModeSub::CsCustomizeSel && s.subNext==ModeSub::CsUnk37))
        || !ReadAt(s.controller+0x18,s.configuration) || !s.configuration
        || !ReadAt(s.controller+0x998,s.manager) || !ReadAt(s.manager,table) || table!=base+rva::kSceneManagerVtable
        || !ReadAt(s.manager+scene_manager::kTreeHead,s.head) || !ReadAt(s.manager+scene_manager::kCurrentNode,s.node) || !Contains(s.head,s.node)
        || !ReadAt(s.manager+scene_manager::kTargetNode,s.target) || !ReadAt(s.manager+scene_manager::kTransition,s.transition)
        || !ReadAt(s.node+scene_manager::kNodeKey,s.key) || s.key<SceneKey::TopMenu || s.key>SceneKey::Item || !ReadAt(s.node+scene_manager::kNodeScene,s.scene)
        || !ReadAt(s.scene,table) || table!=base+rva::kSceneVtables[s.key]) return s;
    uintptr_t sceneController=0;
    if(!ReadAt(s.scene+8,sceneController) || sceneController!=s.controller) return s;
    ReadAt(s.controller+0x38,s.slotCount);ReadAt(s.controller+0x3C,s.slot);
    ReadAt(s.controller+8,s.sourcePvid);ReadAt(base+rva::kCustomizeScopeSelector,s.nativeScope);
    // 当前槽位实际装备的模型：独立记录优先（播放列表来源于 controller+0x10），缺失时用临时显示配置。
    if(s.slot>=0 && s.slot<6) {
        uintptr_t record=0;int32_t value=-1;
        if(ReadAt(s.controller+0x10,record) && record
            && ReadAt(record+static_cast<uintptr_t>(s.slot)*0x1C,value) && value>=0) s.model=value;
        else if(ReadAt(s.controller+0x320+static_cast<uintptr_t>(s.slot)*0xE0,value) && value>=0) s.model=value;
    }
    const uintptr_t stateOffset=s.key==SceneKey::TopMenu?0x178:s.key==SceneKey::Module?0x330:s.key==SceneKey::Hairstyle?0x3A0:0x300;
    if(!ReadAt(s.scene+stateOffset,s.state) || !s.state || !ReadAt(s.state+0x18,s.internal)) return s;
    s.page=s.key==SceneKey::TopMenu?NativePage::Parent:s.key==SceneKey::Module?NativePage::Module:s.key==SceneKey::Hairstyle?NativePage::Hair:NativePage::Accessory;
    s.idle=s.target==s.head && s.transition==scene_manager::kTransitionIdle && s.internal==0;
    if(s.key==SceneKey::TopMenu) {if(!ReadAt(s.scene+0x28,s.parentCursor) || !ReadAt(s.scene+0x1C,s.parentPhase)) s.idle=false;}
    if(s.key==SceneKey::Module) {if(!ReadAt(s.scene+0x60,s.popup) || s.popup!=-1) s.idle=false;}
    return s;
}

bool InitializeNativePageRouter(uintptr_t base) {
    std::lock_guard lock(routeMutex);if(installed) return true;
    // 调用者只有在定位器完整核验SHA、PE及签名后才提供基址；拒绝直接绕过初始化。
    if(!base || PollModuleLocator().phase==ModuleLocatePhase::Disabled) return false;
    for(auto entry:{rva::kModeCtrl,rva::kPvSelectorSwitchCtrl,rva::kSceneStateFrame,rva::kRequestMode,rva::kActivateCustomize,rva::kHideSelector,rva::kTopMenuConfirm,rva::kSceneManagerRequest,rva::kScenePanelHide})
        if(!IsExecutable(base+entry)) return false;
    imageBase=base;originalOuter=reinterpret_cast<OuterFrame>(base+rva::kModeCtrl);
    originalSong=reinterpret_cast<ObjectFrame>(base+rva::kPvSelectorSwitchCtrl);originalState=reinterpret_cast<ObjectFrame>(base+rva::kSceneStateFrame);
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    LONG result=DetourAttach(reinterpret_cast<PVOID*>(&originalOuter),reinterpret_cast<void*>(&OuterHook));
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<PVOID*>(&originalSong),reinterpret_cast<void*>(&SongHook));
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<PVOID*>(&originalState),reinterpret_cast<void*>(&StateHook));
    if(result!=NO_ERROR) {DetourTransactionAbort();return false;}
    if(DetourTransactionCommit()!=NO_ERROR) return false;
    installed=true;InitializeModuleActions(base);
    // 发型／道具原生 Profile 只按已认证规格登记；规格为空时不开放任何定位、预览或更换。
    InitializeCustomizationItemProfiles(base);return true;
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
        if(!facts) {message=Tr(Tx::route_target_gone);return false;}
        if(facts->domain!=request.domain || (facts->domain!=CustomizationDomain::Models && facts->part!=request.part)) {message=Tr(Tx::route_domain_mismatch);return false;}
        request.part=facts->part;request.character=facts->character;
    }
    if(request.key.kind!=CustomizationDomainKind(request.domain)) {message=Tr(Tx::route_kind_mismatch);return false;}
    const bool part=request.part==CustomizationPart::Head || request.part==CustomizationPart::Face || request.part==CustomizationPart::Neck || request.part==CustomizationPart::Back;
    if((request.domain==CustomizationDomain::Hair && request.part!=CustomizationPart::Hair) || (request.domain==CustomizationDomain::Accessories && !part)) {message=Tr(Tx::route_part_missing);return false;}
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
    if(!lock.owns_lock()) {message=Tr(Tx::route_processing);return false;}
    if(SongBusy() && songRoute.cancelled==songCancellation.load()) {message=Tr(Tx::route_song_busy);return false;}
    // 网关独立重算目标页，不信任界面传入的页面；领域、部位与人物以最新目录为准。
    auto request=original;
    if(!ResolveTarget(request,message)) return false;
    const bool models=request.domain==CustomizationDomain::Models;
    const auto target=TargetPage(request.domain);
    const auto page=ReadNativePage(imageBase);
    // 播放列表来源与普通单曲共用场景和动作回调，独立保存记录由 controller+0x10 提供；动作仍需执行器独立核对。
    const bool atTarget=page.page==target || (models && !installed && ReadModuleLocatorContext());
    message=CheckCustomizationNavigation(request,!atTarget);
    if(!message.empty()) return false;
    if(previewContinuation.waiting) {message=Tr(Tx::route_exiting_preview_wait);return false;}
    // 模型预览中：模型动作或任一领域的双击定位先退出预览，续接后再次经过本网关。
    if((page.page==NativePage::Module || (models && atTarget)) && ModulePreviewCanExit() && (models || request.action==CustomizationAction::Locate)) {
        if(!request.serial || request.key.id<0 || !request.queryRevision
            || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256 || request.uiGeneration!=currentUiGeneration) {message=Tr(Tx::route_continuation_bad);return false;}
        const auto preview=PollModuleAction();
        if(!RequestModulePreviewExit(request,message)) return false;
        previewContinuation={};previewContinuation.waiting=true;previewContinuation.request=request;previewContinuation.source=page;
        previewContinuation.cancelled=cancellation.load();previewContinuation.previewSerial=preview.serial;previewContinuation.deadline=GetTickCount64()+20000;
        previewContinuation.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,Tr(Tx::route_exit_then_action)};
        previewContinuation.status.action=request.action;route.phase=RoutePhase::Idle;message=previewContinuation.status.message;return true;
    }
    if(ModuleActionBusy()) {message=Tr(Tx::route_module_busy);return false;}
    // 发型／道具临时预览期间不接受新操作；Profile 提供退出入口时先请求退出，核对原装备后再允许下一步。
    if(CustomizationItemPreviewActive()) {
        if(!request.serial || request.key.id<0 || !request.queryRevision || request.catalogFingerprint.empty()
            || request.uiGeneration!=currentUiGeneration) {message=Tr(Tx::route_continuation_bad);return false;}
        const auto preview=PollCustomizationItemLocator();
        if(!RequestCustomizationItemPreviewExit(message)) return false;
        previewContinuation={};previewContinuation.waiting=previewContinuation.itemPreview=true;
        previewContinuation.request=request;previewContinuation.source=page;
        previewContinuation.cancelled=cancellation.load();previewContinuation.previewSerial=preview.serial;previewContinuation.deadline=GetTickCount64()+20000;
        previewContinuation.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,Tr(Tx::route_exit_restore_then)};
        previewContinuation.status.action=request.action;route.phase=RoutePhase::Idle;message=previewContinuation.status.message;return true;
    }
    if(CustomizationItemLocatorBusy()) {message=Tr(Tx::route_item_busy);return false;}
    if(route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load()) {message=Tr(Tx::route_previous_busy);return false;}
    // 发型：当前歌者已知时其他角色只读，任何入口（双击、Enter、底栏、按钮）都不跨页、不预览、不更换；
    // 歌者未知时仍可浏览与跨页，预览和更换拒绝，到达后再按实际歌者重验。
    if(request.domain==CustomizationDomain::Hair) {
        const auto singer=SingerOf(page);
        if(singer && !CompatibleWithSinger(request.character,singer)) {message=Tr(Tx::itemop_other_character);return false;}
        if(!singer && request.action!=CustomizationAction::Locate) {message=Tr(Tx::itemop_singer_unknown);return false;}
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
        if(locate.phase==ModuleLocatePhase::WaitingForFrame || locate.phase==ModuleLocatePhase::SwitchingCategory || locate.phase==ModuleLocatePhase::Confirming) {message=Tr(Tx::route_locate_busy);return false;}
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
            ?Tr(Tx::route_parent_cursor_retry)
            :Tr(Tx::route_not_certified);
        return false;
    }
    if(request.key.id<0 || request.serial<=lastSerial || !request.serial
        || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedFingerprint.empty() && lockedFingerprint!=request.catalogFingerprint)) {message=Tr(Tx::route_identity_bad);return false;}
    lockedFingerprint=request.catalogFingerprint;lastSerial=request.serial;currentUiGeneration=request.uiGeneration;
    route={};route.request=request;route.source=page;route.target=target;route.cancelled=cancellation.load();
    route.status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,0,false,false,""};
    route.status.action=request.action;
    // 目标定位未认证时在导航开始前说明缺口：到达不等于定位，更不等于装备。
    std::string text=Tr(Tx::route_waiting_frame);
    if(!models) {
        const auto capability=CustomizationItemCapabilities(request.domain);
        if(!capability.locate) text=Trf(Tx::route_entering_with_reason,{{"page",PageName(target)},{"reason",capability.locateReason}});
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
        if(pending.cancelled!=cancellation.load() || pending.request.uiGeneration!=generation) {reject(Tr(Tx::route_followup_changed));return;}
        if(now>pending.deadline) {reject(Tr(Tx::route_exit_timeout));return;}
        const auto previous=pending.itemPreview?PollCustomizationItemLocator():PollModuleAction();
        if(previous.serial!=pending.previewSerial) {reject(Tr(Tx::route_preview_identity));return;}
        if(!previous.completed || (pending.itemPreview?CustomizationItemLocatorBusy():ModuleActionBusy())) return;
        if(!previous.succeeded) {reject(Trf(Tx::route_preview_restore_failed,{{"reason",previous.message}}));return;}
        // 原分类与高亮按用户要求保留；等后续原生帧刷新定位器后再派发，帧内不等待。
        if(!pending.readyAfter) {pending.readyAfter=now+20;return;}
        if(now<pending.readyAfter) return;
        const auto page=ReadNativePage(imageBase);
        if(page.page!=pending.source.page || !page.idle || page.scene!=pending.source.scene || page.state!=pending.source.state || !SameConfiguration(page,pending.source)) {reject(Tr(Tx::route_preview_context_changed));return;}
        auto request=pending.request;
        if(request.action!=CustomizationAction::Locate) {
            const auto capability=request.domain==CustomizationDomain::Models?ReadModuleActionCapability(request.key,request.character):ReadCustomizationItemActionCapability(request.domain,request.key);
            if(capability.slot!=request.slot || capability.nativeScope!=request.nativeScope || capability.scope!=request.scope) {reject(Tr(Tx::route_slot_mismatch));return;}
            // 只更新本次已认证退出产生的上下文代数，保留原请求目标、目录、查询和槽位意图。
            request.contextGeneration=capability.contextGeneration;
        }
        pending.waiting=false;pending.status={};lock.unlock();
        std::string message;const bool accepted=SubmitCustomizationRequest(request,message);
        if(!accepted && lock.try_lock()) {previewContinuation.status={ModuleLocatePhase::Rejected,request.serial,request.uiGeneration,0,true,false,message};previewContinuation.status.action=request.action;}
        return;
    }
    if(route.phase==RoutePhase::Idle || route.phase==RoutePhase::Done) return;
    if(route.cancelled!=cancellation.load() || route.request.uiGeneration!=generation) {Finish(Tr(Tx::route_panel_changed));return;}
    if(now>route.deadline) {
        if(route.target==NativePage::Module) CancelModuleLocate(); else CancelCustomizationItemLocator();
        Finish(Tr(Tx::route_timeout));return;
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
    // 播放列表来源已实测：预览只改临时显示配置，更换同时写入 controller+0x10 的槽位记录；能力仍由动作缓存决定。
    return capability;
}
CustomizationActionCapability ReadCustomizationItemActionCapability(CustomizationDomain domain,std::optional<CustomizationKey> key) {
    auto capability=CustomizationItemCapabilities(domain,key);
    if(domain==CustomizationDomain::Models) return capability;
    const auto page=CachedNativePage();
    const bool inPreview=CustomizationItemPreviewActive() && page.internal==2;
    capability.targetPage=(page.idle || inPreview) && page.page==TargetPage(domain);
    if(capability.contextGeneration!=page.scene || capability.slot!=page.slot || capability.nativeScope!=page.nativeScope) capability.remove=capability.equipped=false;
    capability.slot=page.slot;capability.nativeScope=page.nativeScope;capability.scope=page.nativeScope==native::CustomizeScope::Song?CustomizationScope::Song:CustomizationScope::Common;
    capability.contextGeneration=page.scene;
    capability.busy=capability.busy || (CustomizationItemLocatorBusy() && !inPreview);
    // 播放列表来源的发型／道具记录同样读取 controller+0x10；歌者按来源歌曲与槽位解析，未知时仍在下方关闭写入。
    // 实际歌者未知时预览与更换不开放；人物兼容性由界面只读规则与执行器再次核对。
    if(!SingerOf(page)) {capability.preview=capability.equip=capability.remove=false;capability.previewReason=capability.equipReason=Tr(Tx::itemop_singer_unknown);}
    // 不在目标页时任何能力都不开放；按钮与底栏只提示双击主体进入。
    if(!capability.targetPage) capability.locate=capability.preview=capability.equip=capability.remove=false;
    return capability;
}
NativePage CurrentNativePage() {
    const auto page=CachedNativePage();
    return page.idle?page.page:NativePage::Unknown;
}
void ConfigureCustomizationSingerResolver(std::function<std::optional<CustomizationCharacter>(const NativePageSnapshot&)> resolver) {
    std::lock_guard lock(routeMutex);singerResolver=std::move(resolver);
}
// 参数 characters 为模块 ID 到人物的只读快照；由界面线程在目录更新后发布，游戏帧线程只读取。
void PublishCustomizationModelCharacters(std::shared_ptr<const std::unordered_map<int,CustomizationCharacter>> characters) {
    std::lock_guard lock(modelCharacterMutex);modelCharacters=std::move(characters);
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
    if(!lock.owns_lock()) {message=Tr(Tx::songroute_busy_nav);return false;}
    if(!available || request.pvid<=0 || request.difficulty<0 || request.difficulty>kRequestExtraExtreme || request.style<0 || request.style>2) {message=Tr(Tx::songroute_target_bad);return false;}
    if(!installed || !songSubmit) {message=Tr(Tx::songroute_not_ready);return false;}
    const auto s=ReadNativePage(imageBase);
    // 播放列表自定义返回协议尚未认证，禁止借用普通选曲跨页事务。
    if(PlaylistCustomization(s)) {message=Tr(Tx::songroute_page_not_certified);return false;}
    const bool doubleBody=request.origin==SongOrigin::BodyDoubleClick && request.queryRevision
        && request.firstClickPvid==request.pvid && request.firstClickRevision==request.queryRevision;
    const bool difficultyButton=request.origin==SongOrigin::DifficultyButton;
    if(s.page!=NativePage::Song && !doubleBody && !difficultyButton) {
        message=Tr(Tx::songroute_how_to);return false;
    }
    if(SongBusy() && songRoute.cancelled==songCancellation.load()) {message=Tr(Tx::songroute_previous_busy);return false;}
    if(previewContinuation.waiting) {message=Tr(Tx::songroute_preview_exiting);return false;}
    const bool itemPreview=CustomizationItemPreviewActive() && (s.page==NativePage::Hair || s.page==NativePage::Accessory);
    if((s.page==NativePage::Module && ModulePreviewCanExit()) || itemPreview) {
        if(!request.serial || request.serial<=songLastSerial || !request.catalogGeneration || request.catalogGeneration!=songGeneration || request.queryRevision!=songRevision) {message=Tr(Tx::songroute_identity_expired);return false;}
        const auto preview=itemPreview?PollCustomizationItemLocator():PollModuleAction();
        if(!(itemPreview?RequestCustomizationItemPreviewExit(message):RequestModulePreviewExitForSong(message))) return false;
        songLastSerial=request.serial;songRoute={};songRoute.itemPreview=itemPreview;songRoute.request=request;songRoute.source=s;songRoute.previewSerial=preview.serial;
        songRoute.returnMode=s.mode;songRoute.sourcePvid=s.sourcePvid;songRoute.cancelled=songCancellation.load();songRoute.status.serial=request.serial;songRoute.status.busy=true;
        AdvanceSong(SongPhase::PreviewExit,Tr(Tx::songroute_exit_preview_then));songRoute.deadline=GetTickCount64()+20000;message=songRoute.status.message;return true;
    }
    const auto locate=PollModuleLocator();
    if(ModuleActionBusy() || CustomizationItemLocatorBusy() || (route.phase!=RoutePhase::Idle && route.phase!=RoutePhase::Done && route.cancelled==cancellation.load())
        || locate.phase==ModuleLocatePhase::WaitingForFrame || locate.phase==ModuleLocatePhase::SwitchingCategory || locate.phase==ModuleLocatePhase::Confirming) {
        message=Tr(Tx::songroute_module_busy);return false;
    }
    if(!s.idle || !(s.page==NativePage::Home || s.page==NativePage::Song || s.page==NativePage::Module
        || s.page==NativePage::Hair || s.page==NativePage::Accessory || (s.page==NativePage::Parent && s.parentPhase==1))) {
        message=Tr(Tx::songroute_page_not_certified);return false;
    }
    if(!request.serial || request.serial<=songLastSerial || !request.catalogGeneration
        || request.catalogGeneration!=songGeneration || request.queryRevision!=songRevision) {message=Tr(Tx::songroute_identity_expired);return false;}
    songLastSerial=request.serial;songRoute={};songRoute.request=request;songRoute.source=s;
    songRoute.returnMode=s.mode;songRoute.sourcePvid=s.sourcePvid;songRoute.cancelled=songCancellation.load();
    songRoute.status.serial=request.serial;songRoute.status.busy=true;
    AdvanceSong(s.page==NativePage::Song?SongPhase::Ready:SongPhase::Boundary,
        s.page==NativePage::Song?Tr(Tx::songroute_submitting_here):Tr(Tx::songroute_waiting_frame));
    message=songRoute.status.message;return true;
}
// 参数now/generation/revision由UI宿主逐帧提供；旧查询不在到达后重新绑定当前选中行。
void TickSongNavigation(uint64_t now,uint64_t generation,uint64_t revision) {
    std::unique_lock lock(routeMutex,std::try_to_lock);if(!lock.owns_lock()) return;
    songGeneration=generation;songRevision=revision;
    if(!SongBusy()) return;
    if(songRoute.cancelled!=songCancellation.load() || songRoute.request.catalogGeneration!=generation || songRoute.request.queryRevision!=revision) {
        if(songRoute.phase==SongPhase::Selecting && songCancel) songCancel();
        FinishSong(Tr(Tx::songroute_changed));return;
    }
    if(now>songRoute.deadline) {
        if(songRoute.phase==SongPhase::Selecting && songCancel) songCancel();
        FinishSong(Tr(Tx::songroute_timeout));return;
    }
    if(songRoute.phase==SongPhase::PreviewExit) {
        const auto previous=songRoute.itemPreview?PollCustomizationItemLocator():PollModuleAction();
        if(previous.serial!=songRoute.previewSerial) {FinishSong(Tr(Tx::songroute_preview_changed));return;}
        if(!previous.completed || (songRoute.itemPreview?CustomizationItemLocatorBusy():ModuleActionBusy())) return;
        if(!previous.succeeded) {FinishSong(Tr(Tx::songroute_preview_unverified));return;}
        if(!songRoute.readyAfter) {songRoute.readyAfter=now+20;return;}
        if(now<songRoute.readyAfter) return;
        const auto page=ReadNativePage(imageBase);
        if(page.page!=songRoute.source.page || !page.idle || page.scene!=songRoute.source.scene || !SameConfiguration(page,songRoute.source)) {FinishSong(Tr(Tx::songroute_preview_context));return;}
        songRoute.source=page;AdvanceSong(SongPhase::Boundary,Tr(Tx::songroute_preview_done));return;
    }
    if(songRoute.phase!=SongPhase::Ready || now<songRoute.readyAfter) return;
    const auto current=ReadNativePage(imageBase);
    if(current.page!=NativePage::Song || !current.idle || !SameSource(songRoute.source,current)) {FinishSong(Tr(Tx::songroute_page_changed));return;}
    std::string message;
    if(!songSubmit || !songSubmit(songRoute.request,message)) {FinishSong(message.empty()?Tr(Tx::songroute_here_rejected):message);return;}
    AdvanceSong(SongPhase::Selecting,Tr(Tx::songroute_verifying));
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
