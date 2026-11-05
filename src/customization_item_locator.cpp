#include "i18n.h"
#include "customization_item_locator.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>

namespace diva {
namespace item_runtime {
constexpr uint64_t kStepTimeout=12000;
constexpr int kMaximumPartSteps=4;
constexpr int kConfirmFrames=2;
constexpr int kConfirmMisses=3;
constexpr size_t kMaximumHighlightSteps=1024;
// 事务阶段：Preparing 切部位／移动高亮并发出原生调用；Confirming 只读确认；PreviewActive 为已确认的临时预览，等待原生退出后核对装备。
enum class Phase { Idle, Preparing, Confirming, PreviewActive, Done };
// 预览／更换前的装备备份：当前槽位与全部槽位在本部位的实际装备。
struct Baseline {
    std::optional<int> equipped;
    std::array<std::optional<int>,6> slots{};
    std::array<std::optional<int>,36> configurationIds{};
};
struct Active {
    Phase phase=Phase::Idle;
    CustomizationLocateRequest request;
    CustomizationItemFrame submitted;
    uint64_t cancelled=0,deadline=0;
    uintptr_t expectedList=0;
    CustomizationPart expectedPart=CustomizationPart::Unknown;
    int partSteps=0,matches=0,misses=0;
    size_t highlightSteps=0;
    Baseline baseline;
    bool exitRequested=false,exitCalled=false;
};
std::mutex mutex;
std::atomic<uint64_t> cancellation{0};
std::array<std::optional<CustomizationItemProfile>,3> profiles;
struct Observation {CustomizationItemFrame frame;CustomizationItemSnapshot snapshot;};
std::array<std::optional<Observation>,3> observations;
std::atomic<bool> observationRequested{false};
uint64_t latestNow=0;
Active active;
ModuleLocateStatus status;
uint64_t lastSerial=0,uiGeneration=0;
std::string lockedFingerprint;

// 参数 domain/action 为领域与动作；返回玩家可见的短句，不暴露偏移、RVA 或认证过程。
std::string ShortGap(CustomizationDomain domain,CustomizationAction action) {
    if(action==CustomizationAction::Locate) return domain==CustomizationDomain::Hair?Tr(Tx::itemop_locate_hair_unsupported):Tr(Tx::itemop_locate_accessory_unsupported);
    return Tr(Tx::locate_unsupported);
}
// 参数 domain/action 为领域与动作；返回只写入诊断日志的开发原因，列出缺失的原生证据。
std::string DetailGap(CustomizationDomain domain,CustomizationAction action) {
    const std::string noun=domain==CustomizationDomain::Hair?"发型":"自定义道具";
    if(action==CustomizationAction::Locate) return noun+"原生 Profile 未登记：列表容器、稳定ID→记录映射、高亮入口或跨帧读回字段尚无已认证规格。";
    if(action==CustomizationAction::Preview) return noun+"原生 Profile 未登记：预览入口、加载状态、临时显示与原装备读回尚无已认证规格。";
    return noun+"原生 Profile 未登记：更换入口、实时拥有判定、槽位范围与装备读回尚无已认证规格。";
}
// 参数 domain 为领域，返回 Profile 数组下标；模型不使用本执行器。
std::optional<size_t> Slot(CustomizationDomain domain) {
    if(domain==CustomizationDomain::Models) return std::nullopt;
    return CustomizationDomainIndex(domain);
}
// 参数 a/b 为提交时与当前帧上下文；同一事务内不允许换场景、换配置、换槽、换范围、换歌者或换来源歌曲。
bool SameContext(const CustomizationItemFrame& a,const CustomizationItemFrame& b) {
    return a.valid && b.valid && a.domain==b.domain && a.scene==b.scene && a.state==b.state && a.controller==b.controller
        && a.configuration==b.configuration && a.slot==b.slot && a.slotCount==b.slotCount
        && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid && a.singer==b.singer;
}
// 参数 phase/text/completed/succeeded/stage 为对外状态；只由本执行器在锁内写入。
void Set(ModuleLocatePhase phase,const std::string& text,bool completed,bool succeeded,CustomizationStage stage=CustomizationStage::None) {
    status.phase=phase;status.serial=active.request.serial;status.uiGeneration=active.request.uiGeneration;
    status.message=text;status.completed=completed;status.succeeded=succeeded;status.action=active.request.action;status.stage=stage;
}
// 参数 text/success/stage/detail 为结束原因、是否成功、已确认阶段与日志细节；失败后不自动重试，也不反向撤销。
void Finish(const std::string& text,bool success=false,CustomizationStage stage=CustomizationStage::None,const std::string& detail={}) {
    active.phase=Phase::Done;
    const bool restored=stage==CustomizationStage::PreviewRestored;
    Set(success?ModuleLocatePhase::Succeeded:ModuleLocatePhase::Rejected,text,true,success,stage);
    status.detail=detail;status.previewRestored=restored && success;
    status.previewReady=false;
}
// 参数 part 为道具部位，返回原生左侧列表顺序（头0、脸1、胸口2、背部3）；胸口与背部顺序待本机核对。
int PartOrder(CustomizationPart part) {
    return part==CustomizationPart::Head?0:part==CustomizationPart::Face?1:part==CustomizationPart::Neck?2:part==CustomizationPart::Back?3:-1;
}
// 参数 action/id 为动作与稳定ID；返回已确认阶段的玩家短句。
std::string Confirmed(CustomizationAction action,int id) {
    const auto value=std::to_string(id);
    if(action==CustomizationAction::Locate) return Trf(Tx::itemop_located,{{"id",value}});
    if(action==CustomizationAction::Equip) return Trf(Tx::itemop_equipped,{{"id",value}});
    if(action==CustomizationAction::Remove) return Tr(Tx::itemop_removed_settings);
    return Trf(Tx::itemop_previewing,{{"id",value}});
}
// 参数 snapshot/baseline/slot 为当前读数、备份与当前槽位；核对其他槽位装备未被改变。
bool OtherSlotsUnchanged(const CustomizationItemSnapshot& snapshot,const Baseline& baseline,int slot) {
    for(int index=0;index<6;++index) if(index!=slot && snapshot.slotEquipped[static_cast<size_t>(index)]!=baseline.slots[static_cast<size_t>(index)]) return false;
    return true;
}
// 返回本执行器是否持有未完成事务；预览活动期间即使收到取消也保持忙碌，直到原生退出并核对装备。
bool BusyLocked() {
    if(active.phase==Phase::PreviewActive) return true;
    return (active.phase==Phase::Preparing || active.phase==Phase::Confirming) && active.cancelled==cancellation.load();
}
}

void RegisterCustomizationItemProfile(CustomizationDomain domain,std::optional<CustomizationItemProfile> profile) {
    using namespace item_runtime;
    std::lock_guard lock(mutex);
    const auto slot=Slot(domain);if(!slot) return;
    profiles[*slot]=std::move(profile);
    observations[*slot].reset();
}

CustomizationActionCapability CustomizationItemCapabilities(CustomizationDomain domain,std::optional<CustomizationKey> key) {
    using namespace item_runtime;
    CustomizationActionCapability c;
    c.locateReason=ShortGap(domain,CustomizationAction::Locate);
    c.previewReason=c.equipReason=ShortGap(domain,CustomizationAction::Preview);
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) {c.busy=true;return c;}
    const auto slot=Slot(domain);if(!slot || !profiles[*slot]) return c;
    const auto& profile=*profiles[*slot];
    // 定位需要只读快照与直接高亮或逐步移动入口；预览与更换另需各自入口，拥有与槽位在执行时实时核对。
    c.locate=bool(profile.read) && (bool(profile.highlight) || bool(profile.step));
    c.preview=bool(profile.read) && bool(profile.preview);
    c.equip=bool(profile.read) && bool(profile.equip);
    if(c.locate) c.locateReason.clear();
    if(c.preview) c.previewReason.clear();
    if(c.equip) c.equipReason.clear();
    if(key) {
        observationRequested.store(true);
        const auto& observed=observations[*slot];
        if(observed && latestNow>=observed->frame.now && latestNow-observed->frame.now<500) {
            const auto& frame=observed->frame;const auto& snapshot=observed->snapshot;
            c.slot=frame.slot;c.nativeScope=frame.nativeScope;c.contextGeneration=frame.scene;
            c.scope=frame.nativeScope==3?CustomizationScope::Song:CustomizationScope::Common;
            if(frame.slot>=0 && frame.slot<6) {
                const size_t start=static_cast<size_t>(frame.slot)*6;
                for(size_t part=domain==CustomizationDomain::Hair?5:1;part<=(domain==CustomizationDomain::Hair?5:4);++part)
                    if(snapshot.configurationIds[start+part]==key->id) c.equipped=true;
                c.remove=c.equipped && bool(profile.remove) && frame.nativeScope==3;
                if(c.equipped) c.equipReason=c.remove?(domain==CustomizationDomain::Hair?Tr(Tx::itemop_restore_module_hair):Tr(Tx::itemop_remove_equipped)) :Tr(Tx::itemop_remove_unavailable);
            }
        }
    }
    c.busy=BusyLocked() && !(active.phase==Phase::PreviewActive && profile.exitPreview);
    return c;
}

