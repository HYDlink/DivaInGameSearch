#include "module_locator.h"
#include "module_category.h"
#include "module_action.h"

#include <Windows.h>
#include <wincrypt.h>
#include <detours.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

namespace diva {
namespace {

constexpr uint32_t kExpectedTimestamp=1666920875;
constexpr uint32_t kExpectedImageSize=602353664;
constexpr uintptr_t kModuleFrameRva=0x6910B0;
constexpr uintptr_t kInitializeChoiceRva=0x6916E0;
constexpr uintptr_t kMoveCategoryRva=0x694D00;
constexpr uintptr_t kModuleVtableRva=0xCC4BB0;
constexpr uintptr_t kControllerRootRva=0xCC6F100;
constexpr uintptr_t kScopeSelectorRva=0x12B5A74;
constexpr char kExpectedSha256[]="6F4DF714C28515C93B3023E7D6DAFA9095DD8835F762D54DB1726FB8BC04504D";
constexpr uint64_t kContextFreshMilliseconds=500;
constexpr int32_t kMaximumListCount=10000;

// 模型页帧入口的返回值为页面 +0x25 的字节；该入口只使用 RCX 中的 this。
using ModuleFrameFunction=uint8_t(__fastcall*)(void*);
using InitializeChoiceFunction=void(__fastcall*)(void*,uint8_t);
using MoveCategoryFunction=void(__fastcall*)(void*,int32_t);

ModuleFrameFunction g_originalModuleFrame=nullptr;
std::atomic<uint64_t> g_cancelGeneration{0};

// 读取当前进程已提交内存；参数 address/value 分别为地址和接收值。
template<class T>
bool ReadAt(uintptr_t address,T& value) {
    SIZE_T read=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(T),&read) && read==sizeof(T);
}

// 写入经过提交页与保护检查的字段；参数 address/value 为目标地址和值。
template<class T>
bool WriteValue(uintptr_t address,T value) {
    MEMORY_BASIC_INFORMATION info{};
    if(!address || !VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info))
        || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        || !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&written) && written==sizeof(T);
}

// 验证地址落在可执行提交页；参数 address 为候选原生函数入口。
bool IsExecutable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info))
        && info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}

// 读取并核对已记录的 PE 构建标记；参数 base 为动态主程序基址。
bool ValidatePeProfile(uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if(!ReadAt(base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0
        || !ReadAt(base+static_cast<uintptr_t>(dos.e_lfanew),nt)) return false;
    return nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64
        && nt.FileHeader.TimeDateStamp==kExpectedTimestamp
        && nt.OptionalHeader.SizeOfImage==kExpectedImageSize;
}

// 对当前游戏可执行文件计算 SHA-256；参数 path 为主程序路径，digest 接收 64 个十六进制字符。
bool CalculateSha256(const wchar_t* path,std::string& digest) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider=0;
    HCRYPTHASH hash=0;
    bool ok=false;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) goto cleanup;
    if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) goto cleanup;
    {
        std::array<BYTE,64*1024> buffer{};
        for(;;) {
            DWORD read=0;
            if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)) goto cleanup;
            if(!read) break;
            if(!CryptHashData(hash,buffer.data(),read,0)) goto cleanup;
        }
    }
    {
        std::array<BYTE,32> bytes{};
        DWORD size=static_cast<DWORD>(bytes.size());
        if(!CryptGetHashParam(hash,HP_HASHVAL,bytes.data(),&size,0) || size!=bytes.size()) goto cleanup;
        static constexpr char digits[]="0123456789ABCDEF";
        digest.clear();digest.reserve(64);
        for(BYTE byte:bytes) { digest.push_back(digits[byte>>4]);digest.push_back(digits[byte&0x0F]); }
        ok=true;
    }
cleanup:
    if(hash) CryptDestroyHash(hash);
    if(provider) CryptReleaseContext(provider,0);
    CloseHandle(file);
    return ok;
}

