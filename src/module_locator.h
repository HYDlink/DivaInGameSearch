#pragma once

#include "customization_ui.h"
#include <cstdint>
#include <string>

namespace diva {

// 原生服装定位阶段；完成状态以跨帧读取到的原生列表为准。
enum class ModuleLocatePhase {
    Disabled,
    Idle,
    WaitingForFrame,
    SwitchingCategory,
    Confirming,
    Succeeded,
    Rejected
};

// 面向 UI 的只读定位状态；serial 与 uiGeneration 用于丢弃过期反馈。
struct ModuleLocateStatus {
    ModuleLocatePhase phase=ModuleLocatePhase::Disabled;
    uint64_t serial=0;
    uint64_t uiGeneration=0;
    uint64_t contextGeneration=0;
    bool completed=false;
    bool succeeded=false;
    std::string message="原生服装定位尚未初始化。";
    CustomizationAction action=CustomizationAction::Locate;
    bool previewReady=false,previewRestored=false;
};

// 初始化受支持游戏构建的 Module 原生定位；参数 base 为动态取得的主程序基址。
bool InitializeModuleLocator(uintptr_t base);

// 将 UI 的稳定服装身份排入模型帧定位；参数 request 为 UI 已核对的服装请求，message 接收即时结果。
bool SubmitModuleLocate(const CustomizationLocateRequest& request,std::string& message);

// 由宿主逐帧同步 UI 代数并检测离开模型页；参数 now 为单调毫秒，uiGeneration 为目录快照代数。
void TickModuleLocator(uint64_t now,uint64_t uiGeneration);

// 读取最近一次定位阶段，UI 可轮询但不触发游戏内存操作。
ModuleLocateStatus PollModuleLocator();

// 获取受支持构建中的当前页面上下文代数；没有可用页面时仍保留失效后的代数。
uint64_t ModuleLocatorContextGeneration();
// 递增原子取消代数；不读取或写入游戏对象，可由输入线程立即调用。
void CancelModuleLocate();
// 只读返回已核对的当前角色、歌者槽位与页面代数；读取失败返回未知，不使用默认槽位。
std::optional<CustomizationContext> ReadModuleLocatorContext();

} // namespace diva
