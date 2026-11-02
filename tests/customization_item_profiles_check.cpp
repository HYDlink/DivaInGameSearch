// 发型／道具原生规格构造器隔离回归：用测试进程内的内存与测试函数充当"场景、记录与原生入口"，基址取 0，
// 因此规格中的"RVA"就是测试函数地址；不读写游戏、不调用任何生产地址。模拟布局不代表实机结构。
#include "customization_item_profiles.h"
#include <Windows.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks=0;
// 参数 condition/message 为断言及中文失败原因。
void Check(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
using namespace diva;
constexpr uintptr_t kBegin=0x10,kEnd=0x18,kHighlight=0x20,kPart=0x24,kLoading=0x28,kPreviewFlag=0x29,kPreviewId=0x2C,kCallback=0x30;
constexpr uintptr_t kEquipped=0x40,kStride=0x10;
constexpr int kPreview=41,kEquip=42,kExit=43;
// 模拟记录：稳定 ID 位于 +0x10，拥有标记位于 +0x14。
struct Record {int64_t pad[2]{};int32_t id=-1;uint8_t owned=0;};
alignas(16) unsigned char scene[0x100]{};
alignas(16) unsigned char controller[0x100]{};
struct Callback {uintptr_t scene=0;} callback;
std::vector<Record> hairRecords,headRecords,faceRecords;
std::vector<uintptr_t> hairPointers,headPointers,facePointers;
int highlightCalls=0,stepCalls=0,partCalls=0,previewCalls=0,equipCalls=0,exitCalls=0;
// 参数 offset/value 为模拟场景字段与值。
template<class T> void Put(unsigned char* base,uintptr_t offset,T value) {std::memcpy(base+offset,&value,sizeof(T));}
template<class T> T Get(const unsigned char* base,uintptr_t offset) {T value{};std::memcpy(&value,base+offset,sizeof(T));return value;}
// 参数 pointers 为记录指针数组；写入模拟场景的列表起止。
void Bind(std::vector<uintptr_t>& pointers) {
    Put(scene,kBegin,reinterpret_cast<uintptr_t>(pointers.data()));Put(scene,kEnd,reinterpret_cast<uintptr_t>(pointers.data()+pointers.size()));
}
// 参数 records/pointers/ids/owned 为记录存储、指针数组、稳定 ID 与拥有标记；构造一份列表。
void Build(std::vector<Record>& records,std::vector<uintptr_t>& pointers,std::vector<int> ids,std::vector<uint8_t> owned) {
    records.assign(ids.size(),{});pointers.clear();
    for(size_t n=0;n<ids.size();++n) {records[n].id=ids[n];records[n].owned=owned[n];}
    for(auto& record:records) pointers.push_back(reinterpret_cast<uintptr_t>(&record));
}
// 参数 object/index 为场景与序号；模拟直接高亮。
void __fastcall TestHighlight(void* object,int index) {++highlightCalls;Put(static_cast<unsigned char*>(object),kHighlight,int32_t(index));}
// 参数 object/direction 为场景与方向；模拟逐步移动。
void __fastcall TestStep(void* object,int direction) {++stepCalls;auto* s=static_cast<unsigned char*>(object);Put(s,kHighlight,int32_t(Get<int32_t>(s,kHighlight)+direction));}
// 参数 object/direction 为场景与方向；模拟部位切换并换列表。
void __fastcall TestPart(void* object,int direction) {
    ++partCalls;auto* s=static_cast<unsigned char*>(object);const int32_t part=Get<int32_t>(s,kPart)+direction;Put(s,kPart,part);Put(s,kHighlight,int32_t(0));
    Bind(part==1?facePointers:headPointers);
}
// 参数 object/command 为回调对象与命令号；按当前高亮执行模拟预览、更换或退出预览。
uint8_t __fastcall TestCallback(void* object,int command) {
    auto* s=reinterpret_cast<unsigned char*>(static_cast<Callback*>(object)->scene);
    const auto begin=Get<uintptr_t>(s,kBegin);const auto index=Get<int32_t>(s,kHighlight);
    const auto id=reinterpret_cast<const Record*>(reinterpret_cast<const uintptr_t*>(begin)[index])->id;
    if(command==kPreview) {++previewCalls;Put(s,kPreviewFlag,uint8_t(1));Put(s,kPreviewId,int32_t(id));return 1;}
    if(command==kEquip) {++equipCalls;Put(controller,kEquipped,int32_t(id));return 1;}
    if(command==kExit) {++exitCalls;Put(s,kPreviewFlag,uint8_t(0));return 1;}
    return 0;
}
// 参数 domain 为领域；返回完整模拟规格，RVA 即测试函数地址（基址 0）。
CustomizationItemNativeSpec Spec(CustomizationDomain domain) {
    CustomizationItemNativeSpec spec;spec.evidence="隔离模拟，不是实机证据";
    spec.listBegin=kBegin;spec.listEnd=kEnd;spec.recordId=0x10;spec.recordOwned=0x14;spec.highlightIndex=kHighlight;
    if(domain==CustomizationDomain::Accessories) {spec.partIndex=kPart;spec.partRva=reinterpret_cast<uintptr_t>(&TestPart);}
    spec.loadingFlag=kLoading;spec.previewFlag=kPreviewFlag;spec.previewId=kPreviewId;spec.equippedBase=kEquipped;spec.equippedStride=kStride;
    spec.highlightRva=reinterpret_cast<uintptr_t>(&TestHighlight);spec.callbackObject=kCallback;spec.callbackRva=reinterpret_cast<uintptr_t>(&TestCallback);
    spec.previewCommand=kPreview;spec.equipCommand=kEquip;spec.exitPreviewCommand=kExit;
    return spec;
}
// 参数 domain 为领域；返回指向模拟内存的空闲帧。
CustomizationItemFrame Frame(CustomizationDomain domain) {
    CustomizationItemFrame frame;frame.valid=frame.idle=true;frame.domain=domain;frame.scene=reinterpret_cast<uintptr_t>(scene);frame.state=1;
    frame.controller=reinterpret_cast<uintptr_t>(controller);frame.configuration=2;frame.slot=0;frame.slotCount=2;frame.nativeScope=3;frame.sourcePvid=800;
    frame.singer=CustomizationCharacter::Miku;frame.now=1000;frame.thread=7;return frame;
}
uint64_t serial=0;
// 参数 domain/id/part/action 为请求目标；返回合法请求。
CustomizationLocateRequest Request(CustomizationDomain domain,int id,CustomizationPart part,CustomizationAction action=CustomizationAction::Locate) {
    CustomizationLocateRequest r;r.key={CustomizationKind::Accessory,id};r.domain=domain;r.part=part;r.action=action;r.character=CustomizationCharacter::Miku;
    r.catalogFingerprint="合成";r.uiGeneration=1;r.serial=++serial;r.queryRevision=1;return r;
}
// 参数 count/domain 为帧数与领域；逐帧推进执行器。
void Frames(int count,CustomizationDomain domain) {auto frame=Frame(domain);for(int n=0;n<count;++n) {frame.now+=16;OnCustomizationItemFrame(frame);}}
}

