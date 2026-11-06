// 复用生产页面读取器与网关，隔离地址空间不注入游戏，不调用原生导航入口。
#include "../src/native_page_router.cpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace diva {
// 参数request/message为隔离定位请求和反馈；记录是否错误穿过导航守卫。
static int locateCalls=0;
static bool testPreview=false,testBusy=false,testAcceptActions=false;
static int testActionCalls=0;
static ModuleLocateStatus testActionStatus;
static CustomizationLocateRequest testSubmittedAction;
bool SubmitModuleLocate(const CustomizationLocateRequest& request,std::string& message) {(void)request;++locateCalls;message="隔离定位已受理";return true;}
// 隔离夹具不提供真实定位器上下文，避免冒充游戏页可操作。
std::optional<CustomizationContext> ReadModuleLocatorContext() {return {};}
// 返回无任务状态，测试不安装定位Hook。
ModuleLocateStatus PollModuleLocator() {return {};}
// 测试取消只验证路由代数，不触碰外部对象。
void CancelModuleLocate() {}
// 参数base为隔离基址；路由回归不初始化真实服装动作。
void InitializeModuleActions(uintptr_t base) {(void)base;}
// 参数state为隔离状态机；测试不执行原生动作。
void OnModuleActionStateFrame(uintptr_t state) {(void)state;}
// 参数request/message为无副作用的拒绝路径。
bool SubmitModuleAction(const CustomizationLocateRequest& request,std::string& message) {testSubmittedAction=request;if(testAcceptActions){++testActionCalls;message="隔离动作已受理";return true;}message="隔离测试不允许装备。";return false;}
// 返回未知能力，不把测试数据导入产品。
CustomizationActionCapability ReadModuleActionCapability(const CustomizationKey& key,CustomizationCharacter character) {(void)key;(void)character;CustomizationActionCapability c;if(testAcceptActions){c.targetPage=c.preview=c.equip=true;c.contextGeneration=91;c.slot=0;c.nativeScope=3;c.scope=CustomizationScope::Song;}return c;}
// 路由测试没有并行服装动作。
bool ModuleActionBusy() {return testBusy;}
// 默认夹具没有原生预览；专项续接夹具另行提供状态。
bool ModulePreviewCanExit() {return testPreview;}
// 参数request/message为无副作用的退出请求，默认夹具明确拒绝。
bool RequestModulePreviewExit(const CustomizationLocateRequest& request,std::string& message) {(void)request;if(testPreview){testPreview=false;testBusy=true;message="隔离退出已排队";return true;}message="隔离夹具没有预览。";return false;}
// 默认夹具不提供歌曲接续所需的预览。
bool RequestModulePreviewExitForSong(std::string& message) {CustomizationLocateRequest unused;return RequestModulePreviewExit(unused,message);}
// 返回空动作状态。
ModuleLocateStatus PollModuleAction() {return testActionStatus;}
// 隔离取消没有外部副作用。
void CancelModuleAction() {}
// 原生导航调用替身：只记录调用顺序与参数，不跳入任何生产 RVA。
static std::vector<std::string> nativeLog;
static bool acceptScene=true;
// 参数 mode 为外层请求模式。
bool RecordOuter(int mode) {nativeLog.push_back("outer:"+std::to_string(mode));return true;}
// 参数 pvid/song 为来源歌曲与对象。
bool RecordSong(int pvid,uintptr_t song) {(void)song;nativeLog.push_back("song:"+std::to_string(pvid));return true;}
// 参数 scene 为父页场景。
bool RecordParent(uintptr_t scene) {(void)scene;nativeLog.push_back("parent");return true;}
// 参数 manager/key 为管理器与目标页键；acceptScene=false 模拟原生拒绝。
bool RecordScene(uintptr_t manager,int key) {(void)manager;nativeLog.push_back("scene:"+std::to_string(key));return acceptScene;}
// 参数 panel 为来源面板。
bool RecordPanel(uintptr_t panel) {(void)panel;nativeLog.push_back("panel");return true;}
}
namespace {
// 参数condition/message为边界断言与中文失败说明。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 参数address/value为隔离夹具字段；只写测试自己持有的内存。
template<class T> void Put(uintptr_t address,T value) {std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));}
// 参数address为隔离夹具字段；读取测试持有内存中的值。
template<class T> T Get(uintptr_t address) {T value{};std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(T));return value;}
// 参数log/expected为记录的原生调用与预期顺序。
bool Calls(const std::vector<std::string>& log,std::initializer_list<const char*> expected) {
    if(log.size()!=expected.size()) return false;size_t n=0;for(const auto* item:expected) if(log[n++]!=item) return false;return true;
}
// 模拟道具页列表：仅供隔离事务夹具，不代表实机道具容器布局。
std::vector<int> fakeAccessoryIds{600,601};
int fakeHighlight=1,fakeHighlightCalls=0;
struct Fixture {
    uintptr_t base=0,controller=0,manager=0,head=0,node=0,scene=0,state=0,song=0;
    alignas(uintptr_t) std::array<unsigned char,0xA00> control{};
    alignas(uintptr_t) std::array<unsigned char,0x400> sceneData{},songData{};
    alignas(uintptr_t) std::array<unsigned char,0x50> managerData{},headData{},nodeData{},stateData{};
    // 在项目进程内保留虚拟地址，只提交3个profile字段页，不分配完整游戏映像。
    Fixture() {
        base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0xCC71000,MEM_RESERVE,PAGE_READWRITE));Check(base!=0,"无法保留测试地址空间");
        for(uintptr_t offset:{uintptr_t(0xCC61000),uintptr_t(0xCC6F000),uintptr_t(0xCC5E000),uintptr_t(0x6EF000)})
            Check(VirtualAlloc(reinterpret_cast<void*>(base+offset),0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"无法提交字段页");
        controller=reinterpret_cast<uintptr_t>(control.data());manager=reinterpret_cast<uintptr_t>(managerData.data());
        head=reinterpret_cast<uintptr_t>(headData.data());node=reinterpret_cast<uintptr_t>(nodeData.data());
        scene=reinterpret_cast<uintptr_t>(sceneData.data());state=reinterpret_cast<uintptr_t>(stateData.data());song=reinterpret_cast<uintptr_t>(songData.data());
        Put(base+diva::kOuter,int32_t(9));Put(base+diva::kOuter+0xC,int32_t(1));Put(base+diva::kOuter+0x1C,int32_t(42));
        Put(base+diva::kOuter+0x20,int32_t(43));Put(base+diva::kOuter+0x28,int32_t(1));
        Put(base+0x6EFE8C,int32_t(0x9C8));Put(song,int32_t(800));Put(song+0x6C,int32_t(6));
    }
    // 释放测试持有的虚拟内存，不访问其他进程。
    ~Fixture() {VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);}
    // 切到隔离的空闲选曲来源。
    void Song() {Put(base+diva::kOuter,int32_t(2));Put(base+diva::kOuter+0x1C,int32_t(5));Put(base+diva::kOuter+0x20,int32_t(6));Put(base+0xCC5EF18,song);}
    // 切回隔离的空闲主页：外层 mode9 sub42/43，无自定义控制器与选曲对象。
    void Home() {Put(base+diva::kOuter,int32_t(9));Put(base+diva::kOuter+0x1C,int32_t(42));Put(base+diva::kOuter+0x20,int32_t(43));Put(base+0xCC5EF18,uintptr_t(0));Put(base+diva::kTask+0x78,uintptr_t(0));}
    // 参数 key 为子页；切到主页来源的自定义页（外层 mode7 sub36/37）。
    void HomeCustomize(int key) {Customize(key);Put(base+diva::kOuter,int32_t(7));Put(base+diva::kOuter+0x1C,int32_t(36));Put(base+diva::kOuter+0x20,int32_t(37));}
    // 参数key为父页/模型/发型/饰品，建立有界场景树与真实持有关系。
    void Customize(int key) {
        Song();Put(base+diva::kTask+0x78,controller);Put(controller+0x18,song);Put(controller+0x998,manager);
        Put(manager,base+0xCC4E38);Put(manager+8,head);Put(manager+0x18,node);Put(manager+0x20,head);Put(manager+0x28,int32_t(1));
        Put(head+8,node);Put(node,head);Put(node+0x10,head);Put(node+0x20,int32_t(key));Put(node+0x28,scene);
        Put(scene,base+diva::kSceneTables[key]);Put(scene+8,controller);
        Put(scene+(key==1?0x178:key==2?0x330:key==3?0x3A0:0x300),state);
        Put(scene+0x60,int32_t(-1));Put(scene+0x1C,int32_t(1));Put(state+0x18,int32_t(0));
    }
};
}
// 验证类型化页面、驻留对象排除、来源门禁、过期取消及超时，不把夹具视为实机。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        // 所有原生导航入口替换为记录替身，任何路径都不会调用生产地址。
        diva::nativeCalls={diva::RecordOuter,diva::RecordSong,diva::RecordParent,diva::RecordScene,diva::RecordPanel};
        Fixture f;using namespace diva;
        Check(ReadNativePage(f.base).page==NativePage::Home,"主页读取失败");
        Put(f.base+kOuter+0x11,uint8_t(1));Check(ReadNativePage(f.base).page==NativePage::Home,"把相邻字节当模式切换标记");
        Put(f.base+kOuter+0x10,uint8_t(1));Check(ReadNativePage(f.base).page==NativePage::Unknown,"过渡主页授予导航权限");
        Put(f.base+kOuter+0x10,uint8_t(0));f.Song();Check(ReadNativePage(f.base).page==NativePage::Song,"选曲来源读取失败");
        for(int key=1;key<=4;++key) {f.Customize(key);Check(ReadNativePage(f.base).key==key && ReadNativePage(f.base).idle,"场景树页面读取失败");}
        for(int key=1;key<=4;++key) {
            f.Customize(key);Put(f.base+kOuter+0x1C,int32_t(6));Put(f.base+kOuter+0x20,int32_t(7));
            const auto page=ReadNativePage(f.base);
            Check(page.key==key && page.idle && PlaylistCustomization(page),"播放列表自定义场景未识别");
            Put(f.manager+0x20,f.node);Check(!ReadNativePage(f.base).idle,"播放列表过渡状态误判为空闲");
        }
        f.Customize(2);Put(f.base+kOuter+0x1C,int32_t(6));Put(f.base+kOuter+0x20,int32_t(8));
        Check(ReadNativePage(f.base).page==NativePage::Unknown,"未知播放列表外层状态未拒绝");
        f.Customize(2);
        Put(f.head+8,f.head);Check(ReadNativePage(f.base).page==NativePage::Unknown,"驻留树外节点被误认为活动页");
        f.Customize(2);Put(f.scene+8,uintptr_t(0));Check(ReadNativePage(f.base).page==NativePage::Unknown,"场景控制器身份未核查");
        f.Customize(2);Put(f.state+0x18,int32_t(2));Check(!ReadNativePage(f.base).idle,"临时预览被当空闲页");
        f.Customize(2);Put(f.scene+0x60,int32_t(0));Check(!ReadNativePage(f.base).idle,"弹层被当空闲页");
        imageBase=f.base;installed=true;route={};f.Customize(3);
        CustomizationLocateRequest request;request.key={CustomizationKind::Module,316};request.character=CustomizationCharacter::Miku;
        request.catalogFingerprint="隔离目录";request.serial=1;request.uiGeneration=7;request.queryRevision=3;
        std::string message;
        for(auto origin:{CustomizationOrigin::CardSingleClick,CustomizationOrigin::Keyboard,CustomizationOrigin::Footer,CustomizationOrigin::HoverButton}) {
            request.origin=origin;Check(!SubmitCustomizationRequest(request,message) && route.phase==RoutePhase::Idle,"非双击来源排入跨页事务");
        }
        request.origin=CustomizationOrigin::CardDoubleClick;request.firstClickKey=request.key;request.firstClickDomain=CustomizationDomain::Models;request.firstClickRevision=3;
        Check(SubmitCustomizationRequest(request,message) && locateCalls==0,"双击未排队或提前执行定位");
        const auto source=route.source;
        CancelNativePageRouter();OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded && locateCalls==0,"取消后继续执行旧请求");
        ++request.serial;Check(SubmitCustomizationRequest(request,message),"取消后不能提交新请求");
        TickNativePageRouter(GetTickCount64(),8);Check(route.phase==RoutePhase::Done && !route.status.succeeded,"目录替换后旧请求继续执行");
        ++request.serial;Check(SubmitCustomizationRequest(request,message),"目录替换后不能重试");
        route.deadline=1;TickNativePageRouter(GetTickCount64(),7);Check(route.phase==RoutePhase::Done && !route.status.succeeded,"超时请求未停止");
        ++request.serial;Check(SubmitCustomizationRequest(request,message),"超时后不能重试");
        route.source=source;Put(f.controller+0x18,f.scene);OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded,"换配置后旧请求未取消");
        f.Customize(1);route.source=ReadNativePage(f.base);route.phase=RoutePhase::PageArrival;route.target=route.expect=NativePage::Module;
        f.Customize(2);OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::LocateReady,"同一配置的预期场景切换被误取消");
        f.Customize(1);route.source=ReadNativePage(f.base);route.phase=RoutePhase::PageArrival;route.target=route.expect=NativePage::Module;
        f.Customize(2);Put(f.controller+0x3C,int32_t(1));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded,"进入模型期间换槽后旧目标继续执行");
        Put(f.controller+0x3C,int32_t(0));f.Customize(1);route.phase=RoutePhase::ParentArrival;
        route.source={};route.source.page=NativePage::Song;route.source.mode=2;route.source.pvid=817;
        Put(f.controller+8,int32_t(800));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded,"到达其他曲目自定义页后旧目标继续执行");
        f.Customize(2);route.phase=RoutePhase::Done;++request.serial;
        Check(SubmitCustomizationRequest(request,message) && locateCalls==1 && route.phase==RoutePhase::Idle,"模型原位定位被旧路由结果遮蔽");
        request.action=CustomizationAction::Equip;++request.serial;
        Check(!SubmitCustomizationRequest(request,message) && locateCalls==1,"换装绕过未认证上下文守卫");
        const auto c=ReadCustomizationActionCapability(request.key);
        Check(!c.equip && !c.preview && c.ownership==Ownership::Unknown,"未知拥有或范围意外开放写入");
        int songCalls=0,songCancels=0;
        // 参数value/text为歌曲定位器替身，只记录交接，不执行游戏函数。
        ConfigureSongNavigation([&](diva::Request value,std::string& text) {++songCalls;text="歌曲定位已受理";return value.pvid==800;},[&] {++songCancels;});
        TickSongNavigation(GetTickCount64(),11,17);
        diva::Request song{800,3,0,1};song.catalogGeneration=11;song.queryRevision=17;
        f.Customize(2);
        for(auto origin:{SongOrigin::Keyboard,SongOrigin::BodySingleClick}) {
            song.origin=origin;Check(!SubmitSongNavigation(song,true,message) && !SongBusy(),"歌曲非双击来源产生跨页");
        }
        song.origin=SongOrigin::BodyDoubleClick;song.firstClickPvid=801;song.firstClickRevision=17;
        Check(!SubmitSongNavigation(song,true,message),"歌曲首击PVID不符仍能跨页");
        song.firstClickPvid=800;song.firstClickRevision=16;
        Check(!SubmitSongNavigation(song,true,message),"歌曲首击查询修订不符仍能跨页");
        song.firstClickRevision=17;Check(SubmitSongNavigation(song,true,message) && songCalls==0,"歌曲双击未排队或提前定位");
        TickSongNavigation(GetTickCount64(),11,18);
        Check(songRoute.phase==SongPhase::Done && songCalls==0,"歌曲查询更新后旧导航继续执行");
        TickSongNavigation(GetTickCount64(),11,17);++song.serial;
        song.origin=SongOrigin::DifficultyButton;song.firstClickPvid=0;song.firstClickRevision=0;
        Check(SubmitSongNavigation(song,true,message) && songCalls==0,"明确难度按钮未获准跨页");
        CancelSongNavigation();TickSongNavigation(GetTickCount64(),11,17);++song.serial;
        song.origin=SongOrigin::BodyDoubleClick;song.firstClickPvid=800;song.firstClickRevision=17;
        Check(SubmitSongNavigation(song,true,message),"歌曲取消后不能重试");
        songRoute.phase=SongPhase::ParentArrival;songRoute.source=ReadNativePage(f.base);
        f.Customize(1);Put(f.controller+0x3C,int32_t(1));OnBoundary(2,f.state);
        Check(songRoute.phase==SongPhase::Done && songCalls==0,"歌曲退父页期间换槽未取消");
        Put(f.controller+0x3C,int32_t(0));f.Song();Put(f.base+kTask+0x78,uintptr_t(0));
        song.origin=SongOrigin::Keyboard;++song.serial;
        Check(SubmitSongNavigation(song,true,message),"稳定选曲页键盘原位定位被误拒绝");
        TickSongNavigation(GetTickCount64(),11,17);
        Check(songCalls==1 && songRoute.phase==SongPhase::Selecting && !songRoute.status.succeeded,"歌曲页面到达被当作定位成功");
        CancelSongNavigation();TickSongNavigation(GetTickCount64(),11,17);
        Check(songCancels==1 && songRoute.phase==SongPhase::Done,"歌曲已交接事务取消未送至定位器");
        ++song.serial;Check(SubmitSongNavigation(song,true,message),"原位取消后无法重试");
        TickSongNavigation(GetTickCount64(),11,17);CompleteSongNavigation(true,"真实PVID读回通过");
        Check(songRoute.status.succeeded && songRoute.status.completed,"独立读回结果未反馈歌曲成功");
        songRoute.phase=SongPhase::OuterArrival;songRoute.returnMode=2;songRoute.sourcePvid=817;
        songRoute.cancelled=songCancellation.load();songRoute.deadline=GetTickCount64()+1000;OnBoundary(0,0);
        Check(songRoute.phase==SongPhase::Done && !songRoute.status.succeeded,"歌曲退出后曲目来源不符未拒绝");
        Check(VirtualAlloc(reinterpret_cast<void*>(f.base+0x12B5000),0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"无法提交隔离范围页");
        Put(f.base+0x12B5A74,int32_t(3));Put(f.controller+0x38,int32_t(1));Put(f.controller+0x3C,int32_t(0));Put(f.controller+8,int32_t(800));
        for(auto next:{CustomizationAction::Locate,CustomizationAction::Preview,CustomizationAction::Equip}) {
            route={};songRoute={};previewContinuation={};f.Customize(2);Put(f.state+0x18,int32_t(2));
            testAcceptActions=true;testPreview=true;testBusy=true;testActionStatus={};testActionStatus.serial=200;testActionStatus.action=CustomizationAction::Preview;
            auto follow=request;follow.serial=201+static_cast<int>(next);follow.action=next;follow.uiGeneration=7;follow.slot=0;follow.nativeScope=3;follow.scope=CustomizationScope::Song;
            currentUiGeneration=7;const int previousLocates=locateCalls,previousActions=testActionCalls;
            Check(SubmitCustomizationRequest(follow,message) && previewContinuation.waiting,"预览中后续定位／换装／预览仍被忙碌限制拒绝");
            const auto now=GetTickCount64();TickNativePageRouter(now,7);Check(locateCalls==previousLocates && testActionCalls==previousActions,"旧预览退出前提前执行新动作");
            testBusy=false;testActionStatus.completed=testActionStatus.succeeded=true;Put(f.state+0x18,int32_t(0));
            TickNativePageRouter(now+1,7);TickNativePageRouter(now+25,7);
            Check(!previewContinuation.waiting && (next==CustomizationAction::Locate?locateCalls==previousLocates+1:testActionCalls==previousActions+1),"独立确认退出后未接续原请求");
            if(next!=CustomizationAction::Locate) Check(testSubmittedAction.key==follow.key && testSubmittedAction.queryRevision==follow.queryRevision && testSubmittedAction.contextGeneration==91,"接续改变原目标或未刷新本次退出产生的上下文代数");
        }
        route={};songRoute={};previewContinuation={};f.Customize(2);Put(f.state+0x18,int32_t(2));testPreview=true;testBusy=true;testActionStatus={};testActionStatus.serial=500;
        song.origin=SongOrigin::BodyDoubleClick;song.firstClickPvid=song.pvid;song.firstClickRevision=song.queryRevision;++song.serial;
        Check(SubmitSongNavigation(song,true,message) && songRoute.phase==SongPhase::PreviewExit,"服装预览中歌曲双击未排队原生退出");
        testBusy=false;testActionStatus.completed=testActionStatus.succeeded=true;Put(f.state+0x18,int32_t(0));
        const auto exitNow=GetTickCount64();TickSongNavigation(exitNow,11,17);TickSongNavigation(exitNow+25,11,17);
        Check(songRoute.phase==SongPhase::Boundary && !songRoute.status.succeeded,"退出预览后未接入歌曲路由或把退出当作歌曲定位成功");
        songRoute={};route={};previewContinuation={};f.Customize(2);Put(f.state+0x18,int32_t(2));testPreview=true;testBusy=true;testActionStatus={};testActionStatus.serial=600;
        auto stale=request;stale.serial=601;stale.action=CustomizationAction::Locate;stale.uiGeneration=7;currentUiGeneration=7;
        Check(SubmitCustomizationRequest(stale,message),"取消接续测试未能排队");const int beforeCancelled=locateCalls;
        CancelNativePageRouter();TickNativePageRouter(GetTickCount64(),7);
        Check(previewContinuation.status.completed && !previewContinuation.status.succeeded && locateCalls==beforeCancelled,"查询取消后仍执行预览退出后的旧目标");
        previewContinuation={};songRoute={};testPreview=true;testActionStatus={};testActionStatus.serial=700;f.Customize(2);Put(f.state+0x18,int32_t(2));++song.serial;
        Check(SubmitSongNavigation(song,true,message),"歌曲取消接续测试未能排队");const int beforeSongCancelled=songCalls;
        TickSongNavigation(GetTickCount64(),11,18);
        Check(songRoute.status.completed && !songRoute.status.succeeded && songCalls==beforeSongCancelled,"改搜后仍执行旧歌曲预览接续");

        // ===== 发型／自定义道具泛化路由：所有原生调用均为记录替身 =====
        route={};songRoute={};previewContinuation={};testPreview=false;testBusy=false;testActionStatus={};testAcceptActions=false;currentUiGeneration=7;
        uint64_t itemSerial=1000;
        const uintptr_t panel=f.head;
        // 参数domain/id/part为目标；返回已绑定首击身份、领域与修订的主体双击请求。
        auto itemRequest=[&](CustomizationDomain domain,int id,CustomizationPart part) {
            CustomizationLocateRequest r;r.key={CustomizationKind::Accessory,id};r.domain=domain;r.part=part;r.character=CustomizationCharacter::Miku;
            r.catalogFingerprint="隔离目录";r.serial=++itemSerial;r.uiGeneration=7;r.queryRevision=3;
            r.origin=CustomizationOrigin::CardDoubleClick;r.firstClickKey=r.key;r.firstClickDomain=domain;r.firstClickRevision=3;return r;
        };
        // 主页→父页→模型→发型：只走已验证的父页光标0协议，到达后发型定位未认证，如实不报告成功。
        nativeLog.clear();f.Home();
        Check(ReadNativePage(f.base).page==NativePage::Home,"隔离主页夹具失效");
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair),message) && route.phase==RoutePhase::Boundary
            && route.target==NativePage::Hair && message.find("暂不支持")!=std::string::npos,"主页双击发型未排队或未在导航前说明定位缺口");
        OnBoundary(2,f.state);Check(nativeLog.empty(),"主页来源在错误帧边界执行导航");
        OnBoundary(0,0);Check(Calls(nativeLog,{"outer:7"}) && route.phase==RoutePhase::ParentArrival,"主页未请求进入自定义父页");
        f.HomeCustomize(1);Put(f.scene+0x28,int32_t(0));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::ParentConfirm && nativeLog.size()==1,"父页到达后提前确认或未进入确认阶段");
        OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"outer:7","parent"}) && Get<int32_t>(f.scene+0x1C)==2 && route.phase==RoutePhase::PageArrival && route.expect==NativePage::Module,"父页光标0未经模型入口确认");
        f.HomeCustomize(2);Put(f.scene+0x310,panel);Put(f.scene+0x25,uint8_t(0));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::SiblingBoundary && nativeLog.size()==2,"到达中转模型页后未等待同层边界");
        OnBoundary(2,f.state);Check(nativeLog.size()==2,"中转模型页首个状态帧即请求兄弟页");
        OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"outer:7","parent","scene:3","panel"}) && Get<uint8_t>(f.scene+0x25)==1 && route.expect==NativePage::Hair,"模型→发型同层协议顺序、目标键或完成字节错误");
        f.HomeCustomize(3);OnBoundary(2,f.state);Check(route.phase==RoutePhase::LocateReady,"发型页到达未进入列表核对");
        OnBoundary(0,0);Check(route.phase==RoutePhase::LocateReady,"发型页单帧稳定即分发");
        OnBoundary(0,0);
        Check(route.phase==RoutePhase::Done && route.status.completed && !route.status.succeeded && route.status.message.find("已进入发型页")!=std::string::npos
            && route.status.message.find("暂不支持")!=std::string::npos && nativeLog.size()==4 && route.status.stage==CustomizationStage::PageEntered
            && route.status.message.find("0x")==std::string::npos && !route.status.detail.empty(),"发型页到达被当作定位成功、缺少进入页面阶段或界面显示了实现细节");

        // 模型→自定义道具直接同层；注入模拟道具Profile后由执行器跨帧读回高亮再报告成功。
        CustomizationItemProfile accessory;accessory.evidence="隔离模拟";
        // 参数frame/out为执行器上下文与快照；读取测试内存列表。
        accessory.read=[](const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {
            (void)frame;out.pageReady=out.listReady=true;out.part=CustomizationPart::Head;out.listIdentity=77;out.recordIds=fakeAccessoryIds;
            if(fakeHighlight>=0 && fakeHighlight<static_cast<int>(fakeAccessoryIds.size())) out.highlightedId=fakeAccessoryIds[static_cast<size_t>(fakeHighlight)];return true;
        };
        // 参数frame/index为上下文与列表序号；模拟原位高亮。
        accessory.highlight=[](const CustomizationItemFrame& frame,size_t index) {(void)frame;++fakeHighlightCalls;fakeHighlight=static_cast<int>(index);return true;};
        RegisterCustomizationItemProfile(CustomizationDomain::Accessories,accessory);
        route={};nativeLog.clear();f.Customize(2);Put(f.scene+0x310,panel);Put(f.scene+0x25,uint8_t(0));
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Accessories,600,CustomizationPart::Head),message) && route.target==NativePage::Accessory
            && message.find("暂不支持")==std::string::npos,"模型页双击道具未排队或误报缺口");
        OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"scene:4","panel"}) && Get<uint8_t>(f.scene+0x25)==1 && route.phase==RoutePhase::PageArrival && route.expect==NativePage::Accessory,"模型→道具没有直接请求道具页");
        f.Customize(4);OnBoundary(2,f.state);OnBoundary(0,0);OnBoundary(0,0);
        Check(route.phase==RoutePhase::Locating && fakeHighlightCalls==0,"道具页到达后未交给执行器或同帧执行原生调用");
        for(int frame=0;frame<3;++frame) ForwardItemFrame(f.state);
        TickNativePageRouter(GetTickCount64(),7);
        Check(fakeHighlightCalls==1 && route.phase==RoutePhase::Done && route.status.succeeded && route.status.serial==route.request.serial
            && route.status.stage==CustomizationStage::Highlighted,"道具定位读回后路由未完成、重复调用或阶段不是目标高亮");
        RegisterCustomizationItemProfile(CustomizationDomain::Accessories,std::nullopt);

        // 发型→模型（既有协议）与道具→发型（经模型页中转，未实测直接边不使用）。
        route={};nativeLog.clear();f.Customize(3);Put(f.scene+0x380,panel);
        auto moduleDouble=request;moduleDouble.serial=++itemSerial;moduleDouble.action=CustomizationAction::Locate;moduleDouble.uiGeneration=7;
        moduleDouble.origin=CustomizationOrigin::CardDoubleClick;moduleDouble.firstClickKey=moduleDouble.key;moduleDouble.firstClickDomain=CustomizationDomain::Models;moduleDouble.firstClickRevision=moduleDouble.queryRevision;
        Check(SubmitCustomizationRequest(moduleDouble,message) && route.target==NativePage::Module,"发型页双击模型未排队");
        OnBoundary(2,f.state);Check(Calls(nativeLog,{"scene:2","panel"}) && route.expect==NativePage::Module,"发型→模型同层请求错误");
        f.Customize(2);OnBoundary(2,f.state);Check(route.phase==RoutePhase::LocateReady,"模型页到达未进入定位准备");
        route={};nativeLog.clear();f.Customize(4);Put(f.scene+0x2E0,panel);Put(f.scene+0x29,uint8_t(0));
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,501,CustomizationPart::Hair),message),"道具页双击发型未排队");
        OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"scene:2","panel"}) && Get<uint8_t>(f.scene+0x29)==1 && route.expect==NativePage::Module,"道具→发型未经模型页中转或完成字节错误");
        f.Customize(2);Put(f.scene+0x310,panel);OnBoundary(2,f.state);OnBoundary(2,f.state);OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"scene:2","panel","scene:3","panel"}) && route.expect==NativePage::Hair,"中转模型页未请求发型页");

        // 父页光标非0：来源直接拒绝；从选曲进入后光标非0也明确停止。
        route={};nativeLog.clear();f.Customize(1);Put(f.scene+0x28,int32_t(1));
        Check(!SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair),message) && message.find("光标")!=std::string::npos
            && route.phase==RoutePhase::Idle && nativeLog.empty(),"父页非0光标仍排队跨页");
        Put(f.scene+0x28,int32_t(0));f.Song();Put(f.base+kTask+0x78,uintptr_t(0));
        Check(ReadNativePage(f.base).page==NativePage::Song,"隔离选曲夹具失效");
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Accessories,600,CustomizationPart::Head),message),"选曲双击道具未排队");
        OnBoundary(1,f.song);Check(Calls(nativeLog,{"song:800"}) && route.phase==RoutePhase::ParentArrival,"选曲未保存PVID并进入自定义");
        f.Customize(1);Put(f.scene+0x28,int32_t(2));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded && route.status.message.find("光标")!=std::string::npos && nativeLog.size()==1,"到达父页光标非0仍自动进入");
        Put(f.scene+0x28,int32_t(0));

        // 目标页内单击：交给执行器并明确未认证，不建立路由、不调用原生导航。
        route={};nativeLog.clear();f.Customize(3);
        auto single=itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair);single.origin=CustomizationOrigin::CardSingleClick;single.firstClickKey.reset();
        Check(!SubmitCustomizationRequest(single,message) && message.find("暂不支持")!=std::string::npos && route.phase==RoutePhase::Idle && nativeLog.empty(),"发型页内单击未明确未认证或产生路由");
        // 非目标页：单击、Enter、底栏、按钮均不跨页；伪造双击的预览按钮同样拒绝。
        f.Customize(2);
        for(auto origin:{CustomizationOrigin::CardSingleClick,CustomizationOrigin::Keyboard,CustomizationOrigin::Footer,CustomizationOrigin::HoverButton}) {
            auto r=itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair);r.origin=origin;r.firstClickKey.reset();
            Check(!SubmitCustomizationRequest(r,message) && route.phase==RoutePhase::Idle && nativeLog.empty(),"发型非双击来源产生跨页");
        }
        auto forgedButton=itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair);forgedButton.action=CustomizationAction::Preview;
        Check(!SubmitCustomizationRequest(forgedButton,message) && route.phase==RoutePhase::Idle && nativeLog.empty(),"伪造双击的发型预览按钮获得跨页权限");

        // 网关按最新目录重算：领域或部位不一致、目标缺失、种类矛盾均拒绝。
        // 参数key/fingerprint为请求稳定键与目录指纹；隔离解析器把500声明为头部道具。
        ConfigureCustomizationTargetResolver([](const CustomizationKey& key,const std::string& fingerprint) -> std::optional<CustomizationTargetFacts> {
            if(fingerprint!="隔离目录" || key.id!=500) return std::nullopt;
            CustomizationTargetFacts facts;facts.domain=CustomizationDomain::Accessories;facts.part=CustomizationPart::Head;return facts;
        });
        Check(!SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair),message) && message.find("不一致")!=std::string::npos,"界面领域与最新目录不一致仍受理");
        Check(!SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,501,CustomizationPart::Hair),message) && message.find("已不在最新")!=std::string::npos,"最新目录缺失目标仍受理");
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Accessories,500,CustomizationPart::Head),message) && route.target==NativePage::Accessory,"目录一致的道具请求被拒绝");
        CancelNativePageRouter();OnBoundary(2,f.state);route={};nativeLog.clear();
        ConfigureCustomizationTargetResolver(nullptr);
        auto contradiction=itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Hair);contradiction.key.kind=CustomizationKind::Module;
        Check(!SubmitCustomizationRequest(contradiction,message) && message.find("种类")!=std::string::npos,"模型键冒充发型领域仍受理");
        Check(!SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,500,CustomizationPart::Head),message) && route.phase==RoutePhase::Idle,"发型请求携带道具部位仍受理");

        // 取消、换槽、原生拒绝：停止后续步骤，不再调用。
        f.Customize(2);Put(f.scene+0x310,panel);
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,502,CustomizationPart::Hair),message),"取消场景未排队");
        CancelNativePageRouter();OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && nativeLog.empty(),"取消后仍执行发型导航");
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,502,CustomizationPart::Hair),message),"换槽场景未排队");
        OnBoundary(2,f.state);Check(Calls(nativeLog,{"scene:3","panel"}),"换槽场景未请求发型页");
        f.Customize(3);Put(f.controller+0x3C,int32_t(1));OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded && nativeLog.size()==2,"进入发型期间换槽后旧目标继续");
        Put(f.controller+0x3C,int32_t(0));nativeLog.clear();acceptScene=false;f.Customize(2);
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,503,CustomizationPart::Hair),message),"拒绝场景未排队");
        OnBoundary(2,f.state);
        Check(Calls(nativeLog,{"scene:3"}) && route.phase==RoutePhase::Done && !route.status.succeeded,"原生未受理后仍隐藏面板或继续");
        acceptScene=true;

        // 歌曲退出共用同层协议：子页→父页键1，父页→来源键8。
        nativeLog.clear();f.Customize(3);Put(f.scene+0x380,panel);Put(f.scene+0x25,uint8_t(0));
        Check(InvokeSongExit(ReadNativePage(f.base),false) && Calls(nativeLog,{"scene:1","panel"}) && Get<uint8_t>(f.scene+0x25)==1,"子页→父页退出协议错误");
        nativeLog.clear();f.Customize(1);Put(f.controller+0x28,int32_t(0));Put(f.scene+0x19,uint8_t(0));
        Check(InvokeSongExit(ReadNativePage(f.base),true) && Calls(nativeLog,{"scene:8"}) && Get<int32_t>(f.controller+0x28)==1 && Get<uint8_t>(f.scene+0x19)==1,"父页→来源退出协议错误");

        // 实际歌者守卫：歌者已知时其他角色发型任何入口都不跨页；未知时可跨页但不可预览／更换；到达后按实际歌者重验。
        std::optional<CustomizationCharacter> testSinger=CustomizationCharacter::Rin;
        // 参数page为隔离页面快照；返回测试控制的实际歌者。
        ConfigureCustomizationSingerResolver([&](const NativePageSnapshot& page) {(void)page;return testSinger;});
        route={};nativeLog.clear();f.Customize(2);Put(f.scene+0x310,panel);
        Check(!SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,510,CustomizationPart::Hair),message) && message.find("仅供浏览")!=std::string::npos
            && route.phase==RoutePhase::Idle && nativeLog.empty(),"其他角色发型的双击仍跨页");
        auto ownHair=itemRequest(CustomizationDomain::Hair,511,CustomizationPart::Hair);ownHair.character=CustomizationCharacter::Rin;
        Check(SubmitCustomizationRequest(ownHair,message) && route.target==NativePage::Hair,"当前歌者自己的发型双击未排队");
        CancelNativePageRouter();OnBoundary(2,f.state);route={};nativeLog.clear();
        testSinger.reset();
        Check(SubmitCustomizationRequest(itemRequest(CustomizationDomain::Hair,512,CustomizationPart::Hair),message),"歌者未知时双击发型未允许跨页");
        OnBoundary(2,f.state);f.Customize(3);OnBoundary(2,f.state);
        testSinger=CustomizationCharacter::Rin;OnBoundary(0,0);OnBoundary(0,0);
        Check(route.phase==RoutePhase::Done && !route.status.succeeded && route.status.message.find("仅供浏览")!=std::string::npos,"到达后未按实际歌者重验发型");
        testSinger.reset();f.Customize(3);
        auto previewHair=itemRequest(CustomizationDomain::Hair,511,CustomizationPart::Hair);previewHair.action=CustomizationAction::Preview;previewHair.origin=CustomizationOrigin::HoverButton;
        Check(!SubmitCustomizationRequest(previewHair,message) && message.find("歌者")!=std::string::npos,"歌者未知时仍可预览发型");
        ConfigureCustomizationSingerResolver(nullptr);route={};nativeLog.clear();
        // 卡片能力：只在空闲目标页标记目标页；未登记Profile时定位、预览、更换全部关闭。
        cachedAt=0;f.Customize(3);auto itemCapability=ReadCustomizationItemActionCapability(CustomizationDomain::Hair);
        Check(itemCapability.targetPage && !itemCapability.locate && !itemCapability.preview && !itemCapability.equip && itemCapability.locateReason.find("暂不支持")!=std::string::npos && CurrentNativePage()==NativePage::Hair,"发型页能力或当前页缓存错误");
        Check(!ReadCustomizationItemActionCapability(CustomizationDomain::Accessories).targetPage,"发型页被当作道具目标页");
        cachedAt=0;f.Customize(2);Check(!ReadCustomizationItemActionCapability(CustomizationDomain::Hair).targetPage,"模型页被当作发型目标页");

        // 播放列表自定义：模型与发型能力不再被强制关闭，动作请求按同一网关进入执行器。
        route={};songRoute={};previewContinuation={};testPreview=false;testBusy=false;testActionStatus={};testAcceptActions=true;
        cachedAt=0;f.Customize(2);Put(f.base+kOuter+0x1C,int32_t(6));Put(f.base+kOuter+0x20,int32_t(7));
        Put(f.controller+0x38,int32_t(1));Put(f.controller+0x3C,int32_t(0));Put(f.controller+8,int32_t(730));
        const auto playlistModule=ReadCustomizationActionCapability(request.key);
        Check(playlistModule.preview && playlistModule.equip,"播放列表来源的模型预览／更换能力仍被禁用");
        auto playlistEquip=request;playlistEquip.action=CustomizationAction::Equip;++playlistEquip.serial;
        const int beforePlaylistActions=testActionCalls;
        Check(SubmitCustomizationRequest(playlistEquip,message) && testActionCalls==beforePlaylistActions+1,"播放列表来源的模型更换仍被网关拒绝");
        CustomizationItemProfile playlistHair;playlistHair.evidence="隔离模拟";
        // 参数frame/out为执行器上下文与快照；只读取隔离数据，不写游戏内存。
        playlistHair.read=[](const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {
            out.pageReady=out.listReady=true;out.part=CustomizationPart::Hair;out.listIdentity=88;
            out.recordIds={511};out.highlightedId=511;out.equippedId=-1;return frame.playlist;
        };
        // 参数frame/index为上下文与列表序号；模拟原位高亮。
        playlistHair.highlight=[](const CustomizationItemFrame& frame,size_t index) {(void)frame;(void)index;return true;};
        playlistHair.preview=[](const CustomizationItemFrame& frame,size_t index) {(void)frame;(void)index;return true;};
        playlistHair.equip=[](const CustomizationItemFrame& frame,size_t index) {(void)frame;(void)index;return true;};
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,playlistHair);
        ConfigureCustomizationSingerResolver([](const NativePageSnapshot&) {return std::optional<CustomizationCharacter>(CustomizationCharacter::Rin);});
        cachedAt=0;f.Customize(3);Put(f.base+kOuter+0x1C,int32_t(6));Put(f.base+kOuter+0x20,int32_t(7));
        Put(f.controller+0x38,int32_t(1));Put(f.controller+0x3C,int32_t(0));Put(f.controller+8,int32_t(730));
        CustomizationKey playlistHairKey;playlistHairKey.kind=CustomizationKind::Accessory;playlistHairKey.id=511;
        const auto playlistHairCapability=ReadCustomizationItemActionCapability(CustomizationDomain::Hair,playlistHairKey);
        Check(playlistHairCapability.preview && playlistHairCapability.equip && playlistHairCapability.targetPage,"播放列表来源的发型预览／更换能力仍被禁用");
        // 离开播放列表外层子状态后，同一页面不再具备播放列表来源的动作权限。
        cachedAt=0;Put(f.base+kOuter+0x20,int32_t(8));
        Check(ReadNativePage(f.base).page==NativePage::Unknown,"未知播放列表外层状态仍被当作可操作页");
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,std::nullopt);
        ConfigureCustomizationSingerResolver(nullptr);route={};nativeLog.clear();testAcceptActions=false;

        // 歌者跟随当前槽位实际装备的模型：换过模型后不得继续沿用 PV 演出角色。
        ConfigureCustomizationSingerResolver([](const NativePageSnapshot&) {return std::optional<CustomizationCharacter>(CustomizationCharacter::Luka);});
        auto moduleCharacters=std::make_shared<std::unordered_map<int,CustomizationCharacter>>();
        (*moduleCharacters)[40]=CustomizationCharacter::Rin;
        PublishCustomizationModelCharacters(moduleCharacters);
        route={};songRoute={};previewContinuation={};testPreview=false;testBusy=false;
        cachedAt=0;f.Customize(3);Put(f.controller+0x38,int32_t(1));Put(f.controller+0x3C,int32_t(0));Put(f.controller+8,int32_t(730));
        Put(f.controller+0x320,int32_t(40));Put(f.base+0x12B5A74,int32_t(3));Put(f.base+kOuter+0x1C,int32_t(6));Put(f.base+kOuter+0x20,int32_t(7));
        const auto modelSinger=ReadCustomizationSingerContext();
        Check(modelSinger && modelSinger->character==CustomizationCharacter::Rin,"换过模型后歌者仍沿用 PV 演出角色");
        auto rinHair=itemRequest(CustomizationDomain::Hair,511,CustomizationPart::Hair);rinHair.character=CustomizationCharacter::Rin;
        SubmitCustomizationRequest(rinHair,message);
        Check(message.find("仅供浏览")==std::string::npos,"当前模型为镜音铃时，镜音铃发型仍被当成其他角色");
        auto mikuHair=itemRequest(CustomizationDomain::Hair,512,CustomizationPart::Hair);mikuHair.character=CustomizationCharacter::Miku;
        SubmitCustomizationRequest(mikuHair,message);
        Check(message.find("仅供浏览")!=std::string::npos,"当前模型为镜音铃时，初音未来发型未被拒绝");
        // 模型映射缺失时退回 PV 演出角色，保持无目录时的既有行为。
        PublishCustomizationModelCharacters(nullptr);cachedAt=0;
        const auto fallbackSinger=ReadCustomizationSingerContext();
        Check(fallbackSinger && fallbackSinger->character==CustomizationCharacter::Luka,"模型映射缺失时未退回 PV 演出角色");
        ConfigureCustomizationSingerResolver(nullptr);route={};nativeLog.clear();
        std::cout<<"原生路由隔离回归通过：页面树、来源、取消、超时、预览后定位／换装／预览、歌曲双击接续、发型／道具泛化路由及按实际模型解析歌者；未访问游戏。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"原生路由验证失败："<<error.what()<<"\n";return 1;}
}
