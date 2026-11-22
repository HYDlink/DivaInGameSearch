// 共通A/B/C与单曲配置读取隔离回归：只访问本进程样本，原生获取器使用记录适配器替代。
#include "native_customization_config.h"
#include "customization_action.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks=0,reads=0;
std::array<std::array<std::array<int,7>,6>,4> saved{};
// 参数 condition/message 为断言与中文说明。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
// 参数 base/scope/pvid/slot 为生产读取器转交的配置身份；只返回测试进程内对应范围的独立记录。
uintptr_t ReadSample(uintptr_t base,int scope,int pvid,int slot) {
    Check(base==123 && pvid==817,"原生获取适配器收到错误基址或歌曲");++reads;
    return reinterpret_cast<uintptr_t>(saved[scope][slot].data());
}
uintptr_t testRelocatedSave=0;
// 返回测试进程内的重定位存档缓冲地址，模拟 Hook 了 GetSaveData() 的 Mod 环境。
uintptr_t RelocatedSaveGetter(uintptr_t base) {(void)base;return testRelocatedSave;}
// 返回空指针，模拟存档对象尚未就绪或读取失败的场景。
uintptr_t NullSaveGetter(uintptr_t base) {(void)base;return 0;}
// 管理仅保留地址空间并按页提交的隔离基址，防止测试异常时泄漏虚拟内存。
struct ScopeFixture {
    void* allocation=nullptr;
    uintptr_t base=0;
    ScopeFixture() {
        using namespace diva::native;
        const size_t span=std::max(rva::kCustomizeScopeSelector,rva::kGetSaveData)+0x1000;
        allocation=VirtualAlloc(nullptr,span,MEM_RESERVE,PAGE_READWRITE);
        Check(allocation!=nullptr,"无法保留范围测试隔离地址空间");
        base=reinterpret_cast<uintptr_t>(allocation);
        const auto scopePage=(base+rva::kCustomizeScopeSelector)&~uintptr_t(0xFFF);
        const auto getterPage=(base+rva::kGetSaveData)&~uintptr_t(0xFFF);
        Check(VirtualAlloc(reinterpret_cast<void*>(scopePage),0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"无法提交范围测试静态页");
        if(getterPage!=scopePage)
            Check(VirtualAlloc(reinterpret_cast<void*>(getterPage),0x1000,MEM_COMMIT,PAGE_READONLY)!=nullptr,"无法提交非可执行存档获取器页");
    }
    ~ScopeFixture() {if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
};
// 验证 SaveData 重定位、空存档拒绝回退与合成非可执行夹具回退路径。
void CheckScopeReader() {
    using namespace diva::native;
    int scope=-1;
    Check(!ReadCustomizationScope(0,scope),"空基址仍读取换装范围");
    ScopeFixture fixture;
    std::vector<unsigned char> relocatedBuffer(customize_config::kScope+sizeof(int32_t),0);
    testRelocatedSave=reinterpret_cast<uintptr_t>(relocatedBuffer.data());
    const int32_t staleScope=0;
    std::memcpy(reinterpret_cast<void*>(fixture.base+rva::kCustomizeScopeSelector),&staleScope,sizeof(staleScope));
    for(int32_t expected=0;expected<4;++expected) {
        std::memcpy(relocatedBuffer.data()+customize_config::kScope,&expected,sizeof(expected));
        scope=-1;
        Check(ReadCustomizationScope(fixture.base,scope,RelocatedSaveGetter) && scope==expected,"重定位存档未返回 GetSaveData()+kScope 的真实范围");
    }
    const int32_t staleSong=3,relocatedCommonB=1;
    std::memcpy(reinterpret_cast<void*>(fixture.base+rva::kCustomizeScopeSelector),&staleSong,sizeof(staleSong));
    std::memcpy(relocatedBuffer.data()+customize_config::kScope,&relocatedCommonB,sizeof(relocatedCommonB));
    scope=-1;
    Check(ReadCustomizationScope(fixture.base,scope,RelocatedSaveGetter) && scope==relocatedCommonB,"静态范围与重定位存档不一致时未优先使用重定位存档");
    Check(!ReadCustomizationScope(fixture.base,scope,NullSaveGetter),"存档获取器返回空指针时错误回退到静态范围");
    for(int32_t expected=0;expected<4;++expected) {
        std::memcpy(reinterpret_cast<void*>(fixture.base+rva::kCustomizeScopeSelector),&expected,sizeof(expected));
        scope=-1;
        Check(ReadCustomizationScope(fixture.base,scope) && scope==expected,"非可执行合成测试夹具未能回退读取静态范围");
    }
}
}

// 验证四范围六槽位、覆盖表优先、播放列表缺失拒绝、未知范围及单曲来源保护。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        using namespace diva;using namespace diva::native;
        std::array<uintptr_t,4> controller{};
        const auto address=reinterpret_cast<uintptr_t>(controller.data());
        for(int scope=0;scope<4;++scope) for(int slot=0;slot<6;++slot) {
            saved[scope][slot][0]=scope*100+slot;
            const auto record=ReadCustomizationSlotRecord(123,address,scope,817,slot,6,false,ReadSample);
            int id=-1;
            Check(record && ReadAt(record,id) && id==scope*100+slot,"共通或单曲范围读到了其他范围／槽位记录");
            Check(NativeCustomizationScope(scope)==(scope==3?CustomizationScope::Song:CustomizationScope::Common),"已知范围映射错误");
            Check(ValidCustomizationSlot(scope,scope<3?-1:817,slot,6),"共通配置错误要求来源歌曲");
        }
        Check(reads==24,"原生获取适配器调用次数不符");
        std::array<std::array<int,7>,6> overrides{};
        for(int slot=0;slot<6;++slot) overrides[slot][0]=600+slot;
        controller[2]=reinterpret_cast<uintptr_t>(overrides.data());
        for(int scope=0;scope<4;++scope) for(int slot=0;slot<6;++slot) {
            const auto record=ReadCustomizationSlotRecord(123,address,scope,817,slot,6,true,ReadSample);
            int id=-1;
            Check(record && ReadAt(record,id) && id==600+slot,"播放列表／独立覆盖表未优先于范围获取器");
        }
        Check(reads==24,"存在独立记录时仍调用了其他配置获取器");
        controller[2]=0;
        for(int scope=0;scope<4;++scope)
            Check(!ReadCustomizationSlotRecord(123,address,scope,817,0,1,true,ReadSample),"播放列表缺失独立记录时错误回退");
        for(int scope:{-1,4,99}) {
            Check(!ReadCustomizationSlotRecord(123,address,scope,817,0,1,false,ReadSample),"未知范围仍获取配置");
            Check(NativeCustomizationScope(scope)==CustomizationScope::Unknown,"未知范围被误认为共通配置");
        }
        for(int slot:{-1,6}) Check(!ReadCustomizationSlotRecord(123,address,0,817,slot,6,false,ReadSample),"越界槽位仍获取配置");
        for(int count:{0,7}) Check(!ReadCustomizationSlotRecord(123,address,0,817,0,count,false,ReadSample),"非法槽位数量仍获取配置");
        Check(!ReadCustomizationSlotRecord(123,address,3,-1,0,1,false,ReadSample),"单曲范围接受无效来源歌曲");
        Check(!ReadCustomizationSlotRecord(123,address,0,817,0,1,false,nullptr),"无获取适配器时伪造记录");
        Check(reads==24,"拒绝路径仍调用了原生配置获取器");
        CheckScopeReader();
        std::cout<<"共通与单曲配置读取隔离回归通过："<<checks<<"项；未访问游戏。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"配置读取验证失败："<<error.what()<<"\n";return 1;}
}
