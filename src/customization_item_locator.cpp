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
enum class Phase { Idle, Preparing, Confirming, Done };
struct Active {
    Phase phase=Phase::Idle;
    CustomizationLocateRequest request;
    CustomizationItemFrame submitted;
    uint64_t cancelled=0,deadline=0;
    uintptr_t expectedList=0;
    CustomizationPart expectedPart=CustomizationPart::Unknown;
    int partSteps=0,matches=0,misses=0;
};
std::mutex mutex;
std::atomic<uint64_t> cancellation{0};
std::array<std::optional<CustomizationItemProfile>,3> profiles;
Active active;
ModuleLocateStatus status;
uint64_t lastSerial=0,uiGeneration=0;
std::string lockedFingerprint;

// 参数 domain 为发型或自定义道具；返回未认证时的定位原因，明确列出缺失的原生证据。
std::string LocateGap(CustomizationDomain domain) {
    return domain==CustomizationDomain::Hair
        ?"发型列表容器、稳定ID→记录映射与高亮入口尚未认证；仅改变搜索选择，未改动原生高亮。"
        :"自定义道具稳定ID→列表记录映射与高亮入口尚未认证（scene+0x78 仅为高亮序号候选）；仅改变搜索选择，未改动原生高亮。";
}
// 参数 domain/action 为领域与动作；返回预览或更换未认证的原因，不能套用模型命令。
std::string ActionGap(CustomizationDomain domain,CustomizationAction action) {
    const std::string noun=domain==CustomizationDomain::Hair?"发型":"自定义道具";
    return action==CustomizationAction::Preview?noun+"预览命令与临时显示读回尚未认证；未执行。"
        :noun+"更换命令、拥有状态、槽位范围与装备读回尚未认证；未执行。";
}
// 参数 domain 为领域，返回 Profile 数组下标；模型不使用本执行器。
std::optional<size_t> Slot(CustomizationDomain domain) {
    if(domain==CustomizationDomain::Models) return std::nullopt;
    return CustomizationDomainIndex(domain);
}
// 参数 a/b 为提交时与当前帧上下文；同一事务内不允许换场景、换配置、换槽、换范围或换来源歌曲。
bool SameContext(const CustomizationItemFrame& a,const CustomizationItemFrame& b) {
    return a.valid && b.valid && a.domain==b.domain && a.scene==b.scene && a.state==b.state && a.controller==b.controller
        && a.configuration==b.configuration && a.slot==b.slot && a.slotCount==b.slotCount
        && a.nativeScope==b.nativeScope && a.sourcePvid==b.sourcePvid;
}
// 参数 phase/text/completed/succeeded 为对外状态；只由本执行器在锁内写入。
void Set(ModuleLocatePhase phase,const std::string& text,bool completed,bool succeeded) {
    status.phase=phase;status.serial=active.request.serial;status.uiGeneration=active.request.uiGeneration;
    status.message=text;status.completed=completed;status.succeeded=succeeded;status.action=active.request.action;
}
// 参数 text/success 为结束原因；失败后不自动重试，也不反向撤销已执行步骤。
void Finish(const std::string& text,bool success=false) {
    active.phase=Phase::Done;
    Set(success?ModuleLocatePhase::Succeeded:ModuleLocatePhase::Rejected,text,true,success);
    if(success && active.request.action==CustomizationAction::Preview) status.previewReady=true;
}
// 参数 part 为道具部位，返回原生左侧列表顺序（头0、脸1、胸口2、背部3）；该顺序仅头／脸已有观察。
int PartOrder(CustomizationPart part) {
    return part==CustomizationPart::Head?0:part==CustomizationPart::Face?1:part==CustomizationPart::Neck?2:part==CustomizationPart::Back?3:-1;
}
}

void RegisterCustomizationItemProfile(CustomizationDomain domain,std::optional<CustomizationItemProfile> profile) {
    using namespace item_runtime;
    std::lock_guard lock(mutex);
    const auto slot=Slot(domain);if(!slot) return;
    profiles[*slot]=std::move(profile);
}

CustomizationActionCapability CustomizationItemCapabilities(CustomizationDomain domain) {
    using namespace item_runtime;
    CustomizationActionCapability c;
    c.locateReason=LocateGap(domain);
    c.previewReason=ActionGap(domain,CustomizationAction::Preview);
    c.equipReason=ActionGap(domain,CustomizationAction::Equip);
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) {c.busy=true;return c;}
    const auto slot=Slot(domain);if(!slot || !profiles[*slot]) return c;
    const auto& profile=*profiles[*slot];
    // 定位需要只读快照与高亮入口；道具跨部位还需部位协议，缺失时由执行器在到达后拒绝。
    c.locate=bool(profile.read) && bool(profile.highlight);
    // 预览与更换还依赖拥有状态、槽位和范围读回；即便 Profile 提供入口，也只在快照能读回对应字段时开放。
    c.preview=bool(profile.read) && bool(profile.preview);
    c.equip=bool(profile.read) && bool(profile.equip);
    if(c.locate) c.locateReason="已登记的证据Profile："+profile.evidence;
    c.busy=active.phase==Phase::Preparing || active.phase==Phase::Confirming;
    return c;
}

