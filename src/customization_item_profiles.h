#pragma once
#include "customization_item_locator.h"
#include <array>
#include <optional>
#include <string>

namespace diva {
// 发型／自定义道具原生规格：每一项都必须来自本机实机认证（构建 SHA-256、自然调用样例、成功与拒绝时的字段序列）。
// 未填写的项保持为空；能力只有在所需各项齐全时才开放，缺项即该能力 Unsupported，不从模型页复制默认值。
// 函数签名取自已记录的静态候选形状：逐步移动／部位切换 fn(scene,int)；动作回调 fn(callbackObject,int command)。
struct CustomizationItemNativeSpec {
    // 证据说明（报告或证据目录）；为空表示未认证，整个规格不登记。
    std::string evidence;
    // 列表：scene 内记录指针数组 [begin,end) 偏移（每项 8 字节指针），记录内 int32 稳定 ID 与 uint8 拥有标记偏移。
    std::optional<uintptr_t> listBegin,listEnd,recordId,recordOwned;
    // scene 内 int32 当前高亮序号；道具页 scene 内 int32 当前部位序号（按 partOrder 映射）。
    std::optional<uintptr_t> highlightIndex,partIndex;
    std::array<CustomizationPart,4> partOrder{CustomizationPart::Head,CustomizationPart::Face,CustomizationPart::Neck,CustomizationPart::Back};
    // scene 内 uint8 加载中标记、uint8 预览活动标记与 int32 预览目标 ID。
    std::optional<uintptr_t> loadingFlag,previewFlag,previewId;
    // controller 内各歌者槽位本部位实际装备 int32 的起始偏移与槽位步长。
    std::optional<uintptr_t> equippedBase,equippedStride;
    // 原生入口 RVA：逐步移动 void(scene*,int dir)、部位切换 void(scene*,int dir)、直接高亮 void(scene*,int index)。
    std::optional<uintptr_t> stepRva,partRva,highlightRva;
    // 高亮控件内嵌于 scene 的偏移及卡片刷新入口；为空时保留普通 scene/index ABI，供其他已认证规格使用。
    std::optional<uintptr_t> highlightControl,refreshRva;
    // scene 内动作回调对象指针偏移与回调 RVA uint8(callback*,int command)；预览、更换、退出预览命令号须独立认证。
    std::optional<uintptr_t> callbackObject,callbackRva;
    std::optional<int32_t> previewCommand,equipCommand,exitPreviewCommand;
};

// 参数 domain/spec/base 为领域、规格与已核验主模块基址；所需事实不齐或命中模型页入口时返回空，不登记任何能力。
std::optional<CustomizationItemProfile> BuildCustomizationItemProfile(CustomizationDomain domain,const CustomizationItemNativeSpec& spec,uintptr_t base);
// 参数 base 为路由器已核验的主模块基址；按生产规格表登记 Profile，返回登记数量（规格为空时为 0）。
int InitializeCustomizationItemProfiles(uintptr_t base);
// 返回最近一次生产登记的诊断短句，供日志使用。
std::string CustomizationItemProfileDiagnostics();
}
