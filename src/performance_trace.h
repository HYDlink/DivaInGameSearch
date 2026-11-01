#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace diva {
// 返回单调毫秒时刻；不依赖系统时区或墙上时钟调整。
inline double PerformanceNow() {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// 一条查询携带自身时序，后台线程只填写副本，不访问UI或记录器。
struct SearchTiming {double changed=0,due=0,submitted=0,started=0,finished=0;};
struct PerformanceFrame {
    double gap=0,total=0,frameLock=0,inputLock=0,poll=0,assets=0,draw=0,render=0,upload=0,native=0,inputToDraw=-1;
    unsigned uploads=0;
};
struct PerformanceQuery {std::string mode;uint64_t revision=0;SearchTiming timing;double published=0,publishCpu=0;};
// 记录最近15秒的有界样本；窗口消息只写原子时间戳，记录期间不执行磁盘I/O。
struct PerformanceTrace {
    std::atomic<double> inputAt{0};
    std::atomic<uint64_t> inputSequence{0};
    std::atomic<uint64_t> nativeMicroseconds{0};
    uint64_t consumedInput=0;
    bool active=false,recording=false;
    double previousFrame=0,started=0,recordUntil=0,pendingInputLock=0;
    PerformanceFrame current,last;
    std::vector<PerformanceFrame> frames;
    std::vector<PerformanceQuery> queries;
    // 输入消息抵达时先记时间，不记录用户文本或按键内容。
    void Input() {inputAt.store(PerformanceNow(),std::memory_order_relaxed);inputSequence.fetch_add(1,std::memory_order_release);}
    // 参数milliseconds为输入消息等待Host锁的实测时间，在锁内累积本帧最大值。
    void InputLock(double milliseconds) {pendingInputLock=std::max(pendingInputLock,milliseconds);}
    // 开始一次15秒内存记录，预留容量避免逐帧扩容；不改搜索参数。
    void Start() {frames.clear();queries.clear();frames.reserve(4096);queries.reserve(128);consumedInput=inputSequence.load();recordUntil=PerformanceNow()+15000;recording=true;}
    // 参数now为本Mod回调开始时刻，间隔包含游戏、其他Mod及Present等待，不能归因给搜索。
    void Begin(double now) {active=true;current={};started=now;current.gap=previousFrame?now-previousFrame:0;previousFrame=now;}
    // 参数now为回调完成时刻；输入到绘制耗时不等于显示器真正呈现延迟。
    void End(double now) {
        current.total=now-started;current.inputLock=pendingInputLock;pendingInputLock=0;
        const auto sequence=inputSequence.load(std::memory_order_acquire);
        if(sequence!=consumedInput) {current.inputToDraw=now-inputAt.load(std::memory_order_relaxed);consumedInput=sequence;}
        current.native=nativeMicroseconds.exchange(0,std::memory_order_relaxed)/1000.0;
        last=current;active=false;
        if(recording && frames.size()<4096) frames.push_back(current);
        if(now>=recordUntil || frames.size()>=4096) recording=false;
    }
    // 参数mode/revision/timing/publishCpu为已发布查询及其结果应用CPU耗时，丢弃旧结果不伪造成功发布。
    void Query(const char* mode,uint64_t revision,const SearchTiming& timing,double publishCpu) {
        if(recording && timing.changed>0 && queries.size()<128) queries.push_back({mode,revision,timing,PerformanceNow(),publishCpu});
    }
    // 参数rows/member为样本和待统计字段，报告95百分位而非只看平均值。
    static double Percentile(const std::vector<PerformanceFrame>& rows,double PerformanceFrame::*member) {
        std::vector<double> values;values.reserve(rows.size());for(const auto& row:rows) if(row.*member>=0) values.push_back(row.*member);
        if(values.empty())return 0;std::sort(values.begin(),values.end());return values[static_cast<size_t>((values.size()-1)*0.95)];
    }
    // 导出中文TSV文本；调用方在后台写文件，不在帧内格式化或写磁盘。
    static std::string Report(const std::vector<PerformanceFrame>& frames,const std::vector<PerformanceQuery>& queries) {
        std::ostringstream out;out<<std::fixed<<std::setprecision(3);
        out<<"说明\t单位毫秒，均为同步历时而非线程CPU占用或GPU耗时；帧间隔含游戏、其他Mod和帧外原生Hook，不能仅凭差值归因；输入到绘制不等于最终屏幕延迟；不记录查询文字\n";
        out<<"帧号\t帧间隔\t本Mod回调\t帧锁等待\t输入锁等待\t查询轮询\t资源同步\t界面构建\tDX11提交\t纹理上传\t上传张数\t共享路由Hook\t输入到绘制\n";
        for(size_t i=0;i<frames.size();++i) {const auto& f=frames[i];out<<i<<'\t'<<f.gap<<'\t'<<f.total<<'\t'<<f.frameLock<<'\t'<<f.inputLock<<'\t'<<f.poll<<'\t'<<f.assets<<'\t'<<f.draw<<'\t'<<f.render<<'\t'<<f.upload<<'\t'<<f.uploads<<'\t'<<f.native<<'\t'<<f.inputToDraw<<'\n';}
        out<<"查询类型\t修订\t输入到提交（含防抖）\t截止后调度等待\t后台排队\t查询执行\t完成到发布\t发布应用耗时\t输入到发布\n";
        for(const auto& q:queries) {const auto& t=q.timing;out<<q.mode<<'\t'<<q.revision<<'\t'<<t.submitted-t.changed<<'\t'<<std::max(0.0,t.submitted-t.due)<<'\t'<<t.started-t.submitted<<'\t'<<t.finished-t.started<<'\t'<<q.published-t.finished<<'\t'<<q.publishCpu<<'\t'<<q.published-t.changed<<'\n';}
        out<<"95百分位\t帧间隔\t本Mod回调\t输入到绘制\n毫秒\t"<<Percentile(frames,&PerformanceFrame::gap)<<'\t'<<Percentile(frames,&PerformanceFrame::total)<<'\t'<<Percentile(frames,&PerformanceFrame::inputToDraw)<<'\n';return out.str();
    }
};
// 作用域记录本Mod回调；参数enabled用于共用Preview宿主避免重复统计嵌套帧。
struct PerformanceFrameScope {
    PerformanceTrace& trace;bool enabled;
    PerformanceFrameScope(PerformanceTrace& value,bool enabled=true,double beginAt=0):trace(value),enabled(enabled) {if(enabled)trace.Begin(beginAt?beginAt:PerformanceNow());}
    // 离开回调时只追加内存样本，不等待后台或GPU。
    ~PerformanceFrameScope() {if(enabled)trace.End(PerformanceNow());}
};
// 参数target为该阶段累计毫秒数，支持同帧多张纹理累积。
struct PerformanceStageScope {
    double& target;double started=PerformanceNow();
    PerformanceStageScope(double& value):target(value) {}
    // 累加当前同步CPU阶段，不把GPU完成时间当成已测值。
    ~PerformanceStageScope() {target+=PerformanceNow()-started;}
};
}
