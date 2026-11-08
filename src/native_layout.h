#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// 已核查 PC 构建（DivaMegaMix.exe 1.03）的原生地址与内存布局总表。
// 这里的地址只对下方 build 记录的构建有效，换构建必须重新核查，不能沿用。
//
// 名字来源标记，用来区分“上游叫这个名字”和“我们自己起的名字”：
//   [RTTI]   游戏自带的类名，见 docs/research/PAGE_STATE_MAPPING.md
//   [ReDIVA] korenkonder/ReDIVA 的名字；它基于街机版，结构可参考，枚举数值不能照抄
//   [NC]     mrcloverthecoder/nc（New Classics）的名字，PC 地址一致
//   [DML]    blueskythlikesclouds/DivaModLoader
//   [QS]     Snapmali/QuantumSwitch
//   [MMS]    lsmsmx/MikuMikuSwitchPlugin；来自 Switch 版，数值只在标注“PC 实测”处核实过
//   [自拟]   上游没有名字，按实测行为命名，含义以所引研究文档为准
namespace diva::native {

// 已核查构建的身份，初始化时全部核对，不符就关闭所有原生功能。
namespace build {
constexpr uint32_t kTimestamp=1666920875;
constexpr uint32_t kImageSize=602353664;
}

// 外层模式，即 ModeInfo::current。[MMS] PC 实测过 Game、Customize、MenuSwitch。
// [ReDIVA] 的 6 是它自己加的 MODE_DATA_EDIT，与这里不同，不能混用。
// 用非 class 枚举是因为内存里读出的是原始整数，可能是表外的值，字段也要保留 -1 表示未读取。
namespace Mode {
enum Value:int32_t {
    Startup=0,Advertise=1,Game=2,DataTest=3,TestMode=4,AppError=5,
    CsMenu=6,Customize=7,Gallery=8,MenuSwitch=9,GameSwitch=10,TshirtEdit=11,
};
}

// 外层子状态，即 ModeInfo::sub_current／sub_next。[MMS] 数值来自 Switch 版，
// PC 实测过 PvSel、PlaylistSel、Game、CsCustomizeSel、Unk37，以及 [自拟] 的主页两项。
// 选曲和播放列表的稳定态是 5/6 和 6/7：sub_next 是序列里的下一项，不代表正在切换。
// [ReDIVA] 的 5 是 MODE_SUB_RATING，同样不能混用。
namespace ModeSub {
enum Value:int32_t {
    DataInitialize=0,SystemStartup=1,Logo=2,Title=3,Conceal=4,
    PvSel=5,PlaylistSel=6,Game=7,
    CsMenu=32,CsCommerce=33,CsOptionMenu=34,CsUnkTutorial35=35,
    CsCustomizeSel=36,CsUnk37=37,CsGallery=38,
    // [自拟] 主页稳定态是 42/43，见 OUTER_NATIVE_ROUTING_VERIFIED.md。
    MenuSwitchHome=42,MenuSwitchUnk43=43,
    // 模式初始化的中间哨兵，不是任何稳定页。
    Max=47,
};
}

// [ReDIVA] ModeInfo::proc：Ctrl 是常态；把 sub_proc 写成 Dest 就是请求离开当前子状态。
namespace ModeProc {
enum Value:int32_t {Init=0,Ctrl=1,Dest=2};
}

// [ReDIVA] mode.hpp 的 ModeInfo，位于 rva::kModeInfo。
// 实测字段见 OUTER_NATIVE_ROUTING_VERIFIED.md；只用来计算偏移，不把游戏内存强转成它直接解引用。
struct ModeInfo {
    int32_t current,next,old,proc;
    bool shift,init;
    uint32_t time,etc_work;
    int32_t sub_current,sub_next,sub_old,sub_proc;
    bool sub_shift,sub_init;
    uint32_t sub_call_count;
};
static_assert(offsetof(ModeInfo,proc)==0xC && offsetof(ModeInfo,shift)==0x10);
static_assert(offsetof(ModeInfo,sub_current)==0x1C && offsetof(ModeInfo,sub_next)==0x20);
static_assert(offsetof(ModeInfo,sub_proc)==0x28 && offsetof(ModeInfo,sub_shift)==0x2C);

// 谱面难度，与 [ReDIVA] pv_difficulty 的前四项一致。
// [ReDIVA] 的 4 是 ENCORE；项目请求里的 4 是下面的 kRequestExtraExtreme，两者不是一回事。
namespace PvDifficulty {
enum Value:int32_t {Easy=0,Normal=1,Hard=2,Extreme=3};
}
// [ReDIVA] pv_edition。
namespace PvEdition {
enum Value:int32_t {Original=0,Extra=1};
}
// 项目 Request::difficulty 的取值：0～3 同 PvDifficulty，4 表示 Extreme + PvEdition::Extra。
constexpr int kRequestExtraExtreme=4;

// [RTTI] customize_selector_switch 场景树的键，由 SceneManager 的当前节点读出。
namespace SceneKey {
enum Value:int32_t {Loading=0,TopMenu=1,Module=2,Hairstyle=3,Item=4,Tshirt=5,Sound=6,Config=7,Finish=8};
}

// 换装配置范围，由 F4 循环切换：全曲共通 A/B/C 与曲目单独，见 ACTION_RUNTIME_PROFILE.md。[自拟]
namespace CustomizeScope {
enum Value:int32_t {CommonA=0,CommonB=1,CommonC=2,Song=3};
}

// 相对主程序基址的地址。
namespace rva {
// ---- 外层模式 ----
constexpr uintptr_t kModeInfo=0xCC61078;        // [ReDIVA] ModeInfo 实例
constexpr uintptr_t kRequestMode=0x2C4BB0;      // [自拟] void(int mode)，主页进自定义传 7、进选曲传 2
constexpr uintptr_t kModeCtrl=0x2C4A40;         // [自拟] 外层模式控制帧，路由器在其帧末提交请求

// ---- 选曲 ----
constexpr uintptr_t kPvSelectorSwitch=0xCC5EF18;        // [QS] CURR_PVID_BASE_PTR，指向选曲任务对象的指针，见 pv_selector
constexpr uintptr_t kPvSelectorSwitchCtrl=0x6EDC40;     // [NC] PVSelectorSwitchCtrl 选曲控制帧
constexpr uintptr_t kPvSelectorApplySel=0x6EFCF0;       // [自拟] 歌词项目的 applySel，从其相对调用解出选曲对象获取器
constexpr uintptr_t kVisiblePvidMember=0x6EFE8C;        // [QS] CURR_PVID_SONG_SELECTION_OFFSET_PTR，指令里的成员偏移立即数
constexpr uintptr_t kCreateSortedPvList=0x6F3100;       // [NC] PVSelectorSwitchCreateSortedPVList
constexpr uintptr_t kChangeSortFilter=0x6F2F40;         // [NC] PVSelectorSwitchChangeSortFilter
constexpr uintptr_t kPvListSetSelectedIndex=0x217500;   // [NC] PVListSetSelectedIndex
constexpr uintptr_t kHideSelector=0x6EECD0;             // [自拟] void(song*,uint8)，选曲按 R 进自定义前隐藏选曲

// 上次选曲记录，菜单初始化时从这里恢复。[QS] LAST_SELECT_*
constexpr uintptr_t kLastSelectDifficulty=0x12B634C;    // [QS] DIFF_TYPE
constexpr uintptr_t kLastSelectPvId=0x12B6350;          // [QS] PVID
constexpr uintptr_t kLastSelectSortMode=0x12B6354;      // [QS] SORT
constexpr uintptr_t kLastSelectSortIndex=0x12B635C;     // [QS] 称 DIFF_LEVEL，本项目写入的是全曲分组值

// ---- 换装：任务与场景管理 ----
constexpr uintptr_t kCustomizeTask=0xCC6F100;           // [自拟] 静态任务对象本体而非指针，首次进入前全为 0，见 customize_task
constexpr uintptr_t kActivateCustomize=0x1DE750;        // [自拟] uint8(int pvid,void* context)
constexpr uintptr_t kCustomizeScopeSelector=0x12B5A74;  // [自拟] 当前配置范围，取值见 CustomizeScope
constexpr uintptr_t kSceneManagerVtable=0xCC4E38;       // [RTTI] SceneManager
constexpr uintptr_t kSceneManagerRequest=0x6968D0;      // [自拟] 虚表第 6 槽，bool(manager,key)，请求目标场景
constexpr uintptr_t kSceneStateFrame=0x66B2D0;          // [自拟] 场景内部状态机帧
constexpr uintptr_t kSceneStateSwitch=0x66B3A0;         // [自拟] bool(state,next)，切换场景内部状态
constexpr uintptr_t kScenePanelHide=0x682530;           // [自拟] void(panel)，隐藏来源页列表面板
constexpr uintptr_t kTopMenuConfirm=0x69B040;           // [自拟] void(scene)，父页确认动画
constexpr uintptr_t kListControlSetIndex=0x22D250;      // [自拟] void(control,index)，发型／道具列表控件高亮

// ---- 换装：模型页 ----
constexpr uintptr_t kSceneModuleFrame=0x6910B0;         // [自拟] uint8(page)，模型页帧入口
constexpr uintptr_t kSceneModuleInitChoice=0x6916E0;    // [自拟] void(page,uint8)，按 page+0x30 初始化横向控件
constexpr uintptr_t kSceneModuleMoveCategory=0x694D00;  // [自拟] void(page,int direction)，切到相邻人物分类
constexpr uintptr_t kSceneModuleAction=0x695250;        // [自拟] void(scene,int command)，模型页动作回调
constexpr uintptr_t kSpecialModuleIds=0xC09580;         // [自拟] 10 个 int32：特殊模型项，不能套用普通服装动作

// ---- 换装：发型页 ----
constexpr uintptr_t kSceneHairstyleRefresh=0x6891F0;    // [自拟] void(scene,3)，只刷新卡片显示
constexpr uintptr_t kSceneHairstyleAction=0x68AC40;     // [自拟] void(scene,int command)，发型页动作回调

// ---- 换装：道具页 ----
constexpr uintptr_t kSceneItemRefresh=0x68D180;         // [自拟] void(scene,3)，只刷新卡片显示
constexpr uintptr_t kSceneItemMovePart=0x68EF00;        // [自拟] void(scene,int direction)，切换部位
constexpr uintptr_t kSceneItemAction=0x68F430;          // [自拟] void(scene,int command)，道具页动作回调

// ---- 存档与拥有判定 ----
constexpr uintptr_t kGetSaveData=0x1D6510;              // [NC] GetSaveData
constexpr uintptr_t kFindModuleRecord=0x1D5C90;         // [DML] 同样 hook 此入口；void*(save,int id)
constexpr uintptr_t kIsModuleOwned=0x1DA550;            // [自拟] uint8(record)
constexpr uintptr_t kGetPvSlotConfig=0x1D5E30;          // [自拟] record(save,int pvid,int slot)，单曲槽位配置
constexpr uintptr_t kIsItemOwned=0x6809A0;              // [自拟] uint8(0,record)，发型／道具拥有判定

// ---- 其他 ----
constexpr uintptr_t kGetInputState=0x2AC970;            // [NC] GetInputState，输入隔离 hook 这里
constexpr uintptr_t kGetLanguageDirectory=0x2C8D30;     // [自拟] const char*()，形如 lang2/cn，DML 用同一入口
constexpr uintptr_t kGetLanguageDirectoryCall=0x239835; // [自拟] 调用上面函数的 call 指令，用于核对
constexpr uintptr_t kPvDb=0x1753780;                    // [NC] 静态 PVDB 对象本体，见 pv_db
constexpr uintptr_t kGetPvDb=0x4BBA20;                  // [自拟] 获取器，指令是 lea rax,[rip+x]; ret
constexpr uintptr_t kGetPvDbLeaEnd=0x4BBA27;            // [自拟] 上面 lea 指令的结尾，用于解 RIP 相对地址

// [RTTI] 场景虚表。
constexpr uintptr_t kSceneLoadingVtable=0xCC4A80;
constexpr uintptr_t kSceneTopMenuVtable=0xCC5170;
constexpr uintptr_t kSceneModuleVtable=0xCC4BB0;
constexpr uintptr_t kSceneHairstyleVtable=0xCC4678;
constexpr uintptr_t kSceneItemVtable=0xCC48C8;
// 按 SceneKey 取下标；键 0～4 以外没有用到。
constexpr std::array<uintptr_t,5> kSceneVtables{kSceneLoadingVtable,kSceneTopMenuVtable,kSceneModuleVtable,kSceneHairstyleVtable,kSceneItemVtable};
}

// rva::kModeInfo 各字段相对主程序基址的地址。
namespace mode_info {
constexpr uintptr_t kCurrent=rva::kModeInfo+offsetof(ModeInfo,current);
constexpr uintptr_t kProc=rva::kModeInfo+offsetof(ModeInfo,proc);
constexpr uintptr_t kShift=rva::kModeInfo+offsetof(ModeInfo,shift);
constexpr uintptr_t kSubCurrent=rva::kModeInfo+offsetof(ModeInfo,sub_current);
constexpr uintptr_t kSubNext=rva::kModeInfo+offsetof(ModeInfo,sub_next);
constexpr uintptr_t kSubProc=rva::kModeInfo+offsetof(ModeInfo,sub_proc);
constexpr uintptr_t kSubShift=rva::kModeInfo+offsetof(ModeInfo,sub_shift);
}

// [QS] Eden Core 的数据偏移：该构建的 kLastSelect* 整体后移这么多。启动时探测，不是固定值。
constexpr uintptr_t kEdenDataOffset=0x105F460;

// [NC] PVSelectorSwitch 选曲任务对象的字段偏移，由 pv_sel_switch.cpp 的结构声明逐项算出。
namespace pv_selector {
constexpr uintptr_t kState=0x6C;
constexpr int32_t kStateIdle=6;                 // [自拟] 实测的选曲稳定态
constexpr uintptr_t kSelPvList=0x25930;         // SelPvList，kPvListSetSelectedIndex 的 this
constexpr uintptr_t kSelTargetIndex=kSelPvList+0xE0;    // [自拟] 列表目标下标，落在上游的 gap 内
constexpr uintptr_t kSelPvData=kSelPvList+0xF0;         // SelPvList::pv_data，当前可见列表的 vector 指针
constexpr uintptr_t kSortedPvLists=0x25EE8;     // sorted_pv_lists[32]
constexpr int32_t kSortedPvListStride=0x18;     // sizeof(prj::vector)
constexpr uintptr_t kPvDataSize=0x48;           // sizeof(PvData)
constexpr uintptr_t kCurPvList=0x261E8;         // cur_pv_list
constexpr uintptr_t kCurSortIndex=0x26240;      // cur_sort_index，指向 kSortIndices 的某一项
constexpr uintptr_t kPvId=0x262A4;              // pv_id
constexpr uintptr_t kSortIndices=0x262AC;       // sort_indices[4]，按 sort_mode 取下标
constexpr uintptr_t kSortChangeDir=0x262BC;     // sort_change_dir
constexpr uintptr_t kSortMode=0x262C0;          // sort_mode，0 为曲名排序
constexpr std::array<int32_t,4> kAllSongsSortIndex{10,19,6,5};  // [自拟] 各 sort_mode 下“全曲”分组写入 kSortIndices 的值
constexpr uintptr_t kDifficulty=0x262C8;        // difficulty
constexpr uintptr_t kEdition=0x262CC;           // edition
// 选曲里可见 PVID 的成员偏移要从 rva::kVisiblePvidMember 现读，再减去这个差值。[QS]
constexpr int32_t kVisiblePvidBias=0x9C8;
constexpr int32_t kVisiblePvidMemberMax=0x100000;   // [自拟] 读到的成员偏移的合理上限
}

// [自拟] rva::kCustomizeTask 的字段。见 OUTER_NATIVE_ROUTING_VERIFIED.md。
namespace customize_task {
constexpr uintptr_t kController=0x78;   // 当前控制器，未进入自定义时为空
}

// [RTTI] SceneManager 的字段与其 std::map 节点。见 PAGE_STATE_MAPPING.md。
namespace scene_manager {
constexpr uintptr_t kTreeHead=0x08;     // std::map 表头
constexpr uintptr_t kCurrentNode=0x18;
constexpr uintptr_t kTargetNode=0x20;   // 等于表头表示没有挂起目标
constexpr uintptr_t kTransition=0x28;
constexpr int32_t kTransitionIdle=1;    // 过渡状态的空闲值
constexpr uintptr_t kNodeLeft=0x00;
constexpr uintptr_t kNodeParent=0x08;   // 表头节点的这个字段是树根
constexpr uintptr_t kNodeRight=0x10;
constexpr uintptr_t kNodeKey=0x20;      // 取值见 SceneKey
constexpr uintptr_t kNodeScene=0x28;
}

// [NC] 静态 PVDB 对象与其歌曲条目。见 SONG_RECOMMENDED_MODULES.md。
namespace pv_db {
constexpr uintptr_t kEntriesBegin=0x98;
constexpr uintptr_t kEntriesEnd=0xA0;
constexpr uintptr_t kEntryPerformersBegin=0x98;
constexpr uintptr_t kEntryPerformersEnd=0xA0;
constexpr uintptr_t kPerformerSize=0x44;
}

}
