// 直接检验生产读取器，所有地址属于本测试VirtualAlloc空间，不附加或读取游戏进程。
#include "../src/recommendation_runtime.cpp"
#include <iostream>
#include <stdexcept>

namespace {
// 参数 value/message 为断言和中文错误原因。
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// 参数 address/value 为测试私有内存位置与值；仅夹具写入，不进入产品读取器。
template<class T> void Put(uintptr_t address,const T& value) {std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));}
}
// 检验分帧复制、获取器核查、向量边界及旧数据库失效，无原生调用或真实游戏依赖。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    auto* storage=VirtualAlloc(nullptr,0x1754000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!storage) return 2;
    try {
        using namespace diva;const auto base=reinterpret_cast<uintptr_t>(storage);
        std::array<std::array<uintptr_t,22>,9> pv{};std::array<std::array<int32_t,17>,9> performers{};std::array<uintptr_t,9> table{};
        for(size_t i=0;i<9;++i) {
            table[i]=reinterpret_cast<uintptr_t>(pv[i].data());Put(table[i],static_cast<int32_t>(100+i));
            performers[i].fill(-1);performers[i][0]=0;performers[i][1]=0;for(int d=0;d<5;++d)performers[i][3+d]=114;
            pv[i][0x98/8]=reinterpret_cast<uintptr_t>(performers[i].data());pv[i][0xA0/8]=pv[i][0x98/8]+0x44;
        }
        const auto begin=reinterpret_cast<uintptr_t>(table.data()),end=begin+sizeof(table);
        Put(base+0x1753818,begin);Put(base+0x1753820,end);
        std::array<unsigned char,8> code{0x48,0x8D,0x05,0,0,0,0,0xC3};const int32_t displacement=0x1753780-0x4BBA27;std::memcpy(code.data()+3,&displacement,4);Put(base+0x4BBA20,code);
        InitializeRecommendations(base);RequestRecommendations(false);TickRecommendations();Check(!ReadRecommendations(),"界面未请求仍扫描推荐数据库");
        RequestRecommendations(true);TickRecommendations();Check(!ReadRecommendations(),"每帧8条预算被突破或发布不完整快照");
        TickRecommendations();auto original=ReadRecommendations();Check(original && original->ready && original->songs.size()==9 && original->songs.at(100)[0].costumes[0]==114,"完整私有值快照未正确发布");
        nextRead=0;TickRecommendations();Put(base+0x1753820,begin);TickRecommendations();auto invalid=ReadRecommendations();
        Check(invalid && !invalid->ready && invalid->songs.empty() && invalid->generation>original->generation,"数据库重建未立即撤销旧推荐");
        Check(original->ready && original->songs.size()==9,"已发布快照被游戏指针变化原地修改");
        Put(base+0x1753820,end);nextRead=0;code[0]=0x90;Put(base+0x4BBA20,code);TickRecommendations();Check(!ReadRecommendations()->ready,"错误获取器仍发布推荐");
        code[0]=0x48;Put(base+0x4BBA20,code);nextRead=0;pv[0][0xA0/8]=pv[0][0x98/8]+1;TickRecommendations();Check(!ReadRecommendations()->ready,"错误演出记录步长未拒绝");
        VirtualFree(storage,0,MEM_RELEASE);std::cout<<"推荐运行时读取器隔离验收通过：获取器、分帧预算、值快照及重建失效；未访问游戏。\n";return 0;
    } catch(const std::exception& error) {VirtualFree(storage,0,MEM_RELEASE);std::cerr<<"推荐读取器验收失败："<<error.what()<<"\n";return 1;}
}
