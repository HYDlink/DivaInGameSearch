#pragma once
#include "module_locator.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace diva {
// 发型／自定义道具原生页的一帧只读快照；字段只能来自已认证 Profile 的读取器，未知保持默认值。
// 搜索选中、原生高亮、临时预览与实际装备分别表达，不能互相替代。
struct CustomizationItemSnapshot {
    bool pageReady=false,listReady=false;
    // 模型或缩略图仍在加载时为 true；预览与更换须等待加载结束再进入与确认。
    bool loading=false;
    // 原生临时预览是否处于活动状态。
    bool previewActive=false;
    // 道具页当前部位（头／脸／胸口／背部）；发型页为 Hair。
    CustomizationPart part=CustomizationPart::Unknown;
    // 当前列表容器身份（例如容器起始指针）；变化即视为新列表，旧记录作废。
    uintptr_t listIdentity=0;
    // 当前列表按原生顺序排列的稳定 CustomizeItem ID；不是表内序号、obj_id 或 bind_module。
    std::vector<int> recordIds;
    // 与 recordIds 一一对应的实时拥有状态；未知为空，未知或未拥有一律不更换，也不调用购买。
    std::vector<std::optional<bool>> owned;
    // 当前原生高亮在列表中的序号；逐步移动模式需要此读数。
    std::optional<size_t> highlightIndex;
    // 独立读回的原生高亮、临时预览及当前槽位实际装备的稳定 ID。
    std::optional<int> highlightedId,previewId,equippedId;
    // 原始覆盖配置，-1 表示搭配模型／未设置；不得用解析后的默认发型ID替代。
    std::optional<int> rawEquippedId;
    // 六槽的模型、头、脸、胸口、背部、发型配置，用于卸除后逐项确认其他字段未变。
    std::array<std::optional<int>,36> configurationIds{};
    // 各歌者槽位在本部位的实际装备，用于核对预览／更换没有改变其他槽位。
    std::array<std::optional<int>,6> slotEquipped{};
    // 原生读回的当前歌者人物；若提供，须与路由器按槽位解析的歌者一致。
    std::optional<CustomizationCharacter> singer;
};

// 路由器在匹配的原生内部状态帧转交的上下文：页面领域、场景对象与配置身份。
struct CustomizationItemFrame {
    // valid=false 表示当前已不在发型或自定义道具页；idle=false 表示仍在该页但处于过渡／弹层，执行器只等待。
    bool valid=false,idle=false;
    CustomizationDomain domain=CustomizationDomain::Hair;
    uintptr_t scene=0,state=0,controller=0,configuration=0;
    int slot=-1,slotCount=-1,nativeScope=-1,sourcePvid=-1;
    // 外层自定义菜单子状态：播放列表来源（mode2 子状态6／7）的事务必须绑定同一子状态，单曲来源沿用既有判据。
    bool playlist=false;
    int mode=-1,currentSub=-1,nextSub=-1;
    // 当前实际歌者槽位所属人物；由路由器按来源歌曲与槽位解析，未知为空，此时预览与更换一律拒绝。
    std::optional<CustomizationCharacter> singer;
    uint64_t now=0;
    uint32_t thread=0;
};

// 证据 Profile：每个回调都必须有本机实机证据（构建哈希、RVA、结构偏移、边界与读回序列）。
// 生产 Profile 由 customization_item_profiles.cpp 依据已认证规格构造；规格缺项时不登记，所有动作明确 Unsupported。
// 禁止把模型页 scene+0x60/+0x68、分类函数 0x694D00、动作回调 0x695250、command3/7 或随机／卸除哨兵填作默认值。
struct CustomizationItemProfile {
    std::string evidence;
    // 当前 PC 动作需要先完成真实高亮，再在下一帧查询拥有并准备菜单目标；其他规格可自行处理目标。
    bool requiresHighlightedAction=false;
    // 参数 frame/out 为当前帧上下文与输出快照；读取失败返回 false，不写游戏内存。
    std::function<bool(const CustomizationItemFrame&,CustomizationItemSnapshot&)> read;
    // 参数 frame/direction 为当前帧与部位切换方向（-1／+1）；仅道具页需要，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,int)> switchPart;
    // 参数 frame/index 为当前帧与当前列表序号；已认证的直接高亮入口，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,size_t)> highlight;
    // 参数 frame/direction 为当前帧与左右方向；已认证的逐步移动入口，与 highlight 二选一，未认证时为空。
    std::function<bool(const CustomizationItemFrame&,int)> step;
    // 参数 frame/index 同上；已认证的临时预览与普通更换入口（不得是购买命令），未认证时为空。
    std::function<bool(const CustomizationItemFrame&,size_t)> preview,equip;
    // 参数 frame/index 为场景与当前真实装备的列表索引；只卸除该装备，不写配置ID。
    std::function<bool(const CustomizationItemFrame&,size_t)> remove;
    // 参数 frame 为当前帧；已认证的退出临时预览入口，未认证时为空（只能等待玩家原生 Esc 退出）。
    std::function<bool(const CustomizationItemFrame&)> exitPreview;
};

// 参数 domain/profile 为领域及已认证 Profile；传空值撤销。生产由规格构造器调用，隔离测试注入模拟 Profile。
void RegisterCustomizationItemProfile(CustomizationDomain domain,std::optional<CustomizationItemProfile> profile);
// 参数 domain 为发型或自定义道具；返回 Profile 能力与简短原因，不读取游戏内存，targetPage 由路由器填写。
CustomizationActionCapability CustomizationItemCapabilities(CustomizationDomain domain,std::optional<CustomizationKey> key=std::nullopt);
// 返回是否有事务或界面请求的装备观察；路由器只在匹配的游戏线程帧读取原生数据。
bool CustomizationItemFrameNeeded();
// 参数 request/frame/message 为网关核对后的请求、提交时的目标页上下文与即时反馈；未知 Profile 明确拒绝。
bool SubmitCustomizationItemRequest(const CustomizationLocateRequest& request,const CustomizationItemFrame& frame,std::string& message);
// 参数 frame 为原生内部状态帧上下文；每帧最多执行一步原生调用，确认阶段只读回。
void OnCustomizationItemFrame(const CustomizationItemFrame& frame);
// 参数 now/uiGeneration 为单调毫秒及目录代数；只推进超时与代数取消，不调用原生函数。
void TickCustomizationItemLocator(uint64_t now,uint64_t uiGeneration);
// 返回最近一次发型／道具事务状态，与模型定位共用状态结构及序号语义。
ModuleLocateStatus PollCustomizationItemLocator();
// 返回是否仍有未完成的发型／道具事务（含仍在进行的临时预览）。
bool CustomizationItemLocatorBusy();
// 返回当前是否处于由本执行器进入的临时预览。
bool CustomizationItemPreviewActive();
// 请求在下一原生帧退出临时预览；Profile 未提供退出入口时返回 false，保持等待玩家原生退出。
bool RequestCustomizationItemPreviewExit(std::string& message);
// 发布取消代数；已执行的原生调用不反向撤销，临时预览交由原生退出后再核对装备。
void CancelCustomizationItemLocator();
}
