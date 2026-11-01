// 隔离验证生产动作守卫和跨帧确认器，不向游戏注入，不调用任何原生动作。
#include "../src/module_action.cpp"
#include <iostream>
#include <stdexcept>
namespace diva {
// 参数base为未使用的隔离基址；本回归直接构造只读样本，不访问游戏场景。
NativePageSnapshot ReadNativePage(uintptr_t base) {(void)base;return {};}
}
namespace {
// 参数condition/message为断言条件及中文说明。
void Check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
// 创建单曲第二歌者的隔离样本；搜索高亮、实际装备和加载模型分别设置。
diva::action_runtime::Snapshot Sample() {
    diva::action_runtime::Snapshot s;s.valid=true;
    s.page.page=diva::NativePage::Module;s.page.scene=100;s.page.controller=200;s.page.configuration=300;s.page.state=400;
    s.page.slot=1;s.page.slotCount=3;s.page.nativeScope=3;s.page.sourcePvid=817;s.page.internal=0;s.page.popup=-1;s.page.idle=true;
    s.begin=500;s.end=516;s.count=2;s.category=0;s.highlight=0;s.highlightId=0;s.equippedIndex=0;s.equippedId=0;s.equippedCharacter=0;s.models={45,0,44,0,0,0};return s;
}
// 创建绑定明确范围和槽位的测试请求；参数serial/action分别为序号与动作。
diva::CustomizationLocateRequest ActionRequest(uint64_t serial,diva::CustomizationAction action=diva::CustomizationAction::Equip) {
    diva::CustomizationLocateRequest r;r.key={diva::CustomizationKind::Module,316};r.character=diva::CustomizationCharacter::Miku;
    r.serial=serial;r.uiGeneration=9;r.queryRevision=4;r.contextGeneration=7;r.slot=1;r.nativeScope=3;
    r.scope=diva::CustomizationScope::Song;r.catalogFingerprint="隔离目录";r.origin=diva::CustomizationOrigin::HoverButton;r.action=action;return r;
}
// 遍历七个人物分类的预览／换装，使用生产准备与确认器；测试适配器只修改自己的值快照。
void CrossCategoryActions() {
    using namespace diva;using namespace action_runtime;
    uint64_t serial=100;const int rawCharacters[]={0,1,2,3,6,7,4};
    for(int from=0;from<7;++from) for(int to=0;to<7;++to) if(from!=to) for(auto action:{CustomizationAction::Preview,CustomizationAction::Equip}) {
        auto current=Sample();current.category=from;current.highlightId=1000+from;
        std::array<int,4> first{current.highlightId,0,rawCharacters[from],0},second{900,0,rawCharacters[from],0};
        std::array<uintptr_t,2> records{reinterpret_cast<uintptr_t>(first.data()),reinterpret_cast<uintptr_t>(second.data())};
        current.begin=reinterpret_cast<uintptr_t>(records.data());current.end=current.begin+sizeof(records);
        const auto origin=current;active={};latest=current;cards.clear();lastFrame=GetTickCount64();
        auto capability=ReadModuleActionCapability({CustomizationKind::Module,900},ModuleMenuCharacter(to));Check(capability.preview,"跨分类预览能力被错误禁用");
        targets[900].owned=Ownership::Owned;targets[900].checked=GetTickCount64();
        capability=ReadModuleActionCapability({CustomizationKind::Module,900},ModuleMenuCharacter(to));Check(capability.equip,"跨分类换装能力被错误禁用");
        auto request=ActionRequest(++serial,action);request.key.id=900;request.character=ModuleMenuCharacter(to);std::string message;
        Check(SubmitModuleAction(request,message) && active.phase==Phase::Preparing,"跨分类动作提交仍要求预先定位");
        // 参数direction是生产决定的相邻步；模拟原生列表重建，但不更改装备、范围和槽位。
        const auto move=[&](int direction) {current.category+=direction;current.highlightId=2000+current.category;first[0]=current.highlightId;first[2]=rawCharacters[current.category];second[2]=current.category==6?9:rawCharacters[current.category];return true;};
        // 获取测试自身的快照，不读取游戏。
        const auto capture=[&] {return current;};
        // 参数id为事务保存的原高亮；测试只改变自己的列表高亮。
        const auto restore=[&](const diva::action_runtime::Snapshot&,int id) {current.highlightId=id;return true;};
        for(int frame=0;frame<8 && active.phase==Phase::Preparing;++frame) {const auto beforeFrame=current;ProcessActionModelFrame(beforeFrame,move,capture,restore);}
        Check(active.phase==Phase::Waiting && current.category==to && current.models==origin.models,"跨人物预览／换装没有自动准备对应原生分类");
        int invoked=0,ownershipReads=0;
        // 参数id/owned模拟原生拥有读取，仍由生产执行器决定何时核对。
        const auto ownership=[&](int id,Ownership& owned) {++ownershipReads;owned=Ownership::Owned;return id==900;};
        // 参数card/kind模拟最终原生动作；验证目标记录与动作没有被替换成旧高亮。
        const auto invoke=[&](const diva::action_runtime::Snapshot&,const Card& card,CustomizationAction kind) {++invoked;return card.record==records[1] && card.index==1 && kind==action;};
        Execute(current,ownership,invoke);Check(active.phase==Phase::Loading && invoked==1 && ownershipReads==1,"最终执行入口仍然拒绝跨分类预览／换装或用错目标");
        if(action==CustomizationAction::Equip) {current.equippedId=900;current.equippedIndex=1;current.models[current.page.slot]=900;Confirm(current);Confirm(current);}
        else {
            current.page.internal=2;current.page.idle=false;current.models[current.page.slot]=900;current.equippedIndex=1;Confirm(current);
            Check(active.phase==Phase::Preview,"跨人物原生预览没有进入临时状态");
            current.page.internal=0;current.page.idle=true;current.models=origin.models;Confirm(current);Confirm(current);
        }
        Check(active.phase==Phase::Restoring,"动作独立确认后未恢复原分类");
        for(int frame=0;frame<9 && active.phase==Phase::Restoring;++frame) {const auto beforeFrame=current;ProcessActionModelFrame(beforeFrame,move,capture,restore);}
        Check(status.succeeded && current.category==from && current.highlightId==origin.highlightId,"跨人物动作未恢复原分类和高亮");
        Check(current.equippedId==(action==CustomizationAction::Equip?900:origin.equippedId) && current.page.slot==origin.page.slot,"跨人物动作改错装备或歌者槽位");
        Check(status.previewRestored==(action==CustomizationAction::Preview),"预览恢复与普通换装的搜索重开标记混淆");
    }
}
}
// 覆盖未知/未拥有、过期槽位范围、重复提交及预览退出不反向覆盖玩家装备。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    try {
        using namespace diva;using namespace action_runtime;
        // 配置覆盖表按槽位独立读回装备，浏览分类与显示模型都不能替代此值。
        std::array<int,42> configured{};configured[7]=45;std::array<uintptr_t,4> controller{};controller[2]=reinterpret_cast<uintptr_t>(configured.data());
        auto equipmentPage=Sample().page;equipmentPage.controller=reinterpret_cast<uintptr_t>(controller.data());int equipped=-1;
        Check(ReadEquippedModule(equipmentPage,equipped) && equipped==45,"独立装备配置未按当前槽位读取");
        equipmentPage.slot=-1;Check(!ReadEquippedModule(equipmentPage,equipped),"未知槽位进入装备配置读取");
        base=1;generation=7;uiGeneration=9;lastFrame=GetTickCount64();latest=Sample();active={};cards.clear();targets.clear();specialProfileReadable=true;specialIds.fill(-1);
        Card card;card.index=1;card.record=600;card.character=0;card.checked=lastFrame;
        cards[316]=card;std::string message;
        auto r=ActionRequest(1);
        Check(!SubmitModuleAction(r,message),"未知拥有状态允许换装");
        cards[316].owned=Ownership::Unowned;Check(!SubmitModuleAction(r,message),"未拥有目标允许换装");
        cards[316].owned=Ownership::Owned;
        r.slot=-1;Check(!SubmitModuleAction(r,message),"未知槽位被当作有效槽位");
        r=ActionRequest(1);r.scope=CustomizationScope::Unknown;Check(!SubmitModuleAction(r,message),"未知范围开放写入");
        r=ActionRequest(1);r.nativeScope=2;Check(!SubmitModuleAction(r,message),"不同共通范围通过上下文守卫");
        r=ActionRequest(1);r.contextGeneration=6;Check(!SubmitModuleAction(r,message),"旧上下文通过守卫");
        r=ActionRequest(1);r.uiGeneration=8;Check(!SubmitModuleAction(r,message),"旧目录通过守卫");
        r=ActionRequest(1);cards[316].character=1;Check(!SubmitModuleAction(r,message),"按钮暗中更换表演人物");
        cards[316].character=0;r=ActionRequest(1);Check(SubmitModuleAction(r,message),"已拥有且同歌者的有效请求被误拒绝");
        Check(!SubmitModuleAction(ActionRequest(2),message),"并发动作未串行拒绝");
        CancelModuleAction();TickModuleAction(GetTickCount64(),9);
        Check(active.phase==Phase::Done && !status.succeeded,"未执行动作取消后仍被认为成功");
        // 当前还在初音分类，目标卡片属于KAITO；能力、提交和执行准备不能再要求先定位。
        latest=Sample();active={};const auto preparingCapability=ReadModuleActionCapability({CustomizationKind::Module,45},CustomizationCharacter::Kaito);
        Check(preparingCapability.preview,"不同当前分类的预览按钮仍被禁用");
        targets[45].owned=Ownership::Owned;targets[45].checked=GetTickCount64();
        const auto equippingCapability=ReadModuleActionCapability({CustomizationKind::Module,45},CustomizationCharacter::Kaito);
        Check(equippingCapability.equip,"不同当前分类的已拥有服装换装按钮仍被禁用");
        auto prepare=ActionRequest(2,CustomizationAction::Preview);prepare.key.id=45;prepare.character=CustomizationCharacter::Kaito;
        Check(SubmitModuleAction(prepare,message) && active.phase==Phase::Preparing,"预览提交未进入跨人物分类准备");
        CancelModuleAction();TickModuleAction(GetTickCount64(),9);active={};
        // 当前槽位仍穿初音，但正在浏览KAITO分类；明确点击KAITO服装应作用于同一槽位。
        latest=Sample();latest.category=4;latest.equippedIndex=-1;latest.equippedCharacter=-1;cards[45]=card;
        cards[45].record=700;cards[45].character=6;cards[45].owned=Ownership::Owned;cards[45].checked=GetTickCount64();active={};
        specialProfileReadable=true;specialIds.fill(-1);
        const auto crossCapability=ReadModuleActionCapability({CustomizationKind::Module,45});
        Check(crossCapability.preview && crossCapability.equip,"跨人物分类预览／换装按钮仍被错误禁用");
        auto cross=ActionRequest(3);cross.key.id=45;cross.character=CustomizationCharacter::Kaito;
        Check(SubmitModuleAction(cross,message),"当前装备不在KAITO列表时跨人物换装被误拒绝");
        CancelModuleAction();TickModuleAction(GetTickCount64(),9);active={};latest=Sample();
        auto invalidSlot=Sample();invalidSlot.page.slot=-1;Check(!CertifiedContext(invalidSlot),"无效原生槽位通过上下文认证");
        active={};active.phase=Phase::Loading;active.request=ActionRequest(2);active.before=Sample();active.card=card;
        status={};status.action=CustomizationAction::Equip;
        auto changed=Sample();changed.equippedIndex=1;changed.equippedId=316;changed.models[1]=316;
        Confirm(changed);Check(active.phase==Phase::Loading,"仅一次装备读回提前完成");
        Confirm(changed);Check(status.succeeded && status.completed,"独立装备索引、ID、模型跨帧一致仍未成功");
        active={};active.phase=Phase::Loading;active.request=ActionRequest(3,CustomizationAction::Preview);active.before=Sample();active.card=card;
        status={};status.action=CustomizationAction::Preview;
        auto preview=Sample();preview.page.internal=2;preview.page.idle=false;preview.models[1]=316;
        Confirm(preview);Check(active.phase==Phase::Preview && status.previewReady,"临时预览未与装备状态分开");
        Confirm(Sample());Confirm(Sample());Check(status.succeeded && status.previewRestored,"原生退出且原装备一致未恢复搜索标记");
        active={};active.phase=Phase::Preview;active.request=ActionRequest(4,CustomizationAction::Preview);active.before=Sample();
        status={};status.action=CustomizationAction::Preview;
        Confirm(changed);Check(status.completed && !status.succeeded && !status.previewRestored,"用户后来更换装备仍自动恢复旧搜索或旧装备");
        active={};active.phase=Phase::Loading;active.request=ActionRequest(5);active.before=Sample();status={};
        auto otherSlot=Sample();otherSlot.page.slot=2;
        Confirm(otherSlot);Check(status.completed && !status.succeeded,"旧请求作用到新歌者槽位");
        active={};active.phase=Phase::Loading;active.request=ActionRequest(6);active.before=Sample();status={};
        auto otherScope=Sample();otherScope.page.nativeScope=0;
        Confirm(otherScope);Check(status.completed && !status.succeeded,"同对象不同范围未取消旧动作");
        active={};active.phase=Phase::Preview;active.abandoned=true;active.request=ActionRequest(7,CustomizationAction::Preview);active.before=Sample();status={};
        Confirm(Sample());Confirm(Sample());Check(status.succeeded && !status.previewRestored,"用户已放弃预览事务仍后台重新打开面板");
        CrossCategoryActions();
        for(auto next:{CustomizationAction::Locate,CustomizationAction::Preview,CustomizationAction::Equip}) {
            active={};active.phase=Phase::Preview;active.request=ActionRequest(400,CustomizationAction::Preview);active.before=active.origin=Sample();active.cancelled=cancelled.load();
            latest=Sample();lastFrame=GetTickCount64();status={};status.serial=400;status.action=CustomizationAction::Preview;status.previewReady=true;
            cards[316]=card;cards[316].record=600;cards[316].character=0;cards[316].owned=Ownership::Owned;cards[316].checked=GetTickCount64();
            const auto capability=ReadModuleActionCapability({CustomizationKind::Module,316},CustomizationCharacter::Miku);
            Check(!capability.busy && capability.preview && capability.equip,"稳定预览中的后续动作按钮仍被禁用");
            auto follow=ActionRequest(401,next);Check(RequestModulePreviewExit(follow,message) && active.phase==Phase::Exiting,"后续定位／预览／换装未排队退出旧预览");
            auto previewState=Sample();previewState.page.internal=2;previewState.page.idle=false;previewState.models[1]=316;int exits=0;
            // 仅记录隔离退出适配器调用次数，不调用真实游戏函数。
            const auto exit=[&](const diva::action_runtime::Snapshot&) {++exits;return true;};
            ProcessPreviewExit(previewState,exit);ProcessPreviewExit(previewState,exit);Check(exits==1,"同一后续请求重复退出预览");
            ProcessPreviewExit(Sample(),exit);Check(!status.completed,"尚未跨帧确认便完成退出");ProcessPreviewExit(Sample(),exit);
            Check(status.succeeded && !status.previewRestored,"旧预览退出没有核对原装备或抢回新请求的搜索窗口");
        }
        std::cout<<"原生服装动作隔离回归通过：84组跨分类预览／换装的能力、提交、执行与恢复；拥有、范围、槽位保护保留；未访问游戏。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"服装动作验证失败："<<error.what()<<"\n";return 1;}
}