// 验证规格缺项不登记、模型入口拒绝、只读快照边界，以及按规格完成定位、更换与预览恢复。
int main() {
    try {
        callback.scene=reinterpret_cast<uintptr_t>(scene);Put(scene,kCallback,reinterpret_cast<uintptr_t>(&callback));
        Build(hairRecords,hairPointers,{-1,500,501,502},{1,1,0,1});Bind(hairPointers);
        Put(controller,kEquipped,int32_t(500));Put(controller,kEquipped+kStride,int32_t(777));
        // 生产规格表为空：不登记任何能力。
        Check(InitializeCustomizationItemProfiles(0)==0 && CustomizationItemProfileDiagnostics().find("未登记")!=std::string::npos
            && !CustomizationItemCapabilities(CustomizationDomain::Hair).locate,"空规格仍登记了原生能力");
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Hair,{},0),"空规格构造出 Profile");
        auto spec=Spec(CustomizationDomain::Hair);spec.evidence.clear();
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Hair,spec,0),"缺少证据说明仍构造 Profile");
        spec=Spec(CustomizationDomain::Accessories);spec.partIndex.reset();
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Accessories,spec,0),"道具缺少部位读数仍构造 Profile");
        spec=Spec(CustomizationDomain::Hair);spec.highlightRva.reset();spec.stepRva=0x694D00;
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Hair,spec,0),"引用模型分类函数仍构造 Profile");
        spec=Spec(CustomizationDomain::Hair);spec.callbackRva=0x695250;
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Hair,spec,0),"引用模型动作回调仍构造 Profile");
        spec=Spec(CustomizationDomain::Hair);spec.highlightRva=reinterpret_cast<uintptr_t>(scene);
        Check(!BuildCustomizationItemProfile(CustomizationDomain::Hair,spec,0),"不可执行入口仍构造 Profile");
        spec=Spec(CustomizationDomain::Hair);spec.equipCommand.reset();spec.previewCommand.reset();
        auto partial=BuildCustomizationItemProfile(CustomizationDomain::Hair,spec,0);
        Check(partial && partial->highlight && !partial->preview && !partial->equip,"缺少命令号时仍开放预览或更换");

        // 完整模拟规格：只读快照字段与边界。
        auto hair=BuildCustomizationItemProfile(CustomizationDomain::Hair,Spec(CustomizationDomain::Hair),0);
        Check(hair && hair->read && hair->highlight && hair->preview && hair->equip && hair->exitPreview && !hair->switchPart,"完整规格未构造全部能力");
        CustomizationItemSnapshot snapshot;
        Check(hair->read(Frame(CustomizationDomain::Hair),snapshot) && snapshot.recordIds==std::vector<int>{-1,500,501,502} && snapshot.part==CustomizationPart::Hair
            && snapshot.highlightIndex==size_t(0) && snapshot.equippedId==500 && snapshot.slotEquipped[1]==777 && snapshot.owned[2]==false,"快照读取字段错误");
        Put(scene,kEnd,Get<uintptr_t>(scene,kBegin)-8);CustomizationItemSnapshot broken;
        Check(!hair->read(Frame(CustomizationDomain::Hair),broken),"列表边界异常仍返回快照");
        Bind(hairPointers);
        // 定位：唯一 ID → 序号 → 直接高亮 → 跨帧读回。
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,hair);
        std::string message;
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,502,CustomizationPart::Hair),Frame(CustomizationDomain::Hair),message),"规格定位未受理");
        Frames(3,CustomizationDomain::Hair);auto status=PollCustomizationItemLocator();
        Check(status.succeeded && status.stage==CustomizationStage::Highlighted && highlightCalls==1 && Get<int32_t>(scene,kHighlight)==3,"规格定位未按真实序号高亮或未跨帧确认");
        // 更换：未拥有不调用；已拥有则调用并以独立装备读回确认。
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,501,CustomizationPart::Hair,CustomizationAction::Equip),Frame(CustomizationDomain::Hair),message),"未拥有场景未受理");
        Frames(2,CustomizationDomain::Hair);
        Check(PollCustomizationItemLocator().completed && !PollCustomizationItemLocator().succeeded && equipCalls==0,"未拥有目标仍发出更换命令");
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,502,CustomizationPart::Hair,CustomizationAction::Equip),Frame(CustomizationDomain::Hair),message),"更换未受理");
        Frames(3,CustomizationDomain::Hair);status=PollCustomizationItemLocator();
        Check(status.succeeded && status.stage==CustomizationStage::Equipped && equipCalls==1 && Get<int32_t>(controller,kEquipped)==502 && Get<int32_t>(controller,kEquipped+kStride)==777,"更换未按读回确认或改动了其他槽位");
        // 预览：进入、确认、请求退出、核对原装备。
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Hair,500,CustomizationPart::Hair,CustomizationAction::Preview),Frame(CustomizationDomain::Hair),message),"预览未受理");
        Frames(3,CustomizationDomain::Hair);status=PollCustomizationItemLocator();
        Check(previewCalls==1 && status.previewReady && status.stage==CustomizationStage::Previewing && !status.completed,"预览未进入临时预览阶段");
        Check(RequestCustomizationItemPreviewExit(message),"退出预览请求未受理");
        Frames(2,CustomizationDomain::Hair);status=PollCustomizationItemLocator();
        Check(exitCalls==1 && status.succeeded && status.previewRestored && status.stage==CustomizationStage::PreviewRestored && Get<int32_t>(controller,kEquipped)==502,"退出预览后未核对原装备");
        // 道具：部位读数映射与逐帧部位切换后在新列表中定位。
        Build(headRecords,headPointers,{600,601},{1,1});Build(faceRecords,facePointers,{610,611},{1,1});
        Bind(headPointers);Put(scene,kPart,int32_t(0));Put(scene,kHighlight,int32_t(0));
        auto items=BuildCustomizationItemProfile(CustomizationDomain::Accessories,Spec(CustomizationDomain::Accessories),0);
        Check(items && items->switchPart,"道具规格未开放部位切换");
        RegisterCustomizationItemProfile(CustomizationDomain::Accessories,items);
        Check(SubmitCustomizationItemRequest(Request(CustomizationDomain::Accessories,611,CustomizationPart::Face),Frame(CustomizationDomain::Accessories),message),"道具定位未受理");
        Frames(4,CustomizationDomain::Accessories);status=PollCustomizationItemLocator();
        Check(status.succeeded && partCalls==1 && Get<int32_t>(scene,kHighlight)==1,"道具未切部位后在新列表中定位");
        RegisterCustomizationItemProfile(CustomizationDomain::Hair,std::nullopt);RegisterCustomizationItemProfile(CustomizationDomain::Accessories,std::nullopt);
        std::cout<<"发型／道具原生规格隔离回归通过："<<checks<<" 项；只使用测试进程内模拟内存与函数，生产规格表为空。\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"发型／道具原生规格验证失败："<<error.what()<<"\n";return 1;}
}
