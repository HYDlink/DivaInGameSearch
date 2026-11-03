#pragma once
#include "customization.h"
#include <string>

namespace diva {
// 动作与输入来源分别保存，按钮永远不获得跨页权限。
enum class CustomizationAction { Locate, Preview, Equip, Remove };
enum class CustomizationOrigin { CardSingleClick, CardDoubleClick, HoverButton, Keyboard, Footer };
enum class CustomizationScope { Unknown, Common, Song };
enum class Ownership { Unknown, Unowned, Owned };

// 稳定身份贯穿查询、路由和原生执行，不使用结果行号作为目标。
struct CustomizationLocateRequest {
    CustomizationKey key;
    // 界面领域与实体部位；网关须按最新目录重新核对，不能直接信任界面传入的目标页。
    CustomizationDomain domain=CustomizationDomain::Models;
    CustomizationPart part=CustomizationPart::Unknown;
    CustomizationCharacter character=CustomizationCharacter::Unknown;
    std::string catalogFingerprint;
    uint64_t uiGeneration=0,serial=0,queryRevision=0,contextGeneration=0;
    CustomizationAction action=CustomizationAction::Locate;
    CustomizationOrigin origin=CustomizationOrigin::Keyboard;
    CustomizationScope scope=CustomizationScope::Unknown;
    int slot=-1;
    // 原生范围选择值独立于对象地址；未知保持-1，不能默认为单曲或共通A。
    int nativeScope=-1;
    // 双击首击身份、领域与修订须由真实主体输入记录，不能由普通定位入口补发。
    std::optional<CustomizationKey> firstClickKey;
    std::optional<CustomizationDomain> firstClickDomain;
    uint64_t firstClickRevision=0;
};

// 参数 request/requiresNavigation 为稳定请求和运行时独立计算的跨页需求；返回中文拒绝原因。
// 只有锁定同一 {种类,ID,领域,查询修订} 的鼠标左键主体双击可以跨页；按钮、单击、Enter 与底栏均不放宽。
inline std::string CheckCustomizationNavigation(const CustomizationLocateRequest& request,bool requiresNavigation) {
    if(!requiresNavigation) return {};
    const std::string page=CustomizationDomainPageName(request.domain);
    if(request.action!=CustomizationAction::Locate) return "双击卡片进入"+page+"后使用；操作按钮不会自动跨页。";
    if(request.origin!=CustomizationOrigin::CardDoubleClick || !request.firstClickKey
        || *request.firstClickKey!=request.key || !request.firstClickDomain || *request.firstClickDomain!=request.domain
        || !request.queryRevision || request.firstClickRevision!=request.queryRevision)
        return "双击同一结果卡片进入"+page+"；当前输入只允许原位定位。";
    return {};
}

// 每张卡片独立取得的能力；未知拥有、范围及槽位均不开放写入。
// targetPage 表示当前原生页就是该卡片领域的目标子页；locate 仅对发型／道具表示已认证的原位定位 Profile。
struct CustomizationActionCapability {
    bool targetPage=false,locate=false,preview=false,equip=false,equipped=false,busy=false,remove=false;
    Ownership ownership=Ownership::Unknown;
    uint64_t contextGeneration=0;
    int slot=-1,nativeScope=-1;
    CustomizationScope scope=CustomizationScope::Unknown;
    std::string locateReason="原生列表定位尚未认证。";
    std::string previewReason="原生预览上下文尚未认证。";
    std::string equipReason="拥有状态、槽位及设置范围尚未认证。";
};
}