bool SubmitCustomizationItemRequest(const CustomizationLocateRequest& request,const CustomizationItemFrame& frame,std::string& message) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) {message="发型／道具原生操作正在处理中，请稍候。";return false;}
    if((active.phase==Phase::Preparing || active.phase==Phase::Confirming) && active.cancelled==cancellation.load()) {message="上一条发型／道具操作仍在跨帧确认中。";return false;}
    // 拒绝时也发布带序号的状态，使界面不会把旧事务结果误认为本次反馈。
    const auto reject=[&](const std::string& why) {
        active={};active.request=request;Finish(why);message=why;return false;
    };
    const auto slot=Slot(request.domain);
    if(!slot || request.key.kind!=CustomizationKind::Accessory || request.key.id<0) return reject("发型／道具目标领域或稳定ID无效。");
    if(request.domain==CustomizationDomain::Hair && request.part!=CustomizationPart::Hair) return reject("发型请求部位与发型领域不一致。");
    if(request.domain==CustomizationDomain::Accessories && PartOrder(request.part)<0) return reject("自定义道具请求缺少头／脸／胸口／背部之一。");
    if(!frame.valid || !frame.idle || frame.domain!=request.domain) return reject(std::string("当前不在空闲的")+CustomizationDomainPageName(request.domain)+"，未执行原生操作。");
    // 实际歌者守卫：发型只允许当前歌者自己的发型；预览与更换要求歌者已知且与目标兼容（共通 ALL 兼容任一歌者）。
    const bool compatible=frame.singer && (request.character==CustomizationCharacter::All || request.character==*frame.singer);
    if(request.domain==CustomizationDomain::Hair && frame.singer && !compatible) return reject("该发型属于其他角色，仅供浏览。");
    if(request.action!=CustomizationAction::Locate && !frame.singer) return reject("未读取到当前歌者，暂不支持此操作。");
    if(request.action!=CustomizationAction::Locate && !compatible) return reject("当前歌者不能使用该项目，暂不支持此操作。");
    const auto& profile=profiles[*slot];
    // 未登记 Profile 一律 Unsupported：不写缓存、不写高亮序号、不返回成功。
    if(!profile || !profile->read) return reject(request.action==CustomizationAction::Locate?LocateGap(request.domain):ActionGap(request.domain,request.action));
    if(request.action==CustomizationAction::Locate && !profile->highlight) return reject(LocateGap(request.domain));
    if(request.action==CustomizationAction::Preview && !profile->preview) return reject(ActionGap(request.domain,request.action));
    if(request.action==CustomizationAction::Equip && !profile->equip) return reject(ActionGap(request.domain,request.action));
    if(!request.serial || request.serial<=lastSerial) return reject("发型／道具请求序号过期，已拒绝重复提交。");
    if(request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedFingerprint.empty() && lockedFingerprint!=request.catalogFingerprint)) return reject("定制目录指纹缺失或已变化；请重启游戏后再操作。");
    lockedFingerprint=request.catalogFingerprint;lastSerial=request.serial;uiGeneration=request.uiGeneration;
    active={};active.phase=Phase::Preparing;active.request=request;active.submitted=frame;
    active.cancelled=cancellation.load();active.deadline=frame.now+kStepTimeout;
    Set(ModuleLocatePhase::WaitingForFrame,std::string("已提交，等待")+CustomizationDomainPageName(request.domain)+"原生帧核对列表……",false,false);
    message=status.message;return true;
}

