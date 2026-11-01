#include "module_action.h"
#include "native_page_router.h"
#include "module_category.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <algorithm>

namespace diva {
namespace action_runtime {
uintptr_t base=0;
std::mutex mutex;
std::atomic<uint64_t> cancelled{0};
uint64_t generation=0,lastFrame=0,uiGeneration=0,lastSerial=0;
uint64_t capabilityDemandUntil=0;
DWORD frameThread=0;
std::array<int,10> specialIds{};
bool specialProfileReadable=false;
std::string lockedCatalogFingerprint;
// 参数address/value为当前进程的已知地址与接收变量。
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T count=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}
// 参数address/value为经过提交及保护核查的动作暂存字段，不改原生高亮。
template<class T> bool Write(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};SIZE_T count=0;
    return address && VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}
struct Snapshot {
    NativePageSnapshot page;
    uintptr_t begin=0,end=0;
    int count=0,category=-1,highlight=-1,highlightId=-1,equippedIndex=-1,equippedId=-1,equippedCharacter=-1;
    std::array<int,6> models{};
    bool valid=false;
};
struct Card {uintptr_t record=0;int index=-1,character=-1;Ownership owned=Ownership::Unknown;uint64_t checked=0;};
Snapshot latest;
std::map<int,Card> cards;
struct Target {int category=-1;Ownership owned=Ownership::Unknown;uint64_t checked=0,demandUntil=0;};
std::map<int,Target> targets;
int refreshIndex=0;
enum class Phase { Idle, Preparing, Waiting, Loading, Preview, Exiting, Restoring, Done };
struct Transaction {
    Phase phase=Phase::Idle;
    CustomizationLocateRequest request;
    Snapshot before,origin,expected;
    Card card;
    uint64_t cancelled=0,deadline=0;
    bool abandoned=false;
    int stable=0;
    int targetCategory=-1,categorySteps=0;
    std::string completionMessage;
    bool exitSubmitted=false;
} active;
ModuleLocateStatus status;

