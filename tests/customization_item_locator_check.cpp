// 发型／自定义道具执行器隔离回归：只注入模拟 Profile 与内存内列表，不读写游戏、不调用任何生产 RVA。
// 模拟 Profile 仅用于验证事务守卫，不能视为任何原生发型或道具协议已认证。
#include "customization_item_locator.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
int checks=0;
// 参数 condition/message 为断言及中文失败原因。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
using namespace diva;
// 模拟的一份原生列表：容器身份、记录稳定ID及高亮序号。
struct FakeList {uintptr_t identity=0;std::vector<int> ids;int highlight=0;};
// 模拟原生页：每个部位一份列表，记录每类原生调用次数；forge/refresh 用于伪造成功与陈旧记录。
struct FakeNative {
    std::map<CustomizationPart,FakeList> lists;
    CustomizationPart current=CustomizationPart::Hair;
    bool ready=true,forge=false,refresh=false;
    std::optional<int> equipped;
    int reads=0,switches=0,highlights=0,equips=0;
    size_t lastIndex=~size_t(0);
};
FakeNative native;
// 参数 frame/out 为执行器上下文与快照输出；从模拟列表填写只读快照。
bool FakeRead(const CustomizationItemFrame& frame,CustomizationItemSnapshot& out) {
    (void)frame;++native.reads;auto& list=native.lists[native.current];
    out.pageReady=true;out.listReady=native.ready;out.part=native.current;out.listIdentity=list.identity;out.recordIds=list.ids;
    if(list.highlight>=0 && list.highlight<static_cast<int>(list.ids.size())) out.highlightedId=list.ids[static_cast<size_t>(list.highlight)];
    out.equippedId=native.equipped;return true;
}
// 参数 frame/direction 为上下文与方向；按头→脸→胸口→背部顺序移动一步。
bool FakeSwitch(const CustomizationItemFrame& frame,int direction) {
    (void)frame;++native.switches;
    const CustomizationPart order[]={CustomizationPart::Head,CustomizationPart::Face,CustomizationPart::Neck,CustomizationPart::Back};
    int at=0;for(int n=0;n<4;++n) if(order[n]==native.current) at=n;
    at=std::clamp(at+direction,0,3);native.current=order[at];native.lists[native.current].highlight=0;return true;
}
// 参数 frame/index 为上下文与列表序号；forge 时只返回受理而不改变高亮，refresh 时替换容器身份。
bool FakeHighlight(const CustomizationItemFrame& frame,size_t index) {
    (void)frame;++native.highlights;native.lastIndex=index;auto& list=native.lists[native.current];
    if(!native.forge) list.highlight=static_cast<int>(index);
    if(native.refresh) ++list.identity;
    return true;
}
// 参数 frame/index 为上下文与列表序号；模拟更换后写入实际装备读回。
bool FakeEquip(const CustomizationItemFrame& frame,size_t index) {
    (void)frame;++native.equips;native.equipped=native.lists[native.current].ids[index];return true;
}
// 参数 domain 为目标领域；返回空闲且有效的模拟帧上下文。
CustomizationItemFrame Frame(CustomizationDomain domain,uint64_t now=1000) {
    CustomizationItemFrame frame;frame.valid=frame.idle=true;frame.domain=domain;frame.scene=0x1000;frame.state=0x2000;
    frame.controller=0x3000;frame.configuration=0x4000;frame.slot=0;frame.slotCount=1;frame.nativeScope=3;frame.sourcePvid=800;frame.singer=CustomizationCharacter::Miku;frame.now=now;frame.thread=7;return frame;
}
uint64_t serial=0;
// 参数 domain/id/part/action 为请求目标；返回带合法序号、指纹和查询修订的请求。
CustomizationLocateRequest Request(CustomizationDomain domain,int id,CustomizationPart part,CustomizationAction action=CustomizationAction::Locate) {
    CustomizationLocateRequest request;request.key={CustomizationKind::Accessory,id};request.domain=domain;request.part=part;request.action=action;request.character=CustomizationCharacter::Miku;
    request.catalogFingerprint="合成目录";request.uiGeneration=5;request.serial=++serial;request.queryRevision=3;return request;
}
// 参数 frames/frame 为推进帧数与上下文；逐帧调用执行器，模拟原生状态帧。
void Frames(int frames,CustomizationItemFrame frame) {for(int n=0;n<frames;++n) {frame.now+=16;OnCustomizationItemFrame(frame);}}
// 参数 hair/items 为是否重建发型与道具模拟列表；重置调用计数。
void Reset() {
    native={};native.current=CustomizationPart::Hair;
    native.lists[CustomizationPart::Hair]={10,{500,501,502,503},0};
    native.lists[CustomizationPart::Head]={20,{600,601},0};native.lists[CustomizationPart::Face]={21,{610},0};
    native.lists[CustomizationPart::Neck]={22,{620},0};native.lists[CustomizationPart::Back]={23,{630,631},0};
}
}