bool SubmitCustomizationItemRequest(const CustomizationLocateRequest& request,const CustomizationItemFrame& frame,std::string& message) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) {message=Tr(Tx::itemop_processing);return false;}
    if(BusyLocked()) {message=active.phase==Phase::PreviewActive?Tr(Tx::itemop_preview_active):Tr(Tx::itemop_previous_running);return false;}
    // 拒绝时也发布带序号的状态，使界面不会把旧事务结果误认为本次反馈。
    const auto reject=[&](const std::string& why,const std::string& detail={}) {
        active={};active.request=request;Finish(why,false,CustomizationStage::None,detail);message=why;return false;
    };
    const auto slot=Slot(request.domain);
    if(!slot || request.key.kind!=CustomizationKind::Accessory || request.key.id<0) return reject(Tr(Tx::itemop_invalid_target));
    if(request.domain==CustomizationDomain::Hair && request.part!=CustomizationPart::Hair) return reject(Tr(Tx::itemop_part_mismatch));
    if(request.domain==CustomizationDomain::Accessories && PartOrder(request.part)<0) return reject(Tr(Tx::itemop_part_missing));
    if(!frame.valid || !frame.idle || frame.domain!=request.domain) return reject(Trf(Tx::itemop_not_idle,{{"page",CustomizationDomainPageName(request.domain)}}));
    // 实际歌者守卫：发型只允许当前歌者自己的发型；预览与更换要求歌者已知且与目标兼容（共通 ALL 兼容任一歌者）。
    const bool compatible=frame.singer && (request.character==CustomizationCharacter::All || request.character==*frame.singer);
    if(request.domain==CustomizationDomain::Hair && frame.singer && !compatible) return reject(Tr(Tx::itemop_other_character));
    if(request.action!=CustomizationAction::Locate && !frame.singer) return reject(Tr(Tx::itemop_singer_unknown));
    if(request.action!=CustomizationAction::Locate && !compatible) return reject(Tr(Tx::itemop_singer_incompatible));
    const auto& profile=profiles[*slot];
    // 未登记 Profile 或缺少对应入口一律 Unsupported：不写缓存、不写高亮序号、不返回成功。
    const bool supported=profile && profile->read && (request.action==CustomizationAction::Locate?(bool(profile->highlight) || bool(profile->step))
        :request.action==CustomizationAction::Preview?bool(profile->preview):request.action==CustomizationAction::Remove?bool(profile->remove):bool(profile->equip));
    if(!supported) return reject(ShortGap(request.domain,request.action),DetailGap(request.domain,request.action));
    if(request.action==CustomizationAction::Remove && (frame.nativeScope!=3 || (request.contextGeneration && request.contextGeneration!=frame.scene)))
        return reject(Tr(Tx::itemop_scope_changed));
    if(!request.serial || request.serial<=lastSerial) return reject(Tr(Tx::itemop_stale_request));
    if(request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedFingerprint.empty() && lockedFingerprint!=request.catalogFingerprint)) return reject(Tr(Tx::itemop_catalog_changed));
    lockedFingerprint=request.catalogFingerprint;lastSerial=request.serial;uiGeneration=request.uiGeneration;
    active={};active.phase=Phase::Preparing;active.request=request;active.submitted=frame;
    active.cancelled=cancellation.load();active.deadline=frame.now+kStepTimeout;
    Set(ModuleLocatePhase::WaitingForFrame,Tr(Tx::itemop_checking_list),false,false);status.detail.clear();
    message=status.message;return true;
}

