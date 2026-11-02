#include "customization_item_profiles.h"
#include <Windows.h>
#include <algorithm>
#include <mutex>

namespace diva {
namespace {
constexpr size_t kMaximumRecords=4096;
// 模型页已认证入口；发型／道具规格不得引用，防止套用模型分类函数或动作回调。
constexpr uintptr_t kModelCategoryRva=0x694D00,kModelActionRva=0x695250;
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
// 参数 rva 为规格中的入口；拒绝模型页已认证入口。
bool ModelEntry(const std::optional<uintptr_t>& rva) {return rva && (*rva==kModelCategoryRva || *rva==kModelActionRva);}
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
    for(const auto* rva:{&spec.stepRva,&spec.partRva,&spec.highlightRva,&spec.callbackRva}) {
        if(ModelEntry(*rva)) return std::nullopt;
        if(*rva && !Executable(base+**rva)) return std::nullopt;
    }
    if(!spec.highlightRva && !spec.stepRva) return std::nullopt;
    CustomizationItemProfile profile;profile.evidence=spec.evidence;
    // 参数 frame/out 为帧上下文与快照；只读。
    profile.read=[domain,spec](const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {return ReadSnapshot(domain,spec,frame,out);};
    if(spec.highlightRva) {
        const uintptr_t function=base+*spec.highlightRva;
        // 参数 frame/index 为帧上下文与列表序号；直接高亮。
        profile.highlight=[function](const CustomizationItemFrame& frame,size_t index) {return index<=0x7FFFFFFF && CallObjectInt(function,frame.scene,static_cast<int>(index));};
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
    // 生产规格表：本机实机认证后在此填写；当前两表均为空，因此不登记任何发型／道具原生能力。
    static const CustomizationItemNativeSpec kHairSpec{};
    static const CustomizationItemNativeSpec kAccessorySpec{};
    int registered=0;std::string text;
    for(auto [domain,spec]:{std::pair{CustomizationDomain::Hair,&kHairSpec},std::pair{CustomizationDomain::Accessories,&kAccessorySpec}}) {
        auto profile=base?BuildCustomizationItemProfile(domain,*spec,base):std::nullopt;
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
