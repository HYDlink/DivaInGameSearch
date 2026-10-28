#pragma once
#include <cstdint>
#include <string>
namespace diva {
struct Request { int pvid=0, difficulty=0, style=0; uint64_t serial=0; };
struct Snapshot {
    // 实际菜单、过渡标记、请求字段和实际可见 PVID。
    bool supported=false;
    int current=-1,next=-1,changing=-1,pvid=-1,difficulty=-1,style=-1,visiblePvid=-1;
    // 实际选曲对象的难度与模式；稳定性由连续观察判定，不把控制标记当布尔值。
    int visibleDifficulty=-1,mode=0;
    bool stable=true;
};
// 生成中文状态诊断；参数 snapshot 为独立读回的菜单与选曲数据。
std::string DescribeSnapshot(const Snapshot& snapshot);
class Memory {
public:
    // 读取当前真实状态。
    virtual Snapshot Read()=0;
    // 触发菜单阶段；参数 next 为目标菜单编号。
    virtual bool Transition(int next)=0;
    // 写入选曲字段；参数 request 为已验证请求。
    virtual bool Select(const Request& request)=0;
    // 声明是否支持经过 PC 入口核查的原界面定位。
    virtual bool SupportsDirect() const { return false; }
    // 在当前选曲任务中提交；参数 request 为已验证的歌曲、难度与风格。
    virtual bool DirectSelect(const Request& request) { (void)request;return false; }
    // 允许派生适配器安全析构。
    virtual ~Memory()=default;
};
class Selector {
public:
    // 提交有效谱面；参数 request/available/snapshot/now 为请求、可用性、状态和毫秒时钟。
    bool Submit(Request request,bool available,const Snapshot& snapshot,uint64_t now);
    // 每帧推进一次，禁止阻塞；参数 memory/now 为适配器与毫秒时钟。
    void Tick(Memory& memory,uint64_t now);
    // 返回当前请求是否尚未结束。
    bool Busy() const;
    std::string message="在稳定选曲菜单打开面板后定位。";
    bool succeeded=false;
private:
    enum class Phase { Idle, Begin, Prepare, Verify, Recover };
    Phase phase=Phase::Idle;
    Request request;
    Request original;
    std::string failure;
    bool direct=false;
    uint64_t start=0,phaseTime=0;
    int confirmations=0;
    // 仅在自己进入的已知中间菜单恢复原高亮；参数 memory/snapshot/reason/now 为适配器、状态、原因与时钟。
    void Abort(Memory& memory,const Snapshot& snapshot,const std::string& reason,uint64_t now);
};
}