void OnCustomizationItemFrame(const CustomizationItemFrame& frame) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || (active.phase!=Phase::Preparing && active.phase!=Phase::Confirming)) return;
    if(active.cancelled!=cancellation.load()) {Finish("查询、目录或面板已变化，旧发型／道具操作已取消。");return;}
    if(frame.now>active.deadline) {Finish("等待原生列表超时，已停止发型／道具操作。");return;}
    if(!frame.valid || frame.domain!=active.request.domain) {Finish(std::string("已离开")+CustomizationDomainPageName(active.request.domain)+"，旧操作已取消。");return;}
    if(!SameContext(active.submitted,frame) || active.submitted.singer!=frame.singer) {Finish("页面对象、歌者、设置范围或来源歌曲已变化，旧操作已取消。");return;}
    // 界面线程提交时线程未知，首个原生帧锁定线程；之后换线程即取消。
    if(!active.submitted.thread) active.submitted.thread=frame.thread;
    else if(frame.thread!=active.submitted.thread) {Finish("原生帧线程已改变，已取消发型／道具操作。");return;}
    if(!frame.idle) return;
    const auto& profile=profiles[*Slot(active.request.domain)];
    if(!profile || !profile->read) {Finish("证据Profile已撤销，未继续执行。");return;}
    CustomizationItemSnapshot snapshot;
    // 读取失败或列表未就绪只等待下一帧；页面到达、列表就绪与目标高亮是不同阶段。
    if(!profile->read(frame,snapshot) || !snapshot.pageReady || !snapshot.listReady) return;
    const int id=active.request.key.id;
    if(active.phase==Phase::Preparing) {
        if(active.request.domain==CustomizationDomain::Accessories && snapshot.part!=active.request.part) {
            const int current=PartOrder(snapshot.part),target=PartOrder(active.request.part);
            if(!profile->switchPart) {Finish("目标位于其他部位，道具部位切换协议尚未认证；未切换。");return;}
            if(current<0) {Finish("当前道具部位无法识别，未切换。");return;}
            if(++active.partSteps>kMaximumPartSteps) {Finish("道具部位切换未在有限步骤内到达目标，已停止。");return;}
            // 每帧只切一步；下一帧重新读取列表，禁止沿用切换前的容器或记录。
            if(!profile->switchPart(frame,target>current?1:-1)) {Finish("原生部位切换未受理，已停止。");return;}
            active.deadline=frame.now+kStepTimeout;
            Set(ModuleLocatePhase::SwitchingCategory,"正在切换到目标道具部位……",false,false);return;
        }
        if(active.request.domain==CustomizationDomain::Hair && snapshot.part!=CustomizationPart::Hair) {Finish("发型页读数未处于发型列表，已停止。");return;}
        const auto count=std::count(snapshot.recordIds.begin(),snapshot.recordIds.end(),id);
        if(count==0) {Finish("目标不在当前原生列表中（可能未解锁、属于其他人物分类或未加载），未改动原生高亮。");return;}
        if(count>1) {Finish("当前原生列表存在重复稳定ID，无法唯一定位，已拒绝。");return;}
        const auto index=static_cast<size_t>(std::find(snapshot.recordIds.begin(),snapshot.recordIds.end(),id)-snapshot.recordIds.begin());
        const auto& call=active.request.action==CustomizationAction::Locate?profile->highlight:active.request.action==CustomizationAction::Preview?profile->preview:profile->equip;
        if(!call || !call(frame,index)) {Finish("原生调用未受理，已停止发型／道具操作。");return;}
        active.expectedList=snapshot.listIdentity;active.expectedPart=snapshot.part;active.phase=Phase::Confirming;
        active.deadline=frame.now+kStepTimeout;active.matches=active.misses=0;
        Set(ModuleLocatePhase::Confirming,"已提交原生调用，正在跨帧读回确认……",false,false);return;
    }
    // 确认阶段只读：列表身份或部位变化说明记录已刷新，旧结果作废；读回值必须独立等于目标。
    if(snapshot.listIdentity!=active.expectedList || snapshot.part!=active.expectedPart) {Finish("确认前原生列表已刷新，旧记录作废，未报告成功。");return;}
    const auto& readback=active.request.action==CustomizationAction::Locate?snapshot.highlightedId
        :active.request.action==CustomizationAction::Preview?snapshot.previewId:snapshot.equippedId;
    if(readback && *readback==id) {
        if(++active.matches<kConfirmFrames) return;
        const char* verb=active.request.action==CustomizationAction::Locate?"已定位":active.request.action==CustomizationAction::Preview?"已进入临时预览":"已更换";
        Finish(std::string(verb)+" · 稳定ID "+std::to_string(id),true);return;
    }
    active.matches=0;
    if(++active.misses>kConfirmMisses) Finish("原生读回与目标不符，未报告成功。");
}

void TickCustomizationItemLocator(uint64_t now,uint64_t generation) {
    using namespace item_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()) return;
    uiGeneration=generation;
    if(active.phase!=Phase::Preparing && active.phase!=Phase::Confirming) return;
    if(active.cancelled!=cancellation.load()) {Finish("查询、目录或面板已变化，旧发型／道具操作已取消。");return;}
    if(active.request.uiGeneration!=generation) {Finish("定制目录已重新载入，旧发型／道具操作已失效。");return;}
    if(now>active.deadline) Finish("等待原生列表超时，已停止发型／道具操作。");
}

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
    return (active.phase==Phase::Preparing || active.phase==Phase::Confirming) && active.cancelled==cancellation.load();
}

void CancelCustomizationItemLocator() {item_runtime::cancellation.fetch_add(1);}
}