// 核对固定 RVA 的指令形状并允许同一指令中的相对调用地址变化；参数 base 为动态主程序基址。
bool ValidateNativeSignatures(uintptr_t base) {
    const unsigned char frameBytes[]={
        0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x89,0x30,0x03,0x00,0x00,
        0xE8,0x00,0x00,0x00,0x00,0x0F,0xB6,0x43,0x25,0x48,0x83,0xC4,0x20,0x5B,0xC3
    };
    const unsigned char frameMask[]={
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1
    };
    static_assert(sizeof(frameMask)==sizeof(frameBytes),"模型帧签名与掩码长度必须一致");
    std::array<unsigned char,sizeof(frameBytes)> actual{};
    SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(base+kModuleFrameRva),actual.data(),actual.size(),&read)
        || read!=actual.size()) return false;
    for(size_t i=0;i<actual.size();++i) if(frameMask[i] && actual[i]!=frameBytes[i]) return false;

    const unsigned char choiceBytes[]={0x48,0x89,0x5C,0x24,0x10};
    std::array<unsigned char,sizeof(choiceBytes)> choiceActual{};
    read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(base+kInitializeChoiceRva),choiceActual.data(),choiceActual.size(),&read)
        || read!=choiceActual.size() || memcmp(choiceActual.data(),choiceBytes,sizeof(choiceBytes))!=0) return false;

    unsigned char categoryJump=0;
    int32_t categoryDisplacement=0;
    if(!ReadAt(base+kMoveCategoryRva,categoryJump) || categoryJump!=0xE9
        || !ReadAt(base+kMoveCategoryRva+1,categoryDisplacement)) return false;
    auto categoryTarget=base+kMoveCategoryRva+5+static_cast<intptr_t>(categoryDisplacement);
    return IsExecutable(base+kModuleFrameRva) && IsExecutable(base+kInitializeChoiceRva) && IsExecutable(categoryTarget);
}

// 将原生角色枚举显式映射到已验证的 UI 分类；参数 character 为请求中的展示分类。
bool MapSupportedCategory(CustomizationCharacter character,int32_t& category) {
    category=ModuleMenuCategory(character);return category>=0;
}

// 校验当前列表条目指针可读；参数 record 为列表中一个八字节槽位指向的实体记录。
bool ReadModuleRecord(uintptr_t record,int32_t& id,int32_t& character) {
    return record && ReadAt(record,id) && ReadAt(record+0x08,character);
}

// 读取指定身份在当前原生列表中的唯一位置；参数 snapshot/id/category 为列表快照、稳定 ID 与目标分类。
enum class FindIdResult { Found,NotFound,Duplicate,RoleMismatch,InvalidList };
struct FrameSnapshot;
FindIdResult FindUniqueModuleId(const FrameSnapshot& snapshot,int32_t id,int32_t category,int32_t& index);

