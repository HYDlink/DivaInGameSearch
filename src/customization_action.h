#pragma once
#include "customization.h"
#include <string>

namespace diva {
// 动作与输入来源分别保存，按钮永远不获得跨页权限。
enum class CustomizationAction { Locate, Preview, Equip };
enum class CustomizationOrigin { CardSingleClick, CardDoubleClick, HoverButton, Keyboard, Footer };
enum class CustomizationScope { Unknown, Common, Song };
enum class Ownership { Unknown, Unowned, Owned };

// 稳定身份贯穿查询、路由和原生执行，不使用结果行号作为目标。
struct CustomizationLocateRequest {
    CustomizationKey key;
    CustomizationCharacter character=CustomizationCharacter::Unknown;
    std::string catalogFingerprint;
    uint64_t uiGeneration=0,serial=0,queryRevision=0,contextGeneration=0;
    CustomizationAction action=CustomizationAction::Locate;
    CustomizationOrigin origin=CustomizationOrigin::Keyboard;
    CustomizationScope scope=CustomizationScope::Unknown;
    int slot=-1;
    // 原生范围选择值独立于对象地址；未知保持-1，不能默认为单曲或共通A。
    int nativeScope=-1;
    // 双击首击身份与修订须由真实主体输入记录，不能由普通定位入口补发。
    std::optional<CustomizationKey> firstClickKey;
    uint64_t firstClickRevision=0;
};

// 参数 request/requiresNavigation 为稳定请求和运行时独立计算的跨页需求；返回中文拒绝原因。
inline std::string CheckCustomizationNavigation(const CustomizationLocateRequest& request,bool requiresNavigation) {
    if(!requiresNavigation) return {};
    if(request.action!=CustomizationAction::Locate) return "双击卡片进入模型页后使用；操作按钮不会自动跨页。";
    if(request.origin!=CustomizationOrigin::CardDoubleClick || !request.firstClickKey
        || *request.firstClickKey!=request.key || !request.queryRevision
        || request.firstClickRevision!=request.queryRevision)
        return "双击同一结果卡片进入模型页；当前输入只允许原位定位。";
    return {};
}

// 每张卡片独立取得的能力；未知拥有、范围及槽位均不开放写入。
struct CustomizationActionCapability {
    bool modelPage=false,preview=false,equip=false,equipped=false,busy=false;
    Ownership ownership=Ownership::Unknown;
    uint64_t contextGeneration=0;
    int slot=-1,nativeScope=-1;
    CustomizationScope scope=CustomizationScope::Unknown;
    std::string previewReason="原生预览上下文尚未认证。";
    std::string equipReason="拥有状态、槽位及设置范围尚未认证。";
};
}
