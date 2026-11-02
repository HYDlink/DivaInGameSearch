#pragma once
#include "module_locator.h"
#include <functional>
#include <optional>
#include <vector>

namespace diva {
// 发型／自定义道具原生页的一帧只读快照；字段只能来自已认证 Profile 的读取器，未知保持默认值。
// 搜索选中、原生高亮、临时预览与实际装备分别表达，不能互相替代。
struct CustomizationItemSnapshot {
    bool pageReady=false,listReady=false;
    // 道具页当前部位（头／脸／胸口／背部）；发型页为 Hair。
    CustomizationPart part=CustomizationPart::Unknown;
    // 当前列表容器身份（例如容器起始指针）；变化即视为新列表，旧记录作废。
    uintptr_t listIdentity=0;
    // 当前列表按原生顺序排列的稳定 CustomizeItem ID；不是表内序号、obj_id 或 bind_module。
    std::vector<int> recordIds;
    // 独立读回的原生高亮、临时预览及当前槽位实际装备的稳定 ID。
    std::optional<int> highlightedId,previewId,equippedId;
};

// 路由器在匹配的原生内部状态帧转交的上下文：页面领域、场景对象与配置身份。
struct CustomizationItemFrame {
    // valid=false 表示当前已不在发型或自定义道具页；idle=false 表示仍在该页但处于过渡／弹层，执行器只等待。
    bool valid=false,idle=false;
    CustomizationDomain domain=CustomizationDomain::Hair;
    uintptr_t scene=0,state=0,controller=0,configuration=0;
    int slot=-1,slotCount=-1,nativeScope=-1,sourcePvid=-1;
    uint64_t now=0;
    uint32_t thread=0;
};

// 证据 Profile：每个回调都必须有交接报告第 9 节要求的实机证据（构建哈希、RVA、结构偏移、边界与读回序列）。
// 生产当前未登记任何 Profile，所有发型／道具原生定位、预览与更换均明确返回 Unsupported。
// 禁止把模型页 scene+0x60/+0x68、分类函数 0x694D00、动作回调 0x695250 或 command3/7 填作默认值。
struct CustomizationItemProfile {
    std::string evidence;
    // 参数 frame/out 为当前帧上下文与输出快照；读取失败返回 false，不写游戏内存。
    std::function<bool(const CustomizationItemFrame&,CustomizationItemSnapshot&)> read;
    // 参数 frame/direction 为当前帧与部位切换方向（-1／+1）；仅道具页需要，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,int)> switchPart;
    // 参数 frame/index 为当前帧与当前列表序号；已认证的原位高亮入口，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,size_t)> highlight;
    // 参数 frame/index 同上；已认证的临时预览与普通更换入口，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,size_t)> preview,equip;
};

// 参数 domain/profile 为领域及已认证 Profile；传空值撤销。生产代码不调用，隔离测试注入模拟 Profile。
void RegisterCustomizationItemProfile(CustomizationDomain domain,std::optional<CustomizationItemProfile> profile);
// 参数 domain 为发型或自定义道具；返回 Profile 能力与中文原因，不读取游戏内存，targetPage 由路由器填写。
CustomizationActionCapability CustomizationItemCapabilities(CustomizationDomain domain);
// 参数 request/frame/message 为网关核对后的请求、提交时的目标页上下文与即时反馈；未知 Profile 明确拒绝。
bool SubmitCustomizationItemRequest(const CustomizationLocateRequest& request,const CustomizationItemFrame& frame,std::string& message);
// 参数 frame 为原生内部状态帧上下文；每帧最多执行一步原生调用，确认阶段只读回。
void OnCustomizationItemFrame(const CustomizationItemFrame& frame);
// 参数 now/uiGeneration 为单调毫秒及目录代数；只推进超时与代数取消，不调用原生函数。
void TickCustomizationItemLocator(uint64_t now,uint64_t uiGeneration);
// 返回最近一次发型／道具事务状态，与模型定位共用状态结构及序号语义。
ModuleLocateStatus PollCustomizationItemLocator();
// 返回是否仍有未完成的发型／道具事务。
bool CustomizationItemLocatorBusy();
// 发布取消代数；已执行的原生调用不反向撤销。
void CancelCustomizationItemLocator();
}