// 验证未知 Profile 明确 Unsupported，以及模拟 Profile 下的唯一查找、跨帧读回、部位切换和各类取消。
int main() {
    try {
        Reset();std::string message;
        // 生产默认：没有任何 Profile，定位、预览、更换全部拒绝且不报告成功。
        for(auto domain:{CustomizationDomain::Hair,CustomizationDomain::Accessories}) {
            const auto c=CustomizationItemCapabilities(domain);
            Check(!c.locate && !c.preview && !c.equip && c.locateReason.find("尚未认证")!=std::string::npos,"未知Profile意外开放能力或缺少原因");
        }
        for(auto action:{CustomizationAction::Locate,CustomizationAction::Preview,CustomizationAction::Equip}) {
            Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair,action),Frame(CustomizationDomain::Hair),message)
                && message.find("尚未认证")!=std::string::npos,"未知Profile没有明确拒绝");
            const auto status=PollCustomizationItemLocator();
            Check(status.completed && !status.succeeded && status.phase==ModuleLocatePhase::Rejected && !CustomizationItemLocatorBusy(),"未知Profile拒绝没有发布失败状态");
        }
        Check(native.reads==0 && native.highlights==0,"未知Profile仍读取或写入原生列表");
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Models,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"模型领域请求进入发型执行器");
        // 实际歌者守卫：未知歌者拒绝预览与更换；发型属于其他角色时任何动作都拒绝；共通道具兼容任一歌者。
        auto unknownSinger=Frame(CustomizationDomain::Hair);unknownSinger.singer.reset();
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair,CustomizationAction::Preview),unknownSinger,message)
            && message.find("歌者")!=std::string::npos,"未知歌者仍可预览发型");
        auto otherHair=Request(CustomizationDomain::Hair,500,CustomizationPart::Hair);otherHair.character=CustomizationCharacter::Rin;
        Check(!SubmitCustomizationItemRequest(otherHair,Frame(CustomizationDomain::Hair),message) && message.find("仅供浏览")!=std::string::npos,"其他角色发型仍可定位");
        auto otherItem=Request(CustomizationDomain::Accessories,600,CustomizationPart::Head,CustomizationAction::Equip);otherItem.character=CustomizationCharacter::Kaito;
        Check(!SubmitCustomizationItemRequest(otherItem,Frame(CustomizationDomain::Accessories),message),"当前歌者不能使用的道具仍可更换");

        CustomizationItemProfile hair;hair.evidence="隔离模拟，不是实机证据";hair.read=FakeRead;hair.highlight=FakeHighlight;
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,hair);
        Check(CustomizationItemCapabilities(CustomizationDomain::Hair).locate && !CustomizationItemCapabilities(CustomizationDomain::Hair).equip
            && !CustomizationItemCapabilities(CustomizationDomain::Accessories).locate,"Profile能力未按领域与入口分别开放");
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Head),Frame(CustomizationDomain::Hair),message),"发型请求携带道具部位仍被受理");
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Accessories),message),"不在发型页仍受理发型定位");
        auto notIdle=Frame(CustomizationDomain::Hair);notIdle.idle=false;
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),notIdle,message),"过渡帧仍受理定位");
        Check(!SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair,CustomizationAction::Equip),Frame(CustomizationDomain::Hair),message)
            && message.find("更换")!=std::string::npos,"Profile缺少更换入口仍受理换装");

        // 正常定位：唯一查找、一次高亮调用、跨两帧读回后成功。
        const auto first=Request(CustomizationDomain::Hair,502,CustomizationPart::Hair);
        Check(SubmitCustomizationItemRequest(first,Frame(CustomizationDomain::Hair),message) && CustomizationItemLocatorBusy(),"模拟Profile定位未受理");
        auto duplicate=Request(CustomizationDomain::Hair,503,CustomizationPart::Hair);
        Check(!SubmitCustomizationItemRequest(duplicate,Frame(CustomizationDomain::Hair),message),"事务未完成时受理第二条请求");
        Frames(1,Frame(CustomizationDomain::Hair));
        Check(native.highlights==1 && native.lastIndex==2 && !PollCustomizationItemLocator().completed,"高亮调用次数或序号错误，或调用同帧即报告成功");
        Frames(1,Frame(CustomizationDomain::Hair));Check(!PollCustomizationItemLocator().completed,"仅一帧读回即报告成功");
        Frames(1,Frame(CustomizationDomain::Hair));
        auto status=PollCustomizationItemLocator();
        Check(status.completed && status.succeeded && status.phase==ModuleLocatePhase::Succeeded && native.highlights==1 && !CustomizationItemLocatorBusy(),"跨帧读回一致后未报告成功或重复调用");
        Check(!SubmitCustomizationItemRequest(first,Frame(CustomizationDomain::Hair),message),"重复序号被再次受理");

        // 伪造成功：原生调用返回受理但高亮未变，不能报告成功。
        Reset();native.forge=true;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,503,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"伪造场景未受理");
        Frames(8,Frame(CustomizationDomain::Hair));status=PollCustomizationItemLocator();
        Check(status.completed && !status.succeeded && native.highlights==1,"高亮读回不符仍报告成功或重复调用");
        // 陈旧记录：确认前容器身份变化，旧记录作废。
        Reset();native.refresh=true;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,501,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"陈旧记录场景未受理");
        Frames(3,Frame(CustomizationDomain::Hair));status=PollCustomizationItemLocator();
        Check(status.completed && !status.succeeded && status.message.find("刷新")!=std::string::npos,"列表刷新后仍沿用旧记录报告成功");
        // 目标缺失与重复ID：不调用高亮。
        Reset();
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,999,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"缺失目标场景未受理");
        Frames(2,Frame(CustomizationDomain::Hair));Check(PollCustomizationItemLocator().completed && !PollCustomizationItemLocator().succeeded && native.highlights==0,"目标缺失仍调用高亮或报告成功");
        Reset();native.lists[CustomizationPart::Hair].ids={500,502,502};
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,502,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"重复ID场景未受理");
        Frames(2,Frame(CustomizationDomain::Hair));status=PollCustomizationItemLocator();
        Check(status.completed && !status.succeeded && status.message.find("重复")!=std::string::npos && native.highlights==0,"重复ID仍尝试定位");
        // 列表未就绪只等待，不失败也不调用。
        Reset();native.ready=false;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"未就绪场景未受理");
        Frames(5,Frame(CustomizationDomain::Hair));Check(!PollCustomizationItemLocator().completed && native.highlights==0,"列表未就绪时提前执行或失败");
        auto transient=Frame(CustomizationDomain::Hair);transient.idle=false;Frames(3,transient);
        Check(!PollCustomizationItemLocator().completed,"同页过渡帧导致事务失败");
        // 换槽、换线程、离开页面、取消与目录代数变化都取消且不调用。
        auto changed=Frame(CustomizationDomain::Hair);changed.slot=1;Frames(1,changed);
        Check(PollCustomizationItemLocator().completed && !PollCustomizationItemLocator().succeeded && native.highlights==0,"换槽后旧事务继续");
        Reset();
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"线程场景未受理");
        auto otherThread=Frame(CustomizationDomain::Hair);otherThread.thread=8;Frames(1,otherThread);
        Check(PollCustomizationItemLocator().completed && native.highlights==0,"原生线程变化后继续执行");
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"离页场景未受理");
        CustomizationItemFrame left;left.now=1100;OnCustomizationItemFrame(left);
        Check(PollCustomizationItemLocator().completed && native.highlights==0,"离开发型页后继续执行");
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"取消场景未受理");
        CancelCustomizationItemLocator();Check(!CustomizationItemLocatorBusy(),"取消后仍报告忙碌");Frames(1,Frame(CustomizationDomain::Hair));
        Check(PollCustomizationItemLocator().completed && native.highlights==0,"取消后继续执行");
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"目录代数场景未受理");
        TickCustomizationItemLocator(1100,6);Frames(1,Frame(CustomizationDomain::Hair));
        Check(PollCustomizationItemLocator().completed && native.highlights==0,"目录重载后旧事务继续");
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"超时场景未受理");
        TickCustomizationItemLocator(1000+20000,5);Check(PollCustomizationItemLocator().completed && !PollCustomizationItemLocator().succeeded,"超时未停止");
        auto fingerprint=Request(CustomizationDomain::Hair,500,CustomizationPart::Hair);fingerprint.catalogFingerprint="另一目录";
        Check(!SubmitCustomizationItemRequest(fingerprint,Frame(CustomizationDomain::Hair),message),"目录指纹变化仍受理");

        // 道具：缺少部位协议时跨部位明确拒绝；有协议时每帧一步，切换后重新读取新列表再定位。
        CustomizationItemProfile items;items.evidence="隔离模拟";items.read=FakeRead;items.highlight=FakeHighlight;
        RegisterCustomizationItemProfile(CustomizationDomain::Accessories,items);
        Reset();native.current=CustomizationPart::Head;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Accessories,630,CustomizationPart::Back),Frame(CustomizationDomain::Accessories),message),"道具跨部位场景未受理");
        Frames(2,Frame(CustomizationDomain::Accessories));status=PollCustomizationItemLocator();
        Check(status.completed && !status.succeeded && status.message.find("部位切换")!=std::string::npos && native.highlights==0,"未认证部位协议仍跨部位定位");
        items.switchPart=FakeSwitch;items.equip=FakeEquip;RegisterCustomizationItemProfile(CustomizationDomain::Accessories,items);
        Reset();native.current=CustomizationPart::Head;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Accessories,631,CustomizationPart::Back),Frame(CustomizationDomain::Accessories),message),"道具定位未受理");
        Frames(1,Frame(CustomizationDomain::Accessories));Check(native.switches==1 && native.highlights==0,"部位切换没有逐帧一步");
        Frames(2,Frame(CustomizationDomain::Accessories));Check(native.switches==3 && native.current==CustomizationPart::Back && native.highlights==0,"未到目标部位就定位");
        Frames(1,Frame(CustomizationDomain::Accessories));Check(native.highlights==1 && native.lastIndex==1,"切换部位后未在新列表中查找记录");
        Frames(2,Frame(CustomizationDomain::Accessories));Check(PollCustomizationItemLocator().succeeded,"道具跨部位定位读回后未成功");
        Reset();native.current=CustomizationPart::Head;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Accessories,600,CustomizationPart::Face),Frame(CustomizationDomain::Accessories),message),"部位与目标不符场景未受理");
        Frames(3,Frame(CustomizationDomain::Accessories));
        Check(PollCustomizationItemLocator().completed && !PollCustomizationItemLocator().succeeded && native.highlights==0,"目标不在请求部位列表时误定位其他部位记录");
        // 更换只以独立装备读回为准。
        Reset();native.current=CustomizationPart::Face;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Accessories,610,CustomizationPart::Face,CustomizationAction::Equip),Frame(CustomizationDomain::Accessories),message),"更换未受理");
        Frames(3,Frame(CustomizationDomain::Accessories));status=PollCustomizationItemLocator();
        Check(status.succeeded && native.equips==1 && native.highlights==0 && status.action==CustomizationAction::Equip,"更换读回或调用次数错误");
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,std::nullopt);RegisterCustomizationItemProfile(CustomizationDomain::Accessories,std::nullopt);
        Check(!CustomizationItemCapabilities(CustomizationDomain::Hair).locate,"撤销Profile后仍开放能力");
        std::cout<<"发型／道具执行器隔离回归通过："<<checks<<" 项；只使用模拟Profile，未认证任何原生发型或道具协议。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"发型／道具执行器验证失败："<<error.what()<<"\n";return 1;}
}
