#include "selector.h"
namespace diva {
// 用独立字段解释拒绝或超时原因；参数 snapshot 为当前快照。
std::string DescribeSnapshot(const Snapshot& s) {
    return "菜单="+std::to_string(s.current)+"/"+std::to_string(s.next)+"，控制标记="+std::to_string(s.changing)+"，实际歌曲="+std::to_string(s.visiblePvid)+"，实际难度="+std::to_string(s.visibleDifficulty)+"，请求歌曲="+std::to_string(s.pvid)+"，请求难度="+std::to_string(s.difficulty)+"，风格="+std::to_string(s.style)+"，模式="+std::to_string(s.mode)+"，稳定="+(s.stable?"是":"否");
}
// 查询是否正在切换。
bool Selector::Busy() const { return phase!=Phase::Idle; }
// 在普通歌曲选择界面接受请求，内部状态不是面向玩家的菜单名称；参数 value/available/snapshot/now 为动作与状态。
bool Selector::Submit(Request value,bool available,const Snapshot& snapshot,uint64_t now) {
    if(Busy()) { message="定位进行中，请稍候。"; return false; }
    succeeded=false;
    if(!available || value.pvid<=0 || value.difficulty<0 || value.difficulty>4 || value.style<0 || value.style>2) { message="该风格或难度没有生效谱面，请选择可用等级。"; return false; }
    if(!snapshot.supported) { message="游戏版本或输入隔离签名未验证，已禁止写入。"; return false; }
    // 实机普通选曲长期稳定为 5/6、控制值 1；两个枚举不是必须相等的当前/目标副本。
    if(snapshot.current!=5 || snapshot.next!=6 || !snapshot.stable || snapshot.changing!=1) { message="当前界面尚未稳定或不在歌曲选择界面，请稍后重试。（"+DescribeSnapshot(snapshot)+"）"; return false; }
    if(snapshot.mode!=0 && snapshot.mode!=1) { message="当前为 PV 观看模式，请回到歌曲选择界面。"; return false; }
    if(snapshot.visiblePvid<=0 || snapshot.visibleDifficulty<0) { message="无法读回当前选中歌曲，已停止定位。（"+DescribeSnapshot(snapshot)+"）"; return false; }
    request=value; start=phaseTime=now; confirmations=0; phase=Phase::Begin;
    original={snapshot.visiblePvid,snapshot.visibleDifficulty,snapshot.style,0};failure.clear();
    message="正在准备菜单切换……"; return true;
}
// 中途失败也要尝试返回原选曲，不把用户留在自己触发的播放列表；参数 memory/snapshot/reason/now 为恢复条件。
void Selector::Abort(Memory& memory,const Snapshot& s,const std::string& reason,uint64_t now) {
    succeeded=false;
    if(direct && s.supported && s.current==5 && s.next==6 && s.changing==1 && memory.DirectSelect(original)) {
        phase=Phase::Recover;phaseTime=now;failure=reason;message=reason+"，正在恢复原选曲……";return;
    }
    if(s.supported && s.current==6 && s.next==7 && s.changing==1 && memory.Select(original) && memory.Transition(5)) {
        phase=Phase::Recover;phaseTime=now;failure=reason;message=reason+"，正在返回原选曲……";return;
    }
    phase=Phase::Idle;message=reason+"（"+DescribeSnapshot(s)+"）";
}
// 跨帧执行准备、提交和真实读回；参数 memory/now 为接口与时钟。
void Selector::Tick(Memory& memory,uint64_t now) {
    if(!Busy()) return;
    auto s=memory.Read();
    if(phase==Phase::Recover) {
        if(s.supported && s.current==5 && s.next==6 && s.changing==1 && s.stable && s.visiblePvid==original.pvid && s.visibleDifficulty==original.difficulty) {
            phase=Phase::Idle;message=failure+"；已返回原选曲。";
        } else if(now-phaseTime>3000 || !s.supported) { phase=Phase::Idle;message=failure+"；自动返回未确认，请查看原版菜单。（"+DescribeSnapshot(s)+"）"; }
        return;
    }
    if(!s.supported || now-start>5000) { Abort(memory,s,"定位未确认：版本失效或菜单读回超时",now);return; }
    if(phase==Phase::Begin) {
        direct=memory.SupportsDirect();
        if(direct) {
            if(!memory.DirectSelect(request)) { Abort(memory,s,"原界面定位被拒绝或目标不在生效列表",now);return; }
            phase=Phase::Verify;phaseTime=now;message="原界面定位已提交，正在核对实际高亮……";return;
        }
        if(!memory.Transition(6)) Abort(memory,s,"菜单准备失败",now);
        else { phase=Phase::Prepare; phaseTime=now;message="已发出列表刷新请求，等待返回准备……"; }
        return;
    }
    if(phase==Phase::Prepare) {
        if(now-phaseTime<100) return;
        if(s.current!=6 && s.current!=5) { Abort(memory,s,"菜单已离开允许状态",now);return; }
        // 受控实机已证实 6/7 是正常中间状态；这里必须完成返回 5，而不是错误取消。
        if(s.current!=6 || s.next!=7 || s.changing!=1 || !s.stable) return;
        if(!memory.Select(request) || !memory.Transition(5)) { Abort(memory,s,"目标写入或返回请求失败",now);return; }
        phase=Phase::Verify; phaseTime=now; message="已提交，正在核对实际歌曲、难度和风格……";
    } else {
        if(s.current!=5 && s.current!=6) { Abort(memory,s,"菜单离开选曲状态，未确认定位",now);return; }
        bool matches=s.current==5 && s.next==6 && s.changing==1 && s.stable && (s.mode==0 || s.mode==1) && s.visiblePvid==request.pvid && s.visibleDifficulty==request.difficulty && s.pvid==request.pvid && s.difficulty==request.difficulty && s.style==request.style;
        confirmations=matches?confirmations+1:0;
        if(confirmations>=3 && now-phaseTime>=100) { succeeded=true; phase=Phase::Idle; message="定位成功，已核对 PVID、难度和风格；不会自动开始演奏。"; }
    }
}
}