// 调用原生分类切换函数；参数 function/page/direction 为 profile 入口、模型页 this 和相邻分类方向。
bool InvokeMoveCategory(uintptr_t function,void* page,int32_t direction) {
    __try { reinterpret_cast<MoveCategoryFunction>(function)(page,direction);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 临时借用 +0x30 初始化原生横向控件，并在任何 SEH 路径中恢复；参数 function/page/index 为入口、页面和真实列表位置。
bool InvokeChoiceAtIndex(uintptr_t function,void* page,int32_t index) {
    auto pageAddress=reinterpret_cast<uintptr_t>(page);
    int32_t saved=-1;
    if(!ReadAt(pageAddress+0x30,saved)) return false;
    bool invoked=false;
    bool restored=false;
    __try {
        if(WriteValue(pageAddress+0x30,index)) {
            __try {
                reinterpret_cast<InitializeChoiceFunction>(function)(page,1);
                invoked=true;
            }
            __finally {
                restored=WriteValue(pageAddress+0x30,saved);
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        restored=WriteValue(pageAddress+0x30,saved);
    }
    if(!restored) restored=WriteValue(pageAddress+0x30,saved);
    return invoked && restored;
}

// 模型页观测值；成员字段只保存地址及已有实验核实的原始状态。
struct FrameSnapshot {
    uintptr_t page=0,controller=0,configuration=0,listBegin=0,listEnd=0;
    int32_t pageState=-1,pageEpoch=-1,rawContext=-1,popup=-1,rawModelA=-1,rawModelB=-1,slotCount=-1,nativeScope=-1;
    int32_t category=-1,index=-1,count=0,currentId=-1,currentCharacter=-1;
    bool structurallyValid=false,contextReady=false;
};

// 读取动态 this 关联的页面和生效 Module 列表；参数 base/page 为动态基址及帧入口真实 this。
bool ReadFrameSnapshot(uintptr_t base,uintptr_t page,FrameSnapshot& snapshot) {
    snapshot={};snapshot.page=page;
    uintptr_t vtable=0,controller=0,globalController=0;
    if(!ReadAt(page,vtable) || vtable!=base+kModuleVtableRva
        || !ReadAt(page+0x08,controller) || !controller
        || !ReadAt(base+kControllerRootRva+0x78,globalController) || globalController!=controller) return false;
    snapshot.controller=controller;
    // 原生分类函数实际读取的配置对象身份；只作生命周期token，不假定其设置范围语义。
    if(!ReadAt(controller+0x18,snapshot.configuration) || !snapshot.configuration) return false;
    if(!ReadAt(page+0x20,snapshot.pageState) || !ReadAt(page+0x28,snapshot.pageEpoch)
        || !ReadAt(page+0x110,snapshot.index) || !ReadAt(page+0x60,snapshot.popup)
        || !ReadAt(controller+0x38,snapshot.slotCount) || !ReadAt(controller+0x3C,snapshot.rawContext) || !ReadAt(controller+0x40,snapshot.category)
        || !ReadAt(base+kScopeSelectorRva,snapshot.nativeScope)
        || !ReadAt(page+0x70,snapshot.listBegin) || !ReadAt(page+0x78,snapshot.listEnd)) return false;
    // 自然E/Q已确认3C是当前歌者槽位；按真实槽位读取模型，不回退到0号槽位。
    if(snapshot.rawContext>=0 && snapshot.rawContext<6
        && (!ReadAt(controller+0x320+snapshot.rawContext*0xE0,snapshot.rawModelA)
            || !ReadAt(controller+0x338+snapshot.rawContext*0xE0,snapshot.rawModelB))) return false;
    if(snapshot.listEnd<snapshot.listBegin || ((snapshot.listEnd-snapshot.listBegin)&7)) return false;
    auto count=(snapshot.listEnd-snapshot.listBegin)/8;
    if(!count || count>kMaximumListCount) return false;
    snapshot.count=static_cast<int32_t>(count);
    int32_t displayedCount=0;
    if(!ReadAt(page+0x11C,displayedCount) || displayedCount!=snapshot.count
        || snapshot.index<0 || snapshot.index>=snapshot.count) return false;
    uintptr_t selectedRecord=0;
    if(!ReadAt(snapshot.listBegin+static_cast<uintptr_t>(snapshot.index)*8,selectedRecord)
        || !ReadModuleRecord(selectedRecord,snapshot.currentId,snapshot.currentCharacter)) return false;
    snapshot.structurallyValid=true;
    // 320/338在正常列表也可保持125，不能把非零当作试穿标志；只记录原值并追踪变化。
    snapshot.contextReady=(snapshot.pageState==1 || snapshot.pageState==2)
        && snapshot.slotCount>0 && snapshot.slotCount<=6 && snapshot.rawContext>=0 && snapshot.rawContext<snapshot.slotCount
        && snapshot.nativeScope>=0 && snapshot.nativeScope<=3 && snapshot.popup==-1
        && snapshot.category>=0 && snapshot.category<=6
        && ModuleRecordCategory(snapshot.currentCharacter)==snapshot.category;
    return true;
}

// 判断两个样本是否仍属于同一列表／菜单上下文；参数 left/right 为前后帧观测。
bool SameMenuContext(const FrameSnapshot& left,const FrameSnapshot& right) {
    return left.page==right.page && left.controller==right.controller && left.configuration==right.configuration && left.pageState==right.pageState
        && left.pageEpoch==right.pageEpoch && left.rawContext==right.rawContext && left.slotCount==right.slotCount
        && left.nativeScope==right.nativeScope && left.popup==right.popup
        && left.rawModelA==right.rawModelA && left.rawModelB==right.rawModelB
        && left.category==right.category && left.listBegin==right.listBegin && left.listEnd==right.listEnd
        && left.count==right.count;
}

// 判断请求排队后玩家是否改变了分类或高亮项；参数 initial/current 为排队快照和当前帧快照。
bool SameSubmittedSelection(const FrameSnapshot& initial,const FrameSnapshot& current) {
    return SameMenuContext(initial,current) && initial.index==current.index
        && initial.currentId==current.currentId && initial.currentCharacter==current.currentCharacter;
}

// 返回无效原生上下文的中文原因；参数 snapshot 为当前原生页面读数。
std::string ContextUnavailableMessage(const FrameSnapshot& snapshot) {
    if(!snapshot.structurallyValid) return "原生模型页面或当前服装列表已失效。";
    if(snapshot.pageState!=1 && snapshot.pageState!=2) return "当前不在已验证的模型服装列表状态。";
    if(snapshot.slotCount<1 || snapshot.slotCount>6 || snapshot.rawContext<0 || snapshot.rawContext>=snapshot.slotCount) return "当前歌者槽位或歌者数量无效，未提交定位。";
    if(snapshot.nativeScope<0 || snapshot.nativeScope>3) return "当前设置范围不可识别，未提交定位。";
    if(snapshot.popup!=-1) return "请先关闭服装操作菜单，再提交定位。";
    if(snapshot.category<0 || snapshot.category>6) return "请先进入实际角色服装分类；随机不是服装实体。";
    if(ModuleRecordCategory(snapshot.currentCharacter)!=snapshot.category) return "当前列表记录与角色分类不一致，已拒绝定位。";
    return "当前原生上下文不可用于服装定位。";
}

// 在目标列表中要求稳定 ID 唯一且记录角色吻合；参数定义见返回值与调用处。
FindIdResult FindUniqueModuleId(const FrameSnapshot& snapshot,int32_t id,int32_t category,int32_t& index) {
    int32_t match=-1;
    for(int32_t i=0;i<snapshot.count;++i) {
        uintptr_t record=0;
        int32_t recordId=-1,recordCharacter=-1;
        if(!ReadAt(snapshot.listBegin+static_cast<uintptr_t>(i)*8,record)
            || !ReadModuleRecord(record,recordId,recordCharacter)) return FindIdResult::InvalidList;
        if(recordId!=id) continue;
        if(match>=0) return FindIdResult::Duplicate;
        if(ModuleRecordCategory(recordCharacter)!=category) return FindIdResult::RoleMismatch;
        match=i;
    }
    if(match<0) return FindIdResult::NotFound;
    index=match;
    return FindIdResult::Found;
}

class ModuleLocator {
public:
    // 按已核实 SHA-256、PE 与入口指令初始化；参数 base 为受 GameMemory 核准的动态基址。
    bool Initialize(uintptr_t base);
    // 保存 UI 请求及原生提交上下文；参数 request 为 UI 请求，message 接收即时状态。
    bool Submit(const CustomizationLocateRequest& request,std::string& message);
    // 同步 UI 目录代数并检测模型帧入口超时；参数 now/uiGeneration 为单调时刻及当前目录代数。
    void Tick(uint64_t now,uint64_t uiGeneration);
    // 从模型帧链式 Hook 处理本帧定位；参数 page/now/threadId 为原生 this、单调时刻及动态线程号。
    void OnModelFrame(uintptr_t page,uint64_t now,DWORD threadId);
    // 返回最近一次阶段快照，不读取游戏内存；供 UI 轮询。
    ModuleLocateStatus Poll();
    // 返回页面生命周期代数；参数无，失效与重建时递增。
    uint64_t ContextGeneration();
    // 输出自然E/Q已核对的槽位；未认证读数不返回上下文，不使用默认槽位。
    std::optional<CustomizationContext> ReadContext();
private:
    struct ActiveRequest {
        CustomizationLocateRequest request;
        FrameSnapshot submitted;
        FrameSnapshot expected;
        int categorySteps=0;
        uint64_t contextGeneration=0;
        uint64_t cancelGeneration=0;
        int32_t targetCategory=-1;
        int32_t targetIndex=-1;
        bool active=false;
    };
    // 处理等待中的请求；参数 current 为本帧通过 profile 与上下文核验的快照。
    void Execute(const FrameSnapshot& current);
    // 核对上帧目标并完成跨帧状态；参数 current 为本帧通过 profile 与上下文核验的快照。
    void Confirm(const FrameSnapshot& current);
    // 唯一 ID 查找并调用真实控件初始化；参数 snapshot/id/category 为当前列表与目标身份。
    // 仅在同帧上下文仍安全时恢复原分类和原 ID；参数 current 为失败后的本帧快照。
    bool RestoreOriginal(const FrameSnapshot& current);
    // 使当前页面代数失效；参数 reason 为页面离开或上下文变化原因。
    void InvalidateContext(const std::string& reason);
    // 设置可轮询状态；参数 phase/message/completed/succeeded 为阶段和中文结果。
    void SetStatus(ModuleLocatePhase phase,const std::string& message,bool completed,bool succeeded);
    // 递增原生上下文 generation 并避免零值；参数无。
    void BumpGeneration();

    std::mutex mutex_;
    uintptr_t base_=0,choiceFunction_=0,categoryFunction_=0;
    bool initialized_=false,hookInstalled_=false,hasSnapshot_=false,contextActive_=false;
    uint64_t contextGeneration_=0,lastModelFrame_=0,lastUiGeneration_=0,lastSerial_=0;
    DWORD frameThreadId_=0;
    FrameSnapshot latest_{};
    ActiveRequest active_{};
    std::string lockedCatalogFingerprint_;
    std::string contextUnavailableReason_="未检测到当前有效的模型服装页，请进入服装列表后重试。";
    ModuleLocateStatus status_{};
};

// 取得仅存在于当前 Mod 模块的定位器；参数无，不在 DllMain 中初始化。
ModuleLocator& Locator() {
    static ModuleLocator* locator=new ModuleLocator;
    return *locator;
}

// 让原生模型帧先运行再处理本帧，并保留原函数的字节返回值；参数 page 为原生 this。
uint8_t __fastcall ModuleFrameHook(void* page) {
    auto result=g_originalModuleFrame(page);
    Locator().OnModelFrame(reinterpret_cast<uintptr_t>(page),GetTickCount64(),GetCurrentThreadId());
    OnModuleActionModelFrame(reinterpret_cast<uintptr_t>(page));
    return result;
}

} // namespace

bool ModuleLocator::Initialize(uintptr_t base) {
    std::lock_guard lock(mutex_);
    if(initialized_) return hookInstalled_;
    initialized_=true;
    status_.phase=ModuleLocatePhase::Disabled;
    status_.message="原生服装定位正在核验游戏构建。";
    if(!base || !ValidatePeProfile(base)) { status_.message="游戏构建 PE 标记与已核实版本不符，原生服装定位已禁用。";return false; }
    wchar_t executablePath[32768]{};
    DWORD pathLength=GetModuleFileNameW(nullptr,executablePath,static_cast<DWORD>(std::size(executablePath)));
    if(!pathLength || pathLength>=std::size(executablePath)) { status_.message="无法读取游戏主程序路径，原生服装定位已禁用。";return false; }
    std::string digest;
    if(!CalculateSha256(executablePath,digest) || digest!=kExpectedSha256) {
        status_.message="游戏主程序 SHA-256 与已核实版本不符，原生服装定位已禁用。";return false;
    }
    if(!ValidateNativeSignatures(base)) { status_.message="游戏原生入口指令与已核实 profile 不符，原生服装定位已禁用。";return false; }
    base_=base;choiceFunction_=base+kInitializeChoiceRva;categoryFunction_=base+kMoveCategoryRva;
    g_originalModuleFrame=reinterpret_cast<ModuleFrameFunction>(base+kModuleFrameRva);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG attach=DetourAttach(reinterpret_cast<PVOID*>(&g_originalModuleFrame),reinterpret_cast<PVOID>(&ModuleFrameHook));
    LONG commit=DetourTransactionCommit();
    if(attach!=NO_ERROR || commit!=NO_ERROR) {
        g_originalModuleFrame=nullptr;
        status_.message="模型帧链式 Hook 安装失败，未启用服装定位。";
        return false;
    }
    hookInstalled_=true;
    status_.phase=ModuleLocatePhase::Idle;
    status_.message="原生服装定位已接入六位主角色及衍生角色；扩展分类等待实机验收。";
    return true;
}

bool ModuleLocator::Submit(const CustomizationLocateRequest& request,std::string& message) {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()) { message="模型帧正在处理原生定位，请稍候再试。";return false; }
    auto reject=[&](const std::string& why) { status_={ModuleLocatePhase::Rejected,request.serial,request.uiGeneration,contextGeneration_,true,false,why};message=why;return false; };
    if(!hookInstalled_) return reject(status_.message.empty()?"原生服装定位未启用。":status_.message);
    if(request.action!=CustomizationAction::Locate) return reject("定位适配器不能执行预览或换装。");
    if(active_.active && active_.cancelGeneration!=g_cancelGeneration.load()) active_.active=false;
    if(active_.active) { message="上一条服装定位仍待跨帧确认，请稍候再提交。";return false; }
    if(request.serial==0 || request.serial<=lastSerial_) return reject("服装定位请求序号过期，已拒绝重复提交。");
    if(request.key.kind!=CustomizationKind::Module) return reject("原生定位接口仅接受服装，不接受饰品。");
    if(request.key.id<0) return reject("服装 ID 无效；稳定 ID 0 是允许的。");
    int32_t targetCategory=-1;
    if(!MapSupportedCategory(request.character,targetCategory)) return reject("未知或随机角色没有可提交的实际服装分类。");
    if(request.catalogFingerprint.empty() || request.catalogFingerprint.size()>256) return reject("服装目录指纹缺失或格式异常，已拒绝定位。");
    if(!lockedCatalogFingerprint_.empty() && lockedCatalogFingerprint_!=request.catalogFingerprint)
        return reject("服装目录指纹已变化；请重启游戏后再定位。");
    if(!contextActive_ || !hasSnapshot_ || !latest_.contextReady || !lastModelFrame_
        || GetTickCount64()<lastModelFrame_ || GetTickCount64()-lastModelFrame_>kContextFreshMilliseconds)
        return reject(contextUnavailableReason_);
    lockedCatalogFingerprint_=request.catalogFingerprint;
    lastUiGeneration_=request.uiGeneration;
    lastSerial_=request.serial;
    active_={};active_.request=request;active_.submitted=latest_;active_.contextGeneration=contextGeneration_;
    active_.expected=latest_;
    active_.cancelGeneration=g_cancelGeneration.load();
    active_.targetCategory=targetCategory;active_.active=true;
    SetStatus(ModuleLocatePhase::WaitingForFrame,"服装定位请求已提交，等待模型帧校验上下文。",false,false);
    message=status_.message;
    return true;
}

void ModuleLocator::Tick(uint64_t now,uint64_t uiGeneration) {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()) return;
    if(uiGeneration!=lastUiGeneration_) {
        lastUiGeneration_=uiGeneration;
        if(active_.active && active_.request.uiGeneration!=uiGeneration) {
            active_.active=false;
            SetStatus(ModuleLocatePhase::Rejected,"服装目录已重新载入，旧定位请求已失效。",true,false);
        }
    }
    if(contextActive_ && now>=lastModelFrame_ && now-lastModelFrame_>kContextFreshMilliseconds) {
        InvalidateContext("模型服装页帧回调已停止，页面代数失效。");
    }
}

void ModuleLocator::OnModelFrame(uintptr_t page,uint64_t now,DWORD threadId) {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !hookInstalled_) return;
    lastModelFrame_=now;
    if(frameThreadId_ && threadId!=frameThreadId_) {
        InvalidateContext("模型帧线程发生变化，旧定位上下文已失效。");
        frameThreadId_=threadId;
    } else frameThreadId_=threadId;

    FrameSnapshot current{};
    bool snapshotRead=ReadFrameSnapshot(base_,page,current);
    if(!snapshotRead || !current.contextReady) {
        if(active_.active) {
            active_.active=false;
            auto why=snapshotRead?ContextUnavailableMessage(current):"模型页对象关联或列表读取失败，定位请求已失效。";
            SetStatus(ModuleLocatePhase::Rejected,why,true,false);
        }
        InvalidateContext(snapshotRead?ContextUnavailableMessage(current):"模型页离开或对象关联失效。");
        return;
    }

    if(!contextActive_ || !hasSnapshot_ || !SameMenuContext(latest_,current)) {
        BumpGeneration();
        contextActive_=true;hasSnapshot_=true;latest_=current;
        if(active_.active) {
            active_.active=false;
            SetStatus(ModuleLocatePhase::Rejected,"模型页、分类、列表或未解释的原始上下文值已变化，旧请求失效。",true,false);
        }
        return;
    }
    latest_=current;
    if(!active_.active) return;
    if(active_.cancelGeneration!=g_cancelGeneration.load()) {
        active_.active=false;SetStatus(ModuleLocatePhase::Rejected,"搜索条件或所选卡片已变化，旧定位已取消。",true,false);return;
    }
    if(active_.contextGeneration!=contextGeneration_) {
        active_.active=false;
        SetStatus(ModuleLocatePhase::Rejected,"原生上下文代数已变化，定位请求过期。",true,false);
        return;
    }
    if(active_.request.uiGeneration!=lastUiGeneration_) {
        active_.active=false;
        SetStatus(ModuleLocatePhase::Rejected,"搜索目录代数已变化，定位请求过期。",true,false);
        return;
    }
    if(status_.phase==ModuleLocatePhase::WaitingForFrame || status_.phase==ModuleLocatePhase::SwitchingCategory) {
        if(!SameSubmittedSelection(active_.expected,current)) {
            active_.active=false;
            SetStatus(ModuleLocatePhase::Rejected,"提交后玩家改变了原生分类或高亮项，已取消旧定位。",true,false);
            return;
        }
        Execute(current);
    } else if(status_.phase==ModuleLocatePhase::Confirming) Confirm(current);
}