void OnCustomizationItemFrame(const CustomizationItemFrame& frame) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) return;
    latestNow=frame.now;
    if(observationRequested.load() && frame.valid && frame.idle) {
        const auto slot=Slot(frame.domain);
        if(slot && profiles[*slot] && profiles[*slot]->read) {
            CustomizationItemSnapshot snapshot;
            if(profiles[*slot]->read(frame,snapshot) && snapshot.pageReady && !snapshot.loading && !snapshot.previewActive) {
                observations[*slot]=Observation{frame,std::move(snapshot)};observationRequested.store(false);
            }
        }
    }
    if(active.phase!=Phase::Preparing && active.phase!=Phase::Confirming && active.phase!=Phase::PreviewActive) return;
    const bool previewing=active.phase==Phase::PreviewActive;
    // 已进入的临时预览不因取消或超时中断核对：只等待原生退出，再独立核对原装备与其他槽位。
    if(!previewing) {
        if(active.cancelled!=cancellation.load()) {Finish(Tr(Tx::itemop_cancelled_changed));return;}
        if(frame.now>active.deadline) {Finish(Tr(Tx::itemop_list_timeout));return;}
    }
    if(!frame.valid || frame.domain!=active.request.domain) {Finish(previewing?std::string(Tr(Tx::itemop_preview_left)):Trf(Tx::itemop_left_page,{{"page",CustomizationDomainPageName(active.request.domain)}}));return;}
    if(!SameContext(active.submitted,frame)) {Finish(previewing?Tr(Tx::itemop_preview_context_changed):Tr(Tx::itemop_context_changed));return;}
    // 界面线程提交时线程未知，首个原生帧锁定线程；之后换线程即取消。
    if(!active.submitted.thread) active.submitted.thread=frame.thread;
    else if(frame.thread!=active.submitted.thread) {Finish(Tr(Tx::itemop_thread_changed));return;}
    if(!frame.idle && active.phase==Phase::Preparing) return;
    const auto& profile=profiles[*Slot(active.request.domain)];
    if(!profile || !profile->read) {Finish(Tr(Tx::locate_unsupported),false,CustomizationStage::None,"证据Profile已撤销，未继续执行。");return;}
    CustomizationItemSnapshot snapshot;
    // 读取失败或列表未就绪只等待下一帧；页面到达、列表就绪与目标高亮是不同阶段。
    if(!profile->read(frame,snapshot) || !snapshot.pageReady) return;
    if(snapshot.singer && frame.singer && *snapshot.singer!=*frame.singer) {Finish(Tr(Tx::itemop_singer_mismatch));return;}
    const int id=active.request.key.id;
    const auto action=active.request.action;
    if(previewing) {
        if(active.exitRequested && !active.exitCalled && profile->exitPreview) {active.exitCalled=true;if(!profile->exitPreview(frame)) active.exitRequested=false;return;}
        if(snapshot.previewActive || snapshot.loading) return;
        // 预览已退出：当前槽位与其他槽位装备必须与预览前完全一致，不自动写回。
        if(snapshot.equippedId!=active.baseline.equipped || !OtherSlotsUnchanged(snapshot,active.baseline,frame.slot)) {
            Finish(Tr(Tx::itemop_preview_exit_mismatch));return;
        }
        Finish(Tr(Tx::itemop_preview_exited),true,CustomizationStage::PreviewRestored);return;
    }
    if(!snapshot.listReady) return;
    if(active.phase==Phase::Preparing) {
        if(active.request.domain==CustomizationDomain::Accessories && snapshot.part!=active.request.part) {
            const int current=PartOrder(snapshot.part),target=PartOrder(active.request.part);
            if(!profile->switchPart) {Finish(Tr(Tx::itemop_cross_part_unsupported),false,CustomizationStage::None,"道具部位切换协议未登记。");return;}
            if(current<0) {Finish(Tr(Tx::itemop_part_unknown));return;}
            if(++active.partSteps>kMaximumPartSteps) {Finish(Tr(Tx::itemop_part_switch_failed));return;}
            // 每帧只切一步；下一帧重新读取列表，禁止沿用切换前的容器或记录。
            if(!profile->switchPart(frame,target>current?1:-1)) {Finish(Tr(Tx::itemop_part_switch_rejected));return;}
            active.deadline=frame.now+kStepTimeout;
            Set(ModuleLocatePhase::SwitchingCategory,Tr(Tx::itemop_switching_part),false,false);return;
        }
        if(active.request.domain==CustomizationDomain::Hair && snapshot.part!=CustomizationPart::Hair) {Finish(Tr(Tx::itemop_hair_list_invalid));return;}
        const auto count=std::count(snapshot.recordIds.begin(),snapshot.recordIds.end(),id);
        if(count==0) {Finish(Tr(Tx::itemop_not_in_list));return;}
        if(count>1) {Finish(Tr(Tx::itemop_duplicate_id));return;}
        const auto index=static_cast<size_t>(std::find(snapshot.recordIds.begin(),snapshot.recordIds.end(),id)-snapshot.recordIds.begin());
        if(action==CustomizationAction::Locate) {
            if(profile->highlight) {
                if(!profile->highlight(frame,index)) return;
            } else {
                // 逐步移动：每帧一步，并在下一帧按新读数重新查找目标；读不到当前高亮序号时不移动。
                if(!snapshot.highlightIndex) {Finish(Tr(Tx::itemop_highlight_unreadable));return;}
                if(*snapshot.highlightIndex!=index) {
                    if(++active.highlightSteps>std::min(kMaximumHighlightSteps,snapshot.recordIds.size()+1)) {Finish(Tr(Tx::itemop_highlight_failed));return;}
                    if(!profile->step(frame,*snapshot.highlightIndex<index?1:-1)) {Finish(Tr(Tx::itemop_call_rejected));return;}
                    active.deadline=frame.now+kStepTimeout;
                    Set(ModuleLocatePhase::SwitchingCategory,Tr(Tx::itemop_moving_highlight),false,false);return;
                }
            }
        } else {
            if(action==CustomizationAction::Remove) {
                if(!snapshot.rawEquippedId) {Finish(Tr(Tx::itemop_setting_unknown));return;}
                if(*snapshot.rawEquippedId==-1) {Finish(Tr(Tx::itemop_already_module_hair),true,CustomizationStage::Removed);return;}
                if(snapshot.rawEquippedId!=id) {Finish(Tr(Tx::itemop_not_equipped));return;}
                if(std::any_of(snapshot.configurationIds.begin(),snapshot.configurationIds.end(),[](const auto& value){return !value;})) {Finish(Tr(Tx::itemop_slots_unreadable));return;}
            }
            // 动作先定位到真实目标，下一帧重新读取拥有状态及列表；不能对上一次菜单残留目标发命令。
            if(profile->requiresHighlightedAction && snapshot.highlightedId!=id && profile->highlight) {
                if(!profile->highlight(frame,index)) return;
                Set(ModuleLocatePhase::SwitchingCategory,Tr(Tx::itemop_preparing_target),false,false);return;
            }
            // 预览与更换前先等待加载结束，并备份当前槽位与全部槽位的实际装备。
            if(snapshot.loading) return;
            if(action!=CustomizationAction::Remove && !snapshot.equippedId) {Finish(Tr(Tx::itemop_equip_unreadable));return;}
            if(action==CustomizationAction::Equip) {
                // 实时拥有判定：目录存在不等于拥有；未知或未拥有一律不更换，不调用购买命令。
                const auto owned=index<snapshot.owned.size()?snapshot.owned[index]:std::nullopt;
                if(!owned) {Finish(Tr(Tx::itemop_owned_unknown));return;}
                if(!*owned) {Finish(Tr(Tx::itemop_not_owned));return;}
                if(snapshot.equippedId==id) {Finish(Tr(Tx::itemop_already_equipped),true,CustomizationStage::Equipped);return;}
            }
            active.baseline.equipped=snapshot.equippedId;active.baseline.slots=snapshot.slotEquipped;
            active.baseline.configurationIds=snapshot.configurationIds;
            const auto& call=action==CustomizationAction::Preview?profile->preview:action==CustomizationAction::Remove?profile->remove:profile->equip;
            if(!call(frame,index)) {Finish(Tr(Tx::itemop_call_rejected));return;}
        }
        active.expectedList=snapshot.listIdentity;active.expectedPart=snapshot.part;active.phase=Phase::Confirming;
        active.deadline=frame.now+kStepTimeout;active.matches=active.misses=0;
        Set(ModuleLocatePhase::Confirming,Tr(Tx::itemop_confirming),false,false);return;
    }
    // 确认阶段只读：列表身份或部位变化说明记录已刷新，旧结果作废；读回值必须独立等于目标，不回显请求ID。
    if(snapshot.part!=active.expectedPart || ((action==CustomizationAction::Locate || action==CustomizationAction::Equip) && snapshot.listIdentity!=active.expectedList)) {Finish(Tr(Tx::itemop_list_refreshed));return;}
    bool confirmed=false;
    if(action==CustomizationAction::Locate) confirmed=snapshot.highlightedId==id;
    else if(action==CustomizationAction::Preview) {
        if(snapshot.loading) return;
        confirmed=snapshot.previewActive && snapshot.previewId==id;
    } else if(action==CustomizationAction::Remove) {
        if(snapshot.loading || !frame.idle) return;
        const size_t part=active.request.domain==CustomizationDomain::Hair?5:static_cast<size_t>(PartOrder(active.request.part)+1);
        const size_t changed=static_cast<size_t>(frame.slot)*6+part;
        for(size_t n=0;n<snapshot.configurationIds.size();++n) if(n!=changed && snapshot.configurationIds[n]!=active.baseline.configurationIds[n]) {Finish(Tr(Tx::itemop_remove_side_effect));return;}
        confirmed=snapshot.rawEquippedId==-1 && snapshot.configurationIds[changed]==-1;
    } else {
        if(snapshot.loading) return;
        if(!OtherSlotsUnchanged(snapshot,active.baseline,frame.slot)) {Finish(Tr(Tx::itemop_other_slots_changed));return;}
        confirmed=snapshot.equippedId==id && snapshot.slotEquipped[static_cast<size_t>(std::clamp(frame.slot,0,5))]==id;
    }
    if(confirmed) {
        if(++active.matches<(action==CustomizationAction::Remove?3:kConfirmFrames)) return;
        if(action==CustomizationAction::Preview) {
            // 临时预览已由独立读回确认；在原生退出并核对装备之前事务保持进行，不报告完成。
            active.phase=Phase::PreviewActive;
            Set(ModuleLocatePhase::Confirming,Confirmed(action,id),false,false,CustomizationStage::Previewing);status.previewReady=true;return;
        }
        Finish(action==CustomizationAction::Remove?(active.request.domain==CustomizationDomain::Hair?Tr(Tx::itemop_module_hair_restored):Tr(Tx::itemop_accessory_removed)):Confirmed(action,id),true,
            action==CustomizationAction::Locate?CustomizationStage::Highlighted:action==CustomizationAction::Remove?CustomizationStage::Removed:CustomizationStage::Equipped);return;
    }
    active.matches=0;
    if(++active.misses>kConfirmMisses) Finish(Tr(Tx::itemop_readback_mismatch));
}

