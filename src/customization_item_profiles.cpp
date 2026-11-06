#include "customization_item_profiles.h"
#include "native_layout.h"
#include <Windows.h>
#include <algorithm>
#include <mutex>

namespace diva {
using namespace native;
namespace {
constexpr size_t kMaximumRecords=4096;
std::mutex diagnosticsMutex;
std::string lastDiagnostics="发型／道具原生规格尚未登记。";

// 参数 address/value 为当前进程地址与接收值；不可读时返回 false，不改变页权限。
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T count=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(T),&count) && count==sizeof(T);
}
// 参数 address 为候选函数入口；只接受已提交的可执行页。
bool Executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) && info.State==MEM_COMMIT
        && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
// 参数 function/object/value 为已认证入口、对象与整数参数；结构化异常保护，调用失败返回 false。
bool CallObjectInt(uintptr_t function,uintptr_t object,int value) {
    __try {reinterpret_cast<void(__fastcall*)(void*,int)>(function)(reinterpret_cast<void*>(object),value);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 function/object/command 为动作回调、回调对象与命令号；返回原生是否受理。
bool CallCommand(uintptr_t function,uintptr_t object,int command) {
    __try {return reinterpret_cast<uint8_t(__fastcall*)(void*,int)>(function)(reinterpret_cast<void*>(object),command)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 base/record/owned 为核验构建基址、真实定制记录与拥有结果；沿原生拥有函数保留 Mod 的解锁规则。
bool ReadPcOwnership(uintptr_t base,uintptr_t record,bool& owned) {
    __try {
        const auto result=reinterpret_cast<uint8_t(__fastcall*)(uintptr_t,uintptr_t)>(base+rva::kIsItemOwned)(0,record);
        if(result>1) return false;owned=result!=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 base/frame/slot 为当前构建、单曲配置上下文与歌者槽位；读取持久配置，不用临时显示字段代替装备。
uintptr_t SavedPcSlot(uintptr_t base,const CustomizationItemFrame& frame,int slot) {
    uintptr_t overrides=0;if(!Read(frame.controller+0x10,overrides)) return 0;
    if(overrides) return overrides+static_cast<uintptr_t>(slot)*0x1C;
    // 播放列表来源的独立记录只在 controller+0x10；缺失时不退回单曲配置，避免把另一对象当成本槽装备。
    if(frame.playlist) return 0;
    __try {
        auto* save=reinterpret_cast<void*(__fastcall*)()>(base+rva::kGetSaveData)();
        return save?reinterpret_cast<uintptr_t(__fastcall*)(void*,int,int)>(base+rva::kGetPvSlotConfig)(save,frame.sourcePvid,slot):0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
// 参数 base/frame/spec/index/action 为核验构建、场景、列表规格、目标索引与动作；只允许已认证命令，不开放购买。
bool CallPcAction(uintptr_t base,const CustomizationItemFrame& frame,const CustomizationItemNativeSpec& spec,size_t index,CustomizationAction action) {
    if(!frame.idle || frame.nativeScope!=CustomizeScope::Song || frame.slotCount<1 || frame.slotCount>6 || frame.slot<0 || frame.slot>=frame.slotCount || index>0x7FFFFFFF) return false;
    uintptr_t begin=0,end=0,record=0,callback=0,bound=0,function=0;int32_t current=-1;
    if(!Read(frame.scene+*spec.listBegin,begin) || !Read(frame.scene+*spec.listEnd,end) || end<begin || index>=(end-begin)/8
        || !Read(begin+index*8,record) || !Read(frame.scene+*spec.highlightIndex,current) || current!=static_cast<int32_t>(index)
        || !Read(frame.scene+*spec.callbackObject,callback) || !Read(callback+8,bound) || bound!=frame.scene
        || !Read(callback+0x10,function) || function!=base+*spec.callbackRva) return false;
    bool owned=false;if(action==CustomizationAction::Equip && (!ReadPcOwnership(base,record,owned) || !owned)) return false;
    if(action==CustomizationAction::Remove) {
        int32_t part=4,id=-1,target=-2;
        if(frame.domain!=CustomizationDomain::Hair && (!Read(frame.scene+0x30,part) || part<0 || part>3)) return false;
        if(!Read(record,id) || !Read(frame.controller+0x320+static_cast<uintptr_t>(frame.slot)*0xE0+4+part*4,target) || target<0 || target!=id) return false;
    }
    __try {
        // 自然菜单同样准备这两个动作目标；不写装备、拥有标记或存档。
        *reinterpret_cast<int32_t*>(frame.scene+0x38)=static_cast<int32_t>(index);
        *reinterpret_cast<uintptr_t*>(frame.scene+0x40)=record;
        const int command=action==CustomizationAction::Remove?(frame.domain==CustomizationDomain::Hair?6:5)
            :action==CustomizationAction::Preview?7:(frame.domain==CustomizationDomain::Hair?3:4);
        reinterpret_cast<void(__fastcall*)(uintptr_t,int)>(function)(frame.scene,command);
        const int next=*reinterpret_cast<int32_t*>(frame.scene+0x10);
        if(action==CustomizationAction::Remove && (next!=3 || *reinterpret_cast<int32_t*>(frame.scene+0x14)!=0 || *reinterpret_cast<int32_t*>(frame.scene+0x18)!=4)) return false;
        // +0x10 是待进入状态；+0x14 是加载后返回状态，绝不能跳过状态3的备份及加载。
        return (next==2 || next==3) && reinterpret_cast<uint8_t(__fastcall*)(uintptr_t,int)>(base+rva::kSceneStateSwitch)(frame.state,next)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 base/frame 为核验构建与活动预览；分别复用发型68BD90、道具6903D0的自然退出分支，不直接恢复装备。
bool ExitPcPreview(uintptr_t base,const CustomizationItemFrame& frame) {
    int32_t internal=-1;uint8_t direct=0;
    const uintptr_t flag=frame.domain==CustomizationDomain::Hair?0x27:0x2B;
    if(!frame.valid || frame.nativeScope!=CustomizeScope::Song || !Read(frame.state+0x18,internal) || internal!=2 || !Read(frame.scene+flag,direct)) return false;
    __try {
        const int next=direct?0:3;
        if(direct) *reinterpret_cast<uint8_t*>(frame.scene+flag)=0;
        else {*reinterpret_cast<int32_t*>(frame.scene+0x18)=2;*reinterpret_cast<int32_t*>(frame.scene+0x14)=0;}
        return reinterpret_cast<uint8_t(__fastcall*)(uintptr_t,int)>(base+rva::kSceneStateSwitch)(frame.state,next)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// 参数 entry 为规格中的入口；发型／道具规格不得引用模型页已认证入口，防止套用模型分类函数或动作回调。
bool ModelEntry(const std::optional<uintptr_t>& entry) {return entry && (*entry==rva::kSceneModuleMoveCategory || *entry==rva::kSceneModuleAction);}
// 参数 domain/spec/frame/out 为领域、规格、帧上下文与快照输出；只读内存，任一边界异常即返回 false。
bool ReadSnapshot(CustomizationDomain domain,const CustomizationItemNativeSpec& spec,const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {
    if(!frame.scene) return false;
    uintptr_t begin=0,end=0;
    if(!Read(frame.scene+*spec.listBegin,begin) || !Read(frame.scene+*spec.listEnd,end) || !begin || end<begin || (end-begin)%8) return false;
    const size_t count=static_cast<size_t>((end-begin)/8);
    if(count>kMaximumRecords) return false;
    out.recordIds.reserve(count);out.owned.reserve(count);
    for(size_t index=0;index<count;++index) {
        uintptr_t record=0;int32_t id=-1;
        if(!Read(begin+index*8,record) || !record || !Read(record+*spec.recordId,id)) return false;
        // 负值记录（随机、卸除等哨兵）保留位置以免序号错位，但永远不会匹配非负稳定 ID。
        out.recordIds.push_back(id);
        std::optional<bool> owned;
        if(spec.recordOwned) {uint8_t flag=0;if(Read(record+*spec.recordOwned,flag)) owned=flag!=0;}
        out.owned.push_back(owned);
    }
    out.listIdentity=begin;out.pageReady=true;out.listReady=true;
    int32_t highlight=-1;
    if(Read(frame.scene+*spec.highlightIndex,highlight) && highlight>=0 && static_cast<size_t>(highlight)<count) {
        out.highlightIndex=static_cast<size_t>(highlight);out.highlightedId=out.recordIds[static_cast<size_t>(highlight)];
    }
    if(domain==CustomizationDomain::Hair) out.part=CustomizationPart::Hair;
    else {int32_t part=-1;if(!Read(frame.scene+*spec.partIndex,part) || part<0 || part>3) return false;out.part=spec.partOrder[static_cast<size_t>(part)];}
    if(spec.loadingFlag) {uint8_t flag=0;if(!Read(frame.scene+*spec.loadingFlag,flag)) return false;out.loading=flag!=0;}
    if(spec.previewFlag && spec.previewId) {
        uint8_t flag=0;int32_t id=-1;
        if(Read(frame.scene+*spec.previewFlag,flag)) out.previewActive=flag!=0;
        if(out.previewActive && Read(frame.scene+*spec.previewId,id) && id>=0) out.previewId=id;
    }
    if(spec.equippedBase && spec.equippedStride && frame.controller && frame.slotCount>0) {
        for(int slot=0;slot<std::min(frame.slotCount,6);++slot) {
            int32_t id=-1;
            if(Read(frame.controller+*spec.equippedBase+static_cast<uintptr_t>(slot)*(*spec.equippedStride),id) && id>=0) out.slotEquipped[static_cast<size_t>(slot)]=id;
        }
        if(frame.slot>=0 && frame.slot<6) out.equippedId=out.slotEquipped[static_cast<size_t>(frame.slot)];
    }
    return true;
}
}

std::optional<CustomizationItemProfile> BuildCustomizationItemProfile(CustomizationDomain domain,const CustomizationItemNativeSpec& spec,uintptr_t base) {
    if(domain==CustomizationDomain::Models || spec.evidence.empty()) return std::nullopt;
    if(!spec.listBegin || !spec.listEnd || !spec.recordId || !spec.highlightIndex) return std::nullopt;
    if(domain==CustomizationDomain::Accessories && !spec.partIndex) return std::nullopt;
    if(spec.highlightControl.has_value()!=spec.refreshRva.has_value()) return std::nullopt;
    for(const auto* entry:{&spec.stepRva,&spec.partRva,&spec.highlightRva,&spec.callbackRva,&spec.refreshRva}) {
        if(ModelEntry(*entry)) return std::nullopt;
        if(*entry && !Executable(base+**entry)) return std::nullopt;
    }
    if(!spec.highlightRva && !spec.stepRva) return std::nullopt;
    CustomizationItemProfile profile;profile.evidence=spec.evidence;
    // 参数 frame/out 为帧上下文与快照；只读。
    profile.read=[domain,spec](const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {return ReadSnapshot(domain,spec,frame,out);};
    if(spec.highlightRva) {
        const uintptr_t function=base+*spec.highlightRva;
        // 参数 frame/index 为帧上下文与列表序号；直接高亮。
        profile.highlight=[function,spec,base](const CustomizationItemFrame& frame,size_t index) {
            if(index>0x7FFFFFFF) return false;
            const uintptr_t control=frame.scene+spec.highlightControl.value_or(0);
            // 实机控件 +0xAC 为过渡目标，-1 才允许原位定位；不向过渡中的控件提交。
            if(spec.highlightControl) {int32_t transition=0;if(!Read(control+0xAC,transition) || transition!=-1) return false;}
            if(!CallObjectInt(function,control,static_cast<int>(index))) return false;
            // 只刷新卡片显示，禁止使用会重建列表并重置高亮的分类刷新入口。
            return !spec.refreshRva || CallObjectInt(base+*spec.refreshRva,frame.scene,3);
        };
    } else {
        const uintptr_t function=base+*spec.stepRva;
        // 参数 frame/direction 为帧上下文与方向；逐步移动一格。
        profile.step=[function](const CustomizationItemFrame& frame,int direction) {return (direction==1 || direction==-1) && CallObjectInt(function,frame.scene,direction);};
    }
    if(domain==CustomizationDomain::Accessories && spec.partRva) {
        const uintptr_t function=base+*spec.partRva;
        // 参数 frame/direction 为帧上下文与方向；部位切换一步。
        profile.switchPart=[function](const CustomizationItemFrame& frame,int direction) {return (direction==1 || direction==-1) && CallObjectInt(function,frame.scene,direction);};
    }
    // 预览与更换作用于原生当前高亮：必须有直接高亮入口、动作回调及对应读回字段，否则不开放。
    const bool callback=spec.highlightRva && spec.callbackObject && spec.callbackRva;
    const bool equipReadback=spec.equippedBase && spec.equippedStride;
    // 参数 frame/index/command 为帧上下文、目标序号与命令号；先高亮目标，再向回调对象发出命令。
    auto command=[base,spec](const CustomizationItemFrame& frame,std::optional<size_t> index,int value) {
        if(index && (*index>0x7FFFFFFF || !CallObjectInt(base+*spec.highlightRva,frame.scene,static_cast<int>(*index)))) return false;
        uintptr_t object=0;if(!Read(frame.scene+*spec.callbackObject,object) || !object) return false;
        return CallCommand(base+*spec.callbackRva,object,value);
    };
    if(callback && spec.previewCommand && spec.previewFlag && spec.previewId && equipReadback) {
        const int value=*spec.previewCommand;
        // 参数 frame/index 为帧上下文与目标序号；进入临时预览。
        profile.preview=[command,value](const CustomizationItemFrame& frame,size_t index) {return command(frame,index,value);};
        if(spec.exitPreviewCommand) {
            const int exitValue=*spec.exitPreviewCommand;
            // 参数 frame 为帧上下文；退出临时预览。
            profile.exitPreview=[command,exitValue](const CustomizationItemFrame& frame) {return command(frame,std::nullopt,exitValue);};
        }
    }
    if(callback && spec.equipCommand && spec.recordOwned && equipReadback) {
        const int value=*spec.equipCommand;
        // 参数 frame/index 为帧上下文与目标序号；普通更换（拥有由执行器按记录标记实时核对）。
        profile.equip=[command,value](const CustomizationItemFrame& frame,size_t index) {return command(frame,index,value);};
    }
    return profile;
}

int InitializeCustomizationItemProfiles(uintptr_t base) {
    // 路由器已核验当前 PC 构建；只接入已实机认证的列表定位，不把临时显示配置当持久装备或拥有证据。
    CustomizationItemNativeSpec kHairSpec;
    kHairSpec.evidence="PC 已核验构建；HAIR_ACCESSORY_METHODS_VERIFIED.md，真实控件定位与跨帧 ID 读回";
    kHairSpec.listBegin=0x108;kHairSpec.listEnd=0x110;kHairSpec.recordId=0;
    kHairSpec.highlightIndex=0x138;kHairSpec.highlightControl=0x138;
    kHairSpec.highlightRva=rva::kListControlSetIndex;kHairSpec.refreshRva=rva::kSceneHairstyleRefresh;
    kHairSpec.callbackObject=0x378;kHairSpec.callbackRva=rva::kSceneHairstyleAction;
    CustomizationItemNativeSpec kAccessorySpec=kHairSpec;
    kAccessorySpec.listBegin=0x48;kAccessorySpec.listEnd=0x50;kAccessorySpec.highlightIndex=0x78;
    kAccessorySpec.highlightControl=0x78;kAccessorySpec.refreshRva=rva::kSceneItemRefresh;
    kAccessorySpec.partIndex=0x30;kAccessorySpec.partRva=rva::kSceneItemMovePart;
    kAccessorySpec.callbackObject=0x2D8;kAccessorySpec.callbackRva=rva::kSceneItemAction;
    int registered=0;std::string text;
    for(auto [domain,spec]:{std::pair{CustomizationDomain::Hair,&kHairSpec},std::pair{CustomizationDomain::Accessories,&kAccessorySpec}}) {
        auto profile=base?BuildCustomizationItemProfile(domain,*spec,base):std::nullopt;
        if(profile) {
            profile->requiresHighlightedAction=true;
            const auto readList=profile->read;const auto nativeSpec=*spec;
            // 参数 frame/out 为真实原生帧与只读快照；持久装备和临时预览分开读取，默认发型按当前模型解析。
            profile->read=[readList,base,domain,nativeSpec](const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {
                if(!readList(frame,out)) return false;
                int32_t internal=-1;if(!Read(frame.state+0x18,internal)) return false;
                out.loading=internal!=0 && internal!=2;out.previewActive=internal==2;
                const int part=domain==CustomizationDomain::Hair?4:static_cast<int>(std::find(nativeSpec.partOrder.begin(),nativeSpec.partOrder.end(),out.part)-nativeSpec.partOrder.begin());
                const uintptr_t offset=4+part*4;
                for(size_t n=0;n<out.configurationIds.size();++n) {
                    int32_t value=-1;if(!Read(frame.controller+0x320+(n/6)*0xE0+(n%6)*4,value)) return false;
                    out.configurationIds[n]=value;
                }
                if(frame.slot>=0 && frame.slot<6) out.rawEquippedId=out.configurationIds[static_cast<size_t>(frame.slot)*6+part+1];
                uintptr_t begin=0;if(!Read(frame.scene+*nativeSpec.listBegin,begin)) return false;
                // 只有实际高亮的拥有状态需要查询；动作先定位，下一帧才提交，避免每帧遍历调用数百次。
                if(out.highlightIndex) {uintptr_t record=0;bool owned=false;if(Read(begin+*out.highlightIndex*8,record) && ReadPcOwnership(base,record,owned)) out.owned[*out.highlightIndex]=owned;}
                if(frame.nativeScope!=CustomizeScope::Song || frame.sourcePvid<=0 || frame.slotCount<1 || frame.slotCount>6 || frame.slot<0 || frame.slot>=frame.slotCount) return true;
                for(int slot=0;slot<6;++slot) {
                    const auto saved=SavedPcSlot(base,frame,slot);int32_t id=-1;
                    // 独立保存读取不可用时仍可观察原生显示配置供卸除使用；不因此伪造装备／预览读回。
                    if(!saved || !Read(saved+offset,id)) return true;
                    // -1 是合法的未装备／默认发型，不能当作读取失败。
                    if(slot==frame.slot && domain==CustomizationDomain::Hair && id==-1) {
                        int32_t module=-1;if(!Read(saved,module)) return false;
                        int matches=0;
                        for(size_t n=0;n<out.recordIds.size();++n) {uintptr_t record=0;int32_t binding=-1;if(out.recordIds[n]>=0 && Read(begin+n*8,record) && Read(record+0x5C,binding) && binding==module) {id=out.recordIds[n];++matches;}}
                        if(matches!=1) return true;
                    }
                    out.slotEquipped[static_cast<size_t>(slot)]=id;
                }
                out.equippedId=out.slotEquipped[static_cast<size_t>(frame.slot)];
                if(out.previewActive) {int32_t display=-1;if(Read(frame.controller+0x320+static_cast<uintptr_t>(frame.slot)*0xE0+offset,display) && display>=0) out.previewId=display;}
                return true;
            };
            // 参数 frame/index 为空闲场景与目标序号；目标高亮由执行器先跨帧准备，再走完整加载协议。
            profile->preview=[base,nativeSpec](const CustomizationItemFrame& frame,size_t index) {return CallPcAction(base,frame,nativeSpec,index,CustomizationAction::Preview);};
            // 参数 frame/index 同上；原生拥有函数实时复核，禁止把目录存在或图片可读当作已拥有。
            profile->equip=[base,nativeSpec](const CustomizationItemFrame& frame,size_t index) {return CallPcAction(base,frame,nativeSpec,index,CustomizationAction::Equip);};
            // 参数 frame/index 为当前真实装备及其列表位置；发型命令6、四部位道具命令5，经加载3返回列表0。
            profile->remove=[base,nativeSpec](const CustomizationItemFrame& frame,size_t index) {return CallPcAction(base,frame,nativeSpec,index,CustomizationAction::Remove);};
            // 参数 frame 为活动预览；游戏线程执行自然退出，执行器随后独立核对原装备恢复。
            profile->exitPreview=[base](const CustomizationItemFrame& frame) {return ExitPcPreview(base,frame);};
        }
        const bool ok=profile.has_value();
        RegisterCustomizationItemProfile(domain,std::move(profile));
        registered+=ok;
        text+=std::string(CustomizationDomainName(domain))+(ok?"原生规格已登记；":"原生规格未登记，定位／预览／更换暂不支持；");
    }
    std::lock_guard lock(diagnosticsMutex);lastDiagnostics=text;
    return registered;
}

std::string CustomizationItemProfileDiagnostics() {
    std::lock_guard lock(diagnosticsMutex);return lastDiagnostics;
}
}