void ModuleLocator::Execute(const FrameSnapshot& current) {
    auto page=reinterpret_cast<void*>(current.page);
    const int32_t originCategory=current.category;
    if(originCategory!=active_.targetCategory) {
        if(++active_.categorySteps>6) {active_.active=false;SetStatus(ModuleLocatePhase::Rejected,"角色切换次数超限，已停止定位。",true,false);return;}
        const int32_t direction=ModuleCategoryDirection(originCategory,active_.targetCategory);
        if(!InvokeMoveCategory(categoryFunction_,page,direction)) {
            FrameSnapshot failed{};
            bool restored=ReadFrameSnapshot(base_,current.page,failed) && failed.contextReady
                && failed.category!=originCategory && RestoreOriginal(failed);
            active_.active=false;
            SetStatus(ModuleLocatePhase::Rejected,restored?"原生角色分类切换异常；已恢复原分类与原高亮。":"调用原生角色分类切换时发生异常，未通过守卫时不覆盖玩家操作。",true,false);
            return;
        }
        FrameSnapshot switched{};
        if(!ReadFrameSnapshot(base_,current.page,switched) || !switched.contextReady || switched.category!=originCategory+direction
            || switched.configuration!=current.configuration || switched.controller!=current.controller || switched.rawContext!=current.rawContext) {
            bool restored=switched.contextReady && switched.category!=originCategory && RestoreOriginal(switched);
            active_.active=false;
            SetStatus(ModuleLocatePhase::Rejected,restored?"游戏原生分类检查未允许切换；已恢复原分类与原高亮。":"游戏原生分类检查未允许切换到目标角色，未通过守卫时不覆盖玩家操作。",true,false);
            return;
        }
        latest_=switched;
        active_.expected=switched;
        if(switched.category!=active_.targetCategory) {
            SetStatus(ModuleLocatePhase::SwitchingCategory,"正在逐帧切换原生角色分类……",false,false);return;
        }
    }
    FrameSnapshot targetSnapshot{};
    if(!ReadFrameSnapshot(base_,current.page,targetSnapshot) || !targetSnapshot.contextReady
        || targetSnapshot.category!=active_.targetCategory) {
        bool restored=targetSnapshot.structurallyValid && originCategory!=active_.targetCategory && RestoreOriginal(targetSnapshot);
        active_.active=false;
        SetStatus(ModuleLocatePhase::Rejected,restored?"目标列表不可读；已恢复原分类与原高亮。":"目标角色分类切换后列表不可读，未通过守卫时不覆盖玩家操作。",true,false);
        return;
    }
    int32_t targetIndex=-1;
    auto found=FindUniqueModuleId(targetSnapshot,active_.request.key.id,active_.targetCategory,targetIndex);
    if(found!=FindIdResult::Found) {
        std::string why;
        if(found==FindIdResult::Duplicate) why="目标服装 ID 在当前原生列表中重复，拒绝定位。";
        else if(found==FindIdResult::RoleMismatch) why="当前原生列表中的目标 ID 角色字段不匹配，拒绝定位。";
        else if(found==FindIdResult::NotFound) why="目标服装不在当前原生有效列表中。";
        else why="目标原生列表记录不可读，拒绝定位。";
        bool restored=originCategory!=active_.targetCategory && RestoreOriginal(targetSnapshot);
        if(restored) why+=" 已在上下文未变化时恢复原分类与原高亮。";
        else if(originCategory!=active_.targetCategory) why+=" 由于恢复上下文未通过守卫，未覆盖当前玩家操作。";
        active_.active=false;SetStatus(ModuleLocatePhase::Rejected,why,true,false);return;
    }
    if(active_.cancelGeneration!=g_cancelGeneration.load()) {
        active_.active=false;SetStatus(ModuleLocatePhase::Rejected,"目标提交前选择已变化，取消旧定位。",true,false);return;
    }
    if(!InvokeChoiceAtIndex(choiceFunction_,page,targetIndex)) {
        FrameSnapshot afterCall{};
        bool recoveredContext=ReadFrameSnapshot(base_,current.page,afterCall) && afterCall.contextReady;
        bool restored=recoveredContext && RestoreOriginal(afterCall);
        std::string why="原生控件调用异常或 +0x30 暂存缓存未能确认恢复，已停止定位。";
        if(restored) why+=" 已恢复原分类与原高亮。";
        active_.active=false;SetStatus(ModuleLocatePhase::Rejected,why,true,false);return;
    }
    FrameSnapshot after{};
    if(!ReadFrameSnapshot(base_,current.page,after) || !after.contextReady
        || after.category!=active_.targetCategory || after.index!=targetIndex || after.currentId!=active_.request.key.id) {
        std::string why="原生控件已调用，但同帧实际列表读回不符。";
        bool restored=ReadFrameSnapshot(base_,current.page,after) && after.contextReady && RestoreOriginal(after);
        if(restored) why+=" 已恢复原分类与原高亮。";
        active_.active=false;SetStatus(ModuleLocatePhase::Rejected,why,true,false);return;
    }
    active_.targetIndex=targetIndex;
    latest_=after;
    SetStatus(ModuleLocatePhase::Confirming,"已调用原生服装列表控件，正在等待下一帧确认真实 ID。",false,false);
}

