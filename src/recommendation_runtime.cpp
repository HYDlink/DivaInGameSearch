#include "i18n.h"
#include "recommendation.h"
#include "native_memory.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace diva {
using namespace native;
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
    if(!ReadAt(base+rva::kPvDb+pv_db::kEntriesBegin,begin) || !ReadAt(base+rva::kPvDb+pv_db::kEntriesEnd,end)
        || !begin || end<=begin || (end-begin)%sizeof(uintptr_t) || (end-begin)/sizeof(uintptr_t)>4096) {Invalidate(Tr(Tx::reco_db_not_ready));return;}
    if(!building) {
        std::array<unsigned char,8> code{};int32_t displacement=0;
        if(!ReadAt(base+rva::kGetPvDb,code) || code[0]!=0x48 || code[1]!=0x8D || code[2]!=0x05 || code[7]!=0xC3) {Invalidate(Tr(Tx::reco_getter_mismatch));return;}
        std::memcpy(&displacement,code.data()+3,4);
        if(base+rva::kGetPvDbLeaEnd+displacement!=base+rva::kPvDb) {Invalidate(Tr(Tx::reco_source_mismatch));return;}
        entries.resize((end-begin)/sizeof(uintptr_t));SIZE_T bytes=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(begin),entries.data(),end-begin,&bytes) || bytes!=end-begin) {Invalidate(Tr(Tx::reco_list_unreadable));return;}
        tableBegin=begin;tableEnd=end;cursor=0;building=std::make_shared<RecommendationSnapshot>();
    }
    if(begin!=tableBegin || end!=tableEnd) {Invalidate(Tr(Tx::reco_rebuilding));return;}
    for(size_t batch=0;batch<8 && cursor<entries.size();++batch,++cursor) {
        uintptr_t entry=0,performerBegin=0,performerEnd=0;int pvid=-1;
        if(!ReadAt(begin+cursor*sizeof(uintptr_t),entry) || entry!=entries[cursor] || !ReadAt(entry,pvid) || pvid<0 || pvid>1000000
            || !ReadAt(entry+pv_db::kEntryPerformersBegin,performerBegin) || !ReadAt(entry+pv_db::kEntryPerformersEnd,performerEnd) || performerEnd<performerBegin
            || (performerEnd-performerBegin)%pv_db::kPerformerSize || (performerEnd-performerBegin)/pv_db::kPerformerSize>6) {Invalidate(Tr(Tx::reco_layout_changed));return;}
        std::vector<RecommendedPerformer> values;
        for(uintptr_t at=performerBegin;at<performerEnd;at+=pv_db::kPerformerSize) {
            std::array<int32_t,17> raw{};
            if(!ReadAt(at,raw)) {Invalidate(Tr(Tx::reco_performer_unreadable));return;}
            RecommendedPerformer value;value.type=raw[0];value.character=raw[1];value.pseudoSameId=raw[11];for(int d=0;d<5;++d)value.costumes[d]=raw[3+d];values.push_back(value);
        }
        uintptr_t checkBegin=0,checkEnd=0;
        if(!ReadAt(entry+pv_db::kEntryPerformersBegin,checkBegin) || !ReadAt(entry+pv_db::kEntryPerformersEnd,checkEnd) || checkBegin!=performerBegin || checkEnd!=performerEnd || building->songs.contains(pvid)) {Invalidate(Tr(Tx::reco_identity_unstable));return;}
        building->songs.emplace(pvid,std::move(values));
    }
    if(cursor==entries.size()) {
        uintptr_t checkBegin=0,checkEnd=0;
        if(!ReadAt(base+rva::kPvDb+pv_db::kEntriesBegin,checkBegin) || !ReadAt(base+rva::kPvDb+pv_db::kEntriesEnd,checkEnd) || checkBegin!=begin || checkEnd!=end) {Invalidate(Tr(Tx::reco_rebuilt));return;}
        building->ready=true;building->reason.clear();Publish(building);building.reset();entries.clear();nextRead=now+2000;
    }
    } catch(...) {
        // 分配或快照构造失败只关闭推荐读取，异常不能穿过游戏原生帧调用链。
        building.reset();entries.clear();wanted.store(false);
        std::lock_guard lock(publicationMutex);published.reset();
    }
}
}
