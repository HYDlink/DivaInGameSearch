#include "i18n.h"
#include "selector.h"
#include "native_layout.h"
namespace diva {
using namespace native;
// 用独立字段解释拒绝或超时原因；参数 snapshot 为当前快照。
std::string DescribeSnapshot(const Snapshot& s) {
    return Trf(Tx::selector_snapshot,{{"current",std::to_string(s.subCurrent)},{"next",std::to_string(s.subNext)},{"changing",std::to_string(s.subProc)},{"pvid",std::to_string(s.visiblePvid)},{"difficulty",std::to_string(s.visibleDifficulty)},{"req_pvid",std::to_string(s.pvid)},{"req_difficulty",std::to_string(s.difficulty)},{"style",std::to_string(s.style)},{"mode",std::to_string(s.mode)},{"stable",Tr(s.stable?Tx::selector_yes:Tx::selector_no)}});
}
// 查询是否正在切换。
bool Selector::Busy() const { return phase!=Phase::Idle; }
// 取消等待或读回，不尝试恢复旧歌曲，避免覆盖玩家的新操作。
void Selector::Cancel() {phase=Phase::Idle;succeeded=false;message=Tr(Tx::selector_cancelled);}
// 在普通歌曲选择界面接受请求，内部状态不是面向玩家的菜单名称；参数 value/available/snapshot/now 为动作与状态。
bool Selector::Submit(Request value,bool available,const Snapshot& snapshot,uint64_t now) {
    if(Busy()) { message=Tr(Tx::selector_busy); return false; }
    succeeded=false;
    if(!available || value.pvid<=0 || value.difficulty<0 || value.difficulty>kRequestExtraExtreme || value.style<0 || value.style>2) { message=Tr(Tx::selector_no_chart); return false; }
    if(!snapshot.supported) { message=Tr(Tx::selector_unsupported); return false; }
    // 实机普通选曲长期稳定为 PvSel/PlaylistSel、过程值 Ctrl；sub_next 只是序列的下一项，不是必须与当前相等的副本。
    if(!snapshot.stable || snapshot.subProc!=ModeProc::Ctrl || (!snapshot.playlistSong && (snapshot.subCurrent!=ModeSub::PvSel || snapshot.subNext!=ModeSub::PlaylistSel))) { message=Trf(Tx::selector_not_ready,{{"details",DescribeSnapshot(snapshot)}}); return false; }
    if(!snapshot.playlistSong && snapshot.mode!=0 && snapshot.mode!=1) { message=Tr(Tx::selector_pv_mode); return false; }
    if(snapshot.visiblePvid<=0 || (!snapshot.playlistSong && snapshot.visibleDifficulty<0)) { message=Trf(Tx::selector_read_failed,{{"details",DescribeSnapshot(snapshot)}}); return false; }
    request=value; playlist=snapshot.playlistSong; start=phaseTime=now; confirmations=0; phase=Phase::Begin;
    original={snapshot.visiblePvid,snapshot.visibleDifficulty,snapshot.style,0};failure.clear();
    message=Tr(Tx::selector_preparing); return true;
}
// 中途失败也要尝试返回原选曲，不把用户留在自己触发的播放列表；参数 memory/snapshot/reason/now 为恢复条件。
void Selector::Abort(Memory& memory,const Snapshot& s,const std::string& reason,uint64_t now) {
    succeeded=false;
    // 播放列表是用户主动进入的页面，失败时绝不能套用普通选曲的返回菜单协议。
    if(playlist) { phase=Phase::Idle;message=reason+"（"+DescribeSnapshot(s)+"）";return; }
    if(direct && s.supported && s.subCurrent==ModeSub::PvSel && s.subNext==ModeSub::PlaylistSel && s.subProc==ModeProc::Ctrl && memory.DirectSelect(original)) {
        phase=Phase::Recover;phaseTime=now;failure=reason;message=Trf(Tx::selector_recovering,{{"reason",reason}});return;
    }
    if(s.supported && s.subCurrent==ModeSub::PlaylistSel && s.subNext==ModeSub::Game && s.subProc==ModeProc::Ctrl && memory.Select(original) && memory.Transition(ModeSub::PvSel)) {
        phase=Phase::Recover;phaseTime=now;failure=reason;message=Trf(Tx::selector_returning,{{"reason",reason}});return;
    }
    phase=Phase::Idle;message=reason+"（"+DescribeSnapshot(s)+"）";
}
// 跨帧执行准备、提交和真实读回；参数 memory/now 为接口与时钟。
void Selector::Tick(Memory& memory,uint64_t now) {
    if(!Busy()) return;
    auto s=memory.Read();
    if(playlist && !s.playlistSong) { phase=Phase::Idle;message=Tr(Tx::selector_left_song_select);return; }
    if(phase==Phase::Recover) {
        if(s.supported && s.subCurrent==ModeSub::PvSel && s.subNext==ModeSub::PlaylistSel && s.subProc==ModeProc::Ctrl && s.stable && s.visiblePvid==original.pvid && s.visibleDifficulty==original.difficulty) {
            phase=Phase::Idle;message=Trf(Tx::selector_returned,{{"reason",failure}});
        } else if(now-phaseTime>3000 || !s.supported) { phase=Phase::Idle;message=Trf(Tx::selector_return_unconfirmed,{{"reason",failure},{"details",DescribeSnapshot(s)}}); }
        return;
    }
    if(!s.supported || now-start>5000) { Abort(memory,s,Tr(Tx::selector_unconfirmed),now);return; }
    if(phase==Phase::Begin) {
        direct=memory.SupportsDirect();
        if(direct) {
            if(!memory.DirectSelect(request)) { Abort(memory,s,Tr(Tx::selector_rejected),now);return; }
            phase=Phase::Verify;phaseTime=now;message=Tr(Tx::selector_direct_submitted);return;
        }
        if(!memory.Transition(ModeSub::PlaylistSel)) Abort(memory,s,Tr(Tx::selector_menu_failed),now);
        else { phase=Phase::Prepare; phaseTime=now;message=Tr(Tx::selector_refresh_requested); }
        return;
    }
    if(phase==Phase::Prepare) {
        if(now-phaseTime<100) return;
        if(s.subCurrent!=ModeSub::PlaylistSel && s.subCurrent!=ModeSub::PvSel) { Abort(memory,s,Tr(Tx::selector_menu_left),now);return; }
        // 受控实机已证实 PlaylistSel/Game 是正常中间状态；这里必须完成返回 PvSel，而不是错误取消。
        if(s.subCurrent!=ModeSub::PlaylistSel || s.subNext!=ModeSub::Game || s.subProc!=ModeProc::Ctrl || !s.stable) return;
        if(!memory.Select(request) || !memory.Transition(ModeSub::PvSel)) { Abort(memory,s,Tr(Tx::selector_write_failed),now);return; }
        phase=Phase::Verify; phaseTime=now; message=Tr(Tx::selector_verifying);
    } else {
        if(s.subCurrent!=ModeSub::PvSel && s.subCurrent!=ModeSub::PlaylistSel) { Abort(memory,s,Tr(Tx::selector_left_song_select),now);return; }
        bool matches=playlist ? s.playlistSong && s.playlistHighlightMatched && s.stable && s.visiblePvid==request.pvid : s.subCurrent==ModeSub::PvSel && s.subNext==ModeSub::PlaylistSel && s.subProc==ModeProc::Ctrl && s.stable && (s.mode==0 || s.mode==1) && s.visiblePvid==request.pvid && s.visibleDifficulty==request.difficulty && s.pvid==request.pvid && s.difficulty==request.difficulty && s.style==request.style;
        confirmations=matches?confirmations+1:0;
        if(confirmations>=3 && now-phaseTime>=100) { succeeded=true; phase=Phase::Idle; message=Tr(Tx::selector_success); }
    }
}
}