void ModuleLocator::Confirm(const FrameSnapshot& current) {
    int32_t targetIndex=-1;
    auto found=FindUniqueModuleId(current,active_.request.key.id,active_.targetCategory,targetIndex);
    if(found!=FindIdResult::Found || current.category!=active_.targetCategory
        || current.index!=targetIndex || current.currentId!=active_.request.key.id
        || targetIndex!=active_.targetIndex) {
        active_.active=false;
        std::string why="跨帧原生服装确认失败；页面或玩家高亮已改变，未覆盖当前操作。";
        if(found==FindIdResult::Duplicate) why="跨帧确认时目标 ID 出现重复，未覆盖当前操作。";
        else if(found==FindIdResult::NotFound) why="跨帧确认时目标已不在当前原生列表，未覆盖当前操作。";
        SetStatus(ModuleLocatePhase::Rejected,why,true,false);return;
    }
    active_.active=false;
    SetStatus(ModuleLocatePhase::Succeeded,"已确认原生服装列表中的稳定 ID "+std::to_string(active_.request.key.id)+"；未执行试穿、装备、购买或保存。",true,true);
}

bool ModuleLocator::RestoreOriginal(const FrameSnapshot& current) {
    if(active_.cancelGeneration!=g_cancelGeneration.load()) return false;
    if(!active_.submitted.page || !current.contextReady
        || current.page!=active_.submitted.page || current.controller!=active_.submitted.controller
        || current.configuration!=active_.submitted.configuration
        || current.pageState!=active_.submitted.pageState || current.pageEpoch!=active_.submitted.pageEpoch
        || current.rawContext!=active_.submitted.rawContext) return false;
    auto page=reinterpret_cast<void*>(current.page);
    if(current.category==active_.submitted.category) {
        if(!SameMenuContext(active_.submitted,current)) return false;
        if(current.currentId==active_.submitted.currentId && current.index==active_.submitted.index) return true;
        int32_t originalIndex=-1;
        if(FindUniqueModuleId(current,active_.submitted.currentId,active_.submitted.category,originalIndex)!=FindIdResult::Found) return false;
        if(!InvokeChoiceAtIndex(choiceFunction_,page,originalIndex)) return false;
        FrameSnapshot restored{};
        if(!ReadFrameSnapshot(base_,current.page,restored) || !restored.contextReady
            || restored.category!=active_.submitted.category || restored.currentId!=active_.submitted.currentId
            || restored.index!=originalIndex) return false;
        latest_=restored;
        return true;
    }
    if(current.category!=active_.targetCategory) return false;
    const int32_t distance=active_.submitted.category-current.category;
    // 失败恢复只做相邻一步，不在同一帧盲循环跨越多个未验证分类。
    if(distance!=1 && distance!=-1) return false;
    const int32_t inverse=distance;
    if(!InvokeMoveCategory(categoryFunction_,page,inverse)) return false;
    FrameSnapshot restored{};
    if(!ReadFrameSnapshot(base_,current.page,restored) || !restored.contextReady
        || restored.category!=active_.submitted.category) return false;
    int32_t originalIndex=-1;
    if(FindUniqueModuleId(restored,active_.submitted.currentId,active_.submitted.category,originalIndex)!=FindIdResult::Found) return false;
    if(!InvokeChoiceAtIndex(choiceFunction_,page,originalIndex)) return false;
    if(!ReadFrameSnapshot(base_,current.page,restored) || !restored.contextReady
        || restored.category!=active_.submitted.category || restored.currentId!=active_.submitted.currentId
        || restored.index!=originalIndex) return false;
    latest_=restored;
    return true;
}

