// 在隔离进程内使用真实生产读取器；不注入游戏，也不调用原生选择或Hook初始化。
#include "../src/module_locator.cpp"
#include <iostream>
#include <stdexcept>

namespace {
// 参数 condition/message 为断言与中文原因。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 参数 address/value 为隔离夹具中的字段地址与初值。
template<class T> void Put(uintptr_t address,T value) {std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
// 只保留地址空间并提交根对象所在一页，避免为游戏RVA分配大量物理内存。
struct Fixture {
    void* allocation=nullptr;
    uintptr_t base=0,page=0,controller=0;
    alignas(uintptr_t) std::array<unsigned char,0x400> pageData{},controllerData{};
    alignas(uintptr_t) std::array<int32_t,4> record{};
    uintptr_t recordPointer=0;
    // 创建只读模型页快照所需的最小对象关系和唯一真实列表条目。
    Fixture() {
        allocation=VirtualAlloc(nullptr,diva::kControllerRootRva+0x1000,MEM_RESERVE,PAGE_READWRITE);
        Check(allocation!=nullptr,"无法保留隔离地址空间");base=reinterpret_cast<uintptr_t>(allocation);
        const auto rootPage=(base+diva::kControllerRootRva)&~uintptr_t(0xFFF);
        if(!VirtualAlloc(reinterpret_cast<void*>(rootPage),0x1000,MEM_COMMIT,PAGE_READWRITE)) {
            VirtualFree(allocation,0,MEM_RELEASE);allocation=nullptr;throw std::runtime_error("无法提交隔离根对象页");
        }
        page=reinterpret_cast<uintptr_t>(pageData.data());controller=reinterpret_cast<uintptr_t>(controllerData.data());
        record[0]=137;record[2]=0;recordPointer=reinterpret_cast<uintptr_t>(record.data());
        Put(page,base+diva::kModuleVtableRva);Put(page+8,controller);
        Put(base+diva::kControllerRootRva+0x78,controller);Put(controller+0x18,page);
        Put(page+0x20,int32_t(1));Put(page+0x60,int32_t(-1));
        Put(page+0x70,reinterpret_cast<uintptr_t>(&recordPointer));
        Put(page+0x78,reinterpret_cast<uintptr_t>(&recordPointer)+sizeof(uintptr_t));Put(page+0x11C,int32_t(1));
    }
    // 仅释放本测试保留的地址空间。
    ~Fixture(){if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
    // 复用生产内存读取、对象身份、列表与上下文判定。
    diva::FrameSnapshot Read(){diva::FrameSnapshot result;Check(diva::ReadFrameSnapshot(base,page,result),"生产读取器未能读取有效夹具");return result;}
};
}
// 回归现场137高亮/125原始模型字段，并验证其他页面/弹层/角色守卫继续拒绝。
int main() {
    try {
        Fixture f;
        for(int32_t model:{0,125,316,394}) {
            Put(f.controller+0x320,model);Put(f.controller+0x338,model);
            auto s=f.Read();Check(s.contextReady,"非零原始模型值误杀正常模型列表");
            Check(s.rawModelA==model && s.rawModelB==model && s.currentId==137,"快照丢失原始字段或混淆高亮ID");
        }
        auto before=f.Read();Put(f.controller+0x338,int32_t(125));
        Check(!diva::SameMenuContext(before,f.Read()),"原始字段变化未取消旧上下文");
        Put(f.page+0x20,int32_t(2));Check(f.Read().contextReady,"现场正常列表状态2被误拒绝");
        for(int32_t state:{0,3}) {Put(f.page+0x20,state);Check(!f.Read().contextReady,"非列表页面意外允许定位");}
        Put(f.page+0x20,int32_t(1));Put(f.page+0x60,int32_t(0));Check(!f.Read().contextReady,"操作弹层未阻止定位");
        Put(f.page+0x60,int32_t(-1));Put(f.controller+0x3C,int32_t(1));Check(!f.Read().contextReady,"未知原始上下文未阻止定位");
        Put(f.controller+0x3C,int32_t(0));Put(f.controller+0x40,int32_t(7));Check(!f.Read().contextReady,"随机分类未阻止定位");
        Put(f.controller+0x40,int32_t(1));Check(!f.Read().contextReady,"记录人物不匹配未阻止定位");
        Put(f.controller+0x40,int32_t(0));Put(f.page+0x11C,int32_t(2));
        diva::FrameSnapshot invalid;Check(!diva::ReadFrameSnapshot(f.base,f.page,invalid),"列表数量不一致未被拒绝");
        std::cout<<"生产模型上下文回归通过：非零原始模型值、真实高亮、状态/弹层/人物/列表守卫；未访问游戏。\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
