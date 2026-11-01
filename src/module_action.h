#pragma once
#include "module_locator.h"
namespace diva {
// 参数base只由已核验构建的原生路由器传入，不单独绕过profile初始化。
void InitializeModuleActions(uintptr_t base);
// 参数state为原生内部状态帧真实this；拥有读取及动作调用全部在该边界串行执行。
void OnModuleActionStateFrame(uintptr_t state);
// 参数page为真实模型页；跨人物动作每个模型帧只切一步分类，预览退出后恢复原列表高亮。
void OnModuleActionModelFrame(uintptr_t page);
// 参数request/message为卡片稳定身份及即时中文反馈。
bool SubmitModuleAction(const CustomizationLocateRequest& request,std::string& message);
// 参数now/generation为时刻与目录代数；只更新超时、取消及生命周期，不调用原生函数。
void TickModuleAction(uint64_t now,uint64_t generation);
// 参数key为当前卡片，能力只来自真实列表和帧线程缓存。
CustomizationActionCapability ReadModuleActionCapability(const CustomizationKey& key,CustomizationCharacter targetCharacter=CustomizationCharacter::Unknown);
// 返回最近原生动作的跨帧读回状态，与定位状态使用统一序号。
ModuleLocateStatus PollModuleAction();
// 返回是否有正在执行或等待原生退出的动作。
bool ModuleActionBusy();
// 当前插件预览已稳定时允许新请求先退出它；不把加载或换装中途当作可中断预览。
bool ModulePreviewCanExit();
// 参数request/message为后续操作与反馈；只排队原生退出，不在UI线程调用游戏函数。
bool RequestModulePreviewExit(const CustomizationLocateRequest& request,std::string& message);
// 歌曲请求不伪造服装目标；仅要求旧预览退出，歌曲网关继续保存并验证自己的原请求。
bool RequestModulePreviewExitForSong(std::string& message);
// 发布取消代数；已提交动作不自动反向换装，预览交给原生退出。
void CancelModuleAction();
}