void TickCustomizationItemLocator(uint64_t now,uint64_t generation) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()) return;
    uiGeneration=generation;latestNow=now;
    if(active.phase!=Phase::Preparing && active.phase!=Phase::Confirming) return;
    if(active.cancelled!=cancellation.load()) {Finish(Tr(Tx::itemop_cancelled_changed));return;}
    if(active.request.uiGeneration!=generation) {Finish(Tr(Tx::itemop_catalog_reloaded));return;}
    if(now>active.deadline) Finish(Tr(Tx::itemop_list_timeout));
}

bool CustomizationItemFrameNeeded() {return item_runtime::observationRequested.load() || CustomizationItemLocatorBusy();}

ModuleLocateStatus PollCustomizationItemLocator() {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) return {};
    return status;
}

bool CustomizationItemLocatorBusy() {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) return true;
    return BusyLocked();
}

bool CustomizationItemPreviewActive() {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    return lock.owns_lock() && active.phase==Phase::PreviewActive;
}

bool RequestCustomizationItemPreviewExit(std::string& message) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || active.phase!=Phase::PreviewActive) {message=Tr(Tx::itemop_no_preview);return false;}
    const auto& profile=profiles[*Slot(active.request.domain)];
    if(!profile || !profile->exitPreview) {message=Tr(Tx::itemop_exit_in_game);return false;}
    active.exitRequested=true;message=Tr(Tx::itemop_exiting_preview);return true;
}

void CancelCustomizationItemLocator() {item_runtime::cancellation.fetch_add(1);}
}