void ModuleLocator::InvalidateContext(const std::string& reason) {
    contextUnavailableReason_=reason;
    if(contextActive_ || hasSnapshot_) BumpGeneration();
    contextActive_=false;hasSnapshot_=false;latest_={};
    if(active_.active) {
        active_.active=false;
        SetStatus(ModuleLocatePhase::Rejected,reason,true,false);
    }
}

void ModuleLocator::SetStatus(ModuleLocatePhase phase,const std::string& message,bool completed,bool succeeded) {
    status_.phase=phase;status_.message=message;status_.completed=completed;status_.succeeded=succeeded;
    if(active_.request.serial) {
        status_.serial=active_.request.serial;
        status_.uiGeneration=active_.request.uiGeneration;
    }
    status_.contextGeneration=contextGeneration_;
}

void ModuleLocator::BumpGeneration() {
    ++contextGeneration_;
    if(!contextGeneration_) ++contextGeneration_;
}

ModuleLocateStatus ModuleLocator::Poll() {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()) {
        ModuleLocateStatus busy;
        busy.phase=ModuleLocatePhase::Confirming;
        busy.message="正在模型帧线程处理原生服装定位。";
        return busy;
    }
    return status_;
}

uint64_t ModuleLocator::ContextGeneration() {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock()) return 0;
    return contextGeneration_;
}

bool InitializeModuleLocator(uintptr_t base) { return Locator().Initialize(base); }
std::optional<CustomizationContext> ModuleLocator::ReadContext() {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !contextActive_ || !hasSnapshot_ || !latest_.contextReady || GetTickCount64()-lastModelFrame_>kContextFreshMilliseconds) return {};
    const auto character=ModuleMenuCharacter(latest_.category);
    return CustomizationContext{character,latest_.rawContext,contextGeneration_,true};
}
// 输出最近帧的只读角色上下文，不从UI线程调用游戏函数。
std::optional<CustomizationContext> ReadModuleLocatorContext() {return Locator().ReadContext();}
bool SubmitModuleLocate(const CustomizationLocateRequest& request,std::string& message) { return Locator().Submit(request,message); }
void TickModuleLocator(uint64_t now,uint64_t uiGeneration) { Locator().Tick(now,uiGeneration); }
ModuleLocateStatus PollModuleLocator() { return Locator().Poll(); }
uint64_t ModuleLocatorContextGeneration() { return Locator().ContextGeneration(); }
// 取消仅发布原子代数，原生帧读取后停止旧请求，不恢复覆盖玩家新选择。
void CancelModuleLocate() {g_cancelGeneration.fetch_add(1);}

} // namespace diva