// 参数page/id为已核对的单曲槽位与独立装备值；复用681CE0的配置获取路径，不从当前分类列表反推装备。
bool ReadEquippedModule(const NativePageSnapshot& page,int& id) {
    if(page.slot<0 || page.slot>=6 || page.slot>=page.slotCount || page.nativeScope!=3 || page.sourcePvid<=0 || page.sourcePvid>=10000000) return false;
    uintptr_t overrideTable=0,record=0;
    if(!Read(page.controller+0x10,overrideTable)) return false;
    if(overrideTable) record=overrideTable+static_cast<uintptr_t>(page.slot)*0x1C;
    else {
        // 原生getter沿既有Mod链返回当前曲目/槽位配置；仅在游戏状态帧读取，不调用保存或更换入口。
        __try {
            auto* save=reinterpret_cast<void*(__fastcall*)()>(base+0x1D6510)();
            if(!save) return false;
            record=reinterpret_cast<uintptr_t(__fastcall*)(void*,int,int)>(base+0x1D5E30)(save,page.sourcePvid,page.slot);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    return record && Read(record,id) && id>=0;
}

// 参数s为新鲜场景，列表记录、装备索引及六个槽位分别读取，不使用请求缓存当装备值。
Snapshot Capture() {
    Snapshot s;s.page=ReadNativePage(base);
    if(s.page.page!=NativePage::Module || s.page.slotCount<1 || s.page.slotCount>6
        || s.page.slot<0 || s.page.slot>=s.page.slotCount || s.page.nativeScope<0 || s.page.nativeScope>3) return s;
    if(!Read(s.page.scene+0x70,s.begin) || !Read(s.page.scene+0x78,s.end) || !s.begin || s.end<s.begin || (s.end-s.begin)%8
        || !Read(s.page.scene+0x11C,s.count) || s.count<1 || s.count>10000 || (s.end-s.begin)/8!=static_cast<uintptr_t>(s.count)
        || !Read(s.page.scene+0x110,s.highlight) || s.highlight<0 || s.highlight>=s.count
        || !Read(s.page.scene+0x2C,s.equippedIndex) || !Read(s.page.controller+0x40,s.category)) return s;
    if(!ReadEquippedModule(s.page,s.equippedId)) return s;
    uintptr_t highlighted=0;
    if(!Read(s.begin+s.highlight*8,highlighted) || !Read(highlighted,s.highlightId)) return s;
    if(s.equippedIndex>=0 && s.equippedIndex<s.count) {
        uintptr_t record=0;
        int indexedId=-1;
        if(!Read(s.begin+s.equippedIndex*8,record) || !Read(record,indexedId)) return s;
        if(indexedId==s.equippedId && !Read(record+8,s.equippedCharacter)) return s;
    }
    for(int slot=0;slot<6;++slot) if(!Read(s.page.controller+0x320+slot*0xE0,s.models[slot])) return s;
    s.valid=true;return s;
}
// 参数a/b为动作前后的身份；范围、槽位、曲目与真实列表必须同时不变。
bool SameContext(const Snapshot& a,const Snapshot& b) {
    return a.valid && b.valid && a.page.scene==b.page.scene && a.page.controller==b.page.controller
        && a.page.configuration==b.page.configuration && a.page.state==b.page.state
        && a.page.slot==b.page.slot && a.page.slotCount==b.page.slotCount
        && a.page.nativeScope==b.page.nativeScope && a.page.sourcePvid==b.page.sourcePvid
        && a.begin==b.begin && a.end==b.end && a.category==b.category;
}
// 参数a/b为分类切换前后快照；允许原生列表重建，但槽位、范围、配置、装备和六槽模型必须保持一致。
bool SameActionConfiguration(const Snapshot& a,const Snapshot& b) {
    return a.valid && b.valid && a.page.scene==b.page.scene && a.page.controller==b.page.controller
        && a.page.configuration==b.page.configuration && a.page.state==b.page.state
        && a.page.slot==b.page.slot && a.page.slotCount==b.page.slotCount
        && a.page.nativeScope==b.page.nativeScope && a.page.sourcePvid==b.page.sourcePvid
        && a.equippedId==b.equippedId && a.models==b.models;
}
// 参数s为当前快照；首批仅开放已直接核对的单曲范围，共通A/B仍保留明确禁用原因。
bool CertifiedContext(const Snapshot& s) {
    return s.valid && s.page.nativeScope==3 && s.page.sourcePvid>0 && s.page.sourcePvid<10000000
        && s.page.slotCount>=1 && s.page.slotCount<=6 && s.page.slot>=0 && s.page.slot<s.page.slotCount
        && s.equippedId>=0 && s.category>=0 && s.category<=6
        && s.models[s.page.slot]==s.equippedId;
}
// 参数s为当前真实快照；把范围限制与独立装备读取失败分别说明，避免统一误报为角色不支持。
std::string ContextRejection(const Snapshot& s) {
    if(s.page.nativeScope!=3) return "当前为共通设置；换装与预览仅开放曲目单独设置。";
    if(s.page.slot<0 || s.page.slot>=s.page.slotCount || s.page.slotCount<1 || s.page.slotCount>6) return "当前歌者槽位未能确认。";
    if(!s.valid || s.equippedId<0) return "当前槽位的独立装备配置暂不可读，请等待模型页就绪。";
    if(s.models[s.page.slot]!=s.equippedId) return "模型仍在加载或预览中，等待恢复当前装备。";
    return "当前歌曲或服装分类身份未能确认。";
}
// 参数id为稳定服装；特殊原生模型项使用不同菜单规则，不能套用普通服装动作。
bool OrdinaryModule(int id) {
    return id>=0 && specialProfileReadable && std::find(specialIds.begin(),specialIds.end(),id)==specialIds.end();
}
// 参数id/owned为当前真实列表目标和原生拥有判定结果；保留DML的原有查找Hook。
bool ReadOwnership(int id,Ownership& owned) {
    __try {
        void* save=reinterpret_cast<void*(__fastcall*)()>(base+0x1D6510)();
        if(!save) return false;
        void* record=reinterpret_cast<void*(__fastcall*)(void*,int)>(base+0x1D5C90)(save,id);
        if(!record) return false;
        const auto result=reinterpret_cast<uint8_t(__fastcall*)(void*)>(base+0x1DA550)(record);
        if(result>1) return false;
        owned=result?Ownership::Owned:Ownership::Unowned;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数s/index/id/card为真实当前列表和接收值，索引只由列表记录映射。
bool ReadCard(const Snapshot& s,int index,int& id,Card& card) {
    card={};card.index=index;
    return index>=0 && index<s.count && Read(s.begin+index*8,card.record) && Read(card.record,id)
        && Read(card.record+8,card.character);
}
// 参数s为当前空闲列表；每帧最多核对4个拥有判定，不在渲染线程批量调用游戏函数。
void RefreshCards(const Snapshot& s) {
    for(int step=0;step<4;++step) {
        if(refreshIndex>=s.count) refreshIndex=0;
        Card card;int id=-1;
        if(ReadCard(s,refreshIndex++,id,card) && id>=0 && ModuleRecordCategory(card.character)==s.category) {
            card.checked=GetTickCount64();ReadOwnership(id,card.owned);
            auto found=cards.find(id);
            if(found!=cards.end() && found->second.index!=card.index) {cards[id].record=0;cards[id].owned=Ownership::Unknown;}
            else cards[id]=card;
        }
    }
}
// 可见卡片请求的拥有判定不依赖当前人物分类；每帧最多核对4个目标，不将目录存在当成已拥有。
void RefreshTargets() {
    const auto now=GetTickCount64();
    for(int step=0;step<4;++step) {
        auto chosen=targets.end();
        for(auto it=targets.begin();it!=targets.end();++it) if(it->second.demandUntil>=now && (!it->second.checked || now-it->second.checked>500)
            && (chosen==targets.end() || it->second.checked<chosen->second.checked)) chosen=it;
        if(chosen==targets.end()) break;
        chosen->second.owned=Ownership::Unknown;ReadOwnership(chosen->first,chosen->second.owned);chosen->second.checked=now;
    }
}
// 参数scene/direction为模型页及相邻分类方向；只调用既有定位器已核对的分类入口。
bool MoveActionCategory(uintptr_t scene,int direction) {
    __try {reinterpret_cast<void(__fastcall*)(void*,int)>(base+0x694D00)(reinterpret_cast<void*>(scene),direction);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数s/id/card为当前真实列表、稳定ID与输出记录；所有动作共用唯一目标／人物分类检查。
bool FindActionTarget(const Snapshot& s,int id,Card& card) {
    int matches=0;
    for(int index=0;index<s.count;++index) {
        Card candidate;int candidateId=-1;
        if(!ReadCard(s,index,candidateId,candidate)) return false;
        if(candidateId==id) {card=candidate;++matches;}
    }
    return matches==1 && ModuleRecordCategory(card.character)==s.category;
}
// 参数s/id为恢复后的真实列表与原高亮ID；不装备、不购买，仅恢复列表控件。
bool RestoreActionHighlight(const Snapshot& s,int id) {
    Card card;if(!FindActionTarget(s,id,card)) return false;
    int saved=-1;if(!Read(s.page.scene+0x30,saved)) return false;
    bool invoked=false;
    __try {
        if(Write(s.page.scene+0x30,card.index)) {
            reinterpret_cast<void(__fastcall*)(void*,uint8_t)>(base+0x6916E0)(reinterpret_cast<void*>(s.page.scene),1);invoked=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {invoked=false;}
    return Write(s.page.scene+0x30,saved) && invoked;
}
// 参数text/success/restored为结果、成功与原生预览安全恢复；此处从不反向写回装备。
void Finish(const std::string& text,bool success=false,bool restored=false) {
    active.phase=Phase::Done;status.message=text;status.completed=true;status.succeeded=success;
    status.phase=success?ModuleLocatePhase::Succeeded:ModuleLocatePhase::Rejected;
    status.previewReady=false;status.previewRestored=restored;
}
// 参数s为当前真实快照；核对所有其他槽位没有被此次动作意外改变。
bool OtherSlotsUnchanged(const Snapshot& s) {
    for(int slot=0;slot<6;++slot) if(slot!=s.page.slot && active.before.models[slot]!=s.models[slot]) return false;
    return true;
}
// 参数s/card/action为当前已核实列表、真实目标记录与动作；只可能派发命令7或3。
bool Invoke(const Snapshot& s,const Card& card,CustomizationAction action) {
    uintptr_t callback=0,owner=0,function=0;
    if(!Read(s.page.scene+0x308,callback) || !Read(callback+8,owner) || owner!=s.page.scene
        || !Read(callback+0x10,function) || function!=base+0x695250) return false;
    int savedIndex=-1;uintptr_t savedRecord=0;
    if(!Read(s.page.scene+0x60,savedIndex) || savedIndex!=-1 || !Read(s.page.scene+0x68,savedRecord)) return false;
    bool submitted=false;
    __try {
        if(!Write(s.page.scene+0x60,card.index) || !Write(s.page.scene+0x68,card.record)) __leave;
        const int command=action==CustomizationAction::Preview?7:3;
        reinterpret_cast<void(__fastcall*)(void*,int)>(function)(reinterpret_cast<void*>(s.page.scene),command);
        int next=-1;
        if(!Read(s.page.scene+0x14,next) || (next!=3 && !(action==CustomizationAction::Preview && next==2))) __leave;
        const auto accepted=reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(base+0x66B3A0)(reinterpret_cast<void*>(s.page.state),next);
        int actual=-1;
        submitted=accepted && Read(s.page.state+0x18,actual) && actual==next;
    } __except(EXCEPTION_EXECUTE_HANDLER) {submitted=false;}
    if(!submitted) {
        int internal=-1;
        if(Read(s.page.state+0x18,internal) && internal==0) {Write(s.page.scene+0x60,savedIndex);Write(s.page.scene+0x68,savedRecord);}
    }
    return submitted;
}
// 参数s为稳定预览状态；严格复用6963E0的原生返回分支，不模拟按键、不直接写回装备。
bool InvokePreviewExit(const Snapshot& s) {
    uint8_t direct=0;if(s.page.internal!=2 || !Read(s.page.scene+0x28,direct)) return false;
    __try {
        const int next=direct?0:3;
        if(direct) {if(!Write(s.page.scene+0x28,uint8_t(0))) return false;}
        else if(!Write(s.page.scene+0x1C,int(2)) || !Write(s.page.scene+0x18,int(0))) return false;
        const auto accepted=reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(base+0x66B3A0)(reinterpret_cast<void*>(s.page.state),next);
        int actual=-1;return accepted && Read(s.page.state+0x18,actual) && actual==next;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数s/ownershipReader/invoke为真实状态帧快照及原生适配器；测试可隔离原生调用，生产仍重新查唯一目标与拥有状态。
void Execute(const Snapshot& s,const std::function<bool(int,Ownership&)>& ownershipReader=ReadOwnership,
    const std::function<bool(const Snapshot&,const Card&,CustomizationAction)>& invoke=Invoke) {
    if(!SameContext(active.before,s) || !CertifiedContext(s) || !s.page.idle
        || s.equippedId!=active.before.equippedId || s.models!=active.before.models) {Finish("服装动作前范围、槽位、装备或页面已变化，旧请求已取消。");return;}
    Card card;const int id=active.request.key.id;
    if(s.category!=active.targetCategory || !FindActionTarget(s,id,card) || !OrdinaryModule(id)
        || !ownershipReader(id,card.owned) || (active.request.action==CustomizationAction::Equip && card.owned!=Ownership::Owned)) {
        Finish("目标不是当前歌者可用的唯一普通服装，或尚未拥有，未执行动作。");return;
    }
    active.before=s;active.card=card;
    if(!invoke(s,card,active.request.action)) {Finish("原生服装操作未受理，未继续加载或自动恢复装备。");return;}
    active.phase=Phase::Loading;active.deadline=GetTickCount64()+15000;
    status.message=active.request.action==CustomizationAction::Preview?"正在加载原生模型预览……":"正在更换服装并核对实际装备……";
    status.phase=ModuleLocatePhase::Confirming;
}
// 参数s/message为已独立确认的结果；跨人物动作先恢复原分类和高亮，再完成反馈或恢复搜索。
void CompleteAction(const Snapshot& s,const std::string& message) {
    if(active.origin.valid && active.origin.category!=s.category) {
        active.phase=Phase::Restoring;active.expected=s;active.categorySteps=0;active.completionMessage=message;
        active.deadline=GetTickCount64()+5000;status.previewReady=false;status.message="原生动作已确认，正在恢复原分类与高亮……";return;
    }
    Finish(message,true,active.request.action==CustomizationAction::Preview && !active.abandoned);
}
// 参数s/move/capture/restore为本帧快照及原生适配器；隔离测试可替换操作，生产只在模型帧调用。
void ProcessActionModelFrame(const Snapshot& s,const std::function<bool(int)>& move,const std::function<Snapshot()>& capture,const std::function<bool(const Snapshot&,int)>& restore) {
    if(active.phase!=Phase::Preparing && active.phase!=Phase::Restoring) return;
    if(active.phase==Phase::Preparing && active.cancelled!=cancelled.load()) {Finish("搜索条件已变化，取消跨人物动作。");return;}
    if(!s.page.idle || !SameContext(active.expected,s) || !SameActionConfiguration(active.expected,s)
        || s.highlightId!=active.expected.highlightId) {Finish("跨人物动作期间页面、槽位、装备或高亮已被改变，停止旧动作。");return;}
    const int target=active.phase==Phase::Preparing?active.targetCategory:active.origin.category;
    if(s.category==target) {
        if(active.phase==Phase::Preparing) {
            active.phase=Phase::Waiting;active.before=s;status.phase=ModuleLocatePhase::WaitingForFrame;status.message="目标人物列表已就绪，等待核对并执行原生动作。";return;
        }
        if(s.highlightId!=active.origin.highlightId && !restore(s,active.origin.highlightId)) {Finish("动作已执行，但原列表高亮未能安全恢复；未反向更换装备。");return;}
        const auto restored=capture();
        if(!restored.page.idle || !SameActionConfiguration(s,restored) || restored.category!=target || restored.highlightId!=active.origin.highlightId) {Finish("恢复原分类后读回不符；未继续改变装备。");return;}
        latest=restored;lastFrame=GetTickCount64();
        Finish(active.completionMessage,true,active.request.action==CustomizationAction::Preview && !active.abandoned);return;
    }
    const int direction=ModuleCategoryDirection(s.category,target);
    if(!direction || active.categorySteps>=6 || !move(direction)) {Finish("原生人物分类准备失败，未继续动作或自动反向换装。");return;}
    const auto after=capture();
    if(!after.page.idle || !SameActionConfiguration(s,after) || after.category!=s.category+direction) {Finish("原生分类切换后上下文不符，已停止后续动作。");return;}
    ++active.categorySteps;active.expected=after;
    if(active.phase==Phase::Preparing && after.category==target) {
        active.before=after;active.phase=Phase::Waiting;status.phase=ModuleLocatePhase::WaitingForFrame;status.message="目标人物列表已就绪，等待执行原生动作。";
    }
}
// 参数s为加载后的真实读回，区分临时预览、装备标记及加载态，不把320相等单独当成功。
void Confirm(const Snapshot& s) {
    if(!SameContext(active.before,s) || !OtherSlotsUnchanged(s)) {Finish("动作期间范围、槽位或对象已改变；未自动覆盖玩家装备。");return;}
    if(s.highlight!=active.before.highlight) {Finish("原生高亮已变化，已停止旧动作反馈；未反向换装。");return;}
    const int loaded=s.models[s.page.slot];
    if(active.phase==Phase::Loading && active.request.action==CustomizationAction::Equip) {
        if(s.page.internal==0 && s.page.popup==-1 && s.equippedIndex==active.card.index && s.equippedId==active.request.key.id && loaded==active.request.key.id) {
            if(++active.stable>=2) CompleteAction(s,"已更换服装 · ID "+std::to_string(s.equippedId));
        } else active.stable=0;
        return;
    }
    // 分类内的装备标记索引可能因跨人物预览变化；是否改装只由独立槽位配置核对，不能拿列表标记代替。
    if(active.phase==Phase::Loading && s.page.internal==2 && loaded==active.request.key.id && s.equippedId==active.before.equippedId) {
        active.phase=Phase::Preview;status.previewReady=true;status.message="原生预览中，按 Esc 退出后返回搜索。";active.stable=0;return;
    }
    if((active.phase==Phase::Preview || active.phase==Phase::Exiting) && s.page.internal==0 && s.page.popup==-1) {
        if(s.equippedId!=active.before.equippedId || loaded!=active.before.models[s.page.slot]) {Finish("预览退出后装备已由原生操作改变；未自动写回旧装备。");return;}
        if(++active.stable>=2) CompleteAction(s,"已退出原生预览，原装备读回一致。");
    }
}
// 参数current/exit为预览当前快照与原生退出适配器；只提交一次退出，恢复仍由跨帧独立读回确认。
void ProcessPreviewExit(const Snapshot& current,const std::function<bool(const Snapshot&)>& exit=InvokePreviewExit) {
    if(!active.exitSubmitted && current.page.internal==2) {
        if(!SameContext(active.before,current) || !OtherSlotsUnchanged(current) || current.equippedId!=active.before.equippedId
            || current.models[current.page.slot]!=active.request.key.id || !exit(current)) Finish("原生退出预览未受理，已停止后续操作。");
        else active.exitSubmitted=true;
    } else Confirm(current);
}
}

void InitializeModuleActions(uintptr_t imageBase) {
    using namespace action_runtime;
    std::lock_guard lock(mutex);base=imageBase;specialProfileReadable=true;
    for(size_t index=0;index<specialIds.size();++index)
        if(!Read(base+0xC09580+index*4,specialIds[index])) specialProfileReadable=false;
}

// 参数page是定位器Hook取得的真实模型页；分类切换不借用选曲／渲染线程，不需要先单击定位。
void OnModuleActionModelFrame(uintptr_t page) {
    using namespace action_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock() || !base || (active.phase!=Phase::Preparing && active.phase!=Phase::Restoring)) return;
    const auto current=Capture();
    if(current.page.scene!=page || (frameThread && frameThread!=GetCurrentThreadId())) {Finish("原生模型页或线程已改变，停止跨人物动作。");return;}
    // 三个适配器只接收已在本帧验证的模型页，不接受UI传入地址。
    ProcessActionModelFrame(current,[page](int direction) {return MoveActionCategory(page,direction);},[] {return Capture();},RestoreActionHighlight);
}

void OnModuleActionStateFrame(uintptr_t state) {
    using namespace action_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock() || !base) return;
    if((active.phase==Phase::Idle || active.phase==Phase::Done) && GetTickCount64()>capabilityDemandUntil) return;
    const auto current=Capture();if(current.page.state!=state) return;
    const auto thread=GetCurrentThreadId();
    if(frameThread && thread!=frameThread) {cards.clear();latest={};++generation;Finish("原生模型线程已改变，动作已停止。");return;}
    frameThread=thread;lastFrame=GetTickCount64();
    if(active.phase==Phase::Waiting) {
        if(active.cancelled!=cancelled.load() || active.request.uiGeneration!=uiGeneration) Finish("搜索条件或目录已变化，旧动作已取消。");
        else Execute(current);
    } else if(active.phase==Phase::Exiting) {
        ProcessPreviewExit(current);
    } else if(active.phase==Phase::Loading || active.phase==Phase::Preview) {
        if(active.cancelled!=cancelled.load() || active.request.uiGeneration!=uiGeneration) active.abandoned=true;
        Confirm(current);
    }
    if(current.valid && (active.phase==Phase::Preview || active.phase==Phase::Exiting || active.phase==Phase::Restoring)) RefreshTargets();
    if(current.valid && current.page.idle) {
        if(!SameContext(latest,current) || latest.equippedId!=current.equippedId) {++generation;cards.clear();refreshIndex=0;}
        latest=current;
        if(active.phase==Phase::Idle || active.phase==Phase::Done) {RefreshCards(current);RefreshTargets();}
    } else if(active.phase==Phase::Idle || active.phase==Phase::Done) {latest=current;cards.clear();++generation;}
}

bool SubmitModuleAction(const CustomizationLocateRequest& request,std::string& message) {
    using namespace action_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) {message="模型动作正在处理，请稍候。";return false;}
    const auto reject=[&](const char* reason) {message=reason;return false;};
    if(request.action==CustomizationAction::Locate || request.key.kind!=CustomizationKind::Module || request.key.id<0) return reject("原生动作目标无效。");
    if(active.phase!=Phase::Idle && active.phase!=Phase::Done) return reject("已有原生服装动作正在进行。");
    if(!base || !latest.page.idle || !CertifiedContext(latest) || GetTickCount64()-lastFrame>500) return reject("当前槽位或设置范围尚未认证，未执行服装动作。");
    if(request.contextGeneration!=generation || request.slot!=latest.page.slot || request.nativeScope!=latest.page.nativeScope
        || request.scope!=CustomizationScope::Song || request.uiGeneration!=uiGeneration) return reject("服装动作上下文已过期，请重新点击卡片按钮。");
    if(!request.serial || request.serial<=lastSerial || request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256
        || (!lockedCatalogFingerprint.empty() && lockedCatalogFingerprint!=request.catalogFingerprint)) return reject("服装动作序号或目录身份无效。");
    const int targetCategory=ModuleMenuCategory(request.character);
    if(targetCategory<0 || !OrdinaryModule(request.key.id)) return reject("目标人物或普通服装身份无效。");
    const auto found=cards.find(request.key.id);const auto target=targets.find(request.key.id);
    Ownership ownership=Ownership::Unknown;
    if(target!=targets.end() && target->second.category==targetCategory && GetTickCount64()-target->second.checked<=2500) ownership=target->second.owned;
    else if(found!=cards.end() && found->second.record && ModuleRecordCategory(found->second.character)==targetCategory) ownership=found->second.owned;
    else if(target==targets.end() || target->second.category!=targetCategory) return reject("目标人物信息已过期，请重新点击该服装按钮。");
    if(request.action==CustomizationAction::Equip && ownership!=Ownership::Owned) return reject("目标未拥有或拥有状态未知，禁止换装。");
    lockedCatalogFingerprint=request.catalogFingerprint;
    lastSerial=request.serial;active={};active.phase=targetCategory==latest.category?Phase::Waiting:Phase::Preparing;
    active.request=request;active.before=active.origin=active.expected=latest;active.targetCategory=targetCategory;
    active.cancelled=cancelled.load();active.deadline=GetTickCount64()+15000;
    status={ModuleLocatePhase::WaitingForFrame,request.serial,request.uiGeneration,generation,false,false,"等待原生模型帧核验服装动作。"};
    status.action=request.action;message=status.message;return true;
}

void TickModuleAction(uint64_t now,uint64_t currentGeneration) {
    using namespace action_runtime;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock()) return;
    uiGeneration=currentGeneration;
    if(active.phase==Phase::Idle || active.phase==Phase::Done) return;
    if((active.phase==Phase::Waiting || active.phase==Phase::Preparing) && (active.cancelled!=cancelled.load() || active.request.uiGeneration!=uiGeneration)) {Finish("旧服装动作已取消。");return;}
    if(active.phase==Phase::Restoring && (active.cancelled!=cancelled.load() || active.request.uiGeneration!=uiGeneration)) active.abandoned=true;
    if(lastFrame && now-lastFrame>1000) {latest={};cards.clear();++generation;Finish("已离开原生模型页，停止动作反馈；未自动恢复旧装备。");return;}
    if(active.phase!=Phase::Preview && now>active.deadline) Finish("原生服装加载超时；未继续提交动作或反向换装。");
}

CustomizationActionCapability ReadModuleActionCapability(const CustomizationKey& key,CustomizationCharacter targetCharacter) {
    using namespace action_runtime;
    CustomizationActionCapability c;
    std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock() || key.kind!=CustomizationKind::Module) return c;
    capabilityDemandUntil=GetTickCount64()+1500;
    c.modelPage=latest.page.page==NativePage::Module && GetTickCount64()-lastFrame<=500;
    c.busy=active.phase!=Phase::Idle && active.phase!=Phase::Done && active.phase!=Phase::Preview;
    c.contextGeneration=generation;c.slot=latest.page.slot;c.nativeScope=latest.page.nativeScope;
    c.scope=c.nativeScope==3?CustomizationScope::Song:c.nativeScope>=0 && c.nativeScope<3?CustomizationScope::Common:CustomizationScope::Unknown;
    if(!CertifiedContext(latest)) {c.previewReason=c.equipReason=ContextRejection(latest);return c;}
    if(c.busy) {c.previewReason=c.equipReason="请先完成当前原生预览或换装。";return c;}
    const auto found=cards.find(key.id);int targetCategory=ModuleMenuCategory(targetCharacter);
    if(targetCategory<0 && found!=cards.end() && found->second.record) targetCategory=ModuleRecordCategory(found->second.character);
    if(targetCategory<0 || key.id<0 || (targets.size()>=10000 && !targets.contains(key.id))) {c.previewReason=c.equipReason="目标人物信息不可用。";return c;}
    auto& target=targets[key.id];if(target.category!=targetCategory) {target={};target.category=targetCategory;}
    target.demandUntil=std::max(target.demandUntil,GetTickCount64()+1500);
    if(target.checked && GetTickCount64()-target.checked<=2500) c.ownership=target.owned;
    else if(found!=cards.end() && found->second.record && ModuleRecordCategory(found->second.character)==targetCategory && GetTickCount64()-found->second.checked<=2500) c.ownership=found->second.owned;
    c.equipped=latest.page.idle && latest.equippedId==key.id && latest.models[latest.page.slot]==key.id;
    c.preview=OrdinaryModule(key.id);c.equip=c.preview && c.ownership==Ownership::Owned && !c.equipped;
    c.previewReason=c.preview?"临时原生模型预览；可自动准备目标人物分类，Esc退出后恢复搜索与原分类。":"该特殊服装的原生动作尚未认证。";
    c.equipReason=c.equipped?"当前槽位已装备。":c.ownership==Ownership::Owned?"更换当前歌者的已拥有服装。":c.ownership==Ownership::Unowned?"尚未拥有，不能换装；不会购买。":"拥有状态未知，不能换装。";
    return c;
}

ModuleLocateStatus PollModuleAction() {std::unique_lock lock(action_runtime::mutex,std::try_to_lock);return lock.owns_lock()?action_runtime::status:ModuleLocateStatus{};}
bool ModuleActionBusy() {std::unique_lock lock(action_runtime::mutex,std::try_to_lock);return !lock.owns_lock() || (action_runtime::active.phase!=action_runtime::Phase::Idle && action_runtime::active.phase!=action_runtime::Phase::Done);}
// 只读取事务状态；UI可在稳定预览中选择下一操作，加载和换装期间仍拒绝打断。
bool ModulePreviewCanExit() {
    using namespace action_runtime;std::unique_lock lock(mutex,std::try_to_lock);
    return lock.owns_lock() && active.phase==Phase::Preview && status.previewReady;
}
// 参数request为后续稳定目标；延长其拥有缓存需求，等待原生退出完成后由统一网关重新验证。
bool RequestModulePreviewExit(const CustomizationLocateRequest& request,std::string& message) {
    using namespace action_runtime;std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || active.phase!=Phase::Preview || !status.previewReady) {message="预览状态已改变，请等待当前操作完成。";return false;}
    if(request.serial<=active.request.serial || request.catalogFingerprint!=active.request.catalogFingerprint || request.uiGeneration!=uiGeneration
        || (request.action!=CustomizationAction::Locate && (request.slot!=active.before.page.slot || request.nativeScope!=active.before.page.nativeScope || request.scope!=CustomizationScope::Song))) {
        message="后续目标或预览上下文已过期，未退出预览。";return false;
    }
    active.phase=Phase::Exiting;active.abandoned=true;active.exitSubmitted=false;active.stable=0;active.deadline=GetTickCount64()+15000;
    if(auto target=targets.find(request.key.id);target!=targets.end()) target->second.demandUntil=GetTickCount64()+20000;
    status.previewReady=false;status.message="正在原生退出预览并核对原装备……";message=status.message;return true;
}
// 歌曲跨页接续与服装按钮使用同一个原生退出阶段，保留旧预览的分类／高亮恢复。
bool RequestModulePreviewExitForSong(std::string& message) {
    using namespace action_runtime;std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || active.phase!=Phase::Preview || !status.previewReady) {message="原生预览状态已改变。";return false;}
    active.phase=Phase::Exiting;active.abandoned=true;active.exitSubmitted=false;active.stable=0;active.deadline=GetTickCount64()+15000;
    status.previewReady=false;status.message="正在退出旧预览，随后进入所选歌曲。";message=status.message;return true;
}
void CancelModuleAction() {action_runtime::cancelled.fetch_add(1);}
}
