#pragma once
#include "module_locator.h"
#include "selector.h"
#include <functional>

namespace diva {
// 页身份来自类型化的外层字段和已核查场景树；未知页不授予导航权限。
enum class NativePage { Unknown, Home, Song, Parent, Module, Hair, Accessory };
struct NativePageSnapshot {
    NativePage page=NativePage::Unknown;
    uintptr_t controller=0,configuration=0,manager=0,head=0,node=0,target=0,scene=0,state=0,song=0;
    int mode=-1,modeProcess=-1,currentSub=-1,nextSub=-1,subProcess=-1,key=-1,transition=-1;
    int internal=-1,parentCursor=-1,parentPhase=-1,popup=-1,pvid=-1,songState=-1;
    uint8_t modeSwitch=1,subSwitch=1;
    int slot=-1,slotCount=-1,nativeScope=-1,sourcePvid=-1;
    bool idle=false;
};
// 参数 base 为已通过 SHA-256 核验的动态基址；只读当前页面，不扫描驻留对象。
NativePageSnapshot ReadNativePage(uintptr_t base);
// 参数 base 为定位器已核验基址，先核对入口再链式安装三种真实帧边界。
bool InitializeNativePageRouter(uintptr_t base);
// 参数 request/message 为统一稳定目标及即时反馈；跨页权限在此独立核查。
bool SubmitCustomizationRequest(const CustomizationLocateRequest& request,std::string& message);
// 参数 now/generation 为单调时刻及UI目录代数；只推进超时与读回，不执行原生函数。
void TickNativePageRouter(uint64_t now,uint64_t generation);
// 返回路由或服装定位的单条真实状态。
ModuleLocateStatus PollNativePageRouter();
// 立即发布取消代数，已受理的原生退出由游戏自然收尾，不逆向覆盖玩家行为。
void CancelNativePageRouter();
// 参数 key 为具体卡片；读取当前能力，未知范围、槽位与拥有状态保持关闭。
CustomizationActionCapability ReadCustomizationActionCapability(const CustomizationKey& key,CustomizationCharacter targetCharacter=CustomizationCharacter::Unknown);
// 歌曲路由进度只描述真实阶段；定位成功仍由原歌曲Selector独立读回。
struct SongNavigationStatus {uint64_t serial=0;bool busy=false,completed=false,succeeded=false;std::string message;};
// 参数submit/cancel为既有歌曲定位器的提交与取消接线，不在原生导航帧直接写选曲。
void ConfigureSongNavigation(std::function<bool(Request,std::string&)> submit,std::function<void()> cancel);
// 参数report接收共享路由Hook自身CPU耗时，不包含被保留的游戏原帧函数。
void ConfigureNativePerformance(std::function<void(double)> report);
// 参数request/available/message为稳定歌曲、谱面与反馈；网关只允许主体双击或难度按钮跨页。
bool SubmitSongNavigation(const Request& request,bool available,std::string& message);
// 参数now/generation/revision为当前时刻与UI身份；到达后才提交既有定位器。
void TickSongNavigation(uint64_t now,uint64_t generation,uint64_t revision);
// 参数success/message为既有Selector独立读回结果，不把页面到达当定位成功。
void CompleteSongNavigation(bool success,const std::string& message);
// 取消后续歌曲路由，已执行的原生退出由游戏完成，不反向跳页。
void CancelSongNavigation();
// 返回歌曲路由及定位交接的实际进度。
SongNavigationStatus PollSongNavigation();
}
