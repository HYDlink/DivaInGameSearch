#pragma once
#include "native_memory.h"

namespace diva::native {
// 参数 scope/pvid/slot/count 为本帧范围、来源歌曲、目标槽位和槽位数；共通配置不要求存在来源歌曲。
inline bool ValidCustomizationSlot(int scope,int pvid,int slot,int count) {
    return scope>=CustomizeScope::CommonA && scope<=CustomizeScope::Song
        && count>=1 && count<=6 && slot>=0 && slot<count
        && (scope!=CustomizeScope::Song || (pvid>0 && pvid<10000000));
}

// 参数 base 为已核验构建基址；优先通过 GetSaveData() 获取当前存档对象并保留其他 Mod Hook 链。
inline uintptr_t ReadNativeSaveData(uintptr_t base) {
    if(!base || !IsExecutable(base+rva::kGetSaveData)) return 0;
    __try {
        return reinterpret_cast<uintptr_t(__fastcall*)()>(base+rva::kGetSaveData)();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}

// 参数 base/scope/saveGetter 为构建基址、输出范围与存档获取器；真实游戏从 GetSaveData()+kScope 读取，
// 仅在默认获取器面对非可执行合成测试夹具时退回静态 RVA，存档获取失败时不静默回退。
inline bool ReadCustomizationScope(uintptr_t base,int& scope,uintptr_t (*saveGetter)(uintptr_t)=ReadNativeSaveData) {
    if(!base) return false;
    if(saveGetter==ReadNativeSaveData && !IsExecutable(base+rva::kGetSaveData))
        return ReadAt(base+rva::kCustomizeScopeSelector,scope);
    const uintptr_t save=saveGetter?saveGetter(base):0;
    if(!save) return false;
    return ReadAt(save+customize_config::kScope,scope);
}

// 参数 base/scope/pvid/slot 为已核验基址及配置身份；原生获取器自行处理共通A/B/C、单曲及特殊曲目分支。
inline uintptr_t ReadNativeCustomizationSlot(uintptr_t base,int scope,int pvid,int slot) {
    const auto save=ReadNativeSaveData(base);
    int actualScope=-1;
    if(!save || !ReadAt(save+customize_config::kScope,actualScope) || actualScope!=scope) return 0;
    if(!IsExecutable(base+rva::kGetCustomizationSlotConfig)) return 0;
    __try {
        return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,int,int)>(base+rva::kGetCustomizationSlotConfig)(save,pvid,slot);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}

// 参数 base/controller/scope/pvid/slot/count/playlist 为真实配置上下文；reader 是原生获取适配器，隔离测试可替换。
// 优先沿用控制器独立记录；播放列表缺少记录时不退回其他配置。该函数只获取记录，从不写入装备或保存。
inline uintptr_t ReadCustomizationSlotRecord(uintptr_t base,uintptr_t controller,int scope,int pvid,int slot,int count,bool playlist,
    uintptr_t (*reader)(uintptr_t,int,int,int)=ReadNativeCustomizationSlot) {
    if(!controller || !ValidCustomizationSlot(scope,pvid,slot,count)) return 0;
    uintptr_t records=0;
    if(!ReadAt(controller+customize_config::kOverrideTable,records)) return 0;
    if(records) return records+static_cast<uintptr_t>(slot)*customize_config::kSlotRecordSize;
    if(playlist || !reader) return 0;
    return reader(base,scope,pvid,slot);
}
}
