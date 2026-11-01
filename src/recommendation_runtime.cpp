#include "recommendation.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace diva {
namespace {
std::atomic<uintptr_t> runtimeBase{0};
std::atomic<bool> wanted{false};
std::mutex publicationMutex;
std::shared_ptr<const RecommendationSnapshot> published;
std::shared_ptr<RecommendationSnapshot> building;
std::vector<uintptr_t> entries;
uintptr_t tableBegin=0,tableEnd=0;
size_t cursor=0;
uint64_t nextRead=0;
// 参数 address/value 为本进程只读地址和接收值；不可读时拒绝，不改变页权限。
template<class T> bool ReadValue(uintptr_t address,T& value) {
    SIZE_T count=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(value),&count) && count==sizeof(value);
}
// 参数 snapshot 为完整值快照；只有内容或就绪状态改变才递增代数。
void Publish(std::shared_ptr<RecommendationSnapshot> snapshot) {
    std::lock_guard lock(publicationMutex);
    if(published && snapshot->songs==published->songs && snapshot->ready==published->ready && snapshot->reason==published->reason) return;
    snapshot->generation=published?published->generation+1:1;published=std::move(snapshot);
}
// 参数 reason 为失效原因；立即撤销旧推荐，避免把旧库当作当前数据。
void Invalidate(const std::string& reason) {
    building.reset();entries.clear();auto snapshot=std::make_shared<RecommendationSnapshot>();snapshot->reason=reason;Publish(snapshot);nextRead=GetTickCount64()+2000;
}
}
// 参数 base 已由调用方完成商业PC完整构建核验，读取前仍检查获取器指令。
void InitializeRecommendations(uintptr_t base) {runtimeBase.store(base);}
// 参数 enabled 为当前推荐页需求；通过原子值跨界面与游戏线程传递。
void RequestRecommendations(bool enabled) {wanted.store(enabled);}
// 获取不可变快照；持锁仅复制共享指针，不在锁内扫描游戏。
std::shared_ptr<const RecommendationSnapshot> ReadRecommendations() {std::lock_guard lock(publicationMutex);return published;}
// 外层原生帧末每次最多读取8首歌曲；游戏指针始终留在本线程。
void TickRecommendations() {
    try {
    const auto base=runtimeBase.load();if(!base || !wanted.load()) return;
    const auto now=GetTickCount64();
    if(!building && now<nextRead) return;
    uintptr_t begin=0,end=0;
    if(!ReadValue(base+0x1753780+0x98,begin) || !ReadValue(base+0x1753780+0xA0,end)
        || !begin || end<=begin || (end-begin)%sizeof(uintptr_t) || (end-begin)/sizeof(uintptr_t)>4096) {Invalidate("游戏推荐数据库尚未就绪或边界不符。");return;}
    if(!building) {
        std::array<unsigned char,8> code{};int32_t displacement=0;
        if(!ReadValue(base+0x4BBA20,code) || code[0]!=0x48 || code[1]!=0x8D || code[2]!=0x05 || code[7]!=0xC3) {Invalidate("推荐数据库获取器不匹配，已停止读取。");return;}
        std::memcpy(&displacement,code.data()+3,4);
        if(base+0x4BBA27+displacement!=base+0x1753780) {Invalidate("推荐数据库来源不匹配。");return;}
        entries.resize((end-begin)/sizeof(uintptr_t));SIZE_T bytes=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(begin),entries.data(),end-begin,&bytes) || bytes!=end-begin) {Invalidate("推荐歌曲目录暂不可读。");return;}
        tableBegin=begin;tableEnd=end;cursor=0;building=std::make_shared<RecommendationSnapshot>();
    }
    if(begin!=tableBegin || end!=tableEnd) {Invalidate("推荐数据库正在重建，等待新快照。");return;}
    for(size_t batch=0;batch<8 && cursor<entries.size();++batch,++cursor) {
        uintptr_t entry=0,performerBegin=0,performerEnd=0;int pvid=-1;
        if(!ReadValue(begin+cursor*sizeof(uintptr_t),entry) || entry!=entries[cursor] || !ReadValue(entry,pvid) || pvid<0 || pvid>1000000
            || !ReadValue(entry+0x98,performerBegin) || !ReadValue(entry+0xA0,performerEnd) || performerEnd<performerBegin
            || (performerEnd-performerBegin)%0x44 || (performerEnd-performerBegin)/0x44>6) {Invalidate("推荐记录发生变化或布局不符，未使用不完整数据。");return;}
        std::vector<RecommendedPerformer> values;
        for(uintptr_t at=performerBegin;at<performerEnd;at+=0x44) {
            std::array<int32_t,17> raw{};
            if(!ReadValue(at,raw)) {Invalidate("演出角色推荐暂不可读。");return;}
            RecommendedPerformer value;value.type=raw[0];value.character=raw[1];value.pseudoSameId=raw[11];for(int d=0;d<5;++d)value.costumes[d]=raw[3+d];values.push_back(value);
        }
        uintptr_t checkBegin=0,checkEnd=0;
        if(!ReadValue(entry+0x98,checkBegin) || !ReadValue(entry+0xA0,checkEnd) || checkBegin!=performerBegin || checkEnd!=performerEnd || building->songs.contains(pvid)) {Invalidate("推荐记录身份不稳定，等待重新读取。");return;}
        building->songs.emplace(pvid,std::move(values));
    }
    if(cursor==entries.size()) {
        uintptr_t checkBegin=0,checkEnd=0;
        if(!ReadValue(base+0x1753780+0x98,checkBegin) || !ReadValue(base+0x1753780+0xA0,checkEnd) || checkBegin!=begin || checkEnd!=end) {Invalidate("推荐数据库发布前已重建，等待新快照。");return;}
        building->ready=true;building->reason.clear();Publish(building);building.reset();entries.clear();nextRead=now+2000;
    }
    } catch(...) {
        // 分配或快照构造失败只关闭推荐读取，异常不能穿过游戏原生帧调用链。
        building.reset();entries.clear();wanted.store(false);
        std::lock_guard lock(publicationMutex);published.reset();
    }
}
}
