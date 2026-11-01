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
CustomizationActionCapability ReadModuleActionCapability(const CustomizationKey& key,CustomizationCharacter character) {(void)key;(void)character;CustomizationActionCapability c;if(testAcceptActions){c.modelPage=c.preview=c.equip=true;c.contextGeneration=91;c.slot=0;c.nativeScope=3;c.scope=CustomizationScope::Song;}return c;}
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
}
namespace {
// 参数condition/message为边界断言与中文失败说明。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 参数address/value为隔离夹具字段；只写测试自己持有的内存。
template<class T> void Put(uintptr_t address,T value) {std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));}
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
        Fixture f;using namespace diva;
        Check(ReadNativePage(f.base).page==NativePage::Home,"主页读取失败");
        Put(f.base+kOuter+0x11,uint8_t(1));Check(ReadNativePage(f.base).page==NativePage::Home,"把相邻字节当模式切换标记");
        Put(f.base+kOuter+0x10,uint8_t(1));Check(ReadNativePage(f.base).page==NativePage::Unknown,"过渡主页授予导航权限");
        Put(f.base+kOuter+0x10,uint8_t(0));f.Song();Check(ReadNativePage(f.base).page==NativePage::Song,"选曲来源读取失败");
        for(int key=1;key<=4;++key) {f.Customize(key);Check(ReadNativePage(f.base).key==key && ReadNativePage(f.base).idle,"场景树页面读取失败");}
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
        request.origin=CustomizationOrigin::CardDoubleClick;request.firstClickKey=request.key;request.firstClickRevision=3;
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
        f.Customize(1);route.source=ReadNativePage(f.base);route.phase=RoutePhase::ModelArrival;
        f.Customize(2);OnBoundary(2,f.state);
        Check(route.phase==RoutePhase::LocateReady,"同一配置的预期场景切换被误取消");
        f.Customize(1);route.source=ReadNativePage(f.base);route.phase=RoutePhase::ModelArrival;
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
        std::cout<<"原生路由隔离回归通过：页面树、来源、取消、超时、预览后定位／换装／预览及歌曲双击接续；未访问游戏。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"原生路由验证失败："<<error.what()<<"\n";return 1;}
}
